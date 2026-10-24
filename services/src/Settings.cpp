#include "pacemaker/Settings.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#if __has_include(<filesystem>)
 #include <filesystem>
#endif

namespace pacemaker {

// ---------------------------------------------------------------- patterns
const std::vector<PatternInfo>& patternList()
{
    static const std::vector<PatternInfo> l = {
        { BarPattern::Rock44, "rock44", "Rock 4/4", 4, 4 },
        { BarPattern::HalfTime44, "halftime44", "Half-time 4/4", 4, 4 },
        { BarPattern::FourOnFloor44, "four44", "Four-on-the-floor 4/4", 4, 4 },
        { BarPattern::Waltz34, "waltz34", "Waltz 3/4", 3, 4 },
        { BarPattern::Compound68, "compound68", "6/8", 6, 8 },
        { BarPattern::Quintuple54, "quint54", "5/4 (3+2)", 5, 4 },
        { BarPattern::Septuple78, "sept78", "7/8", 7, 8 },
        { BarPattern::Learned, "learned", "Learned", 4, 4 },
    };
    return l;
}
const char* patternKey(BarPattern p)
{
    for (const auto& i : patternList()) if (i.pattern == p) return i.key;
    return "rock44";
}
BarPattern patternFromKey(const std::string& k, BarPattern fb)
{
    for (const auto& i : patternList()) if (k == i.key) return i.pattern;
    return fb;
}

// ---------------------------------------------------------------- params
static const char* profileKey(ProfileType t) { return t == ProfileType::Stage ? "stage" : t == ProfileType::Rehearsal ? "rehearsal" : "custom"; }
static ProfileType profileFromKey(const std::string& s, ProfileType fb)
{
    if (s == "stage") return ProfileType::Stage;
    if (s == "rehearsal") return ProfileType::Rehearsal;
    if (s == "custom") return ProfileType::Custom;
    return fb;
}

Json Params::toJson() const
{
    Json j = Json::object();
    j.set("follow", follow).set("profile", profileKey(profile)).set("alpha", alpha).set("beta", beta)
     .set("tempoMin", tempoMin).set("tempoMax", tempoMax).set("referenceBpm", referenceBpm)
     .set("meterNum", meterNum).set("meterDen", meterDen).set("pattern", patternKey(pattern))
     .set("autoOctave", autoOctave).set("grouping78", grouping78).set("sensitivity", (double) sensitivity);
    return j;
}

Params Params::fromJson(const Json& j) { return fromJson(j, Params {}); }

Params Params::fromJson(const Json& j, const Params& d)
{
    Params p = d;
    if (!j.isObject()) return p;
    auto num = [&](const char* k, double def) { return j.has(k) ? j[k].getNumber(def) : def; };
    p.follow = j.has("follow") ? j["follow"].getBool(d.follow) : d.follow;
    p.profile = profileFromKey(j["profile"].getString(), d.profile);
    p.alpha = std::clamp(num("alpha", d.alpha), 0.05, 0.8);
    p.beta = std::clamp(num("beta", d.beta), 0.01, 0.5);
    p.tempoMin = std::clamp(num("tempoMin", d.tempoMin), 30.0, 300.0);
    p.tempoMax = std::clamp(num("tempoMax", d.tempoMax), 30.0, 300.0);
    if (p.tempoMax < p.tempoMin + 10.0) p.tempoMax = std::min(300.0, p.tempoMin + 10.0);
    p.referenceBpm = std::clamp(num("referenceBpm", d.referenceBpm), 0.0, 300.0);
    p.meterNum = std::clamp((int) std::lround(num("meterNum", d.meterNum)), 1, 16);
    p.meterDen = (int) std::lround(num("meterDen", d.meterDen)) == 8 ? 8 : 4;
    p.pattern = patternFromKey(j["pattern"].getString(), d.pattern);
    p.grouping78 = std::clamp(j["grouping78"].getInt(d.grouping78), 0, 2);
    p.autoOctave = j.has("autoOctave") ? j["autoOctave"].getBool(d.autoOctave) : d.autoOctave;
    p.sensitivity = (float) std::clamp(num("sensitivity", d.sensitivity), 0.0, 1.0);
    return p;
}

FollowProfile Params::followProfile() const
{
    if (profile == ProfileType::Rehearsal) return FollowProfile::rehearsal();
    FollowProfile f = FollowProfile::stage();
    if (profile == ProfileType::Custom) { f.type = ProfileType::Custom; f.alpha = alpha; f.beta = beta; }
    return f;
}

// ---------------------------------------------------------------- note table / actions
bool NoteTable::classify(int note, InputRole& role) const
{
    if (kick.count(note)) { role = InputRole::Kick; return true; }
    if (snare.count(note)) { role = InputRole::Snare; return true; }
    if (hat.count(note)) { role = InputRole::HiHat; return true; }
    if (anyNote) { role = InputRole::Trigger; return true; }
    return false;
}

static const char* kActionKeys[] = { "none", "follow", "tap", "downbeatNow", "nudgeMinus", "nudgePlus", "relock", "half", "double",
                                     "shiftMinus", "shiftPlus", "nextSong", "prevSong" };
const char* actionKey(Action a) { return (int) a < (int) Action::Count ? kActionKeys[(int) a] : "none"; }
Action actionFromKey(const std::string& s)
{
    for (int i = 0; i < (int) Action::Count; ++i) if (s == kActionKeys[i]) return (Action) i;
    return Action::None;
}

static const char* roleKey(InputRole r)
{
    switch (r) { case InputRole::Kick: return "kick"; case InputRole::Snare: return "snare"; case InputRole::HiHat: return "hihat";
                 case InputRole::Overhead: return "overhead"; case InputRole::Trigger: return "trigger"; default: return "any"; }
}
static InputRole roleFromKey(const std::string& s)
{
    if (s == "kick") return InputRole::Kick;
    if (s == "snare") return InputRole::Snare;
    if (s == "hihat") return InputRole::HiHat;
    if (s == "overhead") return InputRole::Overhead;
    if (s == "trigger") return InputRole::Trigger;
    return InputRole::Any;
}

// ---------------------------------------------------------------- song map
Json SongMap::toJson() const
{
    Json a = Json::array();
    for (const auto& s : songs) {
        Json o = Json::object();
        o.set("name", s.name).set("bpm", s.bpm).set("meterNum", s.meterNum).set("meterDen", s.meterDen)
         .set("pattern", patternKey(s.pattern)).set("countInBeats", s.countInBeats).set("programChange", s.programChange);
        a.push(o);
    }
    Json j = Json::object();
    j.set("songs", a).set("current", current);
    return j;
}

SongMap SongMap::fromJson(const Json& j)
{
    SongMap m;
    const Json& arr = j.isArray() ? j : j["songs"];
    for (const auto& o : arr.items()) {
        Song s;
        s.name = o["name"].getString();
        s.bpm = std::clamp(o["bpm"].getNumber(0.0), 0.0, 300.0);
        s.meterNum = std::clamp(o["meterNum"].getInt(4), 1, 16);
        s.meterDen = o["meterDen"].getInt(4) == 8 ? 8 : 4;
        s.pattern = patternFromKey(o["pattern"].getString(), BarPattern::Rock44);
        s.countInBeats = std::clamp(o["countInBeats"].getInt(4), 0, 16);
        s.programChange = o["programChange"].getInt(-1);
        m.songs.push_back(s);
    }
    m.current = j.isObject() ? j["current"].getInt(-1) : -1;
    if (m.current >= (int) m.songs.size()) m.current = -1;
    return m;
}

static std::vector<std::string> splitCsvLine(const std::string& line)
{
    std::vector<std::string> out; std::string cur; bool q = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (q) { if (c == '"') { if (i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; ++i; } else q = false; } else cur += c; }
        else if (c == '"') q = true;
        else if (c == ',' || c == ';' || c == '\t') { out.push_back(cur); cur.clear(); }
        else if (c != '\r') cur += c;
    }
    out.push_back(cur);
    return out;
}
static std::string lower(std::string s) { for (auto& c : s) c = (char) std::tolower((unsigned char) c); return s; }
static std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char) s[a])) ++a;
    while (b > a && std::isspace((unsigned char) s[b - 1])) --b;
    return s.substr(a, b - a);
}

