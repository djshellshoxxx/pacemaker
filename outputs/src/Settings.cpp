#include "pacemaker/Settings.h"
#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace pacemaker {

namespace {
template <typename T> T clampT(T v, T lo, T hi) { return std::max(lo, std::min(hi, v)); }

bool num(const Json& o, const char* k, double& dst, double lo, double hi, std::string* err)
{
    if (!o.has(k)) return true;
    const Json& v = o.get(k);
    if (!v.isNumber()) { if (err) *err = std::string(k) + " must be a number"; return false; }
    dst = clampT(v.asNumber(), lo, hi);
    return true;
}
bool inum(const Json& o, const char* k, int& dst, int lo, int hi, std::string* err)
{
    double d = dst;
    if (!num(o, k, d, lo, hi, err)) return false;
    dst = (int) d; return true;
}
bool boolean(const Json& o, const char* k, bool& dst, std::string* err)
{
    if (!o.has(k)) return true;
    if (!o.get(k).isBool()) { if (err) *err = std::string(k) + " must be a boolean"; return false; }
    dst = o.get(k).asBool(); return true;
}
bool str(const Json& o, const char* k, std::string& dst, std::string* err, size_t maxLen = 200)
{
    if (!o.has(k)) return true;
    if (!o.get(k).isString()) { if (err) *err = std::string(k) + " must be a string"; return false; }
    dst = o.get(k).asString().substr(0, maxLen); return true;
}
std::string lower(std::string s) { for (auto& c : s) c = (char) std::tolower((unsigned char) c); return s; }
std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
} // namespace

BarPattern patternFromName(const std::string& n)
{
    const std::string s = lower(n);
    if (s == "halftime44") return BarPattern::HalfTime44;
    if (s == "fouronfloor44") return BarPattern::FourOnFloor44;
    if (s == "waltz34") return BarPattern::Waltz34;
    if (s == "compound68") return BarPattern::Compound68;
    return BarPattern::Rock44;
}
const char* patternName(BarPattern p)
{
    switch (p) {
    case BarPattern::HalfTime44: return "halftime44";
    case BarPattern::FourOnFloor44: return "fouronfloor44";
    case BarPattern::Waltz34: return "waltz34";
    case BarPattern::Compound68: return "compound68";
    default: return "rock44";
    }
}
const char* stateName(EngineState s)
{
    switch (s) {
    case EngineState::Idle: return "LISTENING";
    case EngineState::CountIn: return "COUNT-IN";
    case EngineState::Locking: return "LOCKING";
    case EngineState::Locked: return "LOCKED";
    case EngineState::Hold: return "HOLD";
    case EngineState::Chase: return "CHASE";
    }
    return "LISTENING";
}

