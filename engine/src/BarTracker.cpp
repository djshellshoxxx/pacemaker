#include "BarTracker.h"
#include <algorithm>
#include <iterator>

namespace pacemaker {

static PatternTable makeTable(const Meter& meter, BarPattern pattern, Grouping78 grouping, const LearnedPattern* learned)
{
    PatternTable t;
    if (pattern == BarPattern::Learned && learned && learned->beats >= 1 && learned->beats <= 16)
        return BarTracker::tableFromLearned(*learned);
    t.beats = std::clamp(meter.beatsPerBar, 1, 16);
    if (pattern == BarPattern::Quintuple54) t.beats = 5;
    else if (pattern == BarPattern::Septuple78) t.beats = 7;
    else if (pattern == BarPattern::Learned) pattern = BarPattern::Rock44;   // nothing learned yet
    auto set = [&](int pos, float k, float s) { if (pos <= t.beats) { t.kick[pos - 1] = k; t.snare[pos - 1] = s; } };
    switch (pattern) {
    case BarPattern::Rock44:
        set(1, 1.0f, -0.5f); set(2, -0.5f, 1.0f); set(3, 1.0f, -0.5f); set(4, -0.5f, 1.0f); break;
    case BarPattern::HalfTime44:
        set(1, 1.0f, -0.5f); set(2, 0.0f, 0.0f); set(3, -0.5f, 1.0f); set(4, 0.0f, 0.0f); break;
    case BarPattern::FourOnFloor44:
        set(1, 0.25f, -0.5f); set(2, 0.25f, 1.0f); set(3, 0.25f, -0.5f); set(4, 0.25f, 1.0f); break;
    case BarPattern::Waltz34:
        set(1, 1.0f, -0.5f); set(2, -0.25f, 0.5f); set(3, -0.25f, 0.5f); break;
    case BarPattern::Compound68:
        set(1, 1.0f, -0.5f); set(4, -0.5f, 1.0f); break;
    case BarPattern::Quintuple54:   // 3+2: kick on 1, snare on 4
        set(1, 1.0f, -0.5f); set(4, -0.5f, 1.0f); break;
    case BarPattern::Septuple78: {  // kick on 1, snare on the other group starts
        const int starts[3][2] = { { 3, 5 }, { 4, 6 }, { 3, 6 } };
        const int g = grouping == Grouping78::G322 ? 1 : grouping == Grouping78::G232 ? 2 : 0;
        set(1, 1.0f, -0.5f);
        for (int pos : starts[g]) set(pos, -0.5f, 1.0f);
        break;
    }
    case BarPattern::Learned: break;
    }
    t.strong[0] = 0.5f;
    return t;
}

void BarTracker::prepare(const Meter& meter, BarPattern pattern, Grouping78 grouping, const LearnedPattern* learned)
{
    table_ = makeTable(meter, pattern, grouping, learned);
    reset();
}

PatternTable BarTracker::tableFromLearned(const LearnedPattern& l)
{
    PatternTable t;
    t.beats = std::clamp(l.beats, 1, 16);
    for (int i = 0; i < 16; ++i) { t.kick[i] = l.kick[i]; t.snare[i] = l.snare[i]; t.strong[i] = l.strong[i]; }
    return t;
}

void BarTracker::installTable(const PatternTable& t)
{
    if (t.beats != table_.beats) return;
    table_ = t;
}

void BarTracker::reset()
{
    std::fill(std::begin(S_), std::end(S_), 0.0f);
    position_ = 1; confirmed_ = false; candidate_ = -1; stableBeats_ = 0;
}

bool BarTracker::closeBeat(bool kick, bool snare, bool strong)
{
    const int N = table_.beats;
    // S_[b] scores "the beat that just closed is position b+1" given it sits at position_.
    for (int b = 0; b < N; ++b) {
        S_[b] *= 0.9f;
        if (kick) S_[b] += table_.kick[b];
        if (snare) S_[b] += table_.snare[b];
        if (strong) S_[b] += table_.strong[b];
    }
    // Rotate so that S_ indexes the position of the next beat.
    float last = S_[N - 1];
    for (int b = N - 1; b > 0; --b) S_[b] = S_[b - 1];
    S_[0] = last;
    position_ = position_ % N + 1;
    if (N == 1) { confirmed_ = true; return false; }

    int best = 0, second = -1;
    for (int b = 1; b < N; ++b) if (S_[b] > S_[best]) best = b;
    for (int b = 0; b < N; ++b) if (b != best && (second < 0 || S_[b] > S_[second])) second = b;
    const bool margin = S_[best] - S_[second] > 1.5f;
    // S_ is indexed by the next beat's position, which advances every beat, so compare the winning
    // hypothesis by its offset from the current position (constant while the hypothesis is stable).
    const int offset = ((best + 1 - position_) % N + N) % N;
    if (margin && offset == candidate_) ++stableBeats_;
    else { candidate_ = margin ? offset : -1; stableBeats_ = margin ? 1 : 0; }
    if (stableBeats_ >= 2 * N) {
        const bool changed = (best + 1) != position_;
        position_ = best + 1;
        confirmed_ = true;
        return changed;
    }
    return false;
}

void BarTracker::setPosition(int pos, bool confirm)
{
    const int N = table_.beats;
    position_ = ((pos - 1) % N + N) % N + 1;
    std::fill(std::begin(S_), std::end(S_), 0.0f);
    if (confirm) S_[position_ - 1] = 6.0f;
    confirmed_ = confirm; candidate_ = 0; stableBeats_ = 0;   // offset 0 = the current position
}

void BarTracker::shift(int delta) { setPosition(position_ + delta, true); }

} // namespace pacemaker
