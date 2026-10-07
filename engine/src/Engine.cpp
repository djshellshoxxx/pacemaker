#include "pacemaker/Engine.h"
#include "FrontEnd.h"
#include "TempoInduction.h"
#include "Tracker.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace pacemaker {

namespace {
constexpr double kMergeSeconds = 0.015;

float roleWeight(InputRole r)
{
    switch (r) {
    case InputRole::Kick: case InputRole::Snare: case InputRole::Trigger: return 1.0f;
    case InputRole::HiHat: return 0.5f;
    case InputRole::Overhead: case InputRole::Any: default: return 0.7f;
    }
}
uint8_t roleTag(InputRole r)
{
    switch (r) {
    case InputRole::Kick: return BandTag::KickLike;
    case InputRole::Snare: return BandTag::SnareLike;
    default: return BandTag::Other;
    }
}
}

struct Engine::Impl {
    EngineConfig cfg;
    FrontEnd fe[kMaxRoles];
    TempoInduction induction;
    Tracker tracker;
    SpscQueue<EngineEvent, 1024> events;
    SpscQueue<DiscreteOnset, 256> discrete;      // pushEvent() -> hop step (same thread, kept ordered)
    int window = 1024, hop = 256, samplesInHop = 0;
    int64_t sampleCount = 0, now = 0, lastHostTimeUs = 0;
    int64_t mergeSamples = 720;

    struct Pending { bool active = false; FusedOnset o {}; InputRole role {}; } pending;
    struct PendingDiscrete { bool active = false; DiscreteOnset ev {}; } pendingDiscrete;

    // Snapshot seqlock.
    mutable std::atomic<uint32_t> seq { 0 };
    BeatMapSnapshot snap {};
    uint32_t sequence = 0;

    // User actions, applied at the next hop boundary.
    std::atomic<int64_t> tapSample { -1 };
    std::atomic<int> downbeat { 0 }, shift { 0 }, relock { 0 }, half { 0 }, dbl { 0 }, follow { 1 };

    void hopStep();
    void deliver(const FusedOnset& o, InputRole role);
    void flushPending(bool force);
    void publish();
};

Engine::Engine() : impl_(new Impl) {}
Engine::Engine(const EngineConfig& cfg) : impl_(new Impl) { prepare(cfg); }
Engine::~Engine() = default;

const EngineConfig& Engine::config() const { return impl_->cfg; }
int Engine::detectorDelaySamples() const { return impl_->fe[0].delaySamples(); }
int Engine::hopSize() const { return impl_->hop; }
int64_t Engine::samplesProcessed() const { return impl_->sampleCount; }
size_t Engine::droppedEvents() const { return impl_->events.dropped(); }

void Engine::prepare(const EngineConfig& cfgIn)
{
    Impl& m = *impl_;
    m.cfg = cfgIn;
    m.cfg.numRoles = std::clamp(m.cfg.numRoles, 1, kMaxRoles);
    m.cfg.sampleRate = std::clamp(m.cfg.sampleRate, 8000.0, 192000.0);
    m.cfg.meter.beatsPerBar = std::clamp(m.cfg.meter.beatsPerBar, 1, 16);
    m.cfg.tempoRange.minBpm = std::clamp(m.cfg.tempoRange.minBpm, 30.0, 300.0);
    m.cfg.tempoRange.maxBpm = std::clamp(m.cfg.tempoRange.maxBpm, m.cfg.tempoRange.minBpm + 10.0, 300.0);
    // Window/hop scale with sample rate so the frame rate stays about 188 fps.
    int mult = 1;
    while (m.cfg.sampleRate > 48000.0 * mult * 1.5) mult *= 2;
    m.window = 1024 * mult; m.hop = 256 * mult;
    FrontEndParams fp;
    fp.window = m.window; fp.hop = m.hop;
    fp.k = 0.5f + 1.5f * (1.0f - std::clamp(m.cfg.sensitivity, 0.0f, 1.0f)); // sensitivity 0.5 -> k = 1.25
    for (int r = 0; r < kMaxRoles; ++r) m.fe[r].prepare(m.cfg.sampleRate, fp);
    m.induction.prepare(m.cfg.sampleRate, m.cfg.tempoRange.minBpm, m.cfg.tempoRange.maxBpm,
                        m.cfg.profile.kernelSeconds, m.cfg.referenceBpm);
    m.tracker.prepare(m.cfg, &m.events, &m.induction);
    m.mergeSamples = (int64_t) std::llround(kMergeSeconds * m.cfg.sampleRate);
    reset();
}

void Engine::reset()
{
    Impl& m = *impl_;
    for (int r = 0; r < kMaxRoles; ++r) m.fe[r].reset();
    m.induction.reset(); m.tracker.reset();
    m.events.clear(); m.discrete.clear();
    m.samplesInHop = 0; m.sampleCount = 0; m.now = 0;
    m.pending = {}; m.pendingDiscrete = {};
    m.snap = BeatMapSnapshot {}; m.sequence = 0;
    m.publish();
}

void Engine::process(const float* const* roleChannels, int numSamples, int64_t blockStartSample, int64_t blockHostTimeUs)
{
    Impl& m = *impl_;
    (void) blockStartSample;
    m.lastHostTimeUs = blockHostTimeUs;
    int offset = 0;
    while (offset < numSamples) {
        const int take = std::min(m.hop - m.samplesInHop, numSamples - offset);
        for (int r = 0; r < m.cfg.numRoles; ++r) m.fe[r].append(roleChannels[r] + offset, take);
        m.samplesInHop += take; offset += take; m.sampleCount += take;
        if (m.samplesInHop == m.hop) { m.samplesInHop = 0; m.hopStep(); }
    }
    m.publish();
}