bool SongMap::fromCsv(const std::string& csv, SongMap& out, std::string* err)
{
    std::istringstream in(csv);
    std::string line;
    std::vector<std::string> head;
    while (std::getline(in, line)) { if (!trim(line).empty()) { head = splitCsvLine(line); break; } }
    if (head.empty()) { if (err) *err = "empty file"; return false; }
    int cName = -1, cBpm = -1, cNum = -1, cDen = -1, cSig = -1, cPat = -1, cCount = -1, cPc = -1;
    for (size_t i = 0; i < head.size(); ++i) {
        const std::string h = lower(trim(head[i]));
        if (h == "name" || h == "title" || h == "song") cName = (int) i;
        else if (h == "bpm" || h == "tempo") cBpm = (int) i;
        else if (h == "meternum") cNum = (int) i;
        else if (h == "meterden") cDen = (int) i;
        else if (h == "time signature" || h == "timesignature" || h == "meter" || h == "signature") cSig = (int) i;
        else if (h == "pattern") cPat = (int) i;
        else if (h == "countinbeats" || h == "count-in" || h == "countin") cCount = (int) i;
        else if (h == "programchange" || h == "program change" || h == "pc") cPc = (int) i;
    }
    if (cName < 0 || cBpm < 0) { if (err) *err = "need at least name and bpm columns"; return false; }
    SongMap m;
    while (std::getline(in, line)) {
        if (trim(line).empty()) continue;
        auto f = splitCsvLine(line);
        auto at = [&](int c) { return c >= 0 && c < (int) f.size() ? trim(f[(size_t) c]) : std::string(); };
        Song s;
        s.name = at(cName);
        s.bpm = std::clamp(std::atof(at(cBpm).c_str()), 0.0, 300.0);
        if (cNum >= 0 && !at(cNum).empty()) s.meterNum = std::clamp(std::atoi(at(cNum).c_str()), 1, 16);
        if (cDen >= 0 && !at(cDen).empty()) s.meterDen = std::atoi(at(cDen).c_str()) == 8 ? 8 : 4;
        if (cSig >= 0 && !at(cSig).empty()) {
            int n = 4, d = 4;
            if (std::sscanf(at(cSig).c_str(), "%d/%d", &n, &d) == 2) { s.meterNum = std::clamp(n, 1, 16); s.meterDen = d == 8 ? 8 : 4; }
        }
        if (cPat >= 0) s.pattern = patternFromKey(at(cPat), BarPattern::Rock44);
        else if (s.meterNum == 3) s.pattern = BarPattern::Waltz34;
        else if (s.meterNum == 6 && s.meterDen == 8) s.pattern = BarPattern::Compound68;
        if (cCount >= 0 && !at(cCount).empty()) s.countInBeats = std::clamp(std::atoi(at(cCount).c_str()), 0, 16);
        if (cPc >= 0 && !at(cPc).empty()) s.programChange = std::atoi(at(cPc).c_str());
        if (!s.name.empty()) m.songs.push_back(s);
    }
    out = m;
    return true;
}

