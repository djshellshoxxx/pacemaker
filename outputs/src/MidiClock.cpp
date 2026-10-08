#include "pacemaker/MidiClock.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fcntl.h>
#include <unistd.h>

namespace pacemaker {

int64_t steadyNowUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
int64_t ntpOffsetUs()
{
    // NTP era 1900 -> unix 1970 is 2208988800 s. Offset between steady and system clock is sampled once.
    static const int64_t off = [] {
        const int64_t sys = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        return sys - steadyNowUs() + 2208988800LL * 1000000LL;
    }();
    return off;
}

void MidiClockGenerator::generate(const BeatMapSnapshot& s, int beatsPerBar, const ClockMap& map, int64_t nowUs,
                                  int64_t horizonUs, std::vector<MidiMessage>& out)
{
    const EngineState st = (EngineState) s.state;
    const double offsetUs = cfg_.offsetMs * 1000.0;
    if (st == EngineState::Idle) {
        if (started_) { stop(nowUs, out); }
        lastEmitEnd_ = horizonUs;
        return;
    }
    const BeatGrid g = gridFromSnapshot(s, beatsPerBar);
    if (!g.valid()) return;
    auto tickTime = [&](int64_t n) { return (int64_t) std::llround(map.hostTimeForSample(g.originSample + (double) n * g.periodSamples / 24.0) + offsetUs); };
    auto beatTime = [&](int64_t k) { return tickTime(k * 24); };

    const int64_t from = std::max(lastEmitEnd_, nowUs - 2000);   // never replay stale ticks
    if (!started_) {
        // Start on the first beat that is not in the past; ticks begin at that beat.
        int64_t k = (int64_t) std::floor(g.beatAt(map.sampleForHostTime((double) (nowUs - (int64_t) offsetUs)))) + 1;
        while (beatTime(k) < nowUs) ++k;
        out.push_back({ beatTime(k), { 0xFA, 0, 0 }, 1 });
        started_ = true;
        lastTickUs_ = beatTime(k) - 1; haveTick_ = false;
        lastRelockSeq_ = s.sequence;
    }
    if ((s.flags & SnapshotFlags::Relock) && s.sequence != lastRelockSeq_) {
        lastRelockSeq_ = s.sequence;
        if (cfg_.sppOnRelock) {
            const int64_t k = (int64_t) std::floor(g.beatAt(map.sampleForHostTime((double) (nowUs - (int64_t) offsetUs)))) + 1;
            const int bar = g.barAt(k), beat = g.beatInBarAt(k);
            const int sixteenths = std::max(0, (bar - 1) * beatsPerBar * 4 + (beat - 1) * 4);
            const int spp = std::min(sixteenths, 16383);
            out.push_back({ beatTime(k) - 1, { 0xF2, (uint8_t) (spp & 0x7F), (uint8_t) ((spp >> 7) & 0x7F) }, 3 });
            out.push_back({ beatTime(k), { 0xFB, 0, 0 }, 1 });
            lastTickUs_ = std::max(lastTickUs_, beatTime(k) - 1);
        }
    }
    // Tick indices (24 per beat) whose time falls in (max(from, lastTick), horizon].
    const double lo = g.beatAt(map.sampleForHostTime((double) (std::max(from, lastTickUs_) - (int64_t) offsetUs))) * 24.0;
    for (int64_t n = (int64_t) std::floor(lo) - 1;; ++n) {
        const int64_t t = tickTime(n);
        if (t > horizonUs) break;
        // A tick closer than half a nominal tick to the previous one is a grid discontinuity: drop it.
        const int64_t halfTick = (int64_t) std::llround(g.periodSamples / 48.0 * 1e6 / map.sampleRate());
        if (t <= lastTickUs_ || t < from || (haveTick_ && t - lastTickUs_ < halfTick)) continue;
        out.push_back({ t, { 0xF8, 0, 0 }, 1 });
        lastTickUs_ = t; haveTick_ = true;
    }
    lastEmitEnd_ = horizonUs;
    lastSeq_ = s.sequence;
}

void MidiClockGenerator::stop(int64_t nowUs, std::vector<MidiMessage>& out)
{
    if (!started_) return;
    out.push_back({ nowUs, { 0xFC, 0, 0 }, 1 });
    started_ = false;
}

namespace {
class RawSink : public MidiSink {
public:
    RawSink(int fd, std::string p) : fd_(fd), path_(std::move(p)) {}
    ~RawSink() override { if (fd_ >= 0) ::close(fd_); }
    bool send(const uint8_t* b, int len, int64_t) override { return ::write(fd_, b, (size_t) len) == len; }
    std::string name() const override { return path_; }
private:
    int fd_; std::string path_;
};
class CallbackSink : public MidiSink {
public:
    CallbackSink(std::function<void(const uint8_t*, int, int64_t)> f, std::string n) : fn_(std::move(f)), name_(std::move(n)) {}
    bool send(const uint8_t* b, int len, int64_t t) override { fn_(b, len, t); return true; }
    std::string name() const override { return name_; }
private:
    std::function<void(const uint8_t*, int, int64_t)> fn_; std::string name_;
};
}

std::unique_ptr<MidiSink> openRawMidiSink(const std::string& path, std::string* error)
{
    const int fd = ::open(path.c_str(), O_WRONLY | O_NONBLOCK);
    if (fd < 0) { if (error) *error = "port not found: " + path; return nullptr; }
    return std::make_unique<RawSink>(fd, path);
}
std::unique_ptr<MidiSink> makeCallbackSink(std::function<void(const uint8_t*, int, int64_t)> fn, std::string name)
{
    return std::make_unique<CallbackSink>(std::move(fn), std::move(name));
}

ClockThread::ClockThread(const ClockMap& map, std::function<BeatMapSnapshot()> s, std::function<int()> b)
    : map_(map), snapshot_(std::move(s)), beatsPerBar_(std::move(b)) {}
ClockThread::~ClockThread() { stop(); }

void ClockThread::setSink(std::unique_ptr<MidiSink> sink) { sink_ = std::move(sink); status_.connected = sink_ != nullptr; }
void ClockThread::configure(const MidiClockConfig& c) { offsetMs_ = c.offsetMs; spp_ = c.sppOnRelock; }

void ClockThread::start()
{
    if (running_.exchange(true)) return;
    status_.enabled = true;
    thread_ = std::thread([this] { run(); });
}
void ClockThread::stop()
{
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
    status_.enabled = false;
}

void ClockThread::run()
{
    MidiClockGenerator gen;
    std::vector<MidiMessage> pending, batch;
    double slackUs = 1500.0;
    double meanDt = 0, m2 = 0; uint64_t nTick = 0;   // Welford over tick intervals for the jitter figure
    while (running_.load()) {
        MidiClockConfig cfg; cfg.offsetMs = offsetMs_.load(); cfg.sppOnRelock = spp_.load();
        gen.configure(cfg);
        const int64_t now = steadyNowUs();
        batch.clear();
        gen.generate(snapshot_(), beatsPerBar_(), map_, now, now + 40000, batch);
        pending.insert(pending.end(), batch.begin(), batch.end());
        std::sort(pending.begin(), pending.end(), [](const MidiMessage& a, const MidiMessage& b) { return a.hostUs < b.hostUs; });
        size_t sentCount = 0;
        while (sentCount < pending.size() && running_.load()) {
            const MidiMessage& m = pending[sentCount];
            if (m.hostUs > steadyNowUs() + 3000) break;
            // Sleep until `slack` before the due time, then spin. Slack adapts to the scheduler's observed oversleep
            // (a few hundred us on bare metal, milliseconds on busy VMs), trading a little CPU for tick accuracy.
            const int64_t wake = m.hostUs - (int64_t) slackUs;
            if (steadyNowUs() < wake) {
                std::this_thread::sleep_for(std::chrono::microseconds(wake - steadyNowUs()));
                const double over = (double) (steadyNowUs() - wake);
                slackUs = std::max(300.0, std::min(4000.0, std::max(slackUs * 0.98, over * 1.5 + 200.0)));
            }
            while (steadyNowUs() < m.hostUs) {}
            const int64_t at = steadyNowUs();
            if (sink_ && sink_->send(m.bytes, m.len, m.hostUs)) { status_.sent++; }
            else if (sink_) status_.connected = false;
            if (m.bytes[0] == 0xF8) {   // jitter = spread of how late each tick left versus its due time
                const double dt = (double) (at - m.hostUs);
                ++nTick; const double d = dt - meanDt; meanDt += d / (double) nTick; m2 += d * (dt - meanDt);
                if (nTick > 8) status_.jitterUs = std::sqrt(m2 / (double) (nTick - 1));
            }
            ++sentCount;
        }
        pending.erase(pending.begin(), pending.begin() + (long) sentCount);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    batch.clear();
    gen.stop(steadyNowUs(), batch);
    if (sink_) for (const auto& m : batch) sink_->send(m.bytes, m.len, m.hostUs);
}

} // namespace pacemaker
