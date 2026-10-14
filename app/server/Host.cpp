#include "Host.h"
#include "UiData.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>

namespace pacemaker {

namespace {
constexpr int kBlock = 256;
constexpr double kTraceHz = 100.0;
constexpr double kWindowSec = 8.0;

double r3(double v) { return std::round(v * 1000.0) / 1000.0; }

bool readFile(const std::string& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf(); out = ss.str(); return true;
}
bool parseBody(const HttpRequest& r, Json& j, std::string* err)
{
    if (r.body.empty()) { j = Json::object(); return true; }
    return Json::parse(r.body, j, err);
}
} // namespace

Host::Host(HostOptions o) : opt_(std::move(o)), drummer_(opt_.sampleRate)
{
    clock_.reset(new ClockThread(map_, [this] { return engine_.snapshot(); }, [this] { return meterBeatsPerBar_.load(); }));
    osc_.reset(new OscRunner(map_, [this] { return engine_.snapshot(); }, [this] { return meterBeatsPerBar_.load(); }));
}
Host::~Host() { stop(); }

bool Host::start(std::string* error)
{
    {
        std::lock_guard<std::mutex> l(m_);
        loadConfig();
        meterBeatsPerBar_ = settings_.meterNum;
        drummer_.beatsPerBar = settings_.meterNum;
        drummer_.playing = opt_.autoPlay;
        drummer_.targetBpm = settings_.referenceBpm > 0 ? settings_.referenceBpm : 120.0;
        drummer_.currentBpm = drummer_.targetBpm.load();
    }
    if (!http_.start(opt_.bind, opt_.port, [this](const HttpRequest& r) { return handle(r); }, error)) return false;
    running_ = true;
    audioThread_ = std::thread([this] { audioLoop(); });
    statusThread_ = std::thread([this] { statusLoop(); });
    return true;
}

void Host::stop()
{
    if (!running_.exchange(false)) { http_.stop(); return; }
    http_.stop();
    if (audioThread_.joinable()) audioThread_.join();
    if (statusThread_.joinable()) statusThread_.join();
    clock_->stop(); osc_->stop();
    std::lock_guard<std::mutex> l(m_);
    if (configDirty_) saveConfigLocked();
}

// ---------------------------------------------------------------- audio / simulation thread
void Host::audioLoop()
{
    std::vector<float> buf(kBlock);
    const float* ch[1] = { buf.data() };
    const double sr = opt_.sampleRate;
    int64_t processed = 0, t0 = steadyNowUs();
    int envCount = 0; float envPeak = 0.f;
    const int envLen = (int) std::lround(sr / kTraceHz);

    while (running_.load()) {
        if (engineDirty_.exchange(false)) {
            std::lock_guard<std::mutex> l(m_);
            EngineConfig cfg = settings_.toEngineConfig(sr, 1);
            cfg.roles[0] = InputRole::Any; cfg.maxBlockSize = kBlock;
            engine_.prepare(cfg);
            map_.prepare(sr);
            meterBeatsPerBar_ = cfg.meter.beatsPerBar;
            drummer_.beatsPerBar = cfg.meter.beatsPerBar;
            const double comp = (settings_.calibration.inputLatencySamples + acousticDelaySamples(settings_.calibration.distanceM, sr)) / sr * 1e6;
            map_.setInputCompUs(settings_.calibration.calibrated ? comp : 0.0);
            trace_.clear(); traceBase_ = 0; onsets_.clear(); beats_.clear(); bpmHist_.clear();
            envCount = 0; envPeak = 0.f; processed = 0; t0 = steadyNowUs(); lastBpmHist_ = -1e9; lastState_ = -1;
            t0Us_ = t0; ++epoch_;
        }
        drummer_.render(buf.data(), kBlock);
        float pk = 0.f;
        for (float v : buf) pk = std::max(pk, std::fabs(v));
        float prev = peak_.load(); while (pk > prev && !peak_.compare_exchange_weak(prev, pk)) {}
        const int64_t hostUs = t0 + (int64_t) ((double) processed * 1e6 / sr / opt_.speed);
        engine_.process(ch, kBlock, processed, hostUs);
        map_.addBlock(processed, hostUs);
        processed += kBlock;
        processed_ = processed;

        {
        std::lock_guard<std::mutex> l(m_);
        for (float v : buf) {
            envPeak = std::max(envPeak, std::fabs(v));
            if (++envCount == envLen) {
                trace_.push_back(envPeak); envCount = 0; envPeak = 0.f;
                if (trace_.size() > (size_t) (kTraceHz * 12)) { trace_.pop_front(); ++traceBase_; }
            }
        }
        EngineEvent ev;
        while (engine_.popEvent(ev)) {
            if (ev.type == EngineEvent::Onset) {
                onsets_.push_back({ nextId_++, (double) ev.onset.sample / sr, ev.onset.strength, 0 });
                onsetLedUntil_ = processed + (int64_t) (0.08 * sr);
            } else {
                const BeatEvent& b = ev.beat;
                beats_.push_back({ nextId_++, (double) b.sample / sr, b.confidence, b.beatInBar * 10000 + b.barNumber });
                const BeatMapSnapshot s = engine_.snapshot();
                DriftRow row; row.timestampUs = (int64_t) ((double) b.sample / sr * 1e6); row.bpm = s.bpm; row.confidence = b.confidence;
                row.state = b.state; row.bar = b.barNumber; row.beat = b.beatInBar; row.event = b.relock ? 'r' : 'b'; row.song = songs_.current();
                drift_.add(row);
            }
        }
        const double tNow = (double) processed / sr;
        while (!onsets_.empty() && onsets_.front().t < tNow - 12) onsets_.pop_front();
        while (!beats_.empty() && beats_.front().t < tNow - 12) beats_.pop_front();
        while (!bpmHist_.empty() && bpmHist_.front().t < tNow - 60) bpmHist_.pop_front();
        const BeatMapSnapshot s = engine_.snapshot();
        if (s.state != 0 && tNow - lastBpmHist_ >= 0.25) {
            bpmHist_.push_back({ nextId_++, tNow, s.bpm, 0 }); lastBpmHist_ = tNow;
        }
        if ((int) s.state != lastState_) {
            if (lastState_ >= 0) {
                DriftRow row; row.timestampUs = (int64_t) (tNow * 1e6); row.bpm = s.bpm; row.confidence = s.confidence;
                row.state = s.state; row.bar = s.barNumber; row.beat = s.beatInBar; row.event = 's'; row.song = songs_.current();
                drift_.add(row);
            }
            lastState_ = (int) s.state;
        }
        }
        // Pace to the wall clock.
        const int64_t due = t0 + (int64_t) ((double) processed * 1e6 / sr / opt_.speed);
        const int64_t wait = due - steadyNowUs();
        if (wait > 0) std::this_thread::sleep_for(std::chrono::microseconds(wait));
    }
}

// ---------------------------------------------------------------- status
Json Host::statusLocked(bool full)
{
    const double sr = opt_.sampleRate;
    const BeatMapSnapshot s = engine_.snapshot();
    const double tNow = (double) processed_.load() / sr;
    Json j = Json::object();
    j["epoch"] = epoch_.load();
    j["t"] = r3(tNow);
    j["state"] = stateName((EngineState) s.state);
    j["bpm"] = r3(s.bpm);
    j["confidence"] = r3(s.confidence);
    j["bar"] = s.barNumber; j["beat"] = s.beatInBar;
    j["beatsPerBar"] = settings_.meterNum; j["meterDen"] = settings_.meterDen;
    j["follow"] = follow_.load();
    j["flags"]["octave"] = (s.flags & SnapshotFlags::OctaveAmbiguity) != 0;
    j["flags"]["barConfirmed"] = (s.flags & SnapshotFlags::BarConfirmed) != 0;
    j["flags"]["relock"] = (s.flags & SnapshotFlags::Relock) != 0;
    if (s.state != 0 && s.nextBeatSample > s.beatOriginSample) {
        j["grid"]["origin"] = r3((double) s.beatOriginSample / sr);
        j["grid"]["period"] = (double) (s.nextBeatSample - s.beatOriginSample) / sr;
    }
    const float pk = peak_.exchange(0.f);
    Json lv = Json::object();
    lv["role"] = "Drum bus"; lv["peak"] = r3(pk); lv["onset"] = processed_.load() < onsetLedUntil_;
    j["levels"].push(lv);
    j["song"] = songs_.current();
    j["songName"] = songs_.current() >= 0 ? songs_.songs()[(size_t) songs_.current()].name : std::string();

    // Deltas (or the recent window on a full state request).
    int64_t fromTrace = full ? std::max<int64_t>(traceBase_, traceBase_ + (int64_t) trace_.size() - (int64_t) (kWindowSec * kTraceHz))
                             : std::max(sentTrace_, traceBase_);
    int64_t fromOnset = full ? 0 : sentOnset_, fromBeat = full ? 0 : sentBeat_, fromBpm = full ? 0 : sentBpm_;
    Json tr = Json::array();
    for (int64_t i = fromTrace; i < traceBase_ + (int64_t) trace_.size(); ++i) tr.push(r3(trace_[(size_t) (i - traceBase_)]));
    j["trace"]["from"] = (double) fromTrace; j["trace"]["hz"] = kTraceHz; j["trace"]["v"] = tr;
    Json on = Json::array(), be = Json::array(), bh = Json::array();
    for (const auto& o : onsets_) if (o.id > fromOnset && (!full || o.t > tNow - kWindowSec)) { Json a = Json::array(); a.push((double) o.id); a.push(r3(o.t)); a.push(r3(o.a)); on.push(a); }
    for (const auto& b : beats_) if (b.id > fromBeat && (!full || b.t > tNow - kWindowSec)) { Json a = Json::array(); a.push((double) b.id); a.push(r3(b.t)); a.push(b.b / 10000); a.push(b.b % 10000); be.push(a); }
    for (const auto& h : bpmHist_) if (h.id > fromBpm) { Json a = Json::array(); a.push((double) h.id); a.push(r3(h.t)); a.push(r3(h.a)); bh.push(a); }
    j["onsets"] = on; j["beats"] = be; j["bpmHistory"] = bh;
    if (!full) {
        sentTrace_ = traceBase_ + (int64_t) trace_.size();
        if (!onsets_.empty()) sentOnset_ = onsets_.back().id;
        if (!beats_.empty()) sentBeat_ = beats_.back().id;
        if (!bpmHist_.empty()) sentBpm_ = bpmHist_.back().id;
        sentOnset_ = std::max(sentOnset_, nextId_ - 1); sentBeat_ = sentOnset_; sentBpm_ = sentOnset_;
    }

    // Outputs
    Json out = Json::object();
    const auto& st = settings_.outputs;
    out["link"]["enabled"] = st.link.enabled; out["link"]["available"] = false;
    out["link"]["status"] = "Link SDK not built in (needs the Ableton licence, see RS-06)";
    out["midi"]["enabled"] = st.midi.enabled; out["midi"]["available"] = true;
    const OutputStatus& ms = clock_->status();
    out["midi"]["connected"] = ms.connected.load(); out["midi"]["jitterUs"] = r3(ms.jitterUs.load());
    out["midi"]["sent"] = (double) ms.sent.load();
    out["midi"]["port"] = st.midi.port.empty() ? std::string("internal monitor") : st.midi.port;
    out["midi"]["ticks"] = (double) monitorTicks_.load();
    const OutputStatus& os = osc_->status();
    out["osc"]["enabled"] = st.osc.enabled; out["osc"]["available"] = true; out["osc"]["connected"] = os.connected.load();
    out["osc"]["sent"] = (double) os.sent.load(); out["osc"]["error"] = os.error;
    out["sync"]["enabled"] = st.sync.enabled; out["sync"]["available"] = false;
    out["sync"]["status"] = "Needs an audio device (plugin and standalone builds)";
    j["outputs"] = out;
    j["calibration"] = calibrationResult_;
    j["calibration"]["calibrated"] = settings_.calibration.calibrated;
    j["calibration"]["inputLatencySamples"] = settings_.calibration.inputLatencySamples;
    Json w = Json::array();
    for (const auto& s2 : settings_.warnings) w.push(s2);
    if (!settings_.calibration.calibrated) w.push("Not calibrated for this device (Calibrate in the footer).");
    if (s.flags & SnapshotFlags::OctaveAmbiguity) w.push("Tempo may be half or double: set a reference BPM or use Half / Double.");
    if (settings_.outputs.osc.enabled && !os.connected.load()) w.push("OSC: " + (os.error.empty() ? std::string("not connected") : os.error));
    j["warnings"] = w;
    j["sim"]["available"] = true; j["sim"]["playing"] = drummer_.playing.load();
    j["sim"]["bpm"] = r3(drummer_.targetBpm.load()); j["sim"]["currentBpm"] = r3(drummer_.currentBpm.load());
    j["sim"]["jitterMs"] = drummer_.jitterMs.load(); j["sim"]["drift"] = drummer_.drift.load();
    return j;
}

Json Host::buildStatus(bool full)
{
    std::lock_guard<std::mutex> l(m_);
    return statusLocked(full);
}

Json Host::fullState()
{
    std::lock_guard<std::mutex> l(m_);
    Json j = statusLocked(true);
    j["settings"] = settings_.toJson();
    j["songs"] = songs_.toJson();
    Json p = Json::array();
    for (const auto& pr : factoryPresets()) p.push(pr.name);
    j["presets"] = p;
    j["version"] = "0.2.0";
    return j;
}

void Host::statusLoop()
{
    int ticks = 0;
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ++ticks;
        if (http_.streamClients() > 0) {
            std::string data;
            {
                std::lock_guard<std::mutex> l(m_);
                if (sentEpoch_ != epoch_.load()) { sentEpoch_ = epoch_.load(); sentTrace_ = sentOnset_ = sentBeat_ = sentBpm_ = 0; }
                data = statusLocked(false).dump();
            }
            http_.broadcast("status", data);
        }
        // Count internal monitor ticks from the MIDI thread status; debounce config writes.
        std::lock_guard<std::mutex> l(m_);
        monitorTicks_ = clock_->status().sent.load();
        if (configDirty_ && steadyNowUs() - configDirtyAt_ > 1000000) saveConfigLocked();
    }
}