std::string SongMap::toCsv() const
{
    std::string o = "name,bpm,meterNum,meterDen,pattern,countInBeats,programChange\n";
    for (const auto& s : songs) {
        std::string n = s.name; std::string q = "\"";
        for (char c : n) { if (c == '"') q += '"'; q += c; }
        q += '"';
        char b[160];
        std::snprintf(b, sizeof b, ",%g,%d,%d,%s,%d,%d\n", s.bpm, s.meterNum, s.meterDen, patternKey(s.pattern), s.countInBeats, s.programChange);
        o += q + b;
    }
    return o;
}

int SongMap::findProgram(int pc) const
{
    for (size_t i = 0; i < songs.size(); ++i) if (songs[i].programChange == pc) return (int) i;
    return -1;
}
int SongMap::findName(const std::string& name) const
{
    const std::string n = lower(trim(name));
    for (size_t i = 0; i < songs.size(); ++i) if (lower(trim(songs[i].name)) == n) return (int) i;
    return -1;
}
void SongMap::apply(int index, Params& p) const
{
    if (index < 0 || index >= (int) songs.size()) return;
    const Song& s = songs[(size_t) index];
    p.referenceBpm = s.bpm; p.meterNum = s.meterNum; p.meterDen = s.meterDen; p.pattern = s.pattern;
}

