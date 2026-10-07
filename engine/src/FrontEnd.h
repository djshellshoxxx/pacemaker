// Per-role onset front end (ES-01 section 4).
#pragma once
#include "pacemaker/FFT.h"
#include <cstdint>
#include <vector>

namespace pacemaker {

struct FrontEndParams {
    int window = 1024;
    int hop = 256;
    float gamma = 1000.0f;        // log compression
    float whitenDecay = 0.997f;   // adaptive whitening peak decay per frame
    float whitenFloor = 0.02f;    // minimum peak (level floor), magnitude units (FS sine = 0.5)
    int localMeanM = 10;          // local mean subtraction length
    int w1 = 3;                   // peak neighbourhood (frames, both sides)
    int w2 = 10;                  // mean/std history (frames)
    float k = 1.0f;               // delta = k * running std
    float minNovelty = 0.3f;      // absolute floor (silence guard), band-mean flux units
    double minGapSeconds = 0.030;
};

class FrontEnd {
public:
    struct Onset { int64_t sample; float strength; uint8_t tag; };
    struct FrameOut { float novelty = 0.0f; bool hasOnset = false; Onset onset {}; };

    void prepare(double sampleRate, const FrontEndParams& p);
    void reset();

    // Append n samples (n <= hop remaining in the current frame).
    void append(const float* x, int n);

    // Called by the engine exactly once per hop. frameEndSample is the engine sample index
    // just after the last sample of this frame. Returns the full-band novelty of the frame
    // and the onset decided for frame (current - w1), if any.
    FrameOut analyse(int64_t frameEndSample);

    // Delay from an onset's true time to the moment it is reported, in samples (approx).
    int delaySamples() const { return p_.window / 2 + (p_.w1 + 1) * p_.hop; }

private:
    static constexpr int kHist = 64;
    int histIdx(int64_t f) const { return (int) (f & (kHist - 1)); }

    FrontEndParams p_;
    double sr_ = 48000.0;
    FFT fft_;
    std::vector<float> ring_, hann_, re_, im_, mag_, peak_, prevComp_;
    int ringPos_ = 0;
    int bLow0_ = 1, bLow1_ = 3, bMid1_ = 21, bHigh1_ = 341; // bin ranges [b0, b1]
    std::vector<float> fluxHist_;      // last M raw full-band fluxes
    int fluxPos_ = 0;
    float nov_[kHist] {}, low_[kHist] {}, mid_[kHist] {}, high_[kHist] {};
    int64_t frame_ = -1;               // index of the latest analysed frame
    int64_t lastPeakFrame_ = -1000;
    float novNorm_ = 1e-3f;            // decaying max for strength normalisation
    int minGapFrames_ = 6;
};

} // namespace pacemaker