void Engine::pushEvent(DiscreteOnset ev)
{
    impl_->discrete.push(ev);
}

void Engine::Impl::hopStep()
{
    // User actions first so that they take effect before this hop's onsets.
    const int64_t tapAt = tapSample.exchange(-1);
    if (tapAt >= 0) tracker.tap(std::min(tapAt, now));
    if (downbeat.exchange(0)) tracker.downbeatNow();
    if (const int d = shift.exchange(0)) tracker.shiftBar(d);
    if (relock.exchange(0)) tracker.relockNow();
    if (half.exchange(0)) tracker.scalePeriod(2.0);
    if (dbl.exchange(0)) tracker.scalePeriod(0.5);
    tracker.setFollow(follow.load() != 0);

    float fused = 0.0f;
    FrontEnd::FrameOut outs[kMaxRoles];
    for (int r = 0; r < cfg.numRoles; ++r) { outs[r] = fe[r].analyse(sampleCount); fused += outs[r].novelty; }
    induction.push(sampleCount - window / 2, fused);

    // Clock for the tracker: the latest time at which all onsets are known.
    const int64_t decidedCentre = sampleCount - (int64_t) 3 * hop - window / 2;
    now = std::max(now, decidedCentre + hop);
    if (now <= 0) return;

    // Audio onsets of this hop, then discrete events up to now, merged within 15 ms.
    for (int r = 0; r < cfg.numRoles; ++r) {
        if (!outs[r].hasOnset) continue;
        FusedOnset o { std::min(outs[r].onset.sample, now), outs[r].onset.strength * roleWeight(cfg.roles[r]), outs[r].onset.tag };
        if (cfg.roles[r] == InputRole::Kick) o.tag = BandTag::KickLike;
        else if (cfg.roles[r] == InputRole::Snare) o.tag = BandTag::SnareLike;
        if (cfg.roles[r] == InputRole::Trigger) o.tag = BandTag::Other;
        if (events.size() < 1000) {
            EngineEvent ev; ev.type = EngineEvent::Onset;
            ev.onset = { o.sample, cfg.roles[r], outs[r].onset.strength, o.tag };
            events.push(ev);
        }
        deliver(o, cfg.roles[r]);
    }
    for (;;) {
        if (!pendingDiscrete.active && !discrete.pop(pendingDiscrete.ev)) break;
        pendingDiscrete.active = true;
        if (pendingDiscrete.ev.sample > now) break;
        pendingDiscrete.active = false;
        const DiscreteOnset& ev = pendingDiscrete.ev;
        const float vel = ev.velocity > 1.0f ? ev.velocity / 127.0f : ev.velocity;
        FusedOnset o { ev.sample, std::clamp(vel, 0.0f, 1.0f) * roleWeight(ev.role), roleTag(ev.role) };
        EngineEvent e; e.type = EngineEvent::Onset; e.onset = { o.sample, ev.role, o.strength, o.tag };
        events.push(e);
        deliver(o, ev.role);
    }
    flushPending(false);
    tracker.tick(now);
}

void Engine::Impl::deliver(const FusedOnset& o, InputRole role)
{
    if (pending.active && std::llabs(o.sample - pending.o.sample) <= mergeSamples) {
        pending.o.sample = std::min(pending.o.sample, o.sample);
        pending.o.strength = std::min(1.0f, pending.o.strength + o.strength);
        pending.o.tag |= o.tag;
        return;
    }
    flushPending(true);
    pending.active = true; pending.o = o; pending.role = role;
}

void Engine::Impl::flushPending(bool force)
{
    if (!pending.active) return;
    if (!force && now - pending.o.sample <= mergeSamples) return;
    pending.active = false;
    tracker.onOnset(pending.o);
}

void Engine::Impl::publish()
{
    BeatMapSnapshot s;
    tracker.fillSnapshot(s);
    s.sequence = ++sequence;
    const uint32_t v = seq.load(std::memory_order_relaxed);
    seq.store(v + 1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);
    snap = s;
    std::atomic_thread_fence(std::memory_order_release);
    seq.store(v + 2, std::memory_order_release);
}

BeatMapSnapshot Engine::snapshot() const
{
    const Impl& m = *impl_;
    BeatMapSnapshot s;
    for (;;) {
        const uint32_t a = m.seq.load(std::memory_order_acquire);
        if (a & 1u) continue;
        std::atomic_thread_fence(std::memory_order_acquire);
        s = m.snap;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (m.seq.load(std::memory_order_acquire) == a) return s;
    }
}

bool Engine::popEvent(EngineEvent& out) { return impl_->events.pop(out); }

void Engine::tap(int64_t sample) { impl_->tapSample.store(sample); }
void Engine::downbeatNow() { impl_->downbeat.store(1); }
void Engine::shiftBar(int delta) { impl_->shift.fetch_add(delta); }
void Engine::relock() { impl_->relock.store(1); }
void Engine::halfTime() { impl_->half.store(1); }
void Engine::doubleTime() { impl_->dbl.store(1); }
void Engine::setFollow(bool f) { impl_->follow.store(f ? 1 : 0); }

} // namespace pacemaker
