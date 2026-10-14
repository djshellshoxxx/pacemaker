// Synthetic drum machine for engine tests: kick, snare, hi-hat and stick clicks with
// tempo random walk, phase jitter, fills, silence, ramps and count-ins.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace drums {

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
    double uniform() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (double) (s >> 11) / 9007199254740992.0; }
    double gauss() { double u = uniform() + 1e-12, v = uniform(); return std::sqrt(-2.0 * std::log(u)) * std::cos(2.0 * M_PI * v); }
};

struct Beat { double time; double hitTime; int beatInBar; double bpm; bool played; };

class Machine {
public:
    explicit Machine(double sampleRate, uint64_t seed = 1) : sr(sampleRate), rng(seed) {}

    double sr;
    Rng rng;
    std::vector<float> audio;
    std::vector<Beat> beats;
    double bpm = 120.0, time = 0.0;
    double walkSigma = 0.3, jitterMs = 10.0;
    bool hihat8ths = false;
    int beatsPerBar = 4;

    void silence(double seconds) { time += seconds; ensure(time); }

    // Four stick clicks at the current tempo (SnareLike/Other tags). Leaves time at the next beat.
    void countIn(int clicks = 4)
    {
        for (int i = 0; i < clicks; ++i) { click(time, 0.9f); time += 60.0 / bpm; }
    }

    // Render bars. Options: fill = 16th snare roll, silent = beats pass without hits,
    // rampTo > 0 = linear tempo change to rampTo over these bars.
    void bars(int n, bool fill = false, bool silent = false, double rampTo = 0.0)
    {
        const int total = n * beatsPerBar;
        const double ramp = rampTo > 0.0 ? (rampTo - bpm) / total : 0.0;
        for (int k = 0; k < total; ++k) {
            const int pos = k % beatsPerBar + 1;
            const double period = 60.0 / bpm;
            const double jitter = silent ? 0.0 : jitterMs * 1e-3 * rng.gauss();
            Beat b { time, time + jitter, pos, bpm, !silent };
            if (!silent) {
                const bool odd = (pos % 2) == 1;
                if (fill) {
                    if (pos == 1) kick(b.hitTime, 1.0f);
                    for (int s = 0; s < 4; ++s) snare(time + s * period / 4 + jitterMs * 1e-3 * rng.gauss(), s == 0 ? 0.9f : 0.6f);
                } else if (odd) kick(b.hitTime, 1.0f);
                else snare(b.hitTime, 0.9f);
                if (hihat8ths) {
                    if (!fill) hihat(b.hitTime, 0.5f);
                    hihat(time + period / 2 + jitterMs * 1e-3 * rng.gauss(), 0.4f);
                }
            }
            beats.push_back(b);
            time += period;
            if (ramp != 0.0) bpm += ramp;
            else if (walkSigma > 0.0) bpm += walkSigma * rng.gauss();
        }
        ensure(time);
    }

    // Linear fade over the last 10 ms so truncation does not add a click.
    static double fade(int i, int n, double sr) { const int f = (int) (0.010 * sr); return i >= n - f ? (double) (n - i) / f : 1.0; }

    void kick(double t, float vel)
    {
        const int n = (int) (0.080 * sr);
        const int64_t s0 = (int64_t) (t * sr);
        ensure(t + 0.09);
        double ph = 0.0;
        for (int i = 0; i < n; ++i) {
            const double x = (double) i / sr;
            const double f = 60.0 + 40.0 * std::exp(-x / 0.01);
            ph += 2.0 * M_PI * f / sr;
            audio[(size_t) (s0 + i)] += vel * 0.8f * (float) (std::sin(ph) * std::exp(-x / 0.025) * fade(i, n, sr));
        }
    }
    void snare(double t, float vel)
    {
        const int n = (int) (0.060 * sr);
        const int64_t s0 = (int64_t) (t * sr);
        ensure(t + 0.07);
        // Noise through a resonant 2 kHz band-pass plus a 200 Hz body tone.
        const double w = 2.0 * M_PI * 2000.0 / sr, r = 0.95;
        const double a1 = -2.0 * r * std::cos(w), a2 = r * r;
        double y1 = 0, y2 = 0;
        for (int i = 0; i < n; ++i) {
            const double x = (double) i / sr;
            const double noise = (rng.uniform() * 2.0 - 1.0);
            const double y = noise - a1 * y1 - a2 * y2;
            y2 = y1; y1 = y;
            const double body = 0.4 * std::sin(2.0 * M_PI * 200.0 * x) * std::exp(-x / 0.03);
            audio[(size_t) (s0 + i)] += vel * (float) ((0.15 * y + body) * std::exp(-x / 0.02) * fade(i, n, sr));
        }
    }
    void hihat(double t, float vel)
    {
        const int n = (int) (0.020 * sr);
        const int64_t s0 = (int64_t) (t * sr);
        ensure(t + 0.03);
        double prev = 0;
        for (int i = 0; i < n; ++i) {
            const double x = (double) i / sr;
            const double noise = rng.uniform() * 2.0 - 1.0;
            const double hp = noise - prev; prev = noise;   // crude high-pass
            audio[(size_t) (s0 + i)] += vel * 0.25f * (float) (hp * std::exp(-x / 0.006) * fade(i, n, sr));
        }
    }
    void click(double t, float vel)
    {
        const int n = (int) (0.012 * sr);
        const int64_t s0 = (int64_t) (t * sr);
        ensure(t + 0.01);
        for (int i = 0; i < n; ++i) {
            const double x = (double) i / sr;
            audio[(size_t) (s0 + i)] += vel * (float) (std::sin(2.0 * M_PI * 3000.0 * x) * std::exp(-x / 0.001)
                                                       + 0.3 * (rng.uniform() * 2.0 - 1.0) * std::exp(-x / 0.002)) * fade(i, n, sr);
        }
    }
    void addNoiseFloor(float amp = 1e-4f)
    {
        for (auto& v : audio) v += amp * (float) (rng.uniform() * 2.0 - 1.0);
    }

private:
    void ensure(double seconds)
    {
        const size_t need = (size_t) (seconds * sr) + 1;
        if (audio.size() < need) audio.resize(need, 0.0f);
    }
};

} // namespace drums
