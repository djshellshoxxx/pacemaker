// Session drift log, per-song report and tempo map export (RS-01 section 7, RS-04 section 6).
#pragma once
#include "Json.h"
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace pacemaker {

struct DriftRow {
    int64_t timestampUs = 0;
    double bpm = 0.0;
    float confidence = 0.f;
    int state = 0;        // EngineState
    int bar = 0, beat = 0;
    char event = 'b';     // b = beat, r = relock, s = state change, n = song change
    int song = -1;
};

class DriftLog {
public:
    static constexpr size_t kMaxRows = 200000;
    void add(const DriftRow& r);
    void clear() { rows_.clear(); songNames_.clear(); }
    void setSongName(int index, const std::string& n);
    size_t size() const { return rows_.size(); }
    const std::deque<DriftRow>& rows() const { return rows_; }

    std::string toCsv() const;
    // Per song: {name,minBpm,maxBpm,meanBpm,maxDeviationBpm,secondsInHold,relocks,beats}
    Json report() const;
    // Standard MIDI file (format 0, 480 ppq) with a tempo event whenever bpm moves > 0.05 from the last.
    std::string tempoMapMidi() const;

private:
    std::deque<DriftRow> rows_;
    std::vector<std::pair<int, std::string>> songNames_;
};

} // namespace pacemaker
