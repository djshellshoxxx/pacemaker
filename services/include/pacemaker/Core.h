// Pacemaker core: engine + clock map + every output, shared by the plugin, standalone and headless builds.
// Threading follows ES-04 section 2: process() on the audio thread; everything else on other threads.
#pragma once
#include "pacemaker/Calibration.h"
#include "pacemaker/ClockMap.h"
#include "pacemaker/DriftLog.h"
#include "pacemaker/Engine.h"
#include "pacemaker/Extras.h"
#include "pacemaker/LinkOutput.h"
#include "pacemaker/MidiClock.h"
#include "pacemaker/Osc.h"
#include "pacemaker/Settings.h"
#include "pacemaker/SpscQueue.h"
#include "pacemaker/SyncRenderer.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace pacemaker {

constexpr int kMaxChannels = 16;
constexpr int kTraceLen = 800;      // 8 s at 100 fps

struct CoreHostInfo {
    int64_t hostTimeUs = -1;        // steady-clock time of the first sample of the block; -1 = unknown (filter used)
    bool playing = false;
    bool hasPpq = false;
    double ppq = 0, bpm = 0;
    bool offline = false;
    int64_t outputLatencyUs = 0;    // standalone: device output latency, used to place Sync pulses
};

struct CoreMidiIn { int offset = 0; uint8_t status = 0, d1 = 0, d2 = 0; };
struct CoreMidiOut { int offset = 0; uint8_t bytes[3] {}; uint8_t len = 1; };

class Core {
public:
    Core();
    ~Core();

    // ---- message thread
    void prepare(double sampleRate, int maxBlockSize, int numInputChannels);
    void setPluginMidiOutput(bool on) { wantMidiOut_ = on; }
    PluginState state() const;
    void setState(const PluginState& s);
    Params params() const;
    void setParams(const Params& p);
    SessionSettings session() const;
    void setSession(const SessionSettings& s);
    void editSession(const std::function<void(SessionSettings&)>& fn);
    void setMidiSink(std::shared_ptr<MidiSink> sink) { clock_.setSink(std::move(sink)); }

    // Latency inputs (ES-03): driver-reported plus calibrated hidden latency, in samples.
    void setDeviceLatency(int reportedIn, int reportedOut, int hiddenIn, int hiddenOut);

    // ---- audio thread. Returns the number of events written to midiOut (plugin MIDI clock).
    int process(const float* const* in, int numCh, int n, const CoreHostInfo& host,
                const CoreMidiIn* midiIn, int numMidiIn, float* syncL, float* syncR, float* calibrationOut,
                CoreMidiOut* midiOut, int maxMidiOut);
    void notifyDeviceLost();            // any thread: outputs hold the last tempo; MIDI clock keeps running
    void notifyDeviceBack() { deviceLost_ = false; }

    // ---- actions (any thread)
    void doAction(Action a);
    void tap();
    void downbeatNow();
    void relock();
    void half();
    void doubleTime();
    void shiftBar(int d);
    void nudge(double ms);              // adds to the persistent nudge, +-100 ms
    void setFollow(bool f);
    bool follow() const { return follow_.load(); }
    void selectSong(int index);
    void nextSong(int delta);
    int currentSong() const { return currentSong_.load(); }
    void learnPattern();                // bar tracker Learn mode
    // MIDI learn (right-click a control): the next note-on or CC becomes the trigger for `a`.
    void armMidiLearn(Action a) { learnAction_ = (int) a; }
    void cancelMidiLearn() { learnAction_ = 0; }
    Action midiLearnPending() const { return (Action) learnAction_.load(); }
    void clearMapping(Action a);

    // ---- observation (UI threads)
    Timeline timeline() const { return timeline_.read(); }
    bool popUiEvent(EngineEvent& e) { return uiQ_.pop(e); }
    float inputLevel(int ch) const { return ch >= 0 && ch < kMaxChannels ? level_[ch].load() : 0.0f; }
    int numRoleSlots() const { return slotCount_.load(); }
    InputRole slotRole(int i) const { return (InputRole) slotRole_[i].load(); }
    void copyTrace(float* dst, int n) const;            // newest last, n <= kTraceLen
    bool deviceLost() const { return deviceLost_.load(); }
    const SeqLock<Timeline>* timelineSource() const { return &timeline_; }
    ClockThread& midiClock() { return clock_; }
    OscThread& osc() { return oscOut_; }
    LinkOutput& link() { return link_; }
    bool standby() const { return standby_.load(); }
    bool engineLearning() const;
    std::string statusSummary() const;

    // ---- calibration (ES-03)
    void startLoopback(int inputChannel);               // needs calibrationOut wired to an output channel
    bool loopbackActive() const { return loopback_.active(); }
    bool loopbackReady() const { return loopback_.ready(); }
    LoopbackResult finishLoopback(int reportedIn, int reportedOut, bool allOnInput = false);
    void startTapAlong();
    int tapAlongCount() const;
    double tapAlongMedianMs() const;
    void acceptTapAlong();                              // writes the median into session.inputLatencyMs
    void startHardwareCalibration(InputRole clickRole);
    bool hardwareOffset(double& measuredMs);            // measured click offset vs drummer hits
    void applyHardwareOffset(double measuredMs);        // sets midiClockOffsetMs = -measured

