// Phase/period tracker, supervisor state machine and confidence fusion (ES-01 sections 6, 8).
#pragma once
#include "BarTracker.h"
#include "TempoInduction.h"
#include "pacemaker/SpscQueue.h"
#include "pacemaker/Types.h"

namespace pacemaker {

struct FusedOnset { int64_t sample; float strength; uint8_t tag; };

class Tracker {
public:
    using EventQueue = SpscQueue<EngineEvent, 1024>;

    void prepare(const EngineConfig& cfg, EventQueue* out, const TempoInduction* induction);
    void reset();

    void onOnset(const FusedOnset& o);   // onsets arrive in time order, o.sample <= now
    void tick(int64_t now);              // advance the clock (once per hop)
    void fillSnapshot(BeatMapSnapshot& s) const;

    // User actions.
    void tap(int64_t sample);
    void downbeatNow();
    void shiftBar(int delta);
    void relockNow();
    void scalePeriod(double factor);
    void setFollow(bool follow);

private:
    struct Hist { int64_t t; float s; };
    static constexpr int kHist = 64;

    void emitBeat();
    void closeBeat(int64_t now);
    void startBeatAt(double phi, EngineState s, int pos, int bar);
    void goIdle();
    void countInDetect(const FusedOnset& o);
    void maybeStartLocking(int64_t now);
    void updateConfidence();
    double bpmOf(double tau) const { return 60.0 * sr_ / tau; }
    double clampTau(double t) const;

    EngineConfig cfg_;
    double sr_ = 48000.0, minTau_ = 0, maxTau_ = 0, sigmaMeas_ = 0;
    EventQueue* out_ = nullptr;
    const TempoInduction* induction_ = nullptr;
    BarTracker bar_;

    EngineState state_ = EngineState::Idle;
    bool follow_ = true;
    double phi_ = 0, tau_ = 0;                 // predicted time of the open beat, period (samples)
    double pubPhi_ = 0, pubTau_ = 0, pubBpm_ = 0;
    bool hasPublished_ = false, relockPending_ = false, relockFlag_ = false, emitted_ = false;
    int barNumber_ = 0;
    int64_t lastTick_ = 0, holdStart_ = 0, octaveSince_ = -1;
    bool octaveFlag_ = false;

    // Per-beat measurement bookkeeping.
    bool hit_ = false; double bestE_ = 0, bestSw_ = 1; float bestW_ = 0;
    bool evKick_ = false, evSnare_ = false, evStrong_ = false;
    uint8_t hitMask_ = 0; double innov_[8] {}; int innovN_ = 0, innovPos_ = 0;
    int missCount_ = 0, lockHits_ = 0, chaseBeats_ = 0, sameSign_ = 0, lastSign_ = 0;
    float confidence_ = 0.0f;
    double lastBpm_ = 0.0;

    // Count-in.
    int clicks_ = 0; int64_t clickT_[4] {}; int64_t prevOnset_ = -1;
    // Hold re-entry.
    int64_t holdOutside_[2] {}; int holdOutsideN_ = 0;
    // Tap.
    int64_t lastTap_ = -1;
    // Onset history for phase initialisation.
    Hist hist_[kHist] {}; int histPos_ = 0, histN_ = 0;
};

} // namespace pacemaker
