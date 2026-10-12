// Loopback calibration table (ES-03 section 8).
#include "pacemaker/core/CalibrationStore.h"

namespace pacemaker { namespace core {

const DeviceCalibration* CalibrationStore::find(const DeviceKey& key) const
{
    for (const auto& e : entries_) if (e.first == key) return &e.second;
    return nullptr;
}

void CalibrationStore::set(const DeviceKey& key, const DeviceCalibration& cal)
{
    for (auto& e : entries_) if (e.first == key) { e.second = cal; return; }
    entries_.emplace_back(key, cal);
}

bool CalibrationStore::erase(const DeviceKey& key)
{
    for (auto it = entries_.begin(); it != entries_.end(); ++it)
        if (it->first == key) { entries_.erase(it); return true; }
    return false;
}

Json CalibrationStore::toJson() const
{
    Json root = Json::object();
    root.set("stateVersion", kVersion);
    Json arr = Json::array();
    for (const auto& e : entries_) {
        Json d = Json::object();
        d.set("deviceName", e.first.deviceName);
        d.set("sampleRate", e.first.sampleRate);
        d.set("bufferSize", e.first.bufferSize);
        d.set("hiddenInputSamples", e.second.hiddenInputSamples);
        d.set("hiddenOutputSamples", e.second.hiddenOutputSamples);
        d.set("roundTripSamples", e.second.roundTripSamples);
        if (!e.second.timestamp.empty()) d.set("timestamp", e.second.timestamp);
        arr.push(std::move(d));
    }
    root.set("devices", std::move(arr));
    return root;
}

CalibrationStore CalibrationStore::fromJson(const Json& j)
{
    CalibrationStore s;
    for (const Json& d : j["devices"].items()) {
        if (!d.isObject() || !d["deviceName"].isString() || !d["sampleRate"].isNumber()) continue;
        DeviceKey k { d.getString("deviceName", ""), d.getNumber("sampleRate", 0), d.getInt("bufferSize", 0) };
        DeviceCalibration c;
        c.hiddenInputSamples = d.getNumber("hiddenInputSamples", 0);
        c.hiddenOutputSamples = d.getNumber("hiddenOutputSamples", 0);
        c.roundTripSamples = d.getNumber("roundTripSamples", 0);
        c.timestamp = d.getString("timestamp", "");
        s.set(k, c);
    }
    return s;
}

bool CalibrationStore::load(const std::string& path)
{
    auto r = loadJsonFile(path);
    if (!r.ok || !r.value.isObject()) return false;
    *this = fromJson(r.value);
    return true;
}

bool CalibrationStore::save(const std::string& path) const { return saveJsonFile(path, toJson()); }

}} // namespace pacemaker::core
