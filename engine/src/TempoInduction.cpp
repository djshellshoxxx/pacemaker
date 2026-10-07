#include "TempoInduction.h"
#include <algorithm>
#include <cmath>

namespace pacemaker {

void TempoInduction::prepare(double sampleRate, double minBpm, double maxBpm, double kernelSeconds, double referenceBpm)
{
    sr_ = sampleRate; fps_ = 100.0;
    minBpm_ = std::max(20.0, minBpm); maxBpm_ = std::max(minBpm_ + 2.0, maxBpm);
    numBins_ = (int) std::lround(maxBpm_ - minBpm_) + 1;
    K_ = std::max(50, (int) std::lround(kernelSeconds * fps_));
    ring_.assign(K_, 0.0f);
    win_.resize(K_);
    for (int i = 0; i < K_; ++i) win_[i] = 0.5f - 0.5f * (float) std::cos(2.0 * M_PI * (i + 0.5) / K_);
    cosT_.resize((size_t) numBins_ * K_); sinT_.resize((size_t) numBins_ * K_);
    prior_.resize(numBins_); mag_.assign(numBins_, 0.0f); smooth_.assign(numBins_, 0.0f);
    for (int b = 0; b < numBins_; ++b) {
        const double bpm = minBpm_ + b;
        const double w = 2.0 * M_PI * (bpm / 60.0) / fps_;
        for (int i = 0; i < K_; ++i) {
            cosT_[(size_t) b * K_ + i] = (float) std::cos(w * i);
            sinT_[(size_t) b * K_ + i] = (float) std::sin(w * i);
        }
        if (referenceBpm > 0.0) {
            const double z = std::log(bpm / referenceBpm) / 0.12;
            prior_[b] = (float) std::exp(-0.5 * z * z);
        } else {
            const double s = 120.0; // Rayleigh with mode at 120 BPM, normalised to 1 at the mode
            prior_[b] = (float) ((bpm / (s * s)) * std::exp(-bpm * bpm / (2 * s * s)) / (std::exp(-0.5) / s));
        }
    }
    reset();
}

void TempoInduction::reset()
{
    std::fill(ring_.begin(), ring_.end(), 0.0f);
    std::fill(mag_.begin(), mag_.end(), 0.0f);
    ringPos_ = 0; curBin_ = -1; acc_ = 0.0f; accN_ = 0;
    numPeaks_ = 0; peaks_[0] = peaks_[1] = peaks_[2] = Peak {};
    stability_ = 0.0f; maxSeen_ = 1e-6f; prevBest_ = 0.0; stableFrames_ = 0;
}

void TempoInduction::push(int64_t sample, float novelty)
{
    const int64_t bin = (int64_t) ((double) sample * fps_ / sr_);
    if (curBin_ < 0) curBin_ = bin;
    while (bin > curBin_) {
        ring_[ringPos_] = accN_ > 0 ? acc_ / (float) accN_ : 0.0f;
        if (++ringPos_ == K_) ringPos_ = 0;
        acc_ = 0.0f; accN_ = 0; ++curBin_;
        computeFrame();
    }
    acc_ += novelty; ++accN_;
}

void TempoInduction::computeFrame()
{
    // Fourier tempogram magnitude per tempo bin over the Hann-windowed last K_ frames.
    for (int b = 0; b < numBins_; ++b) {
        const float* ct = &cosT_[(size_t) b * K_];
        const float* st = &sinT_[(size_t) b * K_];
        float re = 0.0f, im = 0.0f;
        int idx = ringPos_; // oldest sample
        for (int i = 0; i < K_; ++i) {
            const float v = ring_[idx] * win_[i];
            re += v * ct[i]; im += v * st[i];
            if (++idx == K_) idx = 0;
        }
        mag_[b] = std::sqrt(re * re + im * im) * prior_[b];
    }
    for (int b = 0; b < numBins_; ++b) {
        const float l = mag_[std::max(0, b - 1)], r = mag_[std::min(numBins_ - 1, b + 1)];
        smooth_[b] = (l + mag_[b] + r) / 3.0f;
    }
    numPeaks_ = 0;
    Peak cand[3];
    for (int b = 1; b < numBins_ - 1; ++b) {
        const float v = smooth_[b];
        if (v <= smooth_[b - 1] || v < smooth_[b + 1] || v <= 1e-9f) continue;
        Peak p; p.mag = v;
        const float a = smooth_[b - 1], c = smooth_[b + 1], den = a - 2 * v + c;
        const double off = std::fabs(den) > 1e-12f ? std::clamp(0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
        p.bpm = minBpm_ + b + off;
        for (int k = 0; k < 3; ++k) {
            if (k >= numPeaks_ || p.mag > cand[k].mag) {
                for (int j = std::min(numPeaks_, 2); j > k; --j) cand[j] = cand[j - 1];
                cand[k] = p; numPeaks_ = std::min(3, numPeaks_ + 1);
                break;
            }
        }
    }
    for (int k = 0; k < 3; ++k) peaks_[k] = k < numPeaks_ ? cand[k] : Peak {};
    maxSeen_ = std::max(peaks_[0].mag, maxSeen_ * 0.9995f);
    stability_ = maxSeen_ > 1e-6f ? std::clamp(peaks_[0].mag / maxSeen_, 0.0f, 1.0f) : 0.0f;
    if (numPeaks_ > 0 && std::fabs(peaks_[0].bpm - prevBest_) <= 2.0 && stability_ > 0.3f) ++stableFrames_;
    else stableFrames_ = 0;
    prevBest_ = numPeaks_ > 0 ? peaks_[0].bpm : 0.0;
}

float TempoInduction::magnitudeAt(double bpm) const
{
    const int b = (int) std::lround(bpm - minBpm_);
    if (b < 0 || b >= numBins_) return 0.0f;
    return smooth_[b];
}

float TempoInduction::octaveRatio(double bpm) const
{
    if (numPeaks_ == 0 || peaks_[0].mag <= 0.0f) return 0.0f;
    float best = 0.0f;
    for (double f : { 2.0, 0.5 }) {
        const double target = bpm * f;
        for (int k = 0; k < numPeaks_; ++k)
            if (std::fabs(peaks_[k].bpm - target) <= 0.04 * target) best = std::max(best, peaks_[k].mag / peaks_[0].mag);
    }
    return best;
}

} // namespace pacemaker