// ---------------------------------------------------------------- session <-> json
static Json midiJson(const SessionSettings& s)
{
    Json j = Json::object();
    j.set("enabled", s.midi.enabled).set("port", s.midiPort).set("virtualPort", s.midiVirtualPort)
     .set("offsetMs", s.midi.offsetMs).set("sppOnRelock", s.midi.sppOnRelock);
    return j;
}
static Json oscJson(const OscConfig& c)
{
    Json j = Json::object();
    j.set("enabled", c.enabled).set("host", c.host).set("port", c.port).set("root", c.root)
     .set("profile", (int) c.profile).set("alsoGeneric", c.alsoGeneric).set("tapAddress", c.tapAddress)
     .set("offsetMs", c.offsetMs).set("leadMs", c.leadMs).set("listenPort", c.listenPort);
    return j;
}
static Json linkJson(const LinkSettings& l)
{
    Json j = Json::object();
    j.set("enabled", l.enabled).set("quantum", l.quantum).set("startStopSync", l.startStopSync)
     .set("publishTempo", l.publishTempo).set("publishPhase", l.publishPhase)
     .set("confidenceGate", l.confidenceGate).set("commitIntervalMs", l.commitIntervalMs).set("tempoDeadband", l.tempoDeadband);
    return j;
}

Json PluginState::toJson() const
{
    Json root = Json::object();
    root.set("stateVersion", kStateVersion);
    root.set("params", params.toJson());
    const SessionSettings& s = session;
    Json j = Json::object();
    Json ch = Json::array();
    for (const auto& c : s.channels) {
        Json o = Json::object();
        o.set("channel", c.channel).set("role", roleKey(c.role)).set("distanceM", c.distanceM).set("mute", c.mute);
        ch.push(o);
    }
    j.set("autoRoleMap", s.autoRoleMap);
    j.set("channels", ch).set("moduleLatencyMs", s.moduleLatencyMs).set("inputLatencyMs", s.inputLatencyMs)
     .set("tapAlongValid", s.tapAlongValid).set("lookaheadMs", s.lookaheadMs).set("monitor", s.monitor);
    j.set("midi", midiJson(s)).set("osc", oscJson(s.osc)).set("link", linkJson(s.link));
    Json sy = Json::object();
    sy.set("enabled", s.sync.enabled).set("invert", s.sync.invertPolarity).set("offsetMs", s.sync.offsetMs).set("level", (double) s.sync.level);
    j.set("sync", sy);
    Json nt = Json::object();
    auto setArr = [](const std::set<int>& v) { Json a = Json::array(); for (int n : v) a.push(n); return a; };
    nt.set("kick", setArr(s.notes.kick)).set("snare", setArr(s.notes.snare)).set("hat", setArr(s.notes.hat)).set("anyNote", s.notes.anyNote);
    j.set("notes", nt);
    Json mp = Json::array();
    for (const auto& m : s.mappings) {
        Json o = Json::object();
        o.set("cc", m.isCC).set("channel", m.channel).set("number", m.number).set("action", actionKey(m.action));
        mp.push(o);
    }
    j.set("mappings", mp);
    Json ci = Json::object();
    ci.set("enabled", s.countIn.enabled).set("note", s.countIn.midiNote).set("channel", s.countIn.midiChannel).set("osc", s.countIn.oscAddress);
    j.set("countIn", ci);
    if (s.learnedValid) {
        Json lp = Json::object(); Json k = Json::array(), sn = Json::array(), st2 = Json::array();
        for (int i = 0; i < s.learned.beats; ++i) { k.push((double) s.learned.kick[i]); sn.push((double) s.learned.snare[i]); st2.push((double) s.learned.strong[i]); }
        lp.set("beats", s.learned.beats).set("kick", k).set("snare", sn).set("strong", st2);
        j.set("learned", lp);
    }
    j.set("songs", s.songs.toJson());
    j.set("driftLogEnabled", s.driftLogEnabled).set("driftLogDir", s.driftLogDir)
     .set("offlineOutputs", s.offlineOutputsEnabled).set("driftLogOffline", s.driftLogOffline).set("nudgeMs", s.nudgeMs).set("sectionSuggestions", s.sectionSuggestions)
     .set("redundancy", s.redundancyEnabled).set("redundancyPeer", s.redundancyPeer);
    root.set("session", j);
    return root;
}

