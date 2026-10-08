// Sample time <-> host time (ES-02 section 1) and beat grid helpers.
#pragma once
#include "pacemaker/Types.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace pacemaker {

// Written by the audio thread (addBlock), read by any thread through a seqlock.
// Fits host time against block start samples with a 512 point least squares fallback
// (the role of ableton::link::HostTimeFilter), so jittery callback timestamps are smoothed.
class ClockMap {
public:
    void prepare(double sampleRate)
    {
        sr_ = sampleRate; nominal_ = 1e6 / sampleRate;
        n_ = 0; head_ = 0;
        publish({ 0.0, 0.0 });
    }
    // Audio thread. sample = first sample of the block, hostUs = host clock when it was captured.
    void addBlock(int64_t sample, int64_t hostUs)
    {
        if (n_ == 0) base_ = { sample, hostUs };
        const double x = (double) (sample - base_.sample), y = (double) (hostUs - base_.hostUs) - x * nominal_;
        px_[head_] = x; py_[head_] = y;
        head_ = (head_ + 1) % kPoints; if (n_ < kPoints) ++n_;
        double sx = 0, sy = 0;
        for (int i = 0; i < n_; ++i) { sx += px_[i]; sy += py_[i]; }
        const double mx = sx / n_, my = sy / n_;
        double sxx = 0, sxy = 0;
        for (int i = 0; i < n_; ++i) { sxx += (px_[i] - mx) * (px_[i] - mx); sxy += (px_[i] - mx) * (py_[i] - my); }
        const double slope = (n_ >= 8 && sxx > 0) ? sxy / sxx : 0.0;
        publish({ my - slope * mx, slope });
    }

    // Microseconds the host clock is behind the sample clock because of input and acoustic delay:
    // a hit seen at sample s happened inputCompUs earlier (equivalent to correcting every onset).
    void setInputCompUs(double us) { inputComp_.store(us, std::memory_order_relaxed); }
    void setOutputLatencyUs(double us) { outputLat_.store(us, std::memory_order_relaxed); }

    // Host time at which sample s leaves the system (includes output latency, excludes per-output offsets).
    double hostTimeForSample(double s) const
    {
        const P p = read();
        const double x = s - (double) base_.sample;
        return (double) base_.hostUs + x * nominal_ + p.offset + p.slope * x
               - inputComp_.load(std::memory_order_relaxed) + outputLat_.load(std::memory_order_relaxed);
    }
    double sampleForHostTime(double us) const
    {
        const P p = read();
        const double rate = nominal_ + p.slope;   // us per sample
        return (double) base_.sample + (us - (double) base_.hostUs - p.offset + inputComp_.load(std::memory_order_relaxed)
                                        - outputLat_.load(std::memory_order_relaxed)) / rate;
    }
    double sampleRate() const { return sr_; }

private:
    static constexpr int kPoints = 512;
    struct P { double offset, slope; };
    struct Base { int64_t sample = 0, hostUs = 0; } base_;
    void publish(P p)
    {
        const uint32_t v = seq_.load(std::memory_order_relaxed);
        seq_.store(v + 1, std::memory_order_release);
        std::atomic_thread_fence(std::memory_order_release);
        p_ = p;
        std::atomic_thread_fence(std::memory_order_release);
        seq_.store(v + 2, std::memory_order_release);
    }
    P read() const
    {
        for (;;) {
            const uint32_t a = seq_.load(std::memory_order_acquire);
            if (a & 1u) continue;
            std::atomic_thread_fence(std::memory_order_acquire);
            const P p = p_;
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq_.load(std::memory_order_acquire) == a) return p;
        }
    }
    double sr_ = 48000.0, nominal_ = 1e6 / 48000.0;
    double px_[kPoints] {}, py_[kPoints] {};
    int n_ = 0, head_ = 0;
    P p_ { 0.0, 0.0 };
    mutable std::atomic<uint32_t> seq_ { 0 };
    std::atomic<double> inputComp_ { 0.0 }, outputLat_ { 0.0 };
};

// Beat grid derived from a snapshot: beat k (k may be negative) is at origin + k * period samples.
struct BeatGrid {
    double originSample = 0.0, periodSamples = 0.0;
    int beatInBar = 1, bar = 0, beatsPerBar = 4;
    bool valid() const { return periodSamples > 1.0; }
    double sampleOfBeat(int64_t k) const { return originSample + (double) k * periodSamples; }
    int beatInBarAt(int64_t k) const
    {
        const int64_t v = ((int64_t) (beatInBar - 1) + k) % beatsPerBar;
        return (int) (v < 0 ? v + beatsPerBar : v) + 1;
    }
    int barAt(int64_t k) const
    {
        const int64_t total = (int64_t) (beatInBar - 1) + k;
        int64_t q = total / beatsPerBar; if (total % beatsPerBar < 0) --q;
        return bar + (int) q;
    }
    // Fractional beat index for a sample (can be negative).
    double beatAt(double sample) const { return (sample - originSample) / periodSamples; }
    double barPhaseAt(double sample) const
    {
        double b = (double) (beatInBar - 1) + beatAt(sample);
        b = std::fmod(b, (double) beatsPerBar); if (b < 0) b += beatsPerBar;
        return b / beatsPerBar;
    }
};

inline BeatGrid gridFromSnapshot(const BeatMapSnapshot& s, int beatsPerBar)
{
    BeatGrid g;
    g.originSample = (double) s.beatOriginSample;
    g.periodSamples = (double) (s.nextBeatSample - s.beatOriginSample);
    g.beatInBar = s.beatInBar < 1 ? 1 : s.beatInBar;
    g.bar = s.barNumber;
    g.beatsPerBar = beatsPerBar < 1 ? 1 : beatsPerBar;
    return g;
}

// Wall clock helpers shared by the outputs.
int64_t steadyNowUs();
// Offset in microseconds such that ntp = hostUs + offset (host clock = steady clock).
int64_t ntpOffsetUs();

} // namespace pacemaker
