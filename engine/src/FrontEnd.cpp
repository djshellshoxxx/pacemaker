#include "FrontEnd.h"
#include <algorithm>
#include <cmath>

namespace pacemaker {

void FrontEnd::prepare(double sampleRate, const FrontEndParams& p)
{
    p_ = p;
    sr_ = sampleRate;
    const int n = p_.window;
    fft_.prepare(n);
    ring_.assign(n, 0.0f);
    hann_.resize(n);
    for (int i = 0; i < n; ++i) hann_[i] = 0.5f - 0.5f * (float) std::cos(2.0 * M_PI * i / n);
    re_.assign(n, 0.0f); im_.assign(n, 0.0f);
    mag_.assign(n / 2 + 1, 0.0f); peak_.assign(n / 2 + 1, p_.whitenFloor); prevComp_.assign(n / 2 + 1, 0.0f);
    fluxHist_.assign(p_.localMeanM, 0.0f);
    const double binHz = sr_ / n;
    auto bin = [&](double hz) { return std::clamp((int) std::lround(hz / binHz), 1, n / 2); };
    bLow0_ = bin(20.0); bLow1_ = std::max(bLow0_, bin(150.0));
    bMid1_ = std::max(bLow1_ + 1, bin(1000.0));
    bHigh1_ = std::max(bMid1_ + 1, bin(16000.0));
    minGapFrames_ = std::max(1, (int) std::lround(p_.minGapSeconds * sr_ / p_.hop));
    reset();
}

void FrontEnd::reset()
{
    std::fill(ring_.begin(), ring_.end(), 0.0f);
    std::fill(peak_.begin(), peak_.end(), p_.whitenFloor);
    std::fill(prevComp_.begin(), prevComp_.end(), 0.0f);
    std::fill(fluxHist_.begin(), fluxHist_.end(), 0.0f);
    std::fill(std::begin(nov_), std::end(nov_), 0.0f);
    std::fill(std::begin(low_), std::end(low_), 0.0f);
    std::fill(std::begin(mid_), std::end(mid_), 0.0f);
    std::fill(std::begin(high_), std::end(high_), 0.0f);
    ringPos_ = 0; fluxPos_ = 0; frame_ = -1; lastPeakFrame_ = -1000; novNorm_ = 1e-3f;
}

void FrontEnd::append(const float* x, int n)
{
    const int N = (int) ring_.size();
    for (int i = 0; i < n; ++i) {
        ring_[ringPos_] = x[i];
        if (++ringPos_ == N) ringPos_ = 0;
    }
}

FrontEnd::FrameOut FrontEnd::analyse(int64_t frameEndSample)
{
    FrameOut out;
    const int N = p_.window;
    ++frame_;

    // Windowed copy of the last N samples (ringPos_ points at the oldest sample).
    for (int i = 0; i < N; ++i) {
        int idx = ringPos_ + i; if (idx >= N) idx -= N;
        re_[i] = ring_[idx] * hann_[i];
        im_[i] = 0.0f;
    }
    fft_.forward(re_.data(), im_.data());

    // Magnitude, adaptive whitening, log compression and half-wave rectified flux per band.
    const float scale = 2.0f / (float) N;
    float low = 0, mid = 0, high = 0;
    for (int b = 0; b <= N / 2; ++b) {
        const float m = std::sqrt(re_[b] * re_[b] + im_[b] * im_[b]) * scale;
        peak_[b] = std::max({ m, peak_[b] * p_.whitenDecay, p_.whitenFloor });
        const float comp = std::log1p(p_.gamma * (m / peak_[b]));
        const float d = std::max(0.0f, comp - prevComp_[b]);
        prevComp_[b] = comp;
        if (b < bLow0_) continue;
        if (b <= bLow1_) low += d; else if (b <= bMid1_) mid += d; else if (b <= bHigh1_) high += d;
    }
    // Per-band mean flux so that the 3-bin low band weighs as much as the 320-bin high band.
    low /= (float) (bLow1_ - bLow0_ + 1);
    mid /= (float) (bMid1_ - bLow1_);
    high /= (float) (bHigh1_ - bMid1_);
    const float full = low + mid + high;
    // Local mean subtraction over the previous M frames.
    float mean = 0; for (float v : fluxHist_) mean += v; mean /= (float) fluxHist_.size();
    fluxHist_[fluxPos_] = full; if (++fluxPos_ == (int) fluxHist_.size()) fluxPos_ = 0;
    const float nov = std::max(0.0f, full - mean);
    const int h = histIdx(frame_);
    nov_[h] = nov;
    low_[h] = low; mid_[h] = mid; high_[h] = high;
    out.novelty = nov;
    novNorm_ = std::max(nov, novNorm_ * 0.9995f);

    // Online peak picking for frame n = frame_ - w1 (w1 frames of lookahead).
    const int64_t n = frame_ - p_.w1;
    if (n < p_.w2) return out;
    const float v = nov_[histIdx(n)];
    if (v < p_.minNovelty || n - lastPeakFrame_ < minGapFrames_) return out;
    for (int64_t j = n - p_.w1; j <= n + p_.w1; ++j)
        if (j != n && nov_[histIdx(j)] > v) return out;
    float m2 = 0, s2 = 0;
    for (int64_t j = n - p_.w2; j <= n; ++j) { const float x = nov_[histIdx(j)]; m2 += x; s2 += x * x; }
    const float cnt = (float) (p_.w2 + 1);
    m2 /= cnt; s2 = std::sqrt(std::max(0.0f, s2 / cnt - m2 * m2));
    const float delta = std::max(p_.k * s2, p_.minNovelty);
    if (v < m2 + delta) return out;

    lastPeakFrame_ = n;
    const float a = nov_[histIdx(n - 1)], c = nov_[histIdx(n + 1)];
    const float den = a - 2.0f * v + c;
    float offset = (std::fabs(den) > 1e-9f) ? 0.5f * (a - c) / den : 0.0f;
    offset = std::clamp(offset, -0.5f, 0.5f);
    const double centre = (double) ((n + 1) * p_.hop) - (double) N / 2.0;
    out.hasOnset = true;
    out.onset.sample = (int64_t) std::llround(centre + offset * p_.hop) + (frameEndSample - (frame_ + 1) * p_.hop);
    out.onset.strength = std::clamp(v / std::max(novNorm_, 1e-6f), 0.0f, 1.0f);
    const float l = low_[histIdx(n)], mi = mid_[histIdx(n)], hi = high_[histIdx(n)];
    if (l >= 1.3f * mi && l >= 1.3f * hi) out.onset.tag = 1;              // KickLike: low band dominates
    else if (mi >= 0.3f * hi) out.onset.tag = 2;                          // SnareLike: body content
    else out.onset.tag = 4;                                               // Other: hats, clicks
    return out;
}

} // namespace pacemaker
