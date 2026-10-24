#include "pacemaker/WebUi.h"
#include "Assets.h"
#include "httplib.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

namespace pacemaker {

namespace {

const char* kRoleKeys[] = { "any", "kick", "snare", "hihat", "overhead", "trigger" };
const char* roleName(InputRole r) { return kRoleKeys[std::min<int>((int) r, 5)]; }
bool roleFromName(const std::string& s, InputRole& out)
{
    for (int i = 0; i < 6; ++i) if (s == kRoleKeys[i]) { out = (InputRole) i; return true; }
    return false;
}

Json mergeJson(const Json& base, const Json& patch)
{
    if (!base.isObject() || !patch.isObject()) return patch;
    Json out = base;
    for (const auto& kv : patch.members()) out.set(kv.first, base.has(kv.first) ? mergeJson(base[kv.first], kv.second) : kv.second);
    return out;
}

double dbOf(double lin) { return lin > 1e-6 ? 20.0 * std::log10(lin) : -120.0; }

bool constantTimeEq(const std::string& a, const std::string& b)
{
    unsigned diff = (unsigned) (a.size() ^ b.size());
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) diff |= (unsigned) (a[i] ^ b[i]);
    return diff == 0;
}

bool parseNumber(const std::string& s, double& out)
{
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end && *end == 0 && std::isfinite(out);
}

} // namespace

struct WebUi::Impl {
    explicit Impl(Core& c) : core(c) {}

    Core& core;
    Hooks hooks;
    mutable std::mutex cfgM;                     // guards hooks, token
    std::string token;

    std::unique_ptr<httplib::Server> svr;
    std::thread th;
    std::atomic<bool> running { false }, stopping { false };
    std::atomic<int> boundPort { 0 };
    std::atomic<int> sseClients { 0 };
    mutable std::mutex errM;
    std::string err;

    // UI-side bookkeeping for things Core exposes no getter for.
    std::atomic<double> nudgeTracked { 0.0 };
    std::atomic<bool> tapAlongOn { false }, hardwareOn { false };
    std::atomic<int> hardwareRole { (int) InputRole::Trigger };
    std::atomic<int> loopbackChannel { 0 };
    int64_t startedUs = steadyNowUs();
    mutable std::mutex lastLoopbackM;
    Json lastLoopback;

    Hooks hooksCopy() { std::lock_guard<std::mutex> l(cfgM); return hooks; }

    // ------------------------------------------------------------------ JSON builders
    Json outputsJson(const SessionSettings& s) const
    {
        Json o = Json::object();
        {
            auto& st = core.midiClock().status();
            Json m = Json::object();
            m.set("enabled", s.midi.enabled).set("connected", st.connected.load()).set("jitterUs", st.measuredJitterUs.load())
             .set("sent", (double) st.sent.load()).set("errors", st.errors.load()).set("error", st.lastError()).set("detail", st.detail())
             .set("port", s.midiPort).set("virtual", s.midiVirtualPort).set("offsetMs", s.midi.offsetMs).set("sppOnRelock", s.midi.sppOnRelock);
            o.set("midi", m);
        }
        {
            auto& st = core.osc().status();
            Json m = Json::object();
            m.set("enabled", s.osc.enabled).set("connected", st.connected.load()).set("sent", (double) st.sent.load())
             .set("errors", st.errors.load()).set("error", st.lastError()).set("host", s.osc.host).set("port", s.osc.port)
             .set("root", s.osc.root).set("offsetMs", s.osc.offsetMs).set("listenPort", s.osc.listenPort);
            o.set("osc", m);
        }
        {
            auto& st = core.link().status();
            Json m = Json::object();
            m.set("compiled", linkCompiledIn()).set("available", core.link().available()).set("enabled", s.link.enabled)
             .set("peers", st.peers.load()).set("connected", st.connected.load()).set("publisher", core.link().isPublisher())
             .set("errors", st.errors.load()).set("error", st.lastError()).set("quantum", s.link.quantum);
            o.set("link", m);
        }
        {
            Json m = Json::object();
            m.set("enabled", s.sync.enabled).set("offsetMs", s.sync.offsetMs).set("level", (double) s.sync.level).set("invert", s.sync.invertPolarity);
            o.set("sync", m);
        }
        return o;
    }