// ---------------------------------------------------------------- settings, outputs, persistence
void Host::loadConfig()
{
    std::string text;
    if (!readFile(opt_.configPath, text)) return;
    Json j; std::string err;
    if (!Json::parse(text, j, &err)) { settings_.warnings.push_back("Config unreadable, using defaults: " + err); return; }
    Settings t = settings_;
    if (t.merge(j.get("settings"), &err)) settings_ = t;
    songs_.fromJson(j.get("songs"));
    calStore_.fromJson(j.get("calibrationTable"));
    applyOutputsLocked();
}

void Host::saveConfigLocked()
{
    Json j = Json::object();
    j["settings"] = settings_.toJson(); j["songs"] = songs_.toJson(); j["calibrationTable"] = calStore_.toJson();
    const std::string tmp = opt_.configPath + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f << j.dump();
    }
    std::rename(tmp.c_str(), opt_.configPath.c_str());
    configDirty_ = false;
}

void Host::applyOutputsLocked()
{
    const auto& o = settings_.outputs;
    // MIDI clock: raw port path, or the internal monitor when no port is named.
    MidiClockConfig mc; mc.offsetMs = o.midi.offsetMs; mc.sppOnRelock = o.midi.sppOnRelock;
    clock_->configure(mc);
    clock_->stop();
    if (o.midi.enabled) {
        std::unique_ptr<MidiSink> sink;
        std::string err;
        if (o.midi.port.empty()) {
            sink = makeCallbackSink([this](const uint8_t* b, int, int64_t) { if (b[0] == 0xF8) ++monitorMsgs_; }, "internal monitor");
        } else sink = openRawMidiSink(o.midi.port, &err);
        if (sink) { clock_->setSink(std::move(sink)); clock_->start(); }
        else { clock_->setSink(nullptr); settings_.warnings.push_back("MIDI: " + err); }
    }
    osc_->stop();
    if (o.osc.enabled) {
        OscConfig oc; oc.root = o.osc.root; oc.profile = o.osc.profile;
        std::string err;
        if (!osc_->start(o.osc.host, o.osc.port, oc, &err)) settings_.warnings.push_back("OSC: " + err);
    }
}

