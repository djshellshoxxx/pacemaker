#include "pacemaker/SyncRenderer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace pacemaker {

void renderSync(const BeatMapSnapshot& s, int beatsPerBar, double sr, int64_t blockStart, int n,
                float* left, float* right, const SyncConfig& cfg)
{
    std::memset(left, 0, sizeof(float) * (size_t) n);
    std::memset(right, 0, sizeof(float) * (size_t) n);
    if ((EngineState) s.state == EngineState::Idle) return;
    const BeatGrid g = gridFromSnapshot(s, beatsPerBar);
    if (!g.valid()) return;
    const int pulseLen = std::max(1, (int) std::lround(cfg.pulseMs * 1e-3 * sr));
    const int clickLen = std::max(1, (int) std::lround(cfg.clickMs * 1e-3 * sr));
    const float pol = cfg.invertPulse ? -cfg.pulseLevel : cfg.pulseLevel;

    // Pulses: tick index m (24 per beat) at origin + m * period / 24.
    const double tick = g.periodSamples / 24.0;
    const int64_t m0 = (int64_t) std::ceil(((double) blockStart - pulseLen - g.originSample) / tick);
    for (int64_t m = m0;; ++m) {
        const int64_t at = (int64_t) std::llround(g.originSample + (double) m * tick);
        if (at >= blockStart + n) break;
        for (int i = std::max<int64_t>(0, blockStart - at); i < pulseLen && at + i < blockStart + n; ++i)
            left[at + i - blockStart] = pol;
    }
    // Clicks: beat k at origin + k * period, a sine burst with a short fade.
    const int64_t k0 = (int64_t) std::ceil(((double) blockStart - clickLen - g.originSample) / g.periodSamples);
    for (int64_t k = k0;; ++k) {
        const int64_t at = (int64_t) std::llround(g.sampleOfBeat(k));
        if (at >= blockStart + n) break;
        const double f = g.beatInBarAt(k) == 1 ? 2000.0 : 1000.0;
        for (int i = std::max<int64_t>(0, blockStart - at); i < clickLen && at + i < blockStart + n; ++i) {
            const double env = 1.0 - (double) i / clickLen;
            right[at + i - blockStart] = (float) (0.8 * env * std::sin(2.0 * M_PI * f * (double) i / sr));
        }
    }
}

} // namespace pacemaker
