// Clock output settings with ES-02 defaults (sections 2-5, 7). Plain values; adapters copy them in.
#pragma once
#include <string>

namespace pacemaker { namespace core {

struct LinkSettings {
    bool enabled = false;
    double quantum = 4.0;            // default beatsPerBar (ES-02 s2)
    bool startStopSync = false;
    bool publishTempo = true;
    bool publishPhase = true;
    double confidenceGate = 0.35;
    double commitIntervalMs = 50.0;
    double deadbandBpm = 0.05;

    static LinkSettings stage()      { LinkSettings s; s.commitIntervalMs = 100.0; s.deadbandBpm = 0.10; return s; }
    static LinkSettings rehearsal()  { LinkSettings s; s.commitIntervalMs = 30.0;  s.deadbandBpm = 0.02; return s; }
};

struct MidiClockSettings {
    bool enabled = false;
    std::string portName;
    bool useVirtualPort = true;      // "Pacemaker Clock" (ES-02 s3)
    double offsetMs = 0.0;           // [-300, 300]
    bool sppOnRelock = false;
    bool sendTransport = true;       // Start/Stop/Continue
};

enum class OscProfile { Generic, Resolume, MagicQ };

struct OscSettings {
    bool enabled = false;
    std::string host = "127.0.0.1";
    int port = 9000;
    std::string root = "/pacemaker";
    OscProfile profile = OscProfile::Generic;
    std::string magicQTapAddress = "/tap";   // user-configured MagicQ tap address
    double offsetMs = 0.0;
};

enum class PulsePolarity { Positive, Negative };

struct SyncSettings {
    bool enabled = false;
    PulsePolarity pulsePolarity = PulsePolarity::Positive;
    bool clickEnabled = true;
    double offsetMs = 0.0;
    bool monitorPassThrough = false;  // main bus passes input through (ES-02 s5)
};

struct OutputSettings {
    LinkSettings link;
    MidiClockSettings midi;
    OscSettings osc;
    SyncSettings sync;
    double lookaheadMs = 30.0;        // ES-02 s7, ES-03 s7
};

// Clamp helpers: bring user/preset values into valid ranges.
double clampOffsetMs(double ms);      // [-300, 300]
double clampLookaheadMs(double ms);   // [0, 200]
void sanitize(LinkSettings& s);
void sanitize(MidiClockSettings& s);
void sanitize(OscSettings& s);
void sanitize(SyncSettings& s);
void sanitize(OutputSettings& s);

}} // namespace pacemaker::core
