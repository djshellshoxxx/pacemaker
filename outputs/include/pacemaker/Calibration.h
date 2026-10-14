// Latency calibration maths (ES-03): loopback, acoustic delay, tap-along, hardware offset, storage.
#pragma once
#include "Json.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pacemaker {

constexpr double kSpeedOfSound = 343.0;

// Section 3.
inline double acousticDelaySamples(double distanceM, double sampleRate) { return distanceM / kSpeedOfSound * sampleRate; }
double distancePreset(const std::string& name);   // close 0.1, overhead 1, room 3; else 0.1

// Section 1: onsetSampleCorrected = raw - detectorDelay - inputLatency - acoustic[role].
inline double correctOnsetSample(double raw, double detectorDelay, double inputLatency, double acoustic)
{
    return raw - detectorDelay - inputLatency - acoustic;
}

// Section 4. The click template is 1 ms at full scale (a short Hann-windowed burst so it correlates sharply).
std::vector<float> loopbackClick(double sampleRate);
struct LoopbackPlan { std::vector<int64_t> clickStarts; int64_t totalSamples; };
LoopbackPlan planLoopback(double sampleRate, int clicks = 8, double spacingSec = 0.5, double leadSec = 0.25);
// Writes the click train into out (size plan.totalSamples, zero elsewhere).
void renderLoopbackTrain(const LoopbackPlan& plan, double sampleRate, std::vector<float>& out);

struct LoopbackResult {
    bool ok = false;
    int detected = 0;
    double roundTripSamples = 0;   // median lag
    double spreadSamples = 0;      // max - min of accepted lags
    double hiddenSamples = 0;      // roundTrip - (reportedIn + reportedOut)
    std::string message;
};
LoopbackResult analyseLoopback(const std::vector<float>& input, const LoopbackPlan& plan, double sampleRate,
                               double reportedInSamples, double reportedOutSamples);

// Section 5: onsets and host beats in seconds. Returns median (onset - nearest host beat) in ms.
struct TapAlongResult { bool ok = false; double medianMs = 0; int used = 0; };
TapAlongResult tapAlong(const std::vector<double>& onsetSec, double hostBpm, double hostBeatOriginSec);

// Section 6: hardware click and drummer hit times in seconds; returns the offset to enter (negative of the median).
double hardwareClockOffsetMs(const std::vector<double>& hardwareClickSec, const std::vector<double>& hitSec);

// Section 8: calibration table keyed by device.
struct DeviceKey { std::string name; double sampleRate; int bufferSize;
                   std::string str() const { return name + "|" + std::to_string((int) sampleRate) + "|" + std::to_string(bufferSize); } };
struct CalibrationEntry { double hiddenInputSamples = 0, hiddenOutputSamples = 0; };
class CalibrationStore {
public:
    void set(const DeviceKey& k, const CalibrationEntry& e) { table_[k.str()] = e; }
    bool get(const DeviceKey& k, CalibrationEntry& e) const { auto it = table_.find(k.str()); if (it == table_.end()) return false; e = it->second; return true; }
    Json toJson() const;
    void fromJson(const Json& j);
    size_t size() const { return table_.size(); }
private:
    std::map<std::string, CalibrationEntry> table_;
};

} // namespace pacemaker
