#include "pacemaker/LinkOutput.h"
#include <algorithm>
#include <chrono>
#include <cmath>

#if defined(PACEMAKER_WITH_LINK) && PACEMAKER_WITH_LINK
 #include <ableton/Link.hpp>
#endif

namespace pacemaker {

#if defined(PACEMAKER_WITH_LINK) && PACEMAKER_WITH_LINK
namespace {
class AbletonBackend : public ILinkBackend {
public:
    explicit AbletonBackend(double bpm) : link_(bpm), state_(link_.captureAppSessionState()) {}
    void setEnabled(bool on) override { link_.enable(on); }
    bool isEnabled() const override { return link_.isEnabled(); }
    int numPeers() const override { return (int) link_.numPeers(); }
    int64_t clockMicros() const override { return (int64_t) link_.clock().micros().count(); }
    void setStartStopSyncEnabled(bool on) override { link_.enableStartStopSync(on); }
    void capture() override { state_ = link_.captureAudioSessionState(); }
    double tempo() const override { return state_.tempo(); }
    double phaseAtTime(int64_t us, double q) const override { return state_.phaseAtTime(std::chrono::microseconds(us), q); }
    double beatAtTime(int64_t us, double q) const override { return state_.beatAtTime(std::chrono::microseconds(us), q); }
    void setTempo(double bpm, int64_t us) override { state_.setTempo(bpm, std::chrono::microseconds(us)); }
    void forceBeatAtTime(double beat, int64_t us, double q) override { state_.forceBeatAtTime(beat, std::chrono::microseconds(us), q); }
    void setIsPlaying(bool p, int64_t us) override { state_.setIsPlaying(p, std::chrono::microseconds(us)); }
    void commit() override { link_.commitAudioSessionState(state_); }
private:
    ableton::Link link_;
    ableton::Link::SessionState state_;
};
} // namespace
std::unique_ptr<ILinkBackend> makeAbletonLinkBackend(double bpm) { return std::unique_ptr<ILinkBackend>(new AbletonBackend(bpm)); }
#else
std::unique_ptr<ILinkBackend> makeAbletonLinkBackend(double) { return nullptr; }
#endif

// ---------------------------------------------------------------- publisher
void LinkPublisher::reset()
{
    lastRelockSeq_ = 0; lastCommitUs_ = -1000000000; lastSetTempo_ = 0; lastBeatAbs_ = INT64_MIN;
    driftBeats_ = 0; slewBeatsLeft_ = 0; ourBpm_ = 0; playingSent_ = false; fights_ = 0; fightWindowStart_ = 0;
}

void LinkPublisher::process(ILinkBackend& link, const Timeline& tl, int64_t nowSteadyUs, const LinkSettings& cfg, bool isPublisher)
{
    if (!link.isEnabled() || !isPublisher || tl.suppressed) return;
    const BeatMapSnapshot& s = tl.snap;
    if (s.bpm <= 0.0 || !tl.map.valid) return;
    const double per = periodSamples(s);
    if (per <= 0.0) return;

    link.capture();
    const int64_t off = link.clockMicros() - steadyNowUs();
    auto toLink = [&](int64_t steady) { return steady + off; };
    const double q = std::max(1, cfg.quantum);
    bool dirty = false;

    // Peers fighting over tempo: another app moved the session tempo away from what we last set.
    if (lastSetTempo_ > 0 && std::fabs(link.tempo() - lastSetTempo_) > 2.0) {
        if (nowSteadyUs - fightWindowStart_ > 10000000) { fightWindowStart_ = nowSteadyUs; fights_ = 0; }
        if (++fights_ >= 2) diag_.fightingPeers = true;
        lastSetTempo_ = link.tempo();            // do not escalate: accept the new baseline
    } else if (nowSteadyUs - fightWindowStart_ > 10000000) {
        diag_.fightingPeers = false;
    }

    if (cfg.startStopSync) {
        const bool want = s.state == (uint8_t) EngineState::Locked || s.state == (uint8_t) EngineState::Hold || s.state == (uint8_t) EngineState::Chase;
        if (want != playingSent_) { link.setIsPlaying(want, toLink(nowSteadyUs)); playingSent_ = want; dirty = true; }
    }

    if ((s.flags & SnapshotFlags::Relock) && s.sequence != lastRelockSeq_) {
        lastRelockSeq_ = s.sequence;
        if (cfg.publishPhase) {
            const double beat = (double) s.barNumber * q + (double) (s.beatInBar - 1);
            link.forceBeatAtTime(beat, toLink(tl.map.hostTimeForSample(s.beatOriginSample)), q);
            diag_.forceBeatCalls++;
            dirty = true;
        }
        if (cfg.publishTempo) { link.setTempo(s.bpm, toLink(nowSteadyUs)); lastSetTempo_ = s.bpm; diag_.setTempoCalls++; dirty = true; }
        lastCommitUs_ = nowSteadyUs;
    } else if (cfg.publishTempo && s.confidence >= (float) cfg.confidenceGate
               && (nowSteadyUs - lastCommitUs_) >= (int64_t) (cfg.commitIntervalMs * 1000.0)
               && std::fabs(s.bpm - link.tempo()) > cfg.tempoDeadband && slewBeatsLeft_ == 0) {
        link.setTempo(s.bpm, toLink(nowSteadyUs));
        lastSetTempo_ = s.bpm; lastCommitUs_ = nowSteadyUs; diag_.setTempoCalls++; dirty = true;
    }
    ourBpm_ = s.bpm;

    // Phase drift correction by slewing (never by forcing): evaluated once per predicted beat.
    if (cfg.publishPhase && s.confidence >= (float) cfg.confidenceGate) {
        const int64_t k = firstBeatAtOrAfter(tl, nowSteadyUs + 5000);
        const GridBeat g = gridBeat(tl, k);
        const int64_t abs = (int64_t) g.barNumber * (int64_t) q + (g.beatInBar - 1);
        if (abs != lastBeatAbs_) {
            if (slewBeatsLeft_ > 0 && --slewBeatsLeft_ == 0) {
                link.setTempo(ourBpm_, toLink(nowSteadyUs)); lastSetTempo_ = ourBpm_; dirty = true;
            }
            lastBeatAbs_ = abs;
            const double linkBeat = link.beatAtTime(toLink(g.hostUs), q);
            const double ourBeat = (double) ((g.beatInBar - 1) % (int) q) + 0.0;
            double err = std::fmod(linkBeat, q) - ourBeat;
            err -= std::round(err);                      // fractional beat error only; bar alignment changes only on Relock
            if (std::fabs(err) > 0.02) ++driftBeats_; else driftBeats_ = 0;
            if (driftBeats_ >= 4 && slewBeatsLeft_ == 0) {
                const double dir = err > 0 ? -1.0 : 1.0;   // Link ahead of us: slow Link down
                link.setTempo(ourBpm_ + dir * 0.2, toLink(nowSteadyUs));
                lastSetTempo_ = ourBpm_ + dir * 0.2;
                slewBeatsLeft_ = 1; driftBeats_ = 0; diag_.slewCalls++; dirty = true;
            }
        }
    }
    if (dirty) link.commit();
}

// ---------------------------------------------------------------- service
static std::weak_ptr<LinkService> gService;
static std::mutex gServiceMutex;

std::shared_ptr<LinkService> LinkService::acquire()
{
    std::lock_guard<std::mutex> l(gServiceMutex);
    if (auto s = gService.lock()) return s;
    auto s = std::make_shared<LinkService>();
    s->backend_ = makeAbletonLinkBackend(120.0);
    gService = s;
    return s;
}
std::shared_ptr<LinkService> LinkService::createForTest(std::unique_ptr<ILinkBackend> b)
{
    auto s = std::make_shared<LinkService>();
    s->backend_ = std::move(b);
    return s;
}

int LinkService::registerInstance()
{
    std::lock_guard<std::mutex> l(m_);
    const int id = nextId_++;
    insts_.push_back({ id, false, 0.0f, 0 });
    return id;
}
void LinkService::unregisterInstance(int id)
{
    { std::lock_guard<std::mutex> l(m_); insts_.erase(std::remove_if(insts_.begin(), insts_.end(), [&](const Inst& i) { return i.id == id; }), insts_.end()); }
    arbitrate();
}
void LinkService::report(int id, bool follow, float conf, uint32_t seq)
{
    { std::lock_guard<std::mutex> l(m_); for (auto& i : insts_) if (i.id == id) { i.follow = follow; i.conf = conf; i.seq = seq; } }
    arbitrate();
}
void LinkService::arbitrate()
{
    std::lock_guard<std::mutex> l(m_);
    int best = 0; float bestC = -1.0f;
    for (const auto& i : insts_) if (i.follow && (i.conf > bestC || (i.conf == bestC && i.seq > 0))) { best = i.id; bestC = i.conf; }
    publisher_ = best;
}
bool LinkService::isPublisher(int id) const { return publisher_.load() == id; }
int LinkService::activeFollowers() const
{
    std::lock_guard<std::mutex> l(m_);
    int n = 0; for (const auto& i : insts_) if (i.follow) ++n;
    return n;
}

// ---------------------------------------------------------------- per-instance wrapper
LinkOutput::LinkOutput() { service_ = LinkService::acquire(); id_ = service_->registerInstance(); }
LinkOutput::~LinkOutput()
{
    if (service_) {
        service_->unregisterInstance(id_);
    }
}
void LinkOutput::useServiceForTest(std::shared_ptr<LinkService> s)
{
    if (service_) service_->unregisterInstance(id_);
    service_ = std::move(s); id_ = service_->registerInstance();
}
void LinkOutput::setSettings(const LinkSettings& s)
{
    settings_.write(s);
    enabled_ = s.enabled && available();
    status_.enabled = enabled_.load();
    if (service_ && service_->backend()) {
        service_->backend()->setStartStopSyncEnabled(s.startStopSync);
        service_->backend()->setEnabled(s.enabled);
    }
    if (!available() && s.enabled) status_.setError("Link is not available in this build");
}
void LinkOutput::process(const Timeline& tl, int64_t now)
{
    if (!enabled_.load(std::memory_order_relaxed) || !service_) return;
    pub_.process(*service_->backend(), tl, now, settings_.read(), service_->isPublisher(id_));
}
void LinkOutput::updateArbitration(const Timeline& tl)
{
    if (!service_) return;
    service_->report(id_, tl.follow != 0 && enabled_.load(), tl.snap.confidence, tl.snap.sequence);
    if (service_->backend()) { status_.peers = service_->backend()->numPeers(); status_.connected = service_->backend()->isEnabled(); }
    status_.setDetail(isPublisher() ? "publisher" : "peer");
}

} // namespace pacemaker
