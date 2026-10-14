#include "pacemaker/LinkPolicy.h"
#include <cmath>

namespace pacemaker {

void LinkPolicy::update(const BeatMapSnapshot& s, const ClockMap& map, int64_t nowUs, LinkSession& link)
{
    if (!cfg_.enabled || (EngineState) s.state == EngineState::Idle) return;
    const double commitInterval = cfg_.rehearsal ? 30.0 : cfg_.commitIntervalMs;
    const double deadband = cfg_.rehearsal ? 0.02 : cfg_.deadbandBpm;
    const BeatGrid g = gridFromSnapshot(s, cfg_.quantum);
    if (!g.valid()) return;

    // Another app changing tempo away from ours: warn after two jumps over 2 BPM within 10 s.
    const double linkTempo = link.tempo();
    if (lastSetBpm_ > 0 && std::fabs(linkTempo - lastSetBpm_) > 2.0) {
        foreignChanges_[0] = foreignChanges_[1]; foreignChanges_[1] = nowUs;
        lastSetBpm_ = linkTempo;
        fighting_ = nowUs - foreignChanges_[0] < 10000000;
    } else if (nowUs - foreignChanges_[1] > 10000000) fighting_ = false;

    if ((s.flags & SnapshotFlags::Relock) && s.sequence != lastRelockSeq_) {
        lastRelockSeq_ = s.sequence;
        const int64_t beatTimeUs = (int64_t) std::llround(map.hostTimeForSample(g.originSample));
        const double beat = (double) (s.barNumber * cfg_.quantum + (s.beatInBar - 1));
        link.forceBeatAtTime(beat, beatTimeUs, cfg_.quantum);
        ++forceCount_;
        driftBeats_ = 0;
        return;
    }
    if (!cfg_.publishTempo || s.confidence < (float) cfg_.gate) return;
    if ((double) (nowUs - lastCommitUs_) >= commitInterval * 1000.0 && std::fabs(s.bpm - linkTempo) > deadband) {
        link.setTempo(s.bpm, nowUs);
        lastSetBpm_ = s.bpm; lastCommitUs_ = nowUs; ++tempoCommits_;
    }
    // Phase drift: compare Link's fractional beat at our next beat time with the integer we predict.
    const int64_t k = (int64_t) std::floor(g.beatAt(map.sampleForHostTime((double) nowUs))) + 1;
    if (k != lastBeatChecked_) {
        lastBeatChecked_ = k;
        const int64_t t = (int64_t) std::llround(map.hostTimeForSample(g.sampleOfBeat(k)));
        const double beatNo = (double) (g.barAt(k) * cfg_.quantum + (g.beatInBarAt(k) - 1));
        const double err = link.beatAtTime(t, cfg_.quantum) - beatNo;   // phase error in beats
        if (std::fabs(err) > 0.02) ++driftBeats_; else driftBeats_ = 0;
        if (driftBeats_ >= 4) {
            // Slew by a +-0.2 BPM tempo nudge for one beat; never force.
            link.setTempo(s.bpm + (err > 0 ? -0.2 : 0.2), nowUs);
            lastSetBpm_ = s.bpm + (err > 0 ? -0.2 : 0.2);
            driftBeats_ = 0; ++tempoCommits_;
        }
    }
}

} // namespace pacemaker
