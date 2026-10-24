// Drift log, report and tempo map export (RS-01 section 7, RS-04 section 6).
#pragma once
#include "pacemaker/Types.h"
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace pacemaker {

struct DriftRow {
    int64_t timestampUs = 0;
    double bpm = 0;
    double confidence = 0;
    int state = 0;
    int bar = 0, beat = 0;
    std::string event;          // "", "relock", "state", "octave", "song:<name>"
};

const char* stateName(int state);

class DriftLogger {
public:
    ~DriftLogger() { close(); }
    // Creates <dir>/pacemaker-YYYYmmdd-HHMMSS.csv (creating dir). Returns false when it cannot be written.
    bool open(const std::string& dir, std::string* pathOut = nullptr);
    void close();
    bool isOpen() const { return open_; }
    void write(const DriftRow& r);
    const std::string& path() const { return path_; }
    static std::string defaultDirectory();
private:
    std::mutex m_;
    std::ofstream f_;
    bool open_ = false;
    std::string path_;
    int sinceFlush_ = 0;
};

std::string driftHeader();
std::string driftRowToCsv(const DriftRow& r);
bool parseDriftCsv(const std::string& csv, std::vector<DriftRow>& rows);

struct DriftReport {
    double durationSec = 0;
    double meanBpm = 0, medianBpm = 0, minBpm = 0, maxBpm = 0;
    double maxDeviationBpm = 0;          // max |bpm - median| over locked beats
    double timeInHoldSec = 0;
    double timeLockedSec = 0;
    int relockCount = 0;
    int beats = 0;
    std::vector<std::pair<double, double>> curve;   // (seconds, bpm), downsampled
};
DriftReport analyzeDrift(const std::vector<DriftRow>& rows);
std::string renderDriftReportHtml(const DriftReport& r, const std::string& title);

// Standard MIDI file (format 0) holding the tempo map and time signature of the performance.
std::vector<uint8_t> buildTempoMapMidi(const std::vector<DriftRow>& rows, int beatsPerBar, int beatUnit);
bool exportTempoMapMidi(const std::vector<DriftRow>& rows, int beatsPerBar, int beatUnit, const std::string& path);

} // namespace pacemaker
