// Audio pulse output (ES-02 section 5): left = 24 ppqn pulse, right = beat click (1 kHz, downbeat 2 kHz).
#pragma once
#include "ClockMap.h"

namespace pacemaker {

struct SyncConfig { bool invertPulse = false; double pulseMs = 1.0; double clickMs = 2.0; float pulseLevel = 2.0f; /* +6 dBFS */ };

// Renders samples [blockStart, blockStart + n) of the grid described by the snapshot. Output is
// overwritten. Events are placed to the nearest sample; a pulse that started before the block is
// continued, so block size never changes the result.
void renderSync(const BeatMapSnapshot& s, int beatsPerBar, double sampleRate, int64_t blockStart, int n,
                float* left, float* right, const SyncConfig& cfg = {});

} // namespace pacemaker
