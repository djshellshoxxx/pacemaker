// Latency calibration (ES-03): acoustic delay, loopback analysis and capture, tap-along, hardware offset,
// per-device calibration storage.
#pragma once
#include "pacemaker/Json.h"
#include "pacemaker/Types.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pacemaker {

// ---- section 3: acoustic delay
constexpr double kSpeedOfSound = 343.0;
inline double distanceToDelayMs(double metres) { return metres / kSpeedOfSound * 1000.0; }
enum class MicDistance : uint8_t { Close, Overhead, Room, Custom };
inline double presetDistanceMetres(MicDistance d) { return d == MicDistance::Close ? 0.1 : d == MicDistance::Overhead ? 1.0 : d == MicDistance::Room ? 3.0 : 0.0; }
// Total per-role correction in samples: acoustic path plus module latency for trigger roles. Trigger/MIDI = 0 distance.
int roleDelaySamples(InputRole role, double distanceMetres, double moduleLatencyMs, double sampleRate);

// ---- section 4: loopback
std::vector<float> makeClickTemplate(double sampleRate);   // 1 ms, deterministic broadband burst

struct LoopbackResult {
    bool pass = false;
    int clicksTotal = 0;
    int clicksDetected = 0;
    int roundTripSamples = 0;       // median
    double spreadSamples = 0;       // max - min of the accepted lags
    int reportedSumSamples = 0;
    int hiddenSamples = 0;          // roundTrip - reported (may be negative)
    int hiddenInputSamples = 0;
    int hiddenOutputSamples = 0;
    std::string message;
};

// rec: recorded input, clickStarts: output sample index of each emitted click relative to rec[0].
LoopbackResult analyzeLoopback(const float* rec, int64_t numRec, const std::vector<int64_t>& clickStarts,
                               double sampleRate, int reportedInputLatency, int reportedOutputLatency,
                               bool allOnInput = false);

// Audio-thread capture / emit state machine. Buffers are allocated in prepare().
class LoopbackCapture {
public:
    static constexpr int kClicks = 8;
    void prepare(double sampleRate);
    void start();
    void cancel() { state_ = 0; }
    bool active() const { return state_ == 1; }
    bool ready() const { return state_ == 2; }
    // in: the selected input channel; out: the click output (added into).
    void process(const float* in, float* out, int n);
    const std::vector<float>& recording() const { return rec_; }
    int64_t recordedSamples() const { return pos_; }
    const std::vector<int64_t>& clickStarts() const { return starts_; }
    void finish() { state_ = 0; }
    double sampleRate() const { return sr_; }
private:
    double sr_ = 48000;
    int state_ = 0;                 // 0 idle, 1 running, 2 ready
    std::vector<float> rec_, tmpl_;
    std::vector<int64_t> starts_;
    int64_t pos_ = 0, total_ = 0, spacing_ = 0;
    int nextClick_ = 0;
    int clickLeft_ = 0, clickIdx_ = 0;
};

// ---- storage keyed by device (user settings, section 8)
struct CalibrationEntry {
    int hiddenInputSamples = 0;
    int hiddenOutputSamples = 0;
    int64_t measuredAt = 0;
};
struct CalibrationTable {
    std::map<std::string, CalibrationEntry> entries;
    static std::string key(const std::string& device, double sampleRate, int bufferSize);
    void put(const std::string& k, const CalibrationEntry& e) { entries[k] = e; }
    bool get(const std::string& k, CalibrationEntry& e) const;
    Json toJson() const;
    static CalibrationTable fromJson(const Json& j);
};

// ---- section 5: tap along. Feed the host ppq grid and the detected onsets.
class TapAlong {
public:
    void reset() { errorsMs_.clear(); hostValid_ = false; }
    // Called once per block with the host's beat position at the start of the block.
    void onBlock(bool playing, double ppqAtBlockStart, double bpm, int64_t engineSampleAtBlockStart, double sampleRate);
    // Called for every detected onset (engine clock, detector delay already removed).
    void onOnset(int64_t engineSample);
    int count() const { return (int) errorsMs_.size(); }
    // Median of (onset - nearest host beat) in ms. Positive = hits land after the DAW beat.
    double medianErrorMs() const;
    bool enough() const { return count() >= 24; }
private:
    bool hostValid_ = false, playing_ = false;
    double ppq0_ = 0, bpm_ = 120, sr_ = 48000;
    int64_t sample0_ = 0;
    std::vector<double> errorsMs_;
};

// ---- section 6: hardware clock offset. Click source and drummer hits arrive on different roles.
class HardwareOffsetEstimator {
public:
    void reset() { clicks_.clear(); hits_.clear(); }
    void addClick(int64_t sample) { if (clicks_.size() < 512) clicks_.push_back(sample); }
    void addHit(int64_t sample) { if (hits_.size() < 512) hits_.push_back(sample); }
    // Median offset (ms) of the hardware click relative to the nearest drummer hit; positive = click late.
    // Needs at least 8 pairs within half a period; returns false otherwise.
    bool estimate(double sampleRate, double periodMs, double& offsetMs) const;
    // The value for midiClockOffsetMs: the negative of the measured offset.
    static double suggestedClockOffsetMs(double measuredMs) { return -measuredMs; }
private:
    std::vector<int64_t> clicks_, hits_;
};

} // namespace pacemaker
