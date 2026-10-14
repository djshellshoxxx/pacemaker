// Embedded web UI and JSON API for the headless Pacemaker Box (RS-03 section 2, RS-03 section 6).
// Plain C++17, no JUCE. Serves a compiled-in single-page UI, the drummer's phone metronome and a JSON API.
//
// Deviation from RS-03: cpp-httplib has no WebSocket, so live status uses Server-Sent Events (GET /api/events,
// about 20 Hz) with GET /api/status as the polling fallback.
#pragma once
#include "pacemaker/Core.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pacemaker {

class WebUi {
public:
    // Device-layer callbacks supplied by the host application (all optional; called from HTTP worker threads).
    struct Hooks {
        std::function<std::vector<std::string>()> midiOutPorts;     // /api/ports
        std::function<std::vector<std::string>()> midiInPorts;
        bool virtualMidiSupported = false;
        std::function<Json()> deviceInfo;                           // audio device description for /api/ports and /api/info
        std::function<void(int& reportedIn, int& reportedOut)> reportedLatency;   // for the loopback analysis
        std::function<bool(int channel)> beginLoopback;             // route the click output, then Core::startLoopback
        std::function<void(const LoopbackResult&)> loopbackFinished;   // persist the per-device calibration
        std::function<void(const PluginState&)> stateChanged;       // after any state mutation (apply MIDI port, persist)
        std::function<void(Json&)> extraStatus;                     // merge host specific fields into /api/status
        bool tapAlongAvailable = true;                              // false on devices without a host tempo grid
        std::string title = "Pacemaker";
    };

    explicit WebUi(Core& core);
    ~WebUi();
    WebUi(const WebUi&) = delete;
    WebUi& operator=(const WebUi&) = delete;

    void setHooks(Hooks h);
    // Shared secret. Empty = open. When set, every /api/* request needs `?token=` or the header
    // `X-Pacemaker-Token: <t>` (or `Authorization: Bearer <t>`).
    void setToken(const std::string& token);

    // port 0 picks an ephemeral port (see port()). Returns false and sets lastError() when the bind fails.
    bool start(int port, const std::string& bindAddress = "0.0.0.0");
    void stop();
    bool running() const;
    int port() const;
    std::string lastError() const;

    // The status document served at /api/status (also useful for logging and the headless self test).
    Json statusJson() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace pacemaker
