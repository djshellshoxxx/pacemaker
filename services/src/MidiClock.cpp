#include "pacemaker/MidiClock.h"
#include "pacemaker/Platform.h"
#include <algorithm>
#include <cmath>

#if !defined(_WIN32)
 #include <pthread.h>
 #include <sched.h>
#endif

namespace pacemaker {

void setRealtimeThreadPriority()
{
#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#else
    sched_param sp {};
    sp.sched_priority = std::max(1, sched_get_priority_max(SCHED_FIFO) / 2);
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) {
        sp.sched_priority = sched_get_priority_max(SCHED_OTHER);
        pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);   // best effort
    }
#endif
}

void MidiClockPlanner::plan(const BeatMapSnapshot& s, int beatsPerBar, double origin, double period, double offset,
                            double from, double to, double lead, const MidiClockConfig& cfg, std::vector<MidiTimed>& out)
{
    auto push1 = [&](double t, uint8_t b) { MidiTimed e; e.t = t; e.bytes[0] = b; e.len = 1; out.push_back(e); };
    const bool valid = s.bpm > 0.0 && period > 0.0;
    const double tickP = period / 24.0;

    if (!cfg.enabled) {
        if (running_) { push1(from, 0xFC); running_ = false; haveLast_ = false; }
        return;
    }
    if (running_ && s.state == (uint8_t) EngineState::Idle) {
        push1(from, 0xFC); running_ = false; haveLast_ = false; return;
    }
    if (!valid) return;

    // Beat grid anchored at origin: beat k at origin + k * period (+ offset).
    auto firstBeatTime = [&](double notBefore) {
        const double k = std::ceil((notBefore - origin - offset) / period);
        return std::make_pair(k, origin + offset + k * period);
    };

    if (!running_) {
        if (s.state != (uint8_t) EngineState::Locked) return;
        const auto fb = firstBeatTime(from + lead);
        push1(fb.second, 0xFA);
        running_ = true; haveLast_ = true; lastTick_ = fb.second - tickP;
        lastRelockSeq_ = s.sequence;           // the Relock that completed the lock must not also send SPP
    } else if ((s.flags & SnapshotFlags::Relock) && s.sequence != lastRelockSeq_) {
        lastRelockSeq_ = s.sequence;
        if (cfg.sppOnRelock) {
            const auto fb = firstBeatTime(from + lead);
            const int bpb = std::max(1, beatsPerBar);
            // Position of beat k relative to the origin beat, in MIDI beats (sixteenths).
            const int64_t beatPos = (int64_t) s.barNumber * bpb + (s.beatInBar - 1) + (int64_t) fb.first;
            const uint32_t spp = (uint32_t) std::max<int64_t>(0, beatPos) * 4u & 0x3FFFu;
            MidiTimed e; e.t = fb.second; e.len = 3; e.bytes[0] = 0xF2; e.bytes[1] = (uint8_t) (spp & 0x7F); e.bytes[2] = (uint8_t) ((spp >> 7) & 0x7F);
            out.push_back(e);
            push1(fb.second, 0xFB);
            lastTick_ = fb.second - tickP;
        }
    }

    // Phase-locked ticks: always snap to the grid so slewed phase changes never accumulate.
    for (int guard = 0; guard < 4096; ++guard) {
        const double n = std::ceil((lastTick_ + 0.5 * tickP - origin - offset) / tickP);
        const double t = origin + offset + n * tickP;
        if (t >= to) break;
        if (t < from) { lastTick_ = t; continue; }
        push1(t, 0xF8);
        lastTick_ = t; ++ticks_;
    }
}

void ClockThread::start(TimelineSource src)
{
    stop();
    src_ = src; stop_ = false;
    th_ = std::thread([this] { run(); });
}

void ClockThread::stop()
{
    stop_ = true;
    if (th_.joinable()) th_.join();
}

void ClockThread::postFromAudio(int64_t atUs, const uint8_t* bytes, int len)
{
    MidiTimed e; e.t = (double) atUs; e.len = (uint8_t) std::min(len, 3);
    for (int i = 0; i < e.len; ++i) e.bytes[i] = bytes[i];
    audioQ_.push(e);
}