    Json statusJson() const
    {
        const Timeline tl = core.timeline();
        const int64_t now = steadyNowUs();
        const SessionSettings s = core.session();
        const Params p = core.params();
        Json j = Json::object();
        j.set("v", 1).set("serverMs", (double) now / 1000.0).set("uptimeS", (double) (now - startedUs) / 1.0e6);

        const double per = periodSamples(tl.snap);
        const bool valid = tl.map.valid && per > 0 && tl.map.sampleRate > 0;
        const double periodMs = valid ? per * 1000.0 / tl.map.sampleRate : 0.0;
        Json t = Json::object();
        t.set("bpm", tl.snap.bpm).set("periodMs", periodMs).set("valid", valid);
        j.set("tempo", t);
        j.set("state", stateName(tl.snap.state)).set("stateId", (int) tl.snap.state)
         .set("confidence", (double) tl.snap.confidence)
         .set("relock", (tl.snap.flags & SnapshotFlags::Relock) != 0)
         .set("octaveAmbiguity", (tl.snap.flags & SnapshotFlags::OctaveAmbiguity) != 0)
         .set("bar", tl.snap.barNumber).set("beat", tl.snap.beatInBar).set("beatsPerBar", tl.beatsPerBar)
         .set("beatUnit", p.meterDen);
        // The next beat on the predicted grid, for local extrapolation in the metronome page.
        Json nx = Json::object();
        if (valid && tl.snap.state != (uint8_t) EngineState::Idle) {
            const int64_t k = firstBeatAtOrAfter(tl, now);
            const GridBeat g = gridBeat(tl, k);
            nx.set("inMs", (double) (g.hostUs - now) / 1000.0).set("beat", g.beatInBar).set("bar", g.barNumber);
        } else {
            nx.set("inMs", Json()).set("beat", 0).set("bar", 0);
        }
        j.set("next", nx);
        j.set("follow", core.follow()).set("standby", core.standby()).set("deviceLost", core.deviceLost());
        Json song = Json::object();
        const int cur = core.currentSong();
        song.set("index", cur);
        song.set("name", cur >= 0 && cur < (int) s.songs.songs.size() ? s.songs.songs[(size_t) cur].name : std::string());
        j.set("song", song);
        j.set("params", p.toJson());

        // inputs: one meter per channel in use
        int nCh = 1;
        for (const auto& c : s.channels) nCh = std::max(nCh, c.channel + 1);
        nCh = std::min(nCh, kMaxChannels);
        Json in = Json::array();
        for (int c = 0; c < nCh; ++c) {
            InputRole role = InputRole::Any; bool mute = false;
            for (const auto& a : s.channels) if (a.channel == c) { role = a.role; mute = a.mute; break; }
            const double lv = core.inputLevel(c);
            Json e = Json::object();
            e.set("ch", c).set("role", roleName(role)).set("level", lv).set("db", dbOf(lv)).set("mute", mute);
            in.push(e);
        }
        j.set("inputs", in);
        j.set("outputs", outputsJson(s));
        j.set("nudgeMs", nudgeTracked.load());
        Hooks h; { std::lock_guard<std::mutex> l(cfgM); h = hooks; }
        if (h.extraStatus) h.extraStatus(j);
        return j;
    }

    Json calibrationJson() const
    {
        Json c = Json::object();
        const SessionSettings s = core.session();
        Json lb = Json::object();
        lb.set("active", core.loopbackActive()).set("ready", core.loopbackReady()).set("channel", loopbackChannel.load());
        { std::lock_guard<std::mutex> l(lastLoopbackM); lb.set("last", lastLoopback); }
        c.set("loopback", lb);
        Json ta = Json::object();
        const int n = tapAlongOn.load() ? core.tapAlongCount() : 0;
        Hooks h; { std::lock_guard<std::mutex> l(cfgM); h = hooks; }
        ta.set("available", h.tapAlongAvailable).set("running", tapAlongOn.load()).set("count", n).set("needed", 24)
          .set("medianMs", n > 0 ? core.tapAlongMedianMs() : 0.0).set("accepted", s.tapAlongValid).set("inputLatencyMs", s.inputLatencyMs);
        c.set("tapAlong", ta);
        Json hw = Json::object();
        double ms = 0; bool have = false;
        if (hardwareOn.load()) have = core.hardwareOffset(ms);
        hw.set("running", hardwareOn.load()).set("haveEstimate", have).set("measuredMs", have ? ms : 0.0)
          .set("clickRole", roleName((InputRole) hardwareRole.load())).set("midiClockOffsetMs", s.midi.offsetMs);
        c.set("hardware", hw);
        return c;
    }

