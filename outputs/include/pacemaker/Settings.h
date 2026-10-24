// Session settings, presets and song map (RS-04, RS-07 section 4, RS-01 section 6).
#pragma once
#include "Json.h"
#include "pacemaker/Types.h"
#include <string>
#include <vector>

namespace pacemaker {

constexpr int kStateVersion = 1;

struct OutputSettings {
    struct Link { bool enabled = false; int quantum = 4; bool publishTempo = true; double gate = 0.35; } link;
    struct Midi { bool enabled = false; std::string port; double offsetMs = 0.0; bool sppOnRelock = false; } midi;
    struct Osc { bool enabled = false; std::string host = "127.0.0.1"; int port = 9000; std::string root = "/pacemaker";
                 std::string profile = "generic"; } osc;
    struct Sync { bool enabled = false; } sync;
};

struct CalibrationSettings {
    int inputLatencySamples = 0;
    int hiddenOutputSamples = 0;
    double distanceM = 0.1;
    double moduleLatencyMs = 3.0;
    bool calibrated = false;
};

struct Settings {
    int stateVersion = kStateVersion;
    // engine
    std::string profile = "stage";       // stage | rehearsal
    int meterNum = 4, meterDen = 4;
    std::string pattern = "rock44";
    double referenceBpm = 0.0, minBpm = 50.0, maxBpm = 220.0;
    double sensitivity = 0.5;
    OutputSettings outputs;
    CalibrationSettings calibration;
    bool stageMode = false;
    std::string theme = "dark";
    std::vector<std::string> warnings;   // not serialised

    // Merge a (possibly partial) JSON object: unknown keys ignored, values clamped.
    // Returns false and fills error when a known key has a wrong type.
    bool merge(const Json& j, std::string* error = nullptr);
    Json toJson() const;
    // Only the parameters of RS-04 section 1 (engine block); used by presets and songs.
    EngineConfig toEngineConfig(double sampleRate, int numRoles) const;
};

BarPattern patternFromName(const std::string& n);
const char* patternName(BarPattern p);
const char* stateName(EngineState s);

struct Preset { const char* name; const char* profile; int num, den; const char* pattern; double sensitivity; };
const std::vector<Preset>& factoryPresets();
bool applyPreset(Settings& s, const std::string& name);   // parameters only

struct Song { std::string name; double bpm = 120.0; int meterNum = 4, meterDen = 4;
              std::string pattern = "rock44"; int countInBeats = 4; int programChange = -1; };

class SongMap {
public:
    bool fromCsv(const std::string& csv, std::string* error = nullptr);
    bool fromJson(const Json& j);
    Json toJson() const;
    const std::vector<Song>& songs() const { return songs_; }
    int find(const std::string& name) const;   // -1 if absent
    int findProgram(int pc) const;
    bool apply(int index, Settings& s) const;  // sets bpm, meter, pattern
    int current() const { return current_; }
    void setCurrent(int i) { current_ = i; }
private:
    std::vector<Song> songs_;
    int current_ = -1;
};

} // namespace pacemaker