void ClockThread::setSink(std::shared_ptr<MidiSink> sink)
{
    std::lock_guard<std::mutex> l(m_);
    sink_ = std::move(sink);
}

void ClockThread::setConfig(const MidiClockConfig& c)
{
    std::lock_guard<std::mutex> l(m_);
    cfg_ = c;
    cfg_.offsetMs = std::clamp(cfg_.offsetMs, -300.0, 300.0);
}

void ClockThread::sleepUntil(int64_t targetUs, bool spin)
{
    for (;;) {
        const int64_t now = steadyNowUs();
        const int64_t d = targetUs - now;
        if (d <= 0 || stop_.load(std::memory_order_relaxed)) return;
        if (d > 1500) std::this_thread::sleep_for(std::chrono::microseconds(d - 1200));
        else if (!spin) return;
        else if (d > 300) std::this_thread::yield();
        // else: busy spin
    }
}

void ClockThread::run()
{
    ScopedTimerResolution timerRes;
    (void) timerRes;
    setRealtimeThreadPriority();
    std::vector<MidiTimed> evs;
    int64_t plannedTo = steadyNowUs();
    double jitterAcc = 0; int jitterN = 0;
    while (!stop_.load()) {
        std::shared_ptr<MidiSink> sink; MidiClockConfig cfg;
        { std::lock_guard<std::mutex> l(m_); sink = sink_; cfg = cfg_; }
        status_.enabled = cfg.enabled;
        status_.connected = sink && sink->isOpen();
        if (sink) status_.setDetail(sink->name());

        const int64_t now = steadyNowUs();
        const int64_t to = now + 20000;
        const int64_t from = std::max(plannedTo, now - 2000);
        evs.clear();
        if (src_) {
            const Timeline tl = src_->read();
            if (tl.map.valid && !tl.suppressed) {
                const double per = periodSamples(tl.snap) * 1.0e6 / tl.map.sampleRate;
                const double org = (double) tl.map.hostTimeForSample(tl.snap.beatOriginSample);
                planner_.plan(tl.snap, tl.beatsPerBar, org, per, cfg.offsetMs * 1000.0, (double) from, (double) to,
                              30000.0, cfg, evs);
            }
        }
        plannedTo = to;
        { MidiTimed x; while (audioQ_.pop(x)) extras_.push_back(x); }
        for (auto it = extras_.begin(); it != extras_.end();) {
            if (it->t < (double) to) { evs.push_back(*it); it = extras_.erase(it); } else ++it;
        }
        std::stable_sort(evs.begin(), evs.end(), [](const MidiTimed& a, const MidiTimed& b) { return a.t < b.t; });
        for (const auto& e : evs) {
            const int64_t t = (int64_t) std::llround(e.t);
            if (!sink || !sink->isOpen()) { continue; }
            if (sink->supportsTimestamps()) { sink->sendAt(t, e.bytes, e.len); status_.sent++; continue; }
            sleepUntil(t, true);
            if (stop_.load()) break;
            const int64_t actual = steadyNowUs();
            sink->send(e.bytes, e.len);
            status_.sent++;
            if (e.bytes[0] == 0xF8) {
                const double err = (double) (actual - t);
                jitterAcc += err * err;
                if (++jitterN >= 48) {
                    status_.measuredJitterUs = (int) std::sqrt(jitterAcc / jitterN);
                    jitterAcc = 0; jitterN = 0;
                }
            }
        }
        if (stop_.load()) break;
        // Sleep until shortly before the next window needs planning.
        sleepUntil(to - 12000, false);
        if (evs.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (planner_.running()) {   // leave the hardware stopped on shutdown
        std::shared_ptr<MidiSink> sink; { std::lock_guard<std::mutex> l(m_); sink = sink_; }
        const uint8_t stopB = 0xFC; if (sink && sink->isOpen()) sink->send(&stopB, 1);
    }
}

} // namespace pacemaker