bool Settings::merge(const Json& j, std::string* err)
{
    if (!j.isObject()) { if (err) *err = "settings must be an object"; return false; }
    Settings t = *this;   // all-or-nothing
    t.warnings.clear();
    double v = t.stateVersion;
    if (!num(j, "stateVersion", v, 0, 1e6, err)) return false;
    if (j.has("stateVersion")) t.stateVersion = (int) v;
    if (t.stateVersion > kStateVersion) t.warnings.push_back("Settings saved by a newer version; loaded what is known.");

    const Json& e = j.get("engine");
    if (e.isObject()) {
        std::string prof = t.profile;
        if (!str(e, "profile", prof, err)) return false;
        t.profile = lower(prof) == "rehearsal" ? "rehearsal" : "stage";
        if (!inum(e, "meterNum", t.meterNum, 1, 16, err) || !inum(e, "meterDen", t.meterDen, 4, 8, err)) return false;
        t.meterDen = t.meterDen >= 8 ? 8 : 4;
        std::string pat = t.pattern;
        if (!str(e, "pattern", pat, err)) return false;
        t.pattern = patternName(patternFromName(pat));
        if (!num(e, "referenceBpm", t.referenceBpm, 0, 300, err) || !num(e, "minBpm", t.minBpm, 30, 290, err)
            || !num(e, "maxBpm", t.maxBpm, 40, 300, err) || !num(e, "sensitivity", t.sensitivity, 0, 1, err)) return false;
        if (t.referenceBpm > 0 && t.referenceBpm < 30) t.referenceBpm = 30;
        if (t.maxBpm < t.minBpm + 10) t.maxBpm = t.minBpm + 10;
    }
    const Json& o = j.get("outputs");
    if (o.isObject()) {
        const Json& l = o.get("link");
        if (l.isObject()) {
            if (!boolean(l, "enabled", t.outputs.link.enabled, err) || !inum(l, "quantum", t.outputs.link.quantum, 1, 16, err)
                || !boolean(l, "publishTempo", t.outputs.link.publishTempo, err) || !num(l, "gate", t.outputs.link.gate, 0, 1, err)) return false;
        }
        const Json& m = o.get("midi");
        if (m.isObject()) {
            if (!boolean(m, "enabled", t.outputs.midi.enabled, err) || !str(m, "port", t.outputs.midi.port, err)
                || !num(m, "offsetMs", t.outputs.midi.offsetMs, -300, 300, err) || !boolean(m, "sppOnRelock", t.outputs.midi.sppOnRelock, err)) return false;
        }
        const Json& c = o.get("osc");
        if (c.isObject()) {
            if (!boolean(c, "enabled", t.outputs.osc.enabled, err) || !str(c, "host", t.outputs.osc.host, err)
                || !inum(c, "port", t.outputs.osc.port, 1, 65535, err) || !str(c, "root", t.outputs.osc.root, err, 64)
                || !str(c, "profile", t.outputs.osc.profile, err)) return false;
            t.outputs.osc.profile = lower(t.outputs.osc.profile);
            if (t.outputs.osc.profile != "resolume" && t.outputs.osc.profile != "magicq") t.outputs.osc.profile = "generic";
            if (t.outputs.osc.root.empty() || t.outputs.osc.root[0] != '/') t.outputs.osc.root = "/" + t.outputs.osc.root;
            while (t.outputs.osc.root.size() > 1 && t.outputs.osc.root.back() == '/') t.outputs.osc.root.pop_back();
        }
        const Json& sy = o.get("sync");
        if (sy.isObject() && !boolean(sy, "enabled", t.outputs.sync.enabled, err)) return false;
    }
    const Json& c = j.get("calibration");
    if (c.isObject()) {
        if (!inum(c, "inputLatencySamples", t.calibration.inputLatencySamples, -48000, 48000, err)
            || !inum(c, "hiddenOutputSamples", t.calibration.hiddenOutputSamples, -48000, 48000, err)
            || !num(c, "distanceM", t.calibration.distanceM, 0, 30, err) || !num(c, "moduleLatencyMs", t.calibration.moduleLatencyMs, 0, 50, err)
            || !boolean(c, "calibrated", t.calibration.calibrated, err)) return false;
    }
    const Json& u = j.get("ui");
    if (u.isObject()) {
        if (!boolean(u, "stage", t.stageMode, err) || !str(u, "theme", t.theme, err, 16)) return false;
        if (t.theme != "light") t.theme = "dark";
    }
    *this = t;
    return true;
}

Json Settings::toJson() const
{
    Json j = Json::object();
    j["stateVersion"] = stateVersion;
    Json& e = j["engine"];
    e["profile"] = profile; e["meterNum"] = meterNum; e["meterDen"] = meterDen; e["pattern"] = pattern;
    e["referenceBpm"] = referenceBpm; e["minBpm"] = minBpm; e["maxBpm"] = maxBpm; e["sensitivity"] = sensitivity;
    Json& o = j["outputs"];
    o["link"]["enabled"] = outputs.link.enabled; o["link"]["quantum"] = outputs.link.quantum;
    o["link"]["publishTempo"] = outputs.link.publishTempo; o["link"]["gate"] = outputs.link.gate;
    o["midi"]["enabled"] = outputs.midi.enabled; o["midi"]["port"] = outputs.midi.port;
    o["midi"]["offsetMs"] = outputs.midi.offsetMs; o["midi"]["sppOnRelock"] = outputs.midi.sppOnRelock;
    o["osc"]["enabled"] = outputs.osc.enabled; o["osc"]["host"] = outputs.osc.host; o["osc"]["port"] = outputs.osc.port;
    o["osc"]["root"] = outputs.osc.root; o["osc"]["profile"] = outputs.osc.profile;
    o["sync"]["enabled"] = outputs.sync.enabled;
    Json& c = j["calibration"];
    c["inputLatencySamples"] = calibration.inputLatencySamples; c["hiddenOutputSamples"] = calibration.hiddenOutputSamples;
    c["distanceM"] = calibration.distanceM; c["moduleLatencyMs"] = calibration.moduleLatencyMs; c["calibrated"] = calibration.calibrated;
    j["ui"]["stage"] = stageMode; j["ui"]["theme"] = theme;
    return j;
}

EngineConfig Settings::toEngineConfig(double sampleRate, int numRoles) const
{
    EngineConfig c;
    c.sampleRate = sampleRate; c.numRoles = numRoles;
    c.meter = { meterNum, meterDen };
    c.tempoRange = { minBpm, maxBpm };
    c.referenceBpm = referenceBpm;
    c.profile = profile == "rehearsal" ? FollowProfile::rehearsal() : FollowProfile::stage();
    c.pattern = patternFromName(pattern);
    c.sensitivity = (float) sensitivity;
    return c;
}

