// PLP-style Fourier tempogram on the fused novelty curve at 100 fps (ES-01 section 5).
#pragma once
#include <cstdint>
#include <vector>

namespace pacemaker {

class TempoInduction {
public:
    struct Peak { double bpm = 0.0; float mag = 0.0f; };

    void prepare(double sampleRate, double minBpm, double maxBpm, double kernelSeconds, double referenceBpm);
    void reset();

    // Novelty value with its time (engine samples). Values are accumulated into 10 ms frames.
    void push(int64_t sample, float novelty);

    double bestBpm() const { return peaks_[0].bpm; }
    const Peak* peaks() const { return peaks_; }
    int numPeaks() const { return numPeaks_; }
    float stability() const { return stability_; }
    double stableSeconds() const { return stableFrames_ / fps_; }
    // Ratio of the strongest peak near 2x or 0.5x of the given BPM to the best peak (0 if none).
    float octaveRatio(double bpm) const;
    float magnitudeAt(double bpm) const;

private:
    void computeFrame();

    double sr_ = 48000.0, fps_ = 100.0, minBpm_ = 50.0, maxBpm_ = 220.0;
    int numBins_ = 171, K_ = 500;
    std::vector<float> ring_;          // novelty history (K_)
    int ringPos_ = 0;
    std::vector<float> win_, cosT_, sinT_, prior_, mag_, smooth_;
    int64_t curBin_ = -1; float acc_ = 0.0f; int accN_ = 0;
    Peak peaks_[3]; int numPeaks_ = 0;
    float stability_ = 0.0f, maxSeen_ = 1e-6f;
    double prevBest_ = 0.0; int stableFrames_ = 0;
};

} // namespace pacemaker
