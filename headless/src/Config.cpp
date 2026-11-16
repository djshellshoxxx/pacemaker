#include "Config.h"
#include <algorithm>
#include <cmath>

namespace pmhl {

using pacemaker::Json;

pacemaker::InputRole roleFromString(const std::string& s, bool* ok)
{
    static const char* k[] = { "any", "kick", "snare", "hihat", "overhead", "trigger" };
    for (int i = 0; i < 6; ++i) if (s == k[i]) { if (ok) *ok = true; return (pacemaker::InputRole) i; }
    if (ok) *ok = false;
    return pacemaker::InputRole::Any;
}

namespace {
bool typeErr(std::string& e, const char* key, const char* want) { e = std::string("config: '") + key + "' must be " + want; return false; }

template <typename F> bool member(const Json& o, const char* key, std::string& e, F&& fn)
{
    if (!o.has(key)) return true;
    return fn(o[key], key);
}
bool getString(const Json& v, const char* k, std::string& out, std::string& e) { if (!v.isString()) return typeErr(e, k, "a string"); out = v.getString(); return true; }
bool getBool(const Json& v, const char* k, bool& out, std::string& e) { if (!v.isBool()) return typeErr(e, k, "true or false"); out = v.getBool(); return true; }
bool getInt(const Json& v, const char* k, int& out, std::string& e, int lo, int hi)
{
    if (!v.isNumber()) return typeErr(e, k, "a number");
    const double d = v.getNumber();
    if (!std::isfinite(d) || d < lo || d > hi) { e = std::string("config: '") + k + "' out of range [" + std::to_string(lo) + ", " + std::to_string(hi) + "]"; return false; }
    out = (int) std::lround(d);
    return true;
}
} // namespace

bool HeadlessConfig::parse(const std::string& text, HeadlessConfig& c, std::string& e)
{
    Json root;
    std::string perr;
    if (!Json::parse(text, root, &perr)) { e = "config: invalid JSON: " + perr; return false; }
    if (!root.isObject()) { e = "config: top level must be an object"; return false; }

    if (root.has("audio")) {
        const Json& a = root["audio"];
        if (!a.isObject()) return typeErr(e, "audio", "an object");
        if (!member(a, "deviceType", e, [&](const Json& v, const char* k) { return getString(v, k, c.audio.deviceType, e); })) return false;
        if (!member(a, "inputDevice", e, [&](const Json& v, const char* k) { return getString(v, k, c.audio.inputDevice, e); })) return false;
        if (!member(a, "outputDevice", e, [&](const Json& v, const char* k) { return getString(v, k, c.audio.outputDevice, e); })) return false;
        if (a.has("sampleRate")) {
            int sr = 48000; if (!getInt(a["sampleRate"], "audio.sampleRate", sr, e, 8000, 192000)) return false; c.audio.sampleRate = sr;
        }
        if (!member(a, "bufferSize", e, [&](const Json& v, const char* k) { return getInt(v, k, c.audio.bufferSize, e, 32, 4096); })) return false;
        if (!member(a, "syncLeft", e, [&](const Json& v, const char* k) { return getInt(v, k, c.audio.syncLeft, e, -1, 63); })) return false;
        if (!member(a, "syncRight", e, [&](const Json& v, const char* k) { return getInt(v, k, c.audio.syncRight, e, -1, 63); })) return false;
        if (!member(a, "calibrationOut", e, [&](const Json& v, const char* k) { return getInt(v, k, c.audio.calibrationOut, e, -1, 63); })) return false;
        if (a.has("inputs")) {
            if (!a["inputs"].isArray()) return typeErr(e, "audio.inputs", "an array");
            for (const auto& o : a["inputs"].items()) {
                if (!o.isObject()) return typeErr(e, "audio.inputs[]", "objects");
                pacemaker::ChannelAssign ch;
                int chn = 0;
                if (!o.has("channel") || !getInt(o["channel"], "audio.inputs[].channel", chn, e, 0, 15)) { if (e.empty()) e = "config: audio.inputs[] needs 'channel'"; return false; }
                ch.channel = chn;
                if (o.has("role")) {
                    bool ok = false; ch.role = roleFromString(o["role"].getString(), &ok);
                    if (!ok) { e = "config: unknown role '" + o["role"].getString() + "' (any, kick, snare, hihat, overhead, trigger)"; return false; }
                }
                if (o.has("distanceM")) {
                    if (!o["distanceM"].isNumber()) return typeErr(e, "audio.inputs[].distanceM", "a number");
                    ch.distanceM = std::clamp(o["distanceM"].getNumber(), 0.0, 20.0);
                }
                if (o.has("mute")) { if (!getBool(o["mute"], "audio.inputs[].mute", ch.mute, e)) return false; }
                c.audio.inputs.push_back(ch);
            }
        }
    }
    if (root.has("midi")) {
        const Json& m = root["midi"];
        if (!m.isObject()) return typeErr(e, "midi", "an object");
        if (m.has("inputs")) {
            if (!m["inputs"].isArray()) return typeErr(e, "midi.inputs", "an array of port names");
            c.midi.inputs.clear();
            for (const auto& s : m["inputs"].items()) { if (!s.isString()) return typeErr(e, "midi.inputs[]", "strings"); c.midi.inputs.push_back(s.getString()); }
        }
        if (!member(m, "clockOut", e, [&](const Json& v, const char* k) { return getString(v, k, c.midi.clockOut, e); })) return false;
        if (!member(m, "virtualPort", e, [&](const Json& v, const char* k) { return getBool(v, k, c.midi.virtualPort, e); })) return false;
    }
    if (root.has("web")) {
        const Json& w = root["web"];
        if (!w.isObject()) return typeErr(e, "web", "an object");
        if (!member(w, "enabled", e, [&](const Json& v, const char* k) { return getBool(v, k, c.web.enabled, e); })) return false;
        if (!member(w, "port", e, [&](const Json& v, const char* k) { return getInt(v, k, c.web.port, e, 0, 65535); })) return false;
        if (!member(w, "bind", e, [&](const Json& v, const char* k) { return getString(v, k, c.web.bind, e); })) return false;
        if (!member(w, "token", e, [&](const Json& v, const char* k) { return getString(v, k, c.web.token, e); })) return false;
    }
    if (root.has("gpio")) {
        const Json& g = root["gpio"];
        if (!g.isObject()) return typeErr(e, "gpio", "an object");
        if (!member(g, "enabled", e, [&](const Json& v, const char* k) { return getBool(v, k, c.gpio.enabled, e); })) return false;
        if (!member(g, "chip", e, [&](const Json& v, const char* k) { return getString(v, k, c.gpio.chip, e); })) return false;
        if (!member(g, "beatLine", e, [&](const Json& v, const char* k) { return getInt(v, k, c.gpio.beatLine, e, -1, 1023); })) return false;
        if (!member(g, "downbeatLine", e, [&](const Json& v, const char* k) { return getInt(v, k, c.gpio.downbeatLine, e, -1, 1023); })) return false;
        if (!member(g, "pulseMs", e, [&](const Json& v, const char* k) { return getInt(v, k, c.gpio.pulseMs, e, 5, 500); })) return false;
    }
    if (root.has("oled")) {
        const Json& o = root["oled"];
        if (!o.isObject()) return typeErr(e, "oled", "an object");
        if (!member(o, "enabled", e, [&](const Json& v, const char* k) { return getBool(v, k, c.oled.enabled, e); })) return false;
        if (!member(o, "device", e, [&](const Json& v, const char* k) { return getString(v, k, c.oled.device, e); })) return false;
        if (!member(o, "address", e, [&](const Json& v, const char* k) { return getInt(v, k, c.oled.address, e, 3, 119); })) return false;
        if (!member(o, "rotate180", e, [&](const Json& v, const char* k) { return getBool(v, k, c.oled.rotate180, e); })) return false;
    }
    if (root.has("log")) {
        const Json& l = root["log"];
        if (!l.isObject()) return typeErr(e, "log", "an object");
        if (!member(l, "file", e, [&](const Json& v, const char* k) { return getString(v, k, c.log.file, e); })) return false;
        if (!member(l, "maxKB", e, [&](const Json& v, const char* k) { return getInt(v, k, c.log.maxKB, e, 16, 1 << 20); })) return false;
        if (!member(l, "keep", e, [&](const Json& v, const char* k) { return getInt(v, k, c.log.keep, e, 1, 20); })) return false;
    }
    if (!member(root, "stateFile", e, [&](const Json& v, const char* k) { return getString(v, k, c.stateFile, e); })) return false;
    if (!member(root, "rtPriority", e, [&](const Json& v, const char* k) { return getInt(v, k, c.rtPriority, e, 0, 99); })) return false;
    if (root.has("state")) {
        if (!root["state"].isObject()) return typeErr(e, "state", "an object (a PluginState)");
        Json sj = root["state"];
        if (!sj.has("stateVersion")) sj.set("stateVersion", pacemaker::kStateVersion);     // hand-written configs need not carry it
        c.state = pacemaker::PluginState::fromJson(sj);
    }
    return true;
}

bool HeadlessConfig::load(const std::string& path, HeadlessConfig& out, std::string& e)
{
    std::string text;
    if (!pacemaker::readTextFile(path, text)) { e = "config: cannot read " + path; return false; }
    return parse(text, out, e);
}

} // namespace pmhl
