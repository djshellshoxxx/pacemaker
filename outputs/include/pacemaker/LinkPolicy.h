// Ableton Link publishing policy (ES-02 section 2). The policy is licence-free and talks to Link
// through LinkSession; the real adapter (PACEMAKER_WITH_LINK) wraps ableton::Link and is added when
// the Ableton licence is in place. A mock session drives the tests.
#pragma once
#include "ClockMap.h"

namespace pacemaker {

class LinkSession {
public:
    virtual ~LinkSession() = default;
    virtual double tempo() = 0;
    virtual void setTempo(double bpm, int64_t atUs) = 0;
    virtual void forceBeatAtTime(double beat, int64_t atUs, double quantum) = 0;
    virtual double beatAtTime(int64_t us, double quantum) = 0;
    virtual int numPeers() = 0;
};

struct LinkConfig {
    bool enabled = false, publishTempo = true;
    int quantum = 4;
    double gate = 0.35, commitIntervalMs = 50.0, deadbandBpm = 0.05;
    bool rehearsal = false;   // 30 ms / 0.02 BPM instead of 100 ms / 0.1 BPM (section 2)
};

class LinkPolicy {
public:
    void configure(const LinkConfig& c) { cfg_ = c; }
    // Call from the audio thread once per block. nowUs is the host time of the block start.
    void update(const BeatMapSnapshot& s, const ClockMap& map, int64_t nowUs, LinkSession& link);
    int forceCount() const { return forceCount_; }          // test hook (O2)
    int tempoCommits() const { return tempoCommits_; }
    bool fightingPeers() const { return fighting_; }

private:
    LinkConfig cfg_;
    int64_t lastCommitUs_ = -1000000000;
    uint32_t lastRelockSeq_ = 0;
    int forceCount_ = 0, tempoCommits_ = 0, driftBeats_ = 0;
    int64_t lastBeatChecked_ = INT64_MIN;
    double lastSetBpm_ = 0.0;
    int64_t foreignChanges_[2] = { -1000000000, -1000000000 };
    bool fighting_ = false;
};

} // namespace pacemaker
