// Headless configuration file (see headless/README.md for the schema).
#pragma once
#include "pacemaker/Settings.h"
#include <string>
#include <vector>

namespace pmhl {

struct HeadlessConfig {
    struct Audio {
        std::string deviceType;                 // "ALSA" (default), "JACK", ... empty = JUCE default for the platform
        std::string inputDevice, outputDevice;  // empty = first available / default
        double sampleRate = 48000.0;
        int bufferSize = 256;                   // 128 or 256 recommended
        std::vector<pacemaker::ChannelAssign> inputs;   // device input channel -> role + distance (overrides state.session.channels)
        int syncLeft = -1, syncRight = -1;      // output channels carrying the Sync pulse / click (-1 = off)
        int calibrationOut = -1;                // output channel for the loopback click (-1 = use syncLeft or 0)
    } audio;
    struct Midi {
        std::vector<std::string> inputs { "*" };   // names (substring match) or "*" for every port
        std::string clockOut;                   // output port name; overrides state.session.midiPort when non-empty
        bool virtualPort = false;
    } midi;
    struct Web {
        bool enabled = true;
        int port = 8080;
        std::string bind = "0.0.0.0";
        std::string token;
    } web;
    struct Gpio {
        bool enabled = false;
        std::string chip = "/dev/gpiochip0";
        int beatLine = -1, downbeatLine = -1;
        int pulseMs = 40;
    } gpio;
    struct Oled {
        bool enabled = false;
        std::string device = "/dev/i2c-1";
        int address = 0x3C;
        bool rotate180 = false;
    } oled;
    struct Log {
        std::string file;                       // empty = stderr only
        int maxKB = 1024;
        int keep = 3;
    } log;
    std::string stateFile;                      // live settings made in the web UI are saved here and win over config.state
    int rtPriority = 70;                        // SCHED_FIFO priority for the audio thread (needs LimitRTPRIO), 0 = off
    pacemaker::PluginState state;               // embedded PluginState (params + session)

    // Returns false with a message that names the offending key.
    static bool load(const std::string& path, HeadlessConfig& out, std::string& error);
    static bool parse(const std::string& text, HeadlessConfig& out, std::string& error);
};

pacemaker::InputRole roleFromString(const std::string& s, bool* ok = nullptr);

} // namespace pmhl