// v0 -> v1: flat file -> {params, session{midi, osc}}
static Json migrate0to1(const Json& r)
{
    Json out = Json::object();
    Json params = Json::object(), session = Json::object(), midi = Json::object(), osc = Json::object();
    static const char* pk[] = { "follow", "profile", "alpha", "beta", "tempoMin", "tempoMax", "referenceBpm", "meterNum", "meterDen", "pattern", "autoOctave", "sensitivity" };
    for (const char* k : pk) if (r.has(k)) params.set(k, r[k]);
    if (r.has("midiOffset")) midi.set("offsetMs", r["midiOffset"]);
    if (r.has("midiPort")) midi.set("port", r["midiPort"]);
    if (r.has("oscHost")) osc.set("host", r["oscHost"]);
    if (r.has("oscPort")) osc.set("port", r["oscPort"]);
    session.set("midi", midi).set("osc", osc);
    if (r.has("linkQuantum")) session.set("linkQuantum", r["linkQuantum"]);
    out.set("params", params).set("session", session).set("stateVersion", 1);
    return out;
}
// v1 -> v2: session.linkQuantum -> session.link.quantum
static Json migrate1to2(Json r)
{
    Json session = r["session"];
    if (session.isObject() && session.has("linkQuantum")) {
        Json link = session["link"].isObject() ? session["link"] : Json::object();
        link.set("quantum", session["linkQuantum"]);
        session.set("link", link);
        session.set("linkQuantum", Json());
    }
    r.set("session", session).set("stateVersion", 2);
    return r;
}

Json migrateState(Json root)
{
    int v = root.isObject() ? root["stateVersion"].getInt(0) : 0;
    if (v < 1) { root = migrate0to1(root); v = 1; }
    if (v < 2) { root = migrate1to2(root); v = 2; }
    return root;
}

