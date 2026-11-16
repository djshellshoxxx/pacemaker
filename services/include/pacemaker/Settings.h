// Parameters, session settings, song map, presets and state (de)serialisation (RS-04, ES-04 section 7).
#pragma once
#include "pacemaker/Calibration.h"
#include "pacemaker/Json.h"
#include "pacemaker/MidiClock.h"
#include "pacemaker/Osc.h"
#include "pacemaker/SyncRenderer.h"
#include "pacemaker/Types.h"
#include <set>
#include <string>
#include <vector>

namespace pacemaker {

constexpr int kStateVersion = 2;

// ---- patterns by name
struct PatternInfo { BarPattern pattern; const char* key; const char* label; int beats; int unit; };
const std::vector<PatternInfo>& patternList();
const char* patternKey(BarPattern p);
BarPattern patternFromKey(const std::string& key, BarPattern fallback = BarPattern::Rock44);

// ---- automatable parameters (APVTS mirror); presets change only these
struct Params {
    bool follow = true;
    ProfileType profile = ProfileType::Stage;
    double alpha = 0.25, beta = 0.06;
    double tempoMin = 50.0, tempoMax = 220.0;
    double referenceBpm = 0.0;
    int meterNum = 4, meterDen = 4;
    BarPattern pattern = BarPattern::Rock44;
    bool autoOctave = false;
    int grouping78 = 0;                 // 0: 2+2+3, 1: 3+2+2, 2: 2+3+2
    float sensitivity = 0.5f;

    Json toJson() const;
    static Params fromJson(const Json& j);
    static Params fromJson(const Json& j, const Params& defaults);
    FollowProfile followProfile() const;
};

// ---- input channels and roles
struct ChannelAssign {
    int channel = 0;                    // index into the host/device input channels
    InputRole role = InputRole::Any;
    double distanceM = 0.1;             // microphone distance for acoustic compensation
    bool mute = false;
};

struct NoteTable {
    std::set<int> kick { 36 }, snare { 38, 40 }, hat { 42, 44, 46 };
    bool anyNote = true;                // other notes become Trigger onsets
    bool classify(int note, InputRole& role) const;
};

enum class Action : uint8_t { None, Follow, Tap, DownbeatNow, NudgeMinus, NudgePlus, Relock, Half, Double, ShiftMinus, ShiftPlus, NextSong, PrevSong, Count };
const char* actionKey(Action a);
Action actionFromKey(const std::string& s);

struct MidiMapping {
    bool isCC = false;                  // false = note
    int channel = 0;                    // 0 = any, 1..16
    int number = 0;
    Action action = Action::None;
};

struct Song {
    std::string name;
    double bpm = 0.0;
    int meterNum = 4, meterDen = 4;
    BarPattern pattern = BarPattern::Rock44;
    int countInBeats = 4;
    int programChange = -1;
};

struct SongMap {
    std::vector<Song> songs;
    int current = -1;
    Json toJson() const;
    static SongMap fromJson(const Json& j);
    // CSV import: header row required. Accepts this project's columns and AbleSet-style Name/Tempo/Time Signature.
    static bool fromCsv(const std::string& csv, SongMap& out, std::string* err = nullptr);
    std::string toCsv() const;
    int findProgram(int pc) const;
    int findName(const std::string& name) const;     // case-insensitive
    void apply(int index, Params& p) const;          // bpm -> referenceBpm, meter, pattern
};

struct LinkSettings {
    bool enabled = false;
    int quantum = 4;
    bool startStopSync = false;
    bool publishTempo = true;
    bool publishPhase = true;
    double confidenceGate = 0.35;
    double commitIntervalMs = 100.0;
    double tempoDeadband = 0.10;
};

struct CountInLauncher {
    bool enabled = false;
    int midiNote = -1;                  // note number to fire at bar 1 after a count-in
    int midiChannel = 1;
    std::string oscAddress;             // e.g. /pacemaker/countin or a receiver cue address
};

struct SessionSettings {
    std::vector<ChannelAssign> channels;
    LearnedPattern learned;             // bar pattern recorded by Learn mode
    bool learnedValid = false;
    bool autoRoleMap = true;            // the host wrapper derives default roles from the connected buses
    double moduleLatencyMs = 3.0;
    double inputLatencyMs = 0.0;        // tap-along / manual, session specific
    bool tapAlongValid = false;
    double lookaheadMs = 30.0;
    bool monitor = true;                // main output passes input through

    MidiClockConfig midi;
    std::string midiPort;
    bool midiVirtualPort = false;
    OscConfig osc;
    LinkSettings link;
    SyncConfig sync;
    NoteTable notes;
    std::vector<MidiMapping> mappings;
    CountInLauncher countIn;
    SongMap songs;
    bool driftLogEnabled = true;
    std::string driftLogDir;
    bool offlineOutputsEnabled = false;
    bool driftLogOffline = false;
    double nudgeMs = 0.0;               // persistent output nudge, +-100 ms
    bool sectionSuggestions = false;    // OSC /section on energy or pattern change
    bool redundancyEnabled = false;
    std::string redundancyPeer;         // host:port of the other instance (dual-instance)
};

// Complete saved state: parameters + session settings (user settings are stored separately).
struct PluginState {
    Params params;
    SessionSettings session;
    Json toJson() const;
    // Tolerant: unknown keys ignored, missing keys defaulted, older versions migrated, newer versions
    // load what is understood. `warning` (if given) receives a message for future versions.
    static PluginState fromJson(const Json& j, std::string* warning = nullptr);
    std::string serialise() const { return toJson().dump(); }
    static bool deserialise(const std::string& text, PluginState& out, std::string* warning = nullptr);
};

// Explicit migration steps: v0 -> v1 -> v2.
Json migrateState(Json root);

// ---- global user settings (not in the session)
struct InputPref {                      // standalone: per device input channel
    int deviceChannel = 0;
    bool enabled = false;
    InputRole role = InputRole::Any;
    double distanceM = 0.1;
    bool mute = false;
};

struct UserSettings {
    CalibrationTable calibration;
    std::string audioDeviceXml;         // AudioDeviceManager state (standalone, headless)
    std::vector<InputPref> inputPrefs;
    int syncOutLeft = -1, syncOutRight = -1;
    std::vector<std::string> midiInputs;    // enabled MIDI input device names
    bool autoStart = false;
    bool remotePageEnabled = false;     // phone/browser page (web UI)
    int remotePagePort = 8080;
    bool preferVirtualPort = true;
    std::string theme = "dark";
    std::string lastInputDevice, lastOutputDevice, lastMidiPort;
    int windowW = 1000, windowH = 640;
    bool stageMode = false;
    bool alwaysOnTop = false;
    Json toJson() const;
    static UserSettings fromJson(const Json& j);
};

// ---- presets
struct Preset { std::string name; Params params; };
const std::vector<Preset>& factoryPresets();
bool saveUserPreset(const std::string& dir, const std::string& name, const Params& p, std::string* err = nullptr);
std::vector<Preset> loadUserPresets(const std::string& dir);

// ---- file helpers
bool readTextFile(const std::string& path, std::string& out);
bool writeTextFile(const std::string& path, const std::string& text);

} // namespace pacemaker
