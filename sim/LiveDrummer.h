// Streaming simulated drummer for demos and the web UI host: renders DrumMachine bars on demand.
// Control values are atomics so the HTTP thread can change them while the audio thread renders.
#pragma once
#include "DrumMachine.h"
#include <algorithm>
#include <atomic>
#include <vector>

namespace drums {

class LiveDrummer {
public:
    explicit LiveDrummer(double sampleRate, uint64_t seed = 7) : sr_(sampleRate), seed_(seed) {}

    std::atomic<double> targetBpm { 120.0 };   // requested tempo
    std::atomic<double> jitterMs { 8.0 };      // per-hit timing noise
    std::atomic<double> drift { 0.3 };         // tempo random walk, BPM per beat
    std::atomic<bool> playing { false };
    std::atomic<bool> hihat { true };
    std::atomic<int> beatsPerBar { 4 };
    std::atomic<int> fillRequests { 0 };
    std::atomic<int> countInRequests { 0 };
    std::atomic<double> currentBpm { 120.0 };  // what the drummer actually plays (after drift)

    // Audio thread. Fills n samples.
    void render(float* out, int n)
    {
        while ((int) (queue_.size() - read_) < n + (int) (0.5 * sr_)) generate();
        for (int i = 0; i < n; ++i) out[i] = queue_[read_ + (size_t) i];
        read_ += (size_t) n;
        if (read_ > (size_t) (2 * sr_)) { queue_.erase(queue_.begin(), queue_.begin() + (long) read_); read_ = 0; }
    }

private:
    void generate()
    {
        const int lead = (int) (0.05 * sr_);   // room for hits that land before the bar line
        if (!playing.load()) {
            std::vector<float> a((size_t) (0.25 * sr_), 0.0f);
            mixAndQueue(a, 0, (int) a.size());
            return;
        }
        if (lastTarget_ != targetBpm.load()) { lastTarget_ = targetBpm.load(); bpm_ = lastTarget_; }
        Machine m(sr_, seed_++);
        m.bpm = bpm_; m.walkSigma = drift.load(); m.jitterMs = jitterMs.load();
        m.hihat8ths = hihat.load(); m.beatsPerBar = beatsPerBar.load();
        m.time = (double) lead / sr_;
        if (countInRequests.exchange(0) > 0) m.countIn(m.beatsPerBar);
        m.bars(1, fillRequests.exchange(0) > 0);
        bpm_ = m.bpm;
        currentBpm.store(bpm_);
        const int barEnd = (int) std::llround(m.time * sr_);
        if ((int) m.audio.size() < barEnd) m.audio.resize((size_t) barEnd, 0.0f);
        // Pre-roll hits (negative jitter) belong to the end of what is already queued.
        for (int i = 0; i < lead; ++i) {
            const size_t back = (size_t) (lead - i);
            if (queue_.size() >= read_ + back) queue_[queue_.size() - back] += m.audio[(size_t) i];
        }
        mixAndQueue(m.audio, lead, barEnd - lead);
    }
    // Adds the carried tail into a[from..], queues a[from, from+len) and keeps the rest as the new carry.
    void mixAndQueue(std::vector<float>& a, int from, int len)
    {
        if (a.size() < (size_t) from + carry_.size()) a.resize((size_t) from + carry_.size(), 0.0f);
        for (size_t i = 0; i < carry_.size(); ++i) a[(size_t) from + i] += carry_[i];
        for (int i = 0; i < len; ++i) queue_.push_back((size_t) (from + i) < a.size() ? a[(size_t) (from + i)] : 0.0f);
        const size_t tail = std::min(a.size(), (size_t) (from + len));
        carry_.assign(a.begin() + (long) tail, a.end());
    }
    double sr_;
    uint64_t seed_;
    double bpm_ = 120.0, lastTarget_ = -1.0;
    std::vector<float> queue_, carry_;
    size_t read_ = 0;
};

} // namespace drums