PluginState PluginState::fromJson(const Json& jin, std::string* warning)
{
    PluginState st;
    if (!jin.isObject()) return st;
    const int v = jin["stateVersion"].getInt(0);
    if (v > kStateVersion && warning) *warning = "State was saved by a newer version (" + std::to_string(v) + "); loading what is understood.";
    const Json root = v > kStateVersion ? jin : migrateState(jin);
    st.params = Params::fromJson(root["params"]);
    const Json& j = root["session"];
    SessionSettings& s = st.session;
    s.channels.clear();
    for (const auto& o : j["channels"].items()) {
        ChannelAssign c;
        c.channel = std::clamp(o["channel"].getInt(0), 0, 63);
        c.role = roleFromKey(o["role"].getString("any"));
        c.distanceM = std::clamp(o["distanceM"].getNumber(0.1), 0.0, 20.0);
        c.mute = o["mute"].getBool(false);
        s.channels.push_back(c);
    }
    s.autoRoleMap = j.has("autoRoleMap") ? j["autoRoleMap"].getBool(true) : true;
    s.moduleLatencyMs = std::clamp(j["moduleLatencyMs"].getNumber(3.0), 0.0, 50.0);
    s.inputLatencyMs = std::clamp(j["inputLatencyMs"].getNumber(0.0), -100.0, 200.0);
    s.tapAlongValid = j["tapAlongValid"].getBool(false);
    s.lookaheadMs = std::clamp(j["lookaheadMs"].getNumber(30.0), 5.0, 200.0);
    s.monitor = j.has("monitor") ? j["monitor"].getBool(true) : true;
    const Json& m = j["midi"];
    s.midi.enabled = m["enabled"].getBool(false);
    s.midiPort = m["port"].getString();
    s.midiVirtualPort = m["virtualPort"].getBool(false);
    s.midi.offsetMs = std::clamp(m["offsetMs"].getNumber(0.0), -300.0, 300.0);
    s.midi.sppOnRelock = m["sppOnRelock"].getBool(false);
    const Json& o = j["osc"];
    OscConfig d;
    s.osc.enabled = o["enabled"].getBool(false);
    s.osc.host = o.has("host") ? o["host"].getString(d.host) : d.host;
    s.osc.port = std::clamp(o["port"].getInt(d.port), 1, 65535);
    s.osc.root = o.has("root") ? o["root"].getString(d.root) : d.root;
    if (s.osc.root.empty() || s.osc.root[0] != '/') s.osc.root = d.root;
    s.osc.profile = (OscProfile) std::clamp(o["profile"].getInt(0), 0, 2);
    s.osc.alsoGeneric = o.has("alsoGeneric") ? o["alsoGeneric"].getBool(true) : true;
    s.osc.tapAddress = o.has("tapAddress") ? o["tapAddress"].getString(d.tapAddress) : d.tapAddress;
    s.osc.offsetMs = std::clamp(o["offsetMs"].getNumber(0.0), -300.0, 300.0);
    s.osc.leadMs = std::clamp(o["leadMs"].getNumber(d.leadMs), 1.0, 50.0);
    s.osc.listenPort = std::clamp(o["listenPort"].getInt(0), 0, 65535);
    const Json& l = j["link"];
    LinkSettings ld;
    s.link.enabled = l["enabled"].getBool(false);
    s.link.quantum = std::clamp(l["quantum"].getInt(ld.quantum), 1, 16);
    s.link.startStopSync = l["startStopSync"].getBool(false);
    s.link.publishTempo = l.has("publishTempo") ? l["publishTempo"].getBool(true) : true;
    s.link.publishPhase = l.has("publishPhase") ? l["publishPhase"].getBool(true) : true;
    s.link.confidenceGate = std::clamp(l["confidenceGate"].getNumber(ld.confidenceGate), 0.0, 1.0);
    s.link.commitIntervalMs = std::clamp(l["commitIntervalMs"].getNumber(ld.commitIntervalMs), 10.0, 1000.0);
    s.link.tempoDeadband = std::clamp(l["tempoDeadband"].getNumber(ld.tempoDeadband), 0.001, 5.0);
    const Json& sy = j["sync"];
    s.sync.enabled = sy["enabled"].getBool(false);
    s.sync.invertPolarity = sy["invert"].getBool(false);
    s.sync.offsetMs = std::clamp(sy["offsetMs"].getNumber(0.0), -300.0, 300.0);
    s.sync.level = (float) std::clamp(sy["level"].getNumber(0.9), 0.05, 1.0);
    const Json& nt = j["notes"];
    if (nt.isObject()) {
        auto rd = [](const Json& a, std::set<int>& out) { out.clear(); for (const auto& n : a.items()) out.insert(std::clamp(n.getInt(), 0, 127)); };
        rd(nt["kick"], s.notes.kick); rd(nt["snare"], s.notes.snare); rd(nt["hat"], s.notes.hat);
        s.notes.anyNote = nt.has("anyNote") ? nt["anyNote"].getBool(true) : true;
    }
    s.mappings.clear();
    for (const auto& mo : j["mappings"].items()) {
        MidiMapping mm;
        mm.isCC = mo["cc"].getBool(false);
        mm.channel = std::clamp(mo["channel"].getInt(0), 0, 16);
        mm.number = std::clamp(mo["number"].getInt(0), 0, 127);
        mm.action = actionFromKey(mo["action"].getString());
        if (mm.action != Action::None) s.mappings.push_back(mm);
    }
    const Json& ci = j["countIn"];
    s.countIn.enabled = ci["enabled"].getBool(false);
    s.countIn.midiNote = std::clamp(ci["note"].getInt(-1), -1, 127);
    s.countIn.midiChannel = std::clamp(ci["channel"].getInt(1), 1, 16);
    s.countIn.oscAddress = ci["osc"].getString();
    s.learnedValid = false;
    if (j["learned"].isObject()) {
        const Json& lp = j["learned"];
        s.learned = LearnedPattern {};
        s.learned.beats = std::clamp(lp["beats"].getInt(4), 1, 16);
        for (int i = 0; i < s.learned.beats; ++i) {
            const auto& a = lp["kick"].items(); const auto& b = lp["snare"].items(); const auto& c = lp["strong"].items();
            if ((size_t) i < a.size()) s.learned.kick[i] = (float) a[(size_t) i].getNumber();
            if ((size_t) i < b.size()) s.learned.snare[i] = (float) b[(size_t) i].getNumber();
            if ((size_t) i < c.size()) s.learned.strong[i] = (float) c[(size_t) i].getNumber();
        }
        s.learnedValid = true;
    }
    s.songs = SongMap::fromJson(j["songs"]);
    s.driftLogEnabled = j.has("driftLogEnabled") ? j["driftLogEnabled"].getBool(true) : true;
    s.driftLogDir = j["driftLogDir"].getString();
    s.offlineOutputsEnabled = j["offlineOutputs"].getBool(false);
    s.driftLogOffline = j["driftLogOffline"].getBool(false);
    s.nudgeMs = std::clamp(j["nudgeMs"].getNumber(0.0), -100.0, 100.0);
    s.sectionSuggestions = j["sectionSuggestions"].getBool(false);
    s.redundancyEnabled = j["redundancy"].getBool(false);
    s.redundancyPeer = j["redundancyPeer"].getString();
    return st;
}

