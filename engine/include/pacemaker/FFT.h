// Small in-repo radix-2 complex FFT. Tables are built in prepare(); forward() allocates nothing.
#pragma once
#include <cmath>
#include <vector>

namespace pacemaker {

class FFT {
public:
    void prepare(int n)
    {
        n_ = n;
        cos_.resize(n / 2); sin_.resize(n / 2); rev_.resize(n);
        for (int i = 0; i < n / 2; ++i) {
            cos_[i] = (float) std::cos(-2.0 * M_PI * i / n);
            sin_[i] = (float) std::sin(-2.0 * M_PI * i / n);
        }
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b) r |= ((i >> b) & 1) << (bits - 1 - b);
            rev_[i] = r;
        }
    }
    int size() const { return n_; }

    // In-place forward transform of (re, im), both of length size().
    void forward(float* re, float* im) const
    {
        const int n = n_;
        for (int i = 0; i < n; ++i) {
            const int j = rev_[i];
            if (j > i) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
        }
        for (int len = 2; len <= n; len <<= 1) {
            const int half = len >> 1, step = n / len;
            for (int i = 0; i < n; i += len) {
                for (int k = 0; k < half; ++k) {
                    const float wr = cos_[k * step], wi = sin_[k * step];
                    const int a = i + k, b = a + half;
                    const float tr = re[b] * wr - im[b] * wi;
                    const float ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr;        im[a] += ti;
                }
            }
        }
    }
private:
    int n_ = 0;
    std::vector<float> cos_, sin_;
    std::vector<int> rev_;
};

} // namespace pacemaker