const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> p = {
        { "Rock 4/4 stage", "stage", 4, 4, "rock44", 0.5 },
        { "Rock 4/4 rehearsal", "rehearsal", 4, 4, "rock44", 0.5 },
        { "Four-on-the-floor", "stage", 4, 4, "fouronfloor44", 0.5 },
        { "Half-time", "stage", 4, 4, "halftime44", 0.5 },
        { "6/8 ballad", "stage", 6, 8, "compound68", 0.55 },
        { "3/4 waltz", "stage", 3, 4, "waltz34", 0.5 },
        { "Jazz ride", "rehearsal", 4, 4, "rock44", 0.75 },
        { "E-drums", "stage", 4, 4, "rock44", 0.4 },
    };
    return p;
}

bool applyPreset(Settings& s, const std::string& name)
{
    for (const auto& p : factoryPresets()) {
        if (lower(name) != lower(p.name)) continue;
        s.profile = p.profile; s.meterNum = p.num; s.meterDen = p.den; s.pattern = p.pattern; s.sensitivity = p.sensitivity;
        return true;   // outputs, calibration and UI untouched (RS-04 section 2)
    }
    return false;
}

bool SongMap::fromCsv(const std::string& csv, std::string* error)
{
    std::vector<Song> out;
    std::istringstream in(csv);
    std::string line;
    std::vector<std::string> cols;
    bool header = true;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f;
        std::string cur; bool q = false;
        for (char c : line) {
            if (c == '"') q = !q;
            else if (c == ',' && !q) { f.push_back(trim(cur)); cur.clear(); }
            else cur += c;
        }
        f.push_back(trim(cur));
        if (header) {
            header = false;
            if (!f.empty() && lower(f[0]) == "name") { for (auto& c : f) cols.push_back(lower(c)); continue; }
            cols = { "name", "bpm", "meternum", "meterden", "pattern", "countinbeats", "programchange" };
        }
        Song s;
        for (size_t i = 0; i < f.size() && i < cols.size(); ++i) {
            const std::string& v = f[i];
            if (cols[i] == "name") s.name = v.substr(0, 80);
            else if (v.empty()) continue;
            else if (cols[i] == "bpm") s.bpm = clampT(std::atof(v.c_str()), 30.0, 300.0);
            else if (cols[i] == "meternum") s.meterNum = clampT(std::atoi(v.c_str()), 1, 16);
            else if (cols[i] == "meterden") s.meterDen = std::atoi(v.c_str()) >= 8 ? 8 : 4;
            else if (cols[i] == "pattern") s.pattern = patternName(patternFromName(v));
            else if (cols[i] == "countinbeats") s.countInBeats = clampT(std::atoi(v.c_str()), 0, 16);
            else if (cols[i] == "programchange") s.programChange = clampT(std::atoi(v.c_str()), -1, 127);
        }
        if (s.name.empty()) { if (error) *error = "song without a name: " + line; return false; }
        if (out.size() >= 500) break;
        out.push_back(s);
    }
    songs_ = std::move(out);
    current_ = -1;
    return true;
}

bool SongMap::fromJson(const Json& j)
{
    if (!j.isArray()) return false;
    std::vector<Song> out;
    for (const auto& e : j.items()) {
        Song s;
        s.name = e.get("name").asString().substr(0, 80);
        if (s.name.empty()) continue;
        s.bpm = clampT(e.get("bpm").asNumber(120), 30.0, 300.0);
        s.meterNum = clampT(e.get("meterNum").asInt(4), 1, 16);
        s.meterDen = e.get("meterDen").asInt(4) >= 8 ? 8 : 4;
        s.pattern = patternName(patternFromName(e.get("pattern").asString("rock44")));
        s.countInBeats = clampT(e.get("countInBeats").asInt(4), 0, 16);
        s.programChange = clampT(e.get("programChange").asInt(-1), -1, 127);
        if (out.size() >= 500) break;
        out.push_back(s);
    }
    songs_ = std::move(out);
    current_ = -1;
    return true;
}

Json SongMap::toJson() const
{
    Json a = Json::array();
    for (const auto& s : songs_) {
        Json o = Json::object();
        o["name"] = s.name; o["bpm"] = s.bpm; o["meterNum"] = s.meterNum; o["meterDen"] = s.meterDen;
        o["pattern"] = s.pattern; o["countInBeats"] = s.countInBeats; o["programChange"] = s.programChange;
        a.push(o);
    }
    return a;
}

int SongMap::find(const std::string& name) const
{
    for (size_t i = 0; i < songs_.size(); ++i) if (lower(songs_[i].name) == lower(name)) return (int) i;
    return -1;
}
int SongMap::findProgram(int pc) const
{
    for (size_t i = 0; i < songs_.size(); ++i) if (songs_[i].programChange == pc) return (int) i;
    return -1;
}
bool SongMap::apply(int index, Settings& s) const
{
    if (index < 0 || index >= (int) songs_.size()) return false;
    const Song& g = songs_[(size_t) index];
    s.referenceBpm = g.bpm; s.meterNum = g.meterNum; s.meterDen = g.meterDen; s.pattern = g.pattern;
    return true;
}

} // namespace pacemaker