bool PluginState::deserialise(const std::string& text, PluginState& out, std::string* warning)
{
    Json j; std::string err;
    if (!Json::parse(text, j, &err)) { if (warning) *warning = "state parse error: " + err; return false; }
    out = fromJson(j, warning);
    return true;
}

// ---------------------------------------------------------------- user settings
Json UserSettings::toJson() const
{
    Json j = Json::object();
    Json ip = Json::array();
    for (const auto& c : inputPrefs) {
        Json o = Json::object();
        o.set("ch", c.deviceChannel).set("on", c.enabled).set("role", roleKey(c.role)).set("dist", c.distanceM).set("mute", c.mute);
        ip.push(o);
    }
    Json mi = Json::array(); for (const auto& n : midiInputs) mi.push(n);
    j.set("audioDeviceXml", audioDeviceXml).set("inputPrefs", ip).set("syncOutLeft", syncOutLeft).set("syncOutRight", syncOutRight)
     .set("midiInputs", mi).set("autoStart", autoStart).set("remotePageEnabled", remotePageEnabled).set("remotePagePort", remotePagePort);
    j.set("stateVersion", kStateVersion).set("calibration", calibration.toJson()).set("preferVirtualPort", preferVirtualPort)
     .set("theme", theme).set("lastInputDevice", lastInputDevice).set("lastOutputDevice", lastOutputDevice)
     .set("lastMidiPort", lastMidiPort).set("windowW", windowW).set("windowH", windowH)
     .set("stageMode", stageMode).set("alwaysOnTop", alwaysOnTop);
    return j;
}
UserSettings UserSettings::fromJson(const Json& j)
{
    UserSettings u;
    if (!j.isObject()) return u;
    u.calibration = CalibrationTable::fromJson(j["calibration"]);
    u.audioDeviceXml = j["audioDeviceXml"].getString();
    for (const auto& o : j["inputPrefs"].items()) {
        InputPref c; c.deviceChannel = std::clamp(o["ch"].getInt(0), 0, 255); c.enabled = o["on"].getBool(false);
        c.role = roleFromKey(o["role"].getString("any")); c.distanceM = std::clamp(o["dist"].getNumber(0.1), 0.0, 20.0); c.mute = o["mute"].getBool(false);
        u.inputPrefs.push_back(c);
    }
    u.syncOutLeft = std::clamp(j["syncOutLeft"].getInt(-1), -1, 255); u.syncOutRight = std::clamp(j["syncOutRight"].getInt(-1), -1, 255);
    for (const auto& n : j["midiInputs"].items()) u.midiInputs.push_back(n.getString());
    u.autoStart = j["autoStart"].getBool(false); u.remotePageEnabled = j["remotePageEnabled"].getBool(false);
    u.remotePagePort = std::clamp(j["remotePagePort"].getInt(8080), 1024, 65535);
    u.preferVirtualPort = j.has("preferVirtualPort") ? j["preferVirtualPort"].getBool(true) : true;
    u.theme = j["theme"].getString("dark");
    u.lastInputDevice = j["lastInputDevice"].getString(); u.lastOutputDevice = j["lastOutputDevice"].getString();
    u.lastMidiPort = j["lastMidiPort"].getString();
    u.windowW = std::clamp(j["windowW"].getInt(1000), 400, 8000); u.windowH = std::clamp(j["windowH"].getInt(640), 300, 8000);
    u.stageMode = j["stageMode"].getBool(false); u.alwaysOnTop = j["alwaysOnTop"].getBool(false);
    return u;
}

