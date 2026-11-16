// Sample-time <-> host-time mapping (ES-02 section 1), HostTimeFilter fallback, seqlock and Timeline.
#pragma once
#include "pacemaker/Types.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace pacemaker {

// Monotonic microsecond clock shared by every output thread. Link's own clock is related to this by an
// offset sampled by the Link module.
inline int64_t steadyNowUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline int64_t wallNowUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

struct ClockMap {
    int64_t originSample = 0;
    int64_t originHostUs = 0;
    double sampleRate = 48000.0;
    int64_t outputLatencyUs = 0;   // standalone only (device output latency)
    int64_t userOffsetUs = 0;      // per-output offsets are applied by each output on top of this
    bool valid = false;

    int64_t hostTimeForSample(int64_t s) const
    {
        return originHostUs + (int64_t) std::llround((double) (s - originSample) * 1.0e6 / sampleRate) + outputLatencyUs;
    }
    int64_t sampleForHostTime(int64_t us) const
    {
        return originSample + (int64_t) std::llround((double) (us - originHostUs - outputLatencyUs) * sampleRate / 1.0e6);
    }
};

// Linear regression of block-start sample vs host micros (512 points), the ableton::link::HostTimeFilter scheme.
class HostTimeFilter {
public:
    HostTimeFilter() { reset(); }
    void reset() { pts_.assign(kN, Pt {}); count_ = 0; idx_ = 0; }

    // Feed one (sampleTime, hostMicros) observation, returns the filtered host time for sampleTime.
    int64_t update(int64_t sampleTime, int64_t hostMicros)
    {
        pts_[idx_] = { (double) sampleTime, (double) hostMicros };
        idx_ = (idx_ + 1) % kN;
        if (count_ < kN) ++count_;
        if (count_ < 2) return hostMicros;
        double mx = 0, my = 0;
        for (int i = 0; i < count_; ++i) { mx += pts_[(size_t) i].x; my += pts_[(size_t) i].y; }
        mx /= count_; my /= count_;
        double sxx = 0, sxy = 0;
        for (int i = 0; i < count_; ++i) {
            const double dx = pts_[(size_t) i].x - mx;
            sxx += dx * dx; sxy += dx * (pts_[(size_t) i].y - my);
        }
        const double slope = sxx > 0 ? sxy / sxx : 0.0;
        // Reject nonsense slopes (clock discontinuities): fall back to the raw time.
        if (!(slope > 0.0)) return hostMicros;
        return (int64_t) std::llround(my + slope * ((double) sampleTime - mx));
    }
private:
    struct Pt { double x = 0, y = 0; };
    static constexpr int kN = 512;
    std::vector<Pt> pts_;
    int count_ = 0, idx_ = 0;
};

// Single-writer seqlock for trivially copyable T (ES-04 section 3).
template <typename T>
class SeqLock {
    static_assert(std::is_trivially_copyable<T>::value, "SeqLock<T> needs a trivially copyable T");
public:
    void write(const T& v)
    {
        const uint32_t s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_release);
        std::atomic_thread_fence(std::memory_order_release);
        std::memcpy(&data_, &v, sizeof(T));
        std::atomic_thread_fence(std::memory_order_release);
        seq_.store(s + 2, std::memory_order_release);
    }
    T read() const
    {
        T out;
        for (;;) {
            const uint32_t a = seq_.load(std::memory_order_acquire);
            if (a & 1u) continue;
            std::atomic_thread_fence(std::memory_order_acquire);
            std::memcpy(&out, &data_, sizeof(T));
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq_.load(std::memory_order_acquire) == a) return out;
        }
    }
private:
    std::atomic<uint32_t> seq_ { 0 };
    T data_ {};
};

// Everything an output thread needs, published once per audio block.
struct Timeline {
    BeatMapSnapshot snap {};
    ClockMap map {};
    int beatsPerBar = 4;
    int64_t publishedAtUs = 0;     // steady clock when the audio thread published it
    uint8_t suppressed = 0;     // 1: outputs suppressed (offline bounce)
    uint8_t follow = 1;
};

// Source of the current Timeline (the Core's seqlock).
using TimelineSource = const SeqLock<Timeline>*;

// Position on the predicted grid.
struct GridBeat {
    int64_t hostUs = 0;
    int beatInBar = 1;
    int barNumber = 0;
    int64_t index = 0;             // beats since the snapshot origin (0 = the origin beat)
};

inline double periodSamples(const BeatMapSnapshot& s)
{
    const int64_t d = s.nextBeatSample - s.beatOriginSample;
    return d > 0 ? (double) d : 0.0;
}

// k-th beat after (or before, k < 0) the snapshot origin beat.
inline GridBeat gridBeat(const Timeline& t, int64_t k)
{
    GridBeat g;
    const double per = periodSamples(t.snap);
    const int64_t smp = t.snap.beatOriginSample + (int64_t) std::llround((double) k * per);
    g.hostUs = t.map.hostTimeForSample(smp);
    const int n = t.beatsPerBar > 0 ? t.beatsPerBar : 4;
    const int64_t pos = (int64_t) (t.snap.beatInBar - 1) + k;
    int64_t bar = pos >= 0 ? pos / n : -((-pos + n - 1) / n);
    int64_t inBar = pos - bar * n;
    g.beatInBar = (int) inBar + 1;
    g.barNumber = (int) (t.snap.barNumber + bar);
    g.index = k;
    return g;
}

// Smallest k >= kMin such that the beat k is at or after hostUs.
inline int64_t firstBeatAtOrAfter(const Timeline& t, int64_t hostUs)
{
    const double per = periodSamples(t.snap);
    if (per <= 0) return 0;
    const double perUs = per * 1.0e6 / t.map.sampleRate;
    const int64_t originUs = t.map.hostTimeForSample(t.snap.beatOriginSample);
    return (int64_t) std::ceil((double) (hostUs - originUs) / perUs);
}

} // namespace pacemaker
