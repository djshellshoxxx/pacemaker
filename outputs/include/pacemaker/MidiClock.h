// MIDI clock output (ES-02 section 3): a deterministic generator plus a scheduler thread.
#pragma once
#include "ClockMap.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace pacemaker {

struct MidiMessage { int64_t hostUs; uint8_t bytes[3]; int len; };

struct MidiClockConfig {
    double offsetMs = 0.0;       // -300..300
    bool sppOnRelock = false;
    double lookaheadMs = 30.0;
};

class MidiClockGenerator {
public:
    void configure(const MidiClockConfig& c) { cfg_ = c; }
    void reset() { *this = MidiClockGenerator(cfg_); }
    explicit MidiClockGenerator(MidiClockConfig c = {}) : cfg_(c) {}

    // Append every message due in (previous horizon, horizon]. Times are host microseconds.
    void generate(const BeatMapSnapshot& s, int beatsPerBar, const ClockMap& map, int64_t nowUs, int64_t horizonUs,
                  std::vector<MidiMessage>& out);
    // Emits Stop if running; used when the output is disabled.
    void stop(int64_t nowUs, std::vector<MidiMessage>& out);
    bool running() const { return started_; }

private:
    MidiClockConfig cfg_;
    bool started_ = false, haveTick_ = false;
    int64_t lastTickUs_ = 0, lastEmitEnd_ = 0;
    uint32_t lastRelockSeq_ = 0;
    uint32_t lastSeq_ = 0;
};

class MidiSink {
public:
    virtual ~MidiSink() = default;
    virtual bool send(const uint8_t* bytes, int len, int64_t hostUs) = 0;   // false = port error
    virtual std::string name() const = 0;
};

// Writes raw bytes to a character device or FIFO (ALSA /dev/snd/midiC*D*, a virtual port).
std::unique_ptr<MidiSink> openRawMidiSink(const std::string& path, std::string* error);
// Calls a function; used by tests and the in-process monitor.
std::unique_ptr<MidiSink> makeCallbackSink(std::function<void(const uint8_t*, int, int64_t)> fn, std::string name = "callback");

struct OutputStatus {
    std::atomic<bool> enabled { false }, connected { false };
    std::atomic<double> jitterUs { 0.0 };
    std::atomic<uint64_t> sent { 0 };
    std::string error;   // written only while the thread is stopped
};

class ClockThread {
public:
    ClockThread(const ClockMap& map, std::function<BeatMapSnapshot()> snapshotFn, std::function<int()> beatsPerBarFn);
    ~ClockThread();
    void setSink(std::unique_ptr<MidiSink> sink);       // only while stopped
    void configure(const MidiClockConfig& c);            // any time
    void start();
    void stop();
    const OutputStatus& status() const { return status_; }
    std::string portName() const { return sink_ ? sink_->name() : std::string(); }

private:
    void run();
    const ClockMap& map_;
    std::function<BeatMapSnapshot()> snapshot_;
    std::function<int()> beatsPerBar_;
    std::unique_ptr<MidiSink> sink_;
    std::atomic<double> offsetMs_ { 0.0 };
    std::atomic<bool> spp_ { false };
    std::atomic<bool> running_ { false };
    std::thread thread_;
    OutputStatus status_;
};

} // namespace pacemaker