    Json infoJson() const
    {
        Json j = Json::object();
        j.set("name", "pacemaker").set("apiVersion", 1).set("linkCompiled", linkCompiledIn());
        Json pats = Json::array();
        for (const auto& p : patternList()) {
            Json e = Json::object(); e.set("key", p.key).set("label", p.label).set("beats", p.beats).set("unit", p.unit); pats.push(e);
        }
        j.set("patterns", pats);
        Json roles = Json::array(); for (const char* r : kRoleKeys) roles.push(r);
        j.set("roles", roles);
        Json acts = Json::array();
        for (int a = 1; a < (int) Action::Count; ++a) acts.push(actionKey((Action) a));
        acts.push("nudge"); acts.push("downbeat"); acts.push("learn");
        j.set("actions", acts);
        Hooks h; { std::lock_guard<std::mutex> l(cfgM); h = hooks; }
        j.set("title", h.title).set("tapAlongAvailable", h.tapAlongAvailable);
        if (h.deviceInfo) j.set("device", h.deviceInfo());
        return j;
    }

    // ------------------------------------------------------------------ helpers
    static void sendJson(httplib::Response& res, int code, const Json& j)
    {
        res.status = code;
        res.set_header("Cache-Control", "no-store");
        res.set_content(j.dump(), "application/json; charset=utf-8");
    }
    static void sendErr(httplib::Response& res, int code, const std::string& msg)
    {
        Json j = Json::object(); j.set("ok", false).set("error", msg);
        sendJson(res, code, j);
    }
    static void sendOk(httplib::Response& res, Json j = Json::object())
    {
        j.set("ok", true);
        sendJson(res, 200, j);
    }
    // Body as a JSON object; an empty body counts as an empty object. Returns false and answers 400 otherwise.
    static bool bodyObject(const httplib::Request& req, httplib::Response& res, Json& out, bool allowEmpty = true)
    {
        out = Json::object();
        if (req.body.empty()) { if (allowEmpty) return true; sendErr(res, 400, "empty body"); return false; }
        std::string e;
        Json j;
        if (!Json::parse(req.body, j, &e)) { sendErr(res, 400, "invalid JSON: " + e); return false; }
        if (!j.isObject()) { sendErr(res, 400, "JSON object expected"); return false; }
        out = j;
        return true;
    }
    static bool numParam(const httplib::Request& req, const Json& body, const char* key, double& out)
    {
        if (req.has_param(key)) return parseNumber(req.get_param_value(key), out);
        if (body.has(key) && body[key].isNumber()) { out = body[key].getNumber(); return std::isfinite(out); }
        return false;
    }
    void notifyState()
    {
        Hooks h = hooksCopy();
        if (h.stateChanged) h.stateChanged(core.state());
    }
    bool authorised(const httplib::Request& req)
    {
        std::string t; { std::lock_guard<std::mutex> l(cfgM); t = token; }
        if (t.empty()) return true;
        std::string given = req.get_param_value("token");
        if (given.empty()) given = req.get_header_value("X-Pacemaker-Token");
        if (given.empty()) {
            const std::string a = req.get_header_value("Authorization");
            if (a.compare(0, 7, "Bearer ") == 0) given = a.substr(7);
        }
        return constantTimeEq(given, t);
    }