    // ---- drift log and exports
    bool startDriftLog();
    void stopDriftLog();
    std::string driftLogPath() const;
    std::vector<DriftRow> driftRows() const;
    bool exportTempoMap(const std::string& path) const;
    DriftReport driftReport() const;

    void housekeepingOnce();            // exposed for tests; the internal thread calls it at 30 Hz

private:
    struct Rig;
    struct CountInRT { bool enabled = false; int note = -1; int channel = 1; char osc[64] {}; };

    std::unique_ptr<Rig> buildRig(const Params& p, const SessionSettings& s);
    void installRig(std::unique_ptr<Rig> r);
    bool needsRebuild(const Params& a, const Params& b, const SessionSettings& sa, const SessionSettings& sb) const;
    void applySessionToOutputs(const SessionSettings& s);
    void handleMidi(Rig& r, const CoreMidiIn& m);
    void handleOsc(const OscMessage& m);
    void housekeeping();
    void onEngineEvent(Rig& r, const EngineEvent& e, const Timeline& tl);
    void audioDoAction(Action a, int64_t atSample);

    // configuration (message thread, guarded)
    mutable std::mutex cfgMutex_;
    Params params_;
    SessionSettings session_;
    double sampleRate_ = 48000.0;
    int maxBlock_ = 2048, numChannels_ = 2;
    bool prepared_ = false;
    int reportedIn_ = 0, reportedOut_ = 0, hiddenIn_ = 0, hiddenOut_ = 0;

    // rigs
    std::vector<std::unique_ptr<Rig>> rigs_;
    std::atomic<Rig*> pending_ { nullptr };
    std::atomic<Rig*> uiRig_ { nullptr };
    Rig* active_ = nullptr;             // audio thread

    // real-time shared state
    SeqLock<Timeline> timeline_;
    SeqLock<SyncConfig> syncCfg_;
    SeqLock<CountInRT> countInRT_;
    std::atomic<int> inputLatencySamples_ { 0 }, outputLatencySamples_ { 0 };
    std::atomic<double> inputLatencyMs_ { 0 }, nudgeMs_ { 0 };
    std::atomic<bool> follow_ { true }, deviceLost_ { false }, standby_ { false }, suppressOffline_ { true };
    std::atomic<bool> driftOffline_ { false }, sectionSuggest_ { false }, wantMidiOut_ { false };
    std::atomic<int> pendingSong_ { -1 }, currentSong_ { -1 }, songDelta_ { 0 };
    std::atomic<bool> redundancyOn_ { false };
    std::atomic<int> pendingProgram_ { -1 };
    uint32_t learnRev_ = 0;
    bool learnWatch_ = false;
    std::atomic<int> learnAction_ { 0 };
    SpscQueue<MidiMapping, 8> learnedQ_;
    SeqLock<MidiClockConfig> pluginMidiCfg_;
    std::atomic<int> slotCount_ { 0 };
    std::atomic<int> slotRole_[4];
    std::atomic<float> level_[kMaxChannels];
    std::atomic<float> trace_[kTraceLen];
    std::atomic<uint32_t> traceHead_ { 0 };

    // audio thread state
    int64_t clockSample_ = 0;
    HostTimeFilter filter_;
    SyncRenderer sync_;
    MidiClockPlanner pluginMidi_;
    std::vector<MidiTimed> planBuf_;
    uint8_t prevState_ = 0;
    bool countInSeen_ = false;
    double traceAcc_ = 0, prevDb_ = -90; int traceCount_ = 0, traceBlock_ = 480;
    double barEnergy_ = 0; int64_t barEnergyN_ = 0;
    SectionDetector section_;
    LoopbackCapture loopback_;
    int loopbackChannel_ = 0;
    TapAlong tapAlong_;
    HardwareOffsetEstimator hw_;
    std::atomic<bool> tapAlongOn_ { false }, hwOn_ { false };
    std::atomic<int> hwRole_ { (int) InputRole::Trigger };
    mutable std::mutex calMutex_;

    // outputs
    ClockThread clock_;
    OscThread oscOut_;
    OscInput oscIn_;
    LinkOutput link_;
    int oscListenPort_ = 0;

    // event fan-out
    SpscQueue<EngineEvent, 1024> evQ_;   // audio -> housekeeping (drift log)
    SpscQueue<EngineEvent, 1024> uiQ_;   // housekeeping -> UI

    // drift log
    mutable std::mutex driftMutex_;
    DriftLogger driftLog_;
    std::vector<DriftRow> driftRows_;
    int driftBeatsPerBar_ = 4, driftBeatUnit_ = 4;
    uint8_t lastLoggedState_ = 255;

    // redundancy
    RedundancyArbiter arbiter_;
    UdpSender hbSender_;
    std::string hbPeer_;
    int64_t lastHbUs_ = 0, lastArbUs_ = 0;

    // housekeeping thread
    std::thread hk_;
    std::atomic<bool> hkStop_ { false };
    int64_t lastPortCheck_ = 0;
};

} // namespace pacemaker
