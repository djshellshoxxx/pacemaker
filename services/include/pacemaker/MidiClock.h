// MIDI clock output (ES-02 section 3): tick planner, sink interface and the realtime ClockThread.
#pragma once
#include "pacemaker/ClockMap.h"
#include "pacemaker/OutputStatus.h"
#include "pacemaker/SpscQueue.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace pacemaker {

struct MidiTimed {
    double t = 0;          // time in the planner's unit (microseconds or samples)
    uint8_t bytes[3] {};
    uint8_t len = 1;
};

struct MidiClockConfig {
    bool enabled = false;
    bool sppOnRelock = false;
    double offsetMs = 0.0;         // [-300, +300]
};

// Pure scheduling logic. Works in any linear time unit so the same code drives the OS-MIDI thread
// (microseconds) and the sample-accurate plugin MIDI buffer (samples).
class MidiClockPlanner {
public:
    // origin: time of snapshot.beatOriginSample; period: beat period; offset: added to every event;
    // [from, to): window to fill; lead: minimum distance of a new Start/Continue beat from `from`.
    void plan(const BeatMapSnapshot& s, int beatsPerBar, double origin, double period, double offset,
              double from, double to, double lead, const MidiClockConfig& cfg, std::vector<MidiTimed>& out);
    void reset() { *this = MidiClockPlanner(); }
    bool running() const { return running_; }
    uint64_t ticksPlanned() const { return ticks_; }
private:
    bool running_ = false;
    bool haveLast_ = false;
    double lastTick_ = 0;
    uint32_t lastRelockSeq_ = 0;
    uint64_t ticks_ = 0;
    uint32_t pendingSppBeats_ = 0;
};

class MidiSink {
public:
    virtual ~MidiSink() = default;
    virtual bool isOpen() const = 0;
    virtual std::string name() const { return {}; }
    virtual bool send(const uint8_t* data, int len) = 0;           // immediately
    virtual bool supportsTimestamps() const { return false; }
    virtual bool sendAt(int64_t hostUs, const uint8_t* data, int len) { (void) hostUs; return send(data, len); }
};

// Test and diagnostic sink: records bytes with the arrival time.
class MemorySink : public MidiSink {
public:
    struct Rec { int64_t atUs; uint8_t bytes[3]; int len; };
    bool isOpen() const override { return true; }
    std::string name() const override { return "memory"; }
    bool send(const uint8_t* d, int n) override
    {
        Rec r {}; r.atUs = steadyNowUs(); r.len = n > 3 ? 3 : n;
        for (int i = 0; i < r.len; ++i) r.bytes[i] = d[i];
        std::lock_guard<std::mutex> l(m_); recs_.push_back(r); return true;
    }
    std::vector<Rec> records() const { std::lock_guard<std::mutex> l(m_); return recs_; }
    void clear() { std::lock_guard<std::mutex> l(m_); recs_.clear(); }
private:
    mutable std::mutex m_;
    std::vector<Rec> recs_;
};

class ClockThread {
public:
    ClockThread() = default;
    ~ClockThread() { stop(); }
    void start(TimelineSource src);
    void stop();

    void setSink(std::shared_ptr<MidiSink> sink);
    void setConfig(const MidiClockConfig& c);
    OutputStatus& status() { return status_; }

    // Timed one-shot message at a steady-clock time (count-in launcher). Safe from the audio thread (single producer).
    void postFromAudio(int64_t atUs, const uint8_t* bytes, int len);

private:
    void run();
    void sleepUntil(int64_t targetUs, bool spin);
    SpscQueue<MidiTimed, 64> audioQ_;
    std::vector<MidiTimed> extras_;

    TimelineSource src_ = nullptr;
    std::thread th_;
    std::atomic<bool> stop_ { false };
    std::mutex m_;
    std::shared_ptr<MidiSink> sink_;
    MidiClockConfig cfg_;
    OutputStatus status_;
    MidiClockPlanner planner_;
};

// Raises the calling thread to the highest priority the OS grants; never fails hard.
void setRealtimeThreadPriority();

} // namespace pacemaker