bool Host::applySettingsLocked(const Json& patch, std::string* err)
{
    Settings before = settings_;
    if (!settings_.merge(patch, err)) return false;
    const Settings& a = before; const Settings& b = settings_;
    if (a.profile != b.profile || a.meterNum != b.meterNum || a.meterDen != b.meterDen || a.pattern != b.pattern
        || a.referenceBpm != b.referenceBpm || a.minBpm != b.minBpm || a.maxBpm != b.maxBpm || a.sensitivity != b.sensitivity
        || a.calibration.inputLatencySamples != b.calibration.inputLatencySamples || a.calibration.distanceM != b.calibration.distanceM
        || a.calibration.calibrated != b.calibration.calibrated)
        engineDirty_ = true;
    applyOutputsLocked();
    configDirty_ = true; configDirtyAt_ = steadyNowUs();
    return true;
}

Json Host::runLoopbackLocked()
{
    // Simulated interface: reported 96 in / 64 out, hidden 37, -20 dB noise.
    const double sr = opt_.sampleRate;
    const int repIn = 96, repOut = 64, hidden = 37;
    LoopbackPlan plan = planLoopback(sr);
    std::vector<float> train, rec((size_t) plan.totalSamples, 0.0f);
    renderLoopbackTrain(plan, sr, train);
    const int delay = repIn + repOut + hidden;
    std::mt19937 rng(5); std::normal_distribution<float> nd(0.f, 0.1f);
    for (size_t i = 0; i < rec.size(); ++i) rec[i] = (i >= (size_t) delay ? train[i - (size_t) delay] : 0.f) + nd(rng);
    const LoopbackResult r = analyseLoopback(rec, plan, sr, repIn, repOut);
    Json j = Json::object();
    j["ok"] = r.ok; j["message"] = r.message; j["detected"] = r.detected; j["roundTrip"] = r.roundTripSamples;
    j["spread"] = r.spreadSamples; j["hidden"] = r.hiddenSamples; j["reported"] = repIn + repOut;
    j["device"] = "Simulated interface (hidden 37 samples)";
    if (r.ok) {
        calStore_.set({ "simulated", sr, kBlock }, { r.hiddenSamples * 0.5, r.hiddenSamples * 0.5 });
        settings_.calibration.inputLatencySamples = (int) std::lround(repIn + r.hiddenSamples * 0.5);
        settings_.calibration.hiddenOutputSamples = (int) std::lround(r.hiddenSamples * 0.5);
        settings_.calibration.calibrated = true;
        engineDirty_ = true; configDirty_ = true; configDirtyAt_ = steadyNowUs();
    }
    return j;
}

