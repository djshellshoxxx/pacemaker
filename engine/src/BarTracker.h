// Bar-position histogram with shipped meter patterns (ES-01 section 7).
#pragma once
#include "pacemaker/Types.h"

namespace pacemaker {

struct PatternTable {
    int beats = 4;
    float kick[16] {}, snare[16] {}, strong[16] {};
};

class BarTracker {
public:
    void prepare(const Meter& meter, BarPattern pattern);
    void reset();

    // Evidence collected around the beat that just closed, whose hypothesised position is
    // position_ (1-based). Updates the accumulators and advances to the next beat.
    // Returns true when the confirmed position changed (consumers should re-sync).
    bool closeBeat(bool kick, bool snare, bool strong);

    int position() const { return position_; }
    bool confirmed() const { return confirmed_; }
    int beats() const { return table_.beats; }

    // Overrides.
    void setPosition(int pos, bool confirm);   // current (open) beat is position pos
    void shift(int delta);

private:
    PatternTable table_;
    float S_[16] {};
    int position_ = 1;
    bool confirmed_ = false;
    int candidate_ = -1, stableBeats_ = 0;
};

} // namespace pacemaker
