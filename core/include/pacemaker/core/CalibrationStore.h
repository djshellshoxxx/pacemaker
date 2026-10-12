// Loopback calibration table keyed per device (ES-03 sections 4 step 6 and 8; RS-04 section 1 user settings).
#pragma once
#include "pacemaker/core/Json.h"
#include <string>
#include <vector>

namespace pacemaker { namespace core {

struct DeviceKey {
    std::string deviceName;
    double sampleRate = 48000.0;
    int bufferSize = 512;
    bool operator==(const DeviceKey& o) const
    {
        return deviceName == o.deviceName && sampleRate == o.sampleRate && bufferSize == o.bufferSize;
    }
};

struct DeviceCalibration {
    double hiddenInputSamples = 0.0;
    double hiddenOutputSamples = 0.0;
    double roundTripSamples = 0.0;   // informational
    std::string timestamp;           // free-form, informational
};

class CalibrationStore {
public:
    static constexpr int kVersion = 1;

    const DeviceCalibration* find(const DeviceKey& key) const;
    bool has(const DeviceKey& key) const { return find(key) != nullptr; }
    void set(const DeviceKey& key, const DeviceCalibration& cal); // replaces
    bool erase(const DeviceKey& key);
    void clear() { entries_.clear(); }
    size_t size() const { return entries_.size(); }

    // {"stateVersion":1,"devices":[{deviceName,sampleRate,bufferSize,hiddenInputSamples,...}]}.
    // fromJson skips malformed entries and ignores unknown keys (RS-04 section 4).
    Json toJson() const;
    static CalibrationStore fromJson(const Json& j);
    bool load(const std::string& path); // false and unchanged when unreadable or invalid
    bool save(const std::string& path) const;

private:
    std::vector<std::pair<DeviceKey, DeviceCalibration>> entries_;
};

}} // namespace pacemaker::core