    static const char* mime(const std::string& n)
    {
        if (n.size() > 5 && n.compare(n.size() - 5, 5, ".html") == 0) return "text/html; charset=utf-8";
        if (n.size() > 3 && n.compare(n.size() - 3, 3, ".js") == 0) return "text/javascript; charset=utf-8";
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".css") == 0) return "text/css; charset=utf-8";
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".svg") == 0) return "image/svg+xml";
        return "application/octet-stream";
    }
    static bool serveAsset(httplib::Response& res, const char* name)
    {
        const webassets::Asset* a = webassets::find(name);
        if (!a) { sendErr(res, 404, "asset missing"); return false; }
        res.set_header("Cache-Control", "no-cache");
        res.set_content(std::string((const char*) a->data, a->size), mime(name));
        return true;
    }

    // ------------------------------------------------------------------ routes
    void install(httplib::Server& s)
    {
        // httplib's default sets SO_REUSEPORT on Linux, which lets a second instance bind the same port silently.
        // An appliance wants a clear "port in use" instead: SO_REUSEADDR only (restart without TIME_WAIT delays).
        s.set_socket_options([](auto sock) {
            int yes = 1;
#ifdef _WIN32
            setsockopt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&yes), sizeof(yes));
#else
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
#endif
        });
        s.set_payload_max_length(1024 * 1024);
        s.set_keep_alive_max_count(20);
        s.set_read_timeout(5, 0);
        s.set_write_timeout(5, 0);

        s.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
            res.set_header("X-Content-Type-Options", "nosniff");
            res.set_header("Referrer-Policy", "no-referrer");
            if (req.path.compare(0, 5, "/api/") == 0 && !authorised(req)) {
                sendErr(res, 401, "token required");
                return httplib::Server::HandlerResponse::Handled;
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });
        s.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
            std::string m = "internal error";
            try { if (ep) std::rethrow_exception(ep); } catch (const std::exception& e) { m = e.what(); } catch (...) {}
            sendErr(res, 500, m);
        });
        s.set_error_handler([](const httplib::Request&, httplib::Response& res) {
            if (res.body.empty()) sendErr(res, res.status > 0 ? res.status : 500, "error " + std::to_string(res.status));
        });

        // ---- pages and assets (public: they contain no secrets; the API needs the token)
        s.Get("/", [](const httplib::Request&, httplib::Response& res) { serveAsset(res, "index.html"); });
        s.Get("/index.html", [](const httplib::Request&, httplib::Response& res) { serveAsset(res, "index.html"); });
        s.Get("/metronome", [](const httplib::Request&, httplib::Response& res) { serveAsset(res, "metronome.html"); });
        s.Get("/app.js", [](const httplib::Request&, httplib::Response& res) { serveAsset(res, "app.js"); });
        s.Get("/app.css", [](const httplib::Request&, httplib::Response& res) { serveAsset(res, "app.css"); });
        s.Get("/icon.svg", [](const httplib::Request&, httplib::Response& res) { serveAsset(res, "icon.svg"); });
        s.Get("/favicon.ico", [](const httplib::Request&, httplib::Response& res) { serveAsset(res, "icon.svg"); });

        // ---- status
        s.Get("/api/status", [this](const httplib::Request&, httplib::Response& res) { sendJson(res, 200, statusJson()); });
        s.Get("/api/info", [this](const httplib::Request&, httplib::Response& res) { sendJson(res, 200, infoJson()); });
        s.Get("/api/events", [this](const httplib::Request&, httplib::Response& res) {
            if (sseClients.load() >= 8) { sendErr(res, 503, "too many live clients"); return; }
            sseClients.fetch_add(1);
            res.set_header("Cache-Control", "no-store");
            res.set_header("X-Accel-Buffering", "no");
            res.set_chunked_content_provider("text/event-stream", [this](size_t, httplib::DataSink& sink) {
                std::string hello = "retry: 1000\n\n";
                if (!sink.write(hello.data(), hello.size())) return false;
                int tick = 0;
                while (!stopping.load() && sink.is_writable()) {
                    std::string ev = "event: status\ndata: " + statusJson().dump() + "\n\n";
                    if (!sink.write(ev.data(), ev.size())) break;
                    if (++tick % 100 == 0) { const char* ping = ": keepalive\n\n"; if (!sink.write(ping, std::strlen(ping))) break; }
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                sink.done();
                return false;
            }, [this](bool) { sseClients.fetch_sub(1); });
        });

        // ---- actions
        s.Post(R"(/api/action/([A-Za-z]+))", [this](const httplib::Request& req, httplib::Response& res) {
            std::string name = req.matches[1];
            Json body;
            if (!bodyObject(req, res, body)) return;
            if (name == "downbeat") name = "downbeatNow";
            if (name == "nudge") {
                double ms = 10.0;
                if ((req.has_param("ms") || body.has("ms")) && !numParam(req, body, "ms", ms)) { sendErr(res, 400, "ms must be a number"); return; }
                ms = std::clamp(ms, -100.0, 100.0);
                core.nudge(ms);
                nudgeTracked = std::clamp(nudgeTracked.load() + ms, -100.0, 100.0);
            } else if (name == "learn") {
                core.learnPattern();
            } else if (name == "follow" && (req.has_param("on") || body.has("on"))) {
                bool on = true;
                if (req.has_param("on")) { const std::string v = req.get_param_value("on"); on = !(v == "0" || v == "false" || v == "off"); }
                else on = body["on"].getBool(true);
                core.setFollow(on);
            } else {
                const Action a = actionFromKey(name);
                if (a == Action::None || a == Action::Count) { sendErr(res, 404, "unknown action '" + name + "'"); return; }
                core.doAction(a);
                if (a == Action::NudgeMinus) nudgeTracked = std::clamp(nudgeTracked.load() - 10.0, -100.0, 100.0);
                if (a == Action::NudgePlus) nudgeTracked = std::clamp(nudgeTracked.load() + 10.0, -100.0, 100.0);
            }
            Json r = Json::object(); r.set("action", name).set("follow", core.follow());
            sendOk(res, r);
        });

        // ---- state
        s.Get("/api/state", [this](const httplib::Request&, httplib::Response& res) {
            res.set_header("Cache-Control", "no-store");
            res.set_content(core.state().toJson().dump(), "application/json; charset=utf-8");
        });
        s.Put("/api/state", [this](const httplib::Request& req, httplib::Response& res) {
            PluginState st; std::string warn;
            Json probe;
            if (!Json::parse(req.body, probe) || !probe.isObject()) { sendErr(res, 400, "state must be a JSON object"); return; }
            if (!PluginState::deserialise(req.body, st, &warn)) { sendErr(res, 400, "cannot parse state"); return; }
            core.setState(st);
            notifyState();
            Json r = Json::object(); if (!warn.empty()) r.set("warning", warn);
            sendOk(res, r);
        });
        s.Post("/api/state/patch", [this](const httplib::Request& req, httplib::Response& res) {
            Json patch;
            if (!bodyObject(req, res, patch, false)) return;
            const Json merged = mergeJson(core.state().toJson(), patch);
            PluginState st; std::string warn;
            if (!PluginState::deserialise(merged.dump(), st, &warn)) { sendErr(res, 400, "cannot apply patch"); return; }
            core.setState(st);
            notifyState();
            sendOk(res);
        });
        s.Post("/api/params", [this](const httplib::Request& req, httplib::Response& res) {
            Json patch;
            if (!bodyObject(req, res, patch, false)) return;
            core.setParams(Params::fromJson(patch, core.params()));
            notifyState();
            Json r = Json::object(); r.set("params", core.params().toJson());
            sendOk(res, r);
        });

        // ---- songs
        s.Get("/api/songs", [this](const httplib::Request&, httplib::Response& res) {
            SongMap m = core.session().songs;
            m.current = core.currentSong();
            sendJson(res, 200, m.toJson());
        });
        s.Put("/api/songs", [this](const httplib::Request& req, httplib::Response& res) {
            SongMap m;
            if (req.get_header_value("Content-Type").find("csv") != std::string::npos) {
                std::string e;
                if (!SongMap::fromCsv(req.body, m, &e)) { sendErr(res, 400, e.empty() ? "invalid CSV" : e); return; }
            } else {
                Json j;
                if (!Json::parse(req.body, j) || !(j.isArray() || j.isObject())) { sendErr(res, 400, "song list JSON expected"); return; }
                m = SongMap::fromJson(j);
            }
            if (m.songs.size() > 500) { sendErr(res, 400, "too many songs"); return; }
            m.current = -1;
            core.editSession([&](SessionSettings& ss) { ss.songs = m; });
            notifyState();
            sendOk(res);
        });
        s.Post(R"(/api/song/(-?[0-9]+))", [this](const httplib::Request& req, httplib::Response& res) {
            const long i = std::strtol(req.matches[1].str().c_str(), nullptr, 10);
            const long n = (long) core.session().songs.songs.size();
            if (i < 0 || i >= n) { sendErr(res, 404, "no such song"); return; }
            core.selectSong((int) i);
            notifyState();
            Json r = Json::object(); r.set("index", (int) i); sendOk(res, r);
        });

        // ---- analysis outputs
        s.Get("/api/trace", [this](const httplib::Request&, httplib::Response& res) {
            std::vector<float> t((size_t) kTraceLen);
            core.copyTrace(t.data(), kTraceLen);
            Json a = Json::array();
            for (float v : t) a.push(std::round((double) v * 1000.0) / 1000.0);
            Json j = Json::object(); j.set("rate", 100).set("seconds", kTraceLen / 100.0).set("values", a);
            sendJson(res, 200, j);
        });
        s.Get("/api/report", [this](const httplib::Request&, httplib::Response& res) {
            res.set_header("Cache-Control", "no-store");
            res.set_content(renderDriftReportHtml(core.driftReport(), "Pacemaker drift report"), "text/html; charset=utf-8");
        });
        s.Get("/api/tempomap.mid", [this](const httplib::Request&, httplib::Response& res) {
            const Params p = core.params();
            const auto bytes = buildTempoMapMidi(core.driftRows(), p.meterNum, p.meterDen);
            res.set_header("Content-Disposition", "attachment; filename=\"pacemaker-tempomap.mid\"");
            res.set_header("Cache-Control", "no-store");
            res.set_content(std::string((const char*) bytes.data(), bytes.size()), "audio/midi");
        });
        s.Get("/api/driftlog.csv", [this](const httplib::Request&, httplib::Response& res) {
            std::string csv = driftHeader();
            for (const auto& r : core.driftRows()) csv += driftRowToCsv(r);
            res.set_header("Content-Disposition", "attachment; filename=\"pacemaker-drift.csv\"");
            res.set_content(csv, "text/csv; charset=utf-8");
        });
        s.Post(R"(/api/driftlog/(start|stop))", [this](const httplib::Request& req, httplib::Response& res) {
            Json r = Json::object();
            if (req.matches[1] == "start") { const bool ok = core.startDriftLog(); r.set("started", ok); }
            else core.stopDriftLog();
            r.set("path", core.driftLogPath());
            sendOk(res, r);
        });

        // ---- ports and devices
        s.Get("/api/ports", [this](const httplib::Request&, httplib::Response& res) {
            Hooks h = hooksCopy();
            Json j = Json::object();
            Json out = Json::array(), in = Json::array();
            if (h.midiOutPorts) for (const auto& p : h.midiOutPorts()) out.push(p);
            if (h.midiInPorts) for (const auto& p : h.midiInPorts()) in.push(p);
            j.set("midiOut", out).set("midiIn", in).set("virtualSupported", h.virtualMidiSupported)
             .set("current", core.session().midiPort);
            if (h.deviceInfo) j.set("audio", h.deviceInfo());
            sendJson(res, 200, j);
        });

        // ---- calibration (ES-03)
        s.Get("/api/calibration", [this](const httplib::Request&, httplib::Response& res) { sendJson(res, 200, calibrationJson()); });
        s.Post("/api/calibration/loopback/start", [this](const httplib::Request& req, httplib::Response& res) {
            Json body; if (!bodyObject(req, res, body)) return;
            double ch = 0;
            if ((req.has_param("channel") || body.has("channel")) && !numParam(req, body, "channel", ch)) { sendErr(res, 400, "channel must be a number"); return; }
            if (ch < 0 || ch >= kMaxChannels) { sendErr(res, 400, "channel out of range"); return; }
            Hooks h = hooksCopy();
            loopbackChannel = (int) ch;
            bool ok = true;
            if (h.beginLoopback) ok = h.beginLoopback((int) ch); else core.startLoopback((int) ch);
            if (!ok) { sendErr(res, 409, "loopback needs an audio output channel for the click"); return; }
            { std::lock_guard<std::mutex> l(lastLoopbackM); lastLoopback = Json(); }
            sendOk(res);
        });
        s.Post("/api/calibration/loopback/finish", [this](const httplib::Request&, httplib::Response& res) {
            if (!core.loopbackReady()) { sendErr(res, 409, core.loopbackActive() ? "capture still running" : "no capture to finish"); return; }
            Hooks h = hooksCopy();
            int rin = 0, rout = 0;
            if (h.reportedLatency) h.reportedLatency(rin, rout);
            const LoopbackResult r = core.finishLoopback(rin, rout);
            Json j = Json::object();
            j.set("pass", r.pass).set("clicksTotal", r.clicksTotal).set("clicksDetected", r.clicksDetected)
             .set("roundTripSamples", r.roundTripSamples).set("spreadSamples", r.spreadSamples)
             .set("reportedSumSamples", r.reportedSumSamples).set("hiddenSamples", r.hiddenSamples)
             .set("hiddenInputSamples", r.hiddenInputSamples).set("hiddenOutputSamples", r.hiddenOutputSamples)
             .set("message", r.message);
            if (r.pass) core.setDeviceLatency(rin, rout, r.hiddenInputSamples, r.hiddenOutputSamples);
            if (h.loopbackFinished) h.loopbackFinished(r);
            { std::lock_guard<std::mutex> l(lastLoopbackM); lastLoopback = j; }
            sendOk(res, j);
        });
        s.Post("/api/calibration/tapalong/start", [this](const httplib::Request&, httplib::Response& res) {
            if (!hooksCopy().tapAlongAvailable) { sendErr(res, 409, "tap-along needs a host tempo grid (not available on this device)"); return; }
            core.startTapAlong(); tapAlongOn = true; sendOk(res);
        });
        s.Post("/api/calibration/tapalong/accept", [this](const httplib::Request&, httplib::Response& res) {
            if (!tapAlongOn.load() || core.tapAlongCount() < 8) { sendErr(res, 409, "need at least 8 tapped beats"); return; }
            core.acceptTapAlong(); tapAlongOn = false;
            notifyState();
            sendOk(res, calibrationJson());
        });
        s.Post("/api/calibration/hardware/start", [this](const httplib::Request& req, httplib::Response& res) {
            Json body; if (!bodyObject(req, res, body)) return;
            InputRole role = InputRole::Trigger;
            const std::string r = req.has_param("role") ? req.get_param_value("role") : body["role"].getString("trigger");
            if (!roleFromName(r, role)) { sendErr(res, 400, "unknown role '" + r + "'"); return; }
            hardwareRole = (int) role;
            core.startHardwareCalibration(role); hardwareOn = true; sendOk(res);
        });
        s.Post("/api/calibration/hardware/apply", [this](const httplib::Request&, httplib::Response& res) {
            double ms = 0;
            if (!hardwareOn.load() || !core.hardwareOffset(ms)) { sendErr(res, 409, "no measurement yet (needs 8 click/hit pairs)"); return; }
            core.applyHardwareOffset(ms); hardwareOn = false;
            notifyState();
            sendOk(res, calibrationJson());
        });
    }
};

