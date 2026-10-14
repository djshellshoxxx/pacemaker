#include "pacemaker/SyncRenderer.h"
#include "pacemaker/ClockMap.h"
#include <algorithm>
#include <cmath>

namespace pacemaker {

void SyncRenderer::prepare(double sampleRate)
{
    sr_ = sampleRate;
    pulseLen_ = std::max(1, (int) std::lround(0.001 * sr_));
    clickLen_ = std::max(2, (int) std::lround(0.002 * sr_));
    ticks_.reserve(64); beats_.reserve(16);
    reset();
}

void SyncRenderer::reset()
{
    pulseLeft_ = 0; clickPos_ = -1; running_ = false; haveLast_ = false; lastTick_ = 0;
    ticks_.clear(); beats_.clear();
}

void SyncRenderer::render(const BeatMapSnapshot& s, int beatsPerBar, int64_t blockStart, int n, const SyncConfig& cfg, float* L, float* R)
{
    ticks_.clear(); beats_.clear();
    const double per = periodSamples(s);
    const bool valid = s.bpm > 0.0 && per > 0.0;
    if (!cfg.enabled) { running_ = false; haveLast_ = false; pulseLeft_ = 0; clickPos_ = -1; return; }
    if (running_ && s.state == (uint8_t) EngineState::Idle) { running_ = false; haveLast_ = false; }
    if (!running_ && valid && s.state == (uint8_t) EngineState::Locked) running_ = true;

    const float polarity = cfg.invertPolarity ? -1.0f : 1.0f;
    const float amp = cfg.level * polarity;
    const double offs = cfg.offsetMs * 0.001 * sr_;
    const int bpb = std::max(1, beatsPerBar);

    // Tick event list for this block.
    struct Ev { int pos; bool beat; bool down; };
    Ev evs[64]; int ne = 0;
    if (running_ && valid) {
        const double tickP = per / 24.0;
        const double origin = (double) s.beatOriginSample + offs;
        if (!haveLast_) { lastTick_ = (double) blockStart - 0.5 * tickP; haveLast_ = true; }
        for (int guard = 0; guard < 64; ++guard) {
            const double nTick = std::ceil((lastTick_ + 0.5 * tickP - origin) / tickP);
            const double t = origin + nTick * tickP;
            if (t >= (double) (blockStart + n)) break;
            lastTick_ = t;
            if (t < (double) blockStart) continue;
            const int64_t idx = (int64_t) nTick;
            const bool beat = ((idx % 24) + 24) % 24 == 0;
            bool down = false;
            if (beat) {
                const int64_t k = idx / 24;                      // beats from the origin (exact multiple)
                const int64_t pos = (int64_t) (s.beatInBar - 1) + k;
                down = (((pos % bpb) + bpb) % bpb) == 0;
            }
            const int p = (int) std::llround(t) - (int) blockStart;
            evs[ne++] = { std::clamp(p, 0, n - 1), beat, down };
            ticks_.push_back(blockStart + std::clamp(p, 0, n - 1));
            if (beat) beats_.push_back(blockStart + std::clamp(p, 0, n - 1));
        }
    }

    int e = 0;
    for (int i = 0; i < n; ++i) {
        while (e < ne && evs[e].pos == i) {
            pulseLeft_ = pulseLen_;
            if (evs[e].beat) { clickPos_ = 0; clickFreq_ = evs[e].down ? 2000.0 : 1000.0; }
            ++e;
        }
        if (pulseLeft_ > 0) { L[i] += amp; --pulseLeft_; }
        if (clickPos_ >= 0) {
            const double ph = (double) clickPos_ / (double) clickLen_;
            const double env = 0.5 - 0.5 * std::cos(2.0 * M_PI * ph);   // Hann: no click from the burst itself
            R[i] += amp * (float) (env * std::sin(2.0 * M_PI * clickFreq_ * (double) clickPos_ / sr_));
            if (++clickPos_ >= clickLen_) clickPos_ = -1;
        }
    }
}

} // namespace pacemaker