// ---------------------------------------------------------------- presets
const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> list = [] {
        std::vector<Preset> v;
        auto add = [&](const char* name, auto&& f) { Preset p; p.name = name; f(p.params); v.push_back(p); };
        add("Rock 4/4 stage", [](Params& p) { p.profile = ProfileType::Stage; p.pattern = BarPattern::Rock44; });
        add("Rock 4/4 rehearsal", [](Params& p) { p.profile = ProfileType::Rehearsal; p.pattern = BarPattern::Rock44; });
        add("Four-on-the-floor", [](Params& p) { p.pattern = BarPattern::FourOnFloor44; p.tempoMin = 100; p.tempoMax = 160; });
        add("Half-time", [](Params& p) { p.pattern = BarPattern::HalfTime44; });
        add("6/8 ballad", [](Params& p) { p.meterNum = 6; p.meterDen = 8; p.pattern = BarPattern::Compound68; p.tempoMin = 90; p.tempoMax = 300; p.profile = ProfileType::Rehearsal; });
        add("3/4 waltz", [](Params& p) { p.meterNum = 3; p.meterDen = 4; p.pattern = BarPattern::Waltz34; });
        add("Jazz ride", [](Params& p) { p.sensitivity = 0.8f; p.profile = ProfileType::Rehearsal; p.alpha = 0.2; p.beta = 0.04; });
        add("E-drums", [](Params& p) { p.sensitivity = 0.5f; p.profile = ProfileType::Stage; });
        add("Full mix", [](Params& p) { p.sensitivity = 0.6f; p.referenceBpm = 120.0; p.profile = ProfileType::Rehearsal; });
        return v;
    }();
    return list;
}

static std::string safeName(const std::string& n)
{
    std::string o;
    for (char c : n) o += (std::isalnum((unsigned char) c) || c == '-' || c == '_' || c == ' ') ? c : '_';
    return o.empty() ? "preset" : o;
}

bool saveUserPreset(const std::string& dir, const std::string& name, const Params& p, std::string* err)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    Json j = Json::object();
    j.set("name", name).set("params", p.toJson()).set("stateVersion", kStateVersion);
    if (!writeTextFile(dir + "/" + safeName(name) + ".pmpreset", j.dump(2))) { if (err) *err = "cannot write preset"; return false; }
    return true;
}

std::vector<Preset> loadUserPresets(const std::string& dir)
{
    std::vector<Preset> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return out;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (e.path().extension() != ".pmpreset") continue;
        std::string t; Json j;
        if (!readTextFile(e.path().string(), t) || !Json::parse(t, j)) continue;
        Preset p; p.name = j["name"].getString(e.path().stem().string());
        p.params = Params::fromJson(j["params"]);
        out.push_back(p);
    }
    std::sort(out.begin(), out.end(), [](const Preset& a, const Preset& b) { return a.name < b.name; });
    return out;
}

bool readTextFile(const std::string& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}
bool writeTextFile(const std::string& path, const std::string& text)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    return (bool) f;
}

} // namespace pacemaker