// ---------------------------------------------------------------------------------------------------------
WebUi::WebUi(Core& core) : d_(new Impl(core)) {}
WebUi::~WebUi() { stop(); }

void WebUi::setHooks(Hooks h) { std::lock_guard<std::mutex> l(d_->cfgM); d_->hooks = std::move(h); }
void WebUi::setToken(const std::string& t) { std::lock_guard<std::mutex> l(d_->cfgM); d_->token = t; }
bool WebUi::running() const { return d_->running.load(); }
int WebUi::port() const { return d_->boundPort.load(); }
std::string WebUi::lastError() const { std::lock_guard<std::mutex> l(d_->errM); return d_->err; }
Json WebUi::statusJson() const { return d_->statusJson(); }

bool WebUi::start(int port, const std::string& bindAddress)
{
    stop();
    d_->stopping = false;
    d_->svr = std::make_unique<httplib::Server>();
    // SSE clients each hold a worker for as long as they are connected; keep headroom for normal requests.
    d_->svr->new_task_queue = [] { return new httplib::ThreadPool(16); };
    d_->install(*d_->svr);
    int p = 0;
    if (port > 0) p = d_->svr->bind_to_port(bindAddress, port) ? port : -1;
    else p = d_->svr->bind_to_any_port(bindAddress);
    if (p <= 0) {
        { std::lock_guard<std::mutex> l(d_->errM); d_->err = "cannot bind " + bindAddress + ":" + std::to_string(port); }
        d_->svr.reset();
        return false;
    }
    { std::lock_guard<std::mutex> l(d_->errM); d_->err.clear(); }
    d_->boundPort = p;
    d_->running = true;
    httplib::Server* svr = d_->svr.get();
    d_->th = std::thread([svr] { svr->listen_after_bind(); });
    svr->wait_until_ready();
    return true;
}

void WebUi::stop()
{
    d_->stopping = true;
    if (d_->svr) d_->svr->stop();
    if (d_->th.joinable()) d_->th.join();
    d_->svr.reset();
    d_->running = false;
    d_->boundPort = 0;
}

} // namespace pacemaker
