// pacemaker_server host (RS-07): engine + simulated drummer + outputs + web control surface.
#pragma once
#include "LiveDrummer.h"
#include "pacemaker/Calibration.h"
#include "pacemaker/ClockMap.h"
#include "pacemaker/DriftLog.h"
#include "pacemaker/Engine.h"
#include "pacemaker/Http.h"
#include "pacemaker/MidiClock.h"
#include "pacemaker/Osc.h"
#include "pacemaker/Settings.h"
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

namespace pacemaker {

struct HostOptions {
    int port = 8080;
    std::string bind = "127.0.0.1";
    std::string configPath = "pacemaker.json";
    std::string uiDir;            // serve index.html from disk instead of the embedded copy (development)
    double speed = 1.0;           // simulation speed; outputs are only meaningful at 1.0
    double sampleRate = 48000.0;
    bool autoPlay = true;
};

class Host {
public:
    explicit Host(HostOptions o);
    ~Host();
    bool start(std::string* error);
    void stop();
    int port() const { return http_.port(); }
    HttpResponse handle(const HttpRequest& r);
    drums::LiveDrummer& drummer() { return drummer_; }

private:
    // Threads
    void audioLoop();
    void statusLoop();
    // State
    Json buildStatus(bool full);
    Json statusLocked(bool full);
    Json fullState();
    HttpResponse api(const HttpRequest& r);
    bool applySettingsLocked(const Json& patch, std::string* err);
    void applyOutputsLocked();
    void saveConfigLocked();
    void loadConfig();
    Json runLoopbackLocked();
    static HttpResponse ok() { return { 200, "application/json", "{\"ok\":true}", {}, false }; }
    static HttpResponse fail(const std::string& m, int code = 400)
    {
        Json j = Json::object(); j["ok"] = false; j["error"] = m;
        return { code, "application/json", j.dump(), {}, false };
    }

    HostOptions opt_;
    HttpServer http_;
    Engine engine_;
    ClockMap map_;
    drums::LiveDrummer drummer_;
    std::atomic<bool> running_ { false };
    std::thread audioThread_, statusThread_;

    std::mutex m_;                       // guards everything below
    Settings settings_;
    SongMap songs_;
    DriftLog drift_;
    CalibrationStore calStore_;
    Json calibrationResult_;
    std::atomic<bool> engineDirty_ { true }, follow_ { true };
    std::atomic<int64_t> t0Us_ { 0 };
    std::atomic<int> epoch_ { 0 };
    std::atomic<float> peak_ { 0.f };
    std::atomic<int> meterBeatsPerBar_ { 4 };
    std::atomic<int64_t> processed_ { 0 };

    // Rolling history written by the audio thread under m_.
    struct Item { int64_t id; double t; double a; int b; };
    std::deque<float> trace_;            // 100 Hz envelope
    int64_t traceBase_ = 0;              // global index of trace_.front()
    std::deque<Item> onsets_, beats_, bpmHist_;
    int64_t nextId_ = 1;
    double lastBpmHist_ = -1e9;
    int lastState_ = -1;
    int64_t onsetLedUntil_ = 0;
    int sentEpoch_ = -1;
    int64_t sentTrace_ = 0, sentOnset_ = 0, sentBeat_ = 0, sentBpm_ = 0;
    bool configDirty_ = false;
    int64_t configDirtyAt_ = 0;

    // Outputs
    std::unique_ptr<ClockThread> clock_;
    std::unique_ptr<OscRunner> osc_;
    std::atomic<uint64_t> monitorTicks_ { 0 }, monitorMsgs_ { 0 };
    std::atomic<int64_t> snapshotsServed_ { 0 };
};

} // namespace pacemaker
