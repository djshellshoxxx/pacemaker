#include "BarTracker.h"
#include <algorithm>
#include <iterator>

namespace pacemaker {

static PatternTable makeTable(const Meter& meter, BarPattern pattern)
{
    PatternTable t;
    t.beats = std::clamp(meter.beatsPerBar, 1, 16);
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
    }
    t.strong[0] = 0.5f;
    return t;
}

void BarTracker::prepare(const Meter& meter, BarPattern pattern)
{
    table_ = makeTable(meter, pattern);
    reset();
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
    if (margin && best == candidate_) ++stableBeats_;
    else { candidate_ = margin ? best : -1; stableBeats_ = margin ? 1 : 0; }
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
    confirmed_ = confirm; candidate_ = position_ - 1; stableBeats_ = 0;
}

void BarTracker::shift(int delta) { setPosition(position_ + delta, true); }

} // namespace pacemaker