// ---------------------------------------------------------------- HTTP
HttpResponse Host::handle(const HttpRequest& r)
{
    if (r.method == "GET" && (r.path == "/" || r.path == "/index.html")) {
        std::string html;
        if (!opt_.uiDir.empty() && readFile(opt_.uiDir + "/index.html", html)) {}
        else html = uiHtml();
        return { 200, "text/html; charset=utf-8", html, {}, false };
    }
    if (r.path.rfind("/api/", 0) == 0) return api(r);
    return fail("not found", 404);
}

HttpResponse Host::api(const HttpRequest& r)
{
    const std::string& p = r.path;
    if (r.method == "GET") {
        if (p == "/api/state") return { 200, "application/json", fullState().dump(), {}, false };
        if (p == "/api/events") { HttpResponse x; x.sse = true; return x; }
        if (p == "/api/drift.csv") { std::lock_guard<std::mutex> l(m_); return { 200, "text/csv", drift_.toCsv(), { { "Content-Disposition", "attachment; filename=pacemaker-drift.csv" } }, false }; }
        if (p == "/api/report") {
            std::lock_guard<std::mutex> l(m_);
            Json j = Json::object(); j["songs"] = drift_.report(); j["rows"] = (double) drift_.size();
            return { 200, "application/json", j.dump(), {}, false };
        }
        if (p == "/api/tempomap.mid") { std::lock_guard<std::mutex> l(m_); return { 200, "audio/midi", drift_.tempoMapMidi(), { { "Content-Disposition", "attachment; filename=pacemaker-tempomap.mid" } }, false }; }
        return fail("not found", 404);
    }
    if (r.method != "POST") return fail("method not allowed", 405);
    Json body; std::string err;
    if (!parseBody(r, body, &err)) return fail("invalid JSON: " + err);

    if (p == "/api/action") {
        const std::string n = body.get("name").asString();
        const int64_t sample = (int64_t) ((double) (steadyNowUs() - t0Us_.load()) * opt_.sampleRate * opt_.speed / 1e6);
        if (n == "tap") engine_.tap(std::max<int64_t>(0, sample));
        else if (n == "downbeat") engine_.downbeatNow();
        else if (n == "nudgeMinus") engine_.shiftBar(-1);
        else if (n == "nudgePlus") engine_.shiftBar(1);
        else if (n == "half") engine_.halfTime();
        else if (n == "double") engine_.doubleTime();
        else if (n == "relock") engine_.relock();
        else if (n == "follow") { const bool v = body.has("value") ? body.get("value").asBool() : !follow_.load(); follow_ = v; engine_.setFollow(v); }
        else return fail("unknown action: " + n);
        return ok();
    }
    std::lock_guard<std::mutex> l(m_);
    if (p == "/api/settings") {
        if (!applySettingsLocked(body, &err)) return fail(err);
        return ok();
    }
    if (p == "/api/preset") {
        Settings t = settings_;
        if (!applyPreset(t, body.get("name").asString())) return fail("unknown preset");
        settings_ = t; engineDirty_ = true; configDirty_ = true; configDirtyAt_ = steadyNowUs();
        return ok();
    }
    if (p == "/api/song") {
        int idx = body.has("index") ? body.get("index").asInt(-1) : songs_.find(body.get("name").asString());
        Settings t = settings_;
        if (!songs_.apply(idx, t)) return fail("no such song");
        settings_ = t; songs_.setCurrent(idx); engineDirty_ = true; configDirty_ = true; configDirtyAt_ = steadyNowUs();
        drift_.setSongName(idx, songs_.songs()[(size_t) idx].name);
        drummer_.targetBpm = songs_.songs()[(size_t) idx].bpm;
        if (songs_.songs()[(size_t) idx].countInBeats > 0) drummer_.countInRequests = 1;
        return ok();
    }
    if (p == "/api/songs") {
        if (body.has("csv")) { if (!songs_.fromCsv(body.get("csv").asString(), &err)) return fail(err); }
        else if (!songs_.fromJson(body.get("songs"))) return fail("expected csv or songs");
        for (size_t i = 0; i < songs_.songs().size(); ++i) drift_.setSongName((int) i, songs_.songs()[i].name);
        configDirty_ = true; configDirtyAt_ = steadyNowUs();
        return ok();
    }
    if (p == "/api/sim") {
        auto clampd = [](double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); };
        if (body.has("bpm")) drummer_.targetBpm = clampd(body.get("bpm").asNumber(120), 40, 240);
        if (body.has("jitterMs")) drummer_.jitterMs = clampd(body.get("jitterMs").asNumber(8), 0, 40);
        if (body.has("drift")) drummer_.drift = clampd(body.get("drift").asNumber(0.3), 0, 3);
        if (body.has("playing")) drummer_.playing = body.get("playing").asBool();
        if (body.has("fill") && body.get("fill").asBool()) drummer_.fillRequests = 1;
        if (body.has("countIn") && body.get("countIn").asBool()) drummer_.countInRequests = 1;
        return ok();
    }
    if (p == "/api/calibrate") {
        if (body.get("mode").asString("loopback") == "reset") {
            settings_.calibration = CalibrationSettings{}; calibrationResult_ = Json(); engineDirty_ = true;
            return ok();
        }
        calibrationResult_ = runLoopbackLocked();
        Json j = Json::object(); j["ok"] = true; j["result"] = calibrationResult_;
        return { 200, "application/json", j.dump(), {}, false };
    }
    return fail("not found", 404);
}

} // namespace pacemaker
