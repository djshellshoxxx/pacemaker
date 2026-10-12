// Latency model and calibration procedures (ES-03).
#pragma once
#include "pacemaker/Types.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace pacemaker { namespace core {

// ---- Acoustic delay per role (ES-03 section 3) ----

constexpr double kSpeedOfSound = 343.0;        // m/s
constexpr double kDefaultModuleLatencyMs = 3.0; // e-drum trigger-to-MIDI, Trigger role default
constexpr int kNumInputRoles = 6;               // InputRole enumerators

enum class MicPlacement : uint8_t { Close, Overhead, Room, Custom };

const char* micPlacementName(MicPlacement p);
bool micPlacementFromName(const std::string& name, MicPlacement& out);
double placementDistanceMetres(MicPlacement p);  // 0.1 / 1 / 3; Custom returns 0
double acousticDelayMs(double metres);           // metres / 343 m/s

struct RoleLatency {
    MicPlacement placement = MicPlacement::Close;
    double distanceMetres = 0.1;   // used for Custom; presets override
    double moduleLatencyMs = 0.0;  // Trigger roles only (default 3 ms)

    double distance() const { return placement == MicPlacement::Custom ? distanceMetres : placementDistanceMetres(placement); }
    static RoleLatency defaultsFor(InputRole role);
};

// Full onset correction chain (ES-03 section 1):
// corrected = raw - detectorDelay - (reportedInput + hiddenInput) - acoustic[role].
struct LatencyModel {
    double sampleRate = 48000.0;
    double detectorDelaySamples = 0.0;
    double reportedInputSamples = 0.0;  // plugins: 0 (section 2)
    double hiddenInputSamples = 0.0;    // loopback calibration or tap-along
    RoleLatency roles[kNumInputRoles];

    LatencyModel();
    double inputLatencySamples() const { return reportedInputSamples + hiddenInputSamples; }
    // Acoustic delay (0 for Trigger) plus module latency (Trigger only), in samples.
    double roleDelaySamples(InputRole role) const;
    double correctionSamples(InputRole role) const;
    int64_t correct(int64_t rawOnsetSample, InputRole role) const;
};

// ---- Loopback calibration (ES-03 section 4) ----

struct LoopbackConfig {
    double sampleRate = 48000.0;
    int numClicks = 8;
    double clickMs = 1.0;         // 0 dBFS
    double spacingMs = 500.0;
    double leadInMs = 100.0;      // silence before the first click
    double windowMs = 20.0;       // per-click correlation window
    double maxRoundTripMs = 250.0; // coarse search range
    int outlierSamples = 2;       // reject lags further than this from the median
    int minClicks = 6;            // fail below
    int maxSpreadSamples = 4;     // fail above
    double detectThreshold = 0.5; // normalised correlation needed to count a click

    int clickSamples() const;
    int64_t clickOnsetSample(int k) const; // emission sample of click k
    int64_t totalSamples() const;          // capture length incl. tail
};

enum class HiddenSplit : uint8_t { Equal, AllOnInput };

struct LoopbackResult {
    bool pass = false;
    int clicksDetected = 0;      // clicks whose correlation passed the threshold
    int clicksKept = 0;          // after outlier rejection
    std::vector<int> lags;       // per detected click, samples
    double roundTripSamples = 0; // median lag of kept clicks
    int spreadSamples = 0;       // max - min of kept lags
    double reportedSumSamples = 0;
    double hiddenSamples = 0;    // may be negative: driver over-reports
    double hiddenInputSamples = 0, hiddenOutputSamples = 0;
    std::string message;
};

// Click template: pseudo-random +-1 burst (0 dBFS, clickMs long) with a sharp autocorrelation peak.
std::vector<float> makeClickTemplate(double sampleRate, double clickMs = 1.0);
// Renders the whole stimulus (silence + numClicks clicks) as it is emitted.
std::vector<float> renderLoopbackStimulus(const LoopbackConfig& cfg);
// Analyses a capture whose sample 0 coincides with stimulus sample 0 (duplex callback clock).
LoopbackResult analyseLoopback(const float* capture, size_t numSamples, const LoopbackConfig& cfg,
                               double reportedInputSamples, double reportedOutputSamples,
                               HiddenSplit split = HiddenSplit::Equal);

// Audio-thread driver: emits the clicks and records the input into a pre-allocated buffer.
// start()/analyse() on a non-audio thread; process() is RT-safe (no locks, no allocation).
class LoopbackCalibrator {
public:
    enum class State : uint8_t { Idle, Running, Captured };
    explicit LoopbackCalibrator(const LoopbackConfig& cfg = LoopbackConfig {});

    void start();
    void cancel();
    State state() const { return state_.load(std::memory_order_acquire); }
    double progress() const; // 0..1

    // Mono in/out; out is overwritten (silence plus clicks) while running. Either may be null.
    void process(const float* in, float* out, int numSamples);

    // Valid once state() == Captured.
    LoopbackResult analyse(double reportedInputSamples, double reportedOutputSamples,
                           HiddenSplit split = HiddenSplit::Equal) const;
    const LoopbackConfig& config() const { return cfg_; }

private:
    LoopbackConfig cfg_;
    std::vector<float> stimulus_, capture_;
    std::atomic<int64_t> pos_ { 0 };
    std::atomic<State> state_ { State::Idle };
};

// ---- Tap-along calibration (ES-03 section 5) ----

struct TapAlongResult {
    bool valid = false;
    int count = 0;              // onsets used
    double medianErrorMs = 0;   // positive: hits land after the DAW beat
    double medianErrorSamples = 0;
    double madMs = 0;           // median absolute deviation, a consistency hint
    bool clickLoopback = false; // pure DAW latency without human bias
    std::string message;        // "Your hits land X ms after the DAW beat"
};

// Collects onset - nearest host beat errors over `bars` bars. RT-safe (fixed storage):
// call setHostPosition() at the start of each block, then addOnset() for each onset in it.
// Onsets should already have the detector delay removed.
class TapAlongCalibrator {
public:
    static constexpr int kMaxOnsets = 512;
    TapAlongCalibrator(double sampleRate, int beatsPerBar = 4, int bars = 8, bool clickLoopback = false);

    void reset();
    void setHostPosition(int64_t blockStartSample, double ppq, double bpm, bool playing);
    void addOnset(int64_t sample);
    bool complete() const;
    double beatsCovered() const;
    TapAlongResult result() const; // non-RT (sorts a copy)

private:
    double sr_;
    int beatsPerBar_, bars_;
    bool clickLoopback_;
    int64_t blockSample_ = 0;
    double blockPpq_ = 0, bpm_ = 120;
    bool playing_ = false, started_ = false;
    double startPpq_ = 0, lastPpq_ = 0;
    double errorsMs_[kMaxOnsets] {};
    int count_ = 0;
};

// ---- Hardware clock calibration (ES-03 section 6) ----

struct ClockOffsetResult {
    bool valid = false;
    int pairs = 0;
    double medianOffsetMs = 0;  // hardware click minus drummer hit
    double spreadMs = 0;        // interquartile range
    double suggestedMidiClockOffsetMs = 0; // "Set offset": the negative of medianOffsetMs
};

// Pairs each hardware click with the nearest drummer hit within maxPairMs.
ClockOffsetResult measureHardwareClockOffset(const std::vector<int64_t>& hardwareClicks,
                                             const std::vector<int64_t>& drummerHits,
                                             double sampleRate, double maxPairMs = 100.0);

// Median of a copy (0 for empty).
double medianOf(std::vector<double> v);

}} // namespace pacemaker::core
