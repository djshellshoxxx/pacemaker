// Sync bus: left = 24 ppqn pulse, right = beat click (ES-02 section 5). Sample accurate from the snapshot.
#pragma once
#include "pacemaker/Types.h"
#include <vector>

namespace pacemaker {

struct SyncConfig {
    bool enabled = false;
    bool invertPolarity = false;
    double offsetMs = 0.0;
    float level = 0.9f;
};

class SyncRenderer {
public:
    void prepare(double sampleRate);
    void reset();
    // Adds into L/R (caller zeroes them). Ticks falling inside [blockStart, blockStart + n) are rendered.
    void render(const BeatMapSnapshot& s, int beatsPerBar, int64_t blockStart, int n, const SyncConfig& cfg, float* L, float* R);
    // Sample positions of tick starts that were rendered in the last call (for tests and the O4 check).
    const std::vector<int64_t>& lastTickSamples() const { return ticks_; }
    const std::vector<int64_t>& lastBeatSamples() const { return beats_; }
private:
    double sr_ = 48000.0;
    int pulseLen_ = 48, clickLen_ = 96;
    int pulseLeft_ = 0, clickPos_ = -1;
    double clickFreq_ = 1000.0;
    bool running_ = false, haveLast_ = false;
    double lastTick_ = 0;
    std::vector<int64_t> ticks_, beats_;
};

} // namespace pacemaker
