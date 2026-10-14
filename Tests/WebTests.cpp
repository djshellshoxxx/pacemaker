// Web UI tests: a real WebUi server on an ephemeral port, driven over real sockets with httplib::Client, against a
// Core that is fed by the synthetic drum machine.
#include "TestFramework.h"

#if defined(PM_HAVE_WEB) && defined(PM_HAVE_SERVICES)
#include "DrumMachine.h"
#include "httplib.h"
#include "pacemaker/Core.h"
#include "pacemaker/WebUi.h"
#include <chrono>
#include <cmath>
#include <memory>
#include <thread>

using namespace pacemaker;

namespace {

constexpr double kWebSr = 48000.0;

struct WebRig {
    Core core;
    std::unique_ptr<WebUi> web;
    std::unique_ptr<httplib::Client> cli;
    drums::Machine dm { kWebSr, 11 };
    int64_t fed = 0;

    // bars of 120 BPM drums fed faster than real time (offline host times: outputs stay suppressed).
    explicit WebRig(int bars, bool startWeb = true, const WebUi::Hooks* hooks = nullptr, const std::string& token = "", double feedSec = -1.0)
    {
        dm.bpm = 120; dm.walkSigma = 0.1; dm.jitterMs = 3.0;
        dm.silence(1.0);
        dm.bars(bars);
        SessionSettings s;
        s.channels = { { 0, InputRole::Any, 0.0, false } };
        core.setSession(s);
        core.prepare(kWebSr, 256, 1);
        feedSeconds(feedSec >= 0 ? feedSec : (double) dm.audio.size() / kWebSr);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        core.housekeepingOnce();
        web = std::make_unique<WebUi>(core);
        if (hooks) web->setHooks(*hooks);
        if (!token.empty()) web->setToken(token);
        if (startWeb) {
            REQUIRE(web->start(0, "127.0.0.1"));
            cli = std::make_unique<httplib::Client>("127.0.0.1", web->port());
            cli->set_connection_timeout(3, 0);
            cli->set_read_timeout(5, 0);
        }
    }
    ~WebRig() { if (web) web->stop(); }

    void feedSeconds(double sec)
    {
        const int64_t end = std::min<int64_t>((int64_t) dm.audio.size(), fed + (int64_t) (sec * kWebSr));
        const int64_t base = steadyNowUs();
        while (fed < end) {
            const int n = (int) std::min<int64_t>(256, end - fed);
            const float* ch[1] = { dm.audio.data() + fed };
            CoreHostInfo h; h.offline = true; h.hostTimeUs = base + (int64_t) std::llround((double) fed * 1e6 / kWebSr);
            core.process(ch, 1, n, h, nullptr, 0, nullptr, nullptr, nullptr, nullptr, 0);
            fed += n;
            if ((fed / 256) % 512 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(8));    // let housekeeping drain the queues
        }
    }

    // Continue with the same audio paced to the wall clock; host times then come from the steady clock.
    void feedRealtime(double sec)
    {
        const int64_t end = std::min<int64_t>((int64_t) dm.audio.size(), fed + (int64_t) (sec * kWebSr));
        const auto t0 = std::chrono::steady_clock::now();
        const int64_t start = fed;
        while (fed < end) {
            const int n = (int) std::min<int64_t>(256, end - fed);
            std::this_thread::sleep_until(t0 + std::chrono::microseconds((int64_t) ((double) (fed + n - start) * 1e6 / kWebSr)));
            const float* ch[1] = { dm.audio.data() + fed };
            CoreHostInfo h; h.hostTimeUs = -1;
            core.process(ch, 1, n, h, nullptr, 0, nullptr, nullptr, nullptr, nullptr, 0);
            fed += n;
        }
    }

    Json getJson(const std::string& path, int* status = nullptr)
    {
        auto r = cli->Get(path);
        Json j;
        if (!r) { if (status) *status = 0; return j; }
        if (status) *status = r->status;
        Json::parse(r->body, j);
        return j;
    }
    Json postJson(const std::string& path, const std::string& body, int* status)
    {
        auto r = cli->Post(path, body, "application/json");
        Json j;
        if (!r) { *status = 0; return j; }
        *status = r->status;
        Json::parse(r->body, j);
        return j;
    }
    int status(const httplib::Result& r) { return r ? r->status : 0; }
};

} // namespace

TEST_CASE(Web_PagesAndAssetsAreEmbedded)
{
    WebRig rig(2);
    auto idx = rig.cli->Get("/");
    REQUIRE(idx && idx->status == 200);
    CHECK(idx->get_header_value("Content-Type").find("text/html") != std::string::npos);
    CHECK(idx->body.find("Pacemaker") != std::string::npos);
    CHECK(idx->body.find("/app.js") != std::string::npos);
    auto met = rig.cli->Get("/metronome");
    REQUIRE(met && met->status == 200);
    CHECK(met->body.find("wakeLock") != std::string::npos);          // keeps the phone awake
    CHECK(met->body.find("/api/events") != std::string::npos);       // uses the status stream
    CHECK(met->body.find("viewport") != std::string::npos);          // phone friendly
    auto js = rig.cli->Get("/app.js");
    CHECK(js && js->status == 200 && js->get_header_value("Content-Type").find("javascript") != std::string::npos);
    auto css = rig.cli->Get("/app.css");
    CHECK(css && css->status == 200 && css->body.find("--bg") != std::string::npos);
    auto svg = rig.cli->Get("/icon.svg");
    CHECK(svg && svg->status == 200 && svg->get_header_value("Content-Type").find("svg") != std::string::npos);
    auto nope = rig.cli->Get("/does-not-exist");
    CHECK(nope && nope->status == 404);
}

TEST_CASE(Web_StatusJsonHasAllFieldsAndReflectsTheEngine)
{
    WebRig rig(30);                                              // 60 s of 120 BPM drums
    int code = 0;
    const Json j = rig.getJson("/api/status", &code);
    REQUIRE(code == 200);
    for (const char* k : { "v", "serverMs", "tempo", "state", "stateId", "confidence", "bar", "beat", "beatsPerBar", "beatUnit", "next",
                           "follow", "standby", "deviceLost", "song", "params", "inputs", "outputs", "nudgeMs" })
        CHECK_MSG(j.has(k), std::string("missing key ") + k);
    for (const char* k : { "bpm", "periodMs", "valid" }) CHECK_MSG(j["tempo"].has(k), std::string("tempo.") + k);
    for (const char* k : { "inMs", "beat", "bar" }) CHECK_MSG(j["next"].has(k), std::string("next.") + k);
    for (const char* k : { "midi", "osc", "link", "sync" }) CHECK_MSG(j["outputs"].has(k), std::string("outputs.") + k);
    CHECK(j["outputs"]["link"].has("peers"));
    CHECK(j["outputs"]["midi"].has("jitterUs"));
    CHECK(j["outputs"]["osc"].has("host"));
    REQUIRE(j["inputs"].items().size() >= 1);
    CHECK(j["inputs"].items()[0].has("db"));
    CHECK(j["inputs"].items()[0].has("role"));

    const Timeline tl = rig.core.timeline();
    CHECK(j["stateId"].getInt() == (int) tl.snap.state);
    CHECK(j["state"].getString() == "LOCKED");
    CHECK_MSG(std::fabs(j["tempo"]["bpm"].getNumber() - 120.0) < 5.0, "bpm = " + std::to_string(j["tempo"]["bpm"].getNumber()));
    CHECK(j["tempo"]["valid"].getBool());
    CHECK(j["confidence"].getNumber() > 0.3);
    CHECK(j["beatsPerBar"].getInt() == 4);
    CHECK(j["follow"].getBool());
    CHECK(j["tempo"]["periodMs"].getNumber() > 400 && j["tempo"]["periodMs"].getNumber() < 600);
    CHECK(j["params"]["meterNum"].getInt() == 4);
    CHECK(j["next"]["beat"].getInt() >= 1 && j["next"]["beat"].getInt() <= 4);
}

TEST_CASE(Web_ActionsChangeTheCore)
{
    WebRig rig(4);
    int code = 0;
    CHECK(rig.core.follow());
    rig.postJson("/api/action/follow", "", &code);
    CHECK(code == 200);
    CHECK(!rig.core.follow());
    CHECK(!rig.getJson("/api/status")["follow"].getBool(true));
    rig.postJson("/api/action/follow?on=1", "", &code);
    CHECK(code == 200 && rig.core.follow());
    rig.postJson("/api/action/follow", R"({"on":false})", &code);
    CHECK(code == 200 && !rig.core.follow());
    rig.postJson("/api/action/follow?on=true", "", &code);
    CHECK(rig.core.follow());

    // nudge: explicit amount, then the +/- buttons, clamped to +-100 ms
    rig.postJson("/api/action/nudge?ms=20", "", &code);
    CHECK(code == 200);
    CHECK(std::fabs(rig.getJson("/api/status")["nudgeMs"].getNumber() - 20.0) < 1e-6);
    rig.postJson("/api/action/nudgePlus", "", &code);
    CHECK(std::fabs(rig.getJson("/api/status")["nudgeMs"].getNumber() - 30.0) < 1e-6);
    rig.postJson("/api/action/nudgeMinus", "", &code);
    rig.postJson("/api/action/nudge", R"({"ms":-5})", &code);
    CHECK(std::fabs(rig.getJson("/api/status")["nudgeMs"].getNumber() - 15.0) < 1e-6);
    rig.postJson("/api/action/nudge?ms=1e9", "", &code);
    CHECK(code == 200);
    CHECK(rig.getJson("/api/status")["nudgeMs"].getNumber() <= 100.0);
    rig.postJson("/api/action/nudge?ms=abc", "", &code);
    CHECK(code == 400);

    // the plain actions are accepted (their engine effects are covered by the engine tests)
    for (const char* a : { "tap", "downbeatNow", "downbeat", "relock", "half", "double", "shiftMinus", "shiftPlus", "learn" }) {
        Json r = rig.postJson(std::string("/api/action/") + a, "", &code);
        CHECK_MSG(code == 200 && r["ok"].getBool(), std::string("action ") + a);
    }
    rig.postJson("/api/action/doesNotExist", "", &code);
    CHECK(code == 404);
    rig.postJson("/api/action/none", "", &code);
    CHECK(code == 404);
    auto wrongMethod = rig.cli->Get("/api/action/tap");
    CHECK(wrongMethod && wrongMethod->status >= 400 && wrongMethod->status < 500);
    rig.postJson("/api/action/tap", "{broken", &code);
    CHECK(code == 400);
}

TEST_CASE(Web_RelockActionTakesEffectInTheEngine)
{
    WebRig rig(32, true, nullptr, "", 50.0);
    REQUIRE(rig.core.timeline().snap.state == (uint8_t) EngineState::Locked);
    int code = 0;
    rig.postJson("/api/action/relock", "", &code);
    CHECK(code == 200);
    // The request is consumed by the next audio blocks: the engine leaves Locked or flags the relock in its snapshot.
    bool seen = false;
    for (int i = 0; i < 40 && !seen; ++i) {
        rig.feedSeconds(0.25);
        const auto snap = rig.core.timeline().snap;
        seen = snap.state != (uint8_t) EngineState::Locked || (snap.flags & SnapshotFlags::Relock) != 0;
    }
    CHECK(seen);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    rig.core.housekeepingOnce();
    bool logged = false;
    for (const auto& r : rig.core.driftRows()) if (r.event == "relock" || r.event == "state") logged = true;
    CHECK(logged);                                                 // and the drift log records it
}

TEST_CASE(Web_SongsListSelectAndNext)
{
    WebRig rig(2);
    int code = 0;
    auto put = rig.cli->Put("/api/songs", "Name,Tempo,Time Signature\nOpener,128,4/4\nSlow Waltz,90,3/4\nCloser,140,4/4\n", "text/csv");
    REQUIRE(put && put->status == 200);
    Json songs = rig.getJson("/api/songs", &code);
    REQUIRE(code == 200);
    REQUIRE(songs["songs"].items().size() == 3);
    CHECK(songs["songs"].items()[1]["name"].getString() == "Slow Waltz");

    rig.postJson("/api/song/1", "", &code);
    CHECK(code == 200);
    CHECK(rig.core.currentSong() == 1);
    CHECK(std::fabs(rig.core.params().referenceBpm - 90.0) < 1e-6);
    CHECK(rig.core.params().meterNum == 3);
    Json st = rig.getJson("/api/status");
    CHECK(st["song"]["index"].getInt() == 1);
    CHECK(st["song"]["name"].getString() == "Slow Waltz");

    rig.postJson("/api/action/nextSong", "", &code);
    for (int i = 0; i < 40 && rig.core.currentSong() != 2; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(25));
    CHECK(rig.core.currentSong() == 2);
    rig.postJson("/api/action/nextSong", "", &code);
    for (int i = 0; i < 40 && rig.core.currentSong() != 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(25));
    CHECK_MSG(rig.core.currentSong() == 0, "next wraps around");
    rig.postJson("/api/action/prevSong", "", &code);
    for (int i = 0; i < 40 && rig.core.currentSong() != 2; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(25));
    CHECK(rig.core.currentSong() == 2);

    rig.postJson("/api/song/3", "", &code);
    CHECK(code == 404);
    rig.postJson("/api/song/-1", "", &code);
    CHECK(code == 404);
    rig.postJson("/api/song/abc", "", &code);
    CHECK(code == 404);
    rig.postJson("/api/song/99999999999999999999", "", &code);
    CHECK(code >= 400 && code < 500);

    // JSON form, and bad input
    auto j = rig.cli->Put("/api/songs", R"({"songs":[{"name":"Only","bpm":100,"meterNum":4,"meterDen":4}]})", "application/json");
    CHECK(j && j->status == 200);
    CHECK(rig.getJson("/api/songs")["songs"].items().size() == 1);
    auto bad = rig.cli->Put("/api/songs", "not a list at all", "application/json");
    CHECK(bad && bad->status == 400);
    auto badCsv = rig.cli->Put("/api/songs", "just one line", "text/csv");
    CHECK(badCsv && badCsv->status == 400);
}

TEST_CASE(Web_StateRoundTripAndPatch)
{
    WebRig rig(2);
    auto got = rig.cli->Get("/api/state");
    REQUIRE(got && got->status == 200);
    Json state;
    REQUIRE(Json::parse(got->body, state));
    CHECK(state.has("params"));
    CHECK(state.has("session"));

    // PUT a modified full state and read it back
    state.set("params", [&] { Json p = state["params"]; p.set("referenceBpm", 133.0).set("pattern", "waltz34").set("meterNum", 3); return p; }());
    state.set("session", [&] {
        Json s = state["session"];
        Json midi = s["midi"]; midi.set("offsetMs", 12.5).set("enabled", true); s.set("midi", midi);
        Json osc = s["osc"]; osc.set("host", "10.1.2.3").set("port", 7700); s.set("osc", osc);
        return s;
    }());
    auto put = rig.cli->Put("/api/state", state.dump(), "application/json");
    REQUIRE(put && put->status == 200);
    const PluginState now = rig.core.state();
    CHECK(now.params.referenceBpm == 133.0);
    CHECK(now.params.pattern == BarPattern::Waltz34);
    CHECK(now.params.meterNum == 3);
    CHECK(now.session.midi.offsetMs == 12.5);
    CHECK(now.session.midi.enabled);
    CHECK(now.session.osc.host == "10.1.2.3");
    CHECK(now.session.osc.port == 7700);
    Json back;
    REQUIRE(Json::parse(rig.cli->Get("/api/state")->body, back));
    CHECK(back["params"]["referenceBpm"].getNumber() == 133.0);
    CHECK(back["session"]["osc"]["host"].getString() == "10.1.2.3");
    CHECK(back["session"]["midi"]["offsetMs"].getNumber() == 12.5);
    // the status shows what was set
    Json st = rig.getJson("/api/status");
    CHECK(st["outputs"]["midi"]["enabled"].getBool());
    CHECK(st["outputs"]["osc"]["host"].getString() == "10.1.2.3");
    CHECK(st["params"]["pattern"].getString() == "waltz34");

    // patch: merge into the live state, leaves everything else alone
    int code = 0;
    rig.postJson("/api/state/patch", R"({"session":{"sync":{"enabled":true,"offsetMs":-2.5}}})", &code);
    CHECK(code == 200);
    const PluginState after = rig.core.state();
    CHECK(after.session.sync.enabled);
    CHECK(after.session.sync.offsetMs == -2.5);
    CHECK(after.session.osc.host == "10.1.2.3");            // untouched
    CHECK(after.params.referenceBpm == 133.0);

    // params endpoint with clamping
    rig.postJson("/api/params", R"({"referenceBpm":9999,"meterNum":99,"pattern":"rock44","meterDen":4})", &code);
    CHECK(code == 200);
    CHECK(rig.core.params().referenceBpm <= 300.0);
    CHECK(rig.core.params().meterNum <= 16);
    rig.postJson("/api/params", R"({"profile":"rehearsal"})", &code);
    CHECK(rig.core.params().profile == ProfileType::Rehearsal);

    // a future version with unknown keys loads without crashing and keeps the server alive
    auto fut = rig.cli->Put("/api/state", R"({"stateVersion":99,"params":{"referenceBpm":101,"mystery":1},"session":{"unknown":{"a":[1,2,3]}}})", "application/json");
    CHECK(fut && fut->status == 200);
    CHECK(rig.core.params().referenceBpm == 101.0);
    CHECK(rig.getJson("/api/status", &code).has("state"));
}

TEST_CASE(Web_StateStateChangedHookFires)
{
    int calls = 0; std::string lastPort;
    WebUi::Hooks hk;
    hk.stateChanged = [&](const PluginState& s) { ++calls; lastPort = s.session.midiPort; };
    WebRig rig(2, true, &hk);
    int code = 0;
    rig.postJson("/api/state/patch", R"({"session":{"midi":{"port":"Pisound MIDI"}}})", &code);
    CHECK(code == 200 && calls == 1 && lastPort == "Pisound MIDI");
    rig.postJson("/api/params", R"({"referenceBpm":110})", &code);
    CHECK(calls == 2);
    rig.postJson("/api/action/tap", "", &code);
    CHECK(calls == 2);                                            // actions are not persisted state
}

TEST_CASE(Web_ServerSentEventsDeliverLiveStatus)
{
    WebRig rig(30);
    httplib::Client c("127.0.0.1", rig.web->port());
    c.set_read_timeout(5, 0);
    std::string acc;
    int events = 0;
    const auto t0 = std::chrono::steady_clock::now();
    std::string contentType;
    auto res = c.Get("/api/events", [&](const httplib::Response& r) { contentType = r.get_header_value("Content-Type"); return true; },
                     [&](const char* d, size_t n) {
                         acc.append(d, n);
                         events = 0;
                         for (size_t p = 0; (p = acc.find("event: status", p)) != std::string::npos; ++p) ++events;
                         return events < 12 && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(4);
                     });
    (void) res;
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(contentType.find("text/event-stream") != std::string::npos);
    CHECK_MSG(events >= 12, "events = " + std::to_string(events));
    CHECK_MSG(events / std::max(secs, 0.05) > 10.0, "rate " + std::to_string(events / secs) + " /s");     // about 20 Hz
    // each event carries a complete status document
    const auto p = acc.find("data: ");
    REQUIRE(p != std::string::npos);
    const auto e = acc.find('\n', p);
    Json j;
    REQUIRE(Json::parse(acc.substr(p + 6, e - p - 6), j));
    CHECK(j["state"].getString() == "LOCKED");
    CHECK(j.has("next"));
    // the server still serves normal requests after a client hung up mid-stream
    CHECK(rig.status(rig.cli->Get("/api/status")) == 200);
}

TEST_CASE(Web_AnalysisEndpoints)
{
    WebRig rig(30);
    int code = 0;
    const Json tr = rig.getJson("/api/trace", &code);
    CHECK(code == 200);
    CHECK((int) tr["values"].items().size() == kTraceLen);
    double mx = 0; for (const auto& v : tr["values"].items()) mx = std::max(mx, v.getNumber());
    CHECK_MSG(mx > 0.1, "trace shows onsets, max " + std::to_string(mx));

    auto rep = rig.cli->Get("/api/report");
    REQUIRE(rep && rep->status == 200);
    CHECK(rep->get_header_value("Content-Type").find("text/html") != std::string::npos);
    CHECK(rep->body.find("<html") != std::string::npos);

    auto mid = rig.cli->Get("/api/tempomap.mid");
    REQUIRE(mid && mid->status == 200);
    CHECK(mid->get_header_value("Content-Type") == "audio/midi");
    CHECK(mid->body.size() > 14);
    CHECK(mid->body.compare(0, 4, "MThd") == 0);
    CHECK(mid->get_header_value("Content-Disposition").find("tempomap") != std::string::npos);

    auto csv = rig.cli->Get("/api/driftlog.csv");
    REQUIRE(csv && csv->status == 200);
    CHECK(csv->body.compare(0, 11, "timestampUs") == 0);
    CHECK(csv->body.size() > 200);                                // rows from the 60 s run

    const Json info = rig.getJson("/api/info", &code);
    CHECK(code == 200 && info["patterns"].items().size() >= 5 && info["actions"].items().size() >= 12);
}

TEST_CASE(Web_PortsAndDeviceHooks)
{
    WebUi::Hooks hk;
    hk.midiOutPorts = [] { return std::vector<std::string> { "Pisound MIDI", "USB MIDI 1" }; };
    hk.midiInPorts = [] { return std::vector<std::string> { "E-Drum Module" }; };
    hk.virtualMidiSupported = true;
    hk.deviceInfo = [] { Json d = Json::object(); d.set("name", "Test Interface").set("bufferSize", 128); return d; };
    hk.extraStatus = [](Json& j) { j.set("host", "unit-test"); };
    WebRig rig(2, true, &hk);
    int code = 0;
    const Json p = rig.getJson("/api/ports", &code);
    CHECK(code == 200);
    REQUIRE(p["midiOut"].items().size() == 2);
    CHECK(p["midiOut"].items()[0].getString() == "Pisound MIDI");
    CHECK(p["midiIn"].items()[0].getString() == "E-Drum Module");
    CHECK(p["virtualSupported"].getBool());
    CHECK(p["audio"]["name"].getString() == "Test Interface");
    CHECK(rig.getJson("/api/status")["host"].getString() == "unit-test");

    WebRig bare(2);                                               // no hooks: empty lists, still valid JSON
    const Json q = bare.getJson("/api/ports", &code);
    CHECK(code == 200 && q["midiOut"].isArray() && q["midiOut"].items().empty());
}

TEST_CASE(Web_CalibrationEndpoints)
{
    bool routed = false;
    WebUi::Hooks hk;
    hk.beginLoopback = [&](int ch) { routed = ch == 1; return ch == 1; };
    hk.tapAlongAvailable = false;
    WebRig rig(2, true, &hk);
    int code = 0;
    const Json c = rig.getJson("/api/calibration", &code);
    CHECK(code == 200);
    CHECK(c.has("loopback") && c.has("tapAlong") && c.has("hardware"));
    CHECK(!c["tapAlong"]["available"].getBool(true));

    rig.postJson("/api/calibration/loopback/finish", "", &code);
    CHECK(code == 409);                                           // nothing captured
    rig.postJson("/api/calibration/loopback/start", R"({"channel":0})", &code);
    CHECK(code == 409);                                           // the hook refuses: no click output routed
    rig.postJson("/api/calibration/loopback/start", R"({"channel":99})", &code);
    CHECK(code == 400);
    rig.postJson("/api/calibration/loopback/start", R"({"channel":"x"})", &code);
    CHECK(code == 400);
    rig.postJson("/api/calibration/loopback/start?channel=1", "", &code);
    CHECK(code == 200 && routed);

    rig.postJson("/api/calibration/tapalong/start", "", &code);
    CHECK(code == 409);                                           // not available on this device
    rig.postJson("/api/calibration/tapalong/accept", "", &code);
    CHECK(code == 409);

    rig.postJson("/api/calibration/hardware/apply", "", &code);
    CHECK(code == 409);
    rig.postJson("/api/calibration/hardware/start", R"({"role":"cowbell"})", &code);
    CHECK(code == 400);
    rig.postJson("/api/calibration/hardware/start", R"({"role":"kick"})", &code);
    CHECK(code == 200);
    CHECK(rig.getJson("/api/calibration")["hardware"]["running"].getBool());
    rig.postJson("/api/calibration/hardware/apply", "", &code);
    CHECK(code == 409);                                           // no click/hit pairs yet

    // default behaviour without hooks: the loopback starts on the core directly
    WebRig plain(2);
    plain.postJson("/api/calibration/loopback/start", R"({"channel":0})", &code);
    CHECK(code == 200 && plain.core.loopbackActive());
    plain.postJson("/api/calibration/tapalong/start", "", &code);
    CHECK(code == 200);
    CHECK(plain.getJson("/api/calibration")["tapAlong"]["running"].getBool());
    plain.postJson("/api/calibration/tapalong/accept", "", &code);
    CHECK(code == 409);                                           // fewer than 8 taps
}

TEST_CASE(Web_TokenProtectsTheApiButNotThePages)
{
    WebRig rig(2, true, nullptr, "s3cret-token");
    auto page = rig.cli->Get("/");
    CHECK(page && page->status == 200);                           // the page prompts for the token itself
    CHECK(rig.status(rig.cli->Get("/metronome")) == 200);
    CHECK(rig.status(rig.cli->Get("/api/status")) == 401);
    CHECK(rig.status(rig.cli->Get("/api/status?token=wrong")) == 401);
    CHECK(rig.status(rig.cli->Get("/api/status?token=s3cret")) == 401);             // prefix
    CHECK(rig.status(rig.cli->Get("/api/status?token=s3cret-token-and-more")) == 401);
    CHECK(rig.status(rig.cli->Get("/api/status?token=s3cret-token")) == 200);
    CHECK(rig.status(rig.cli->Get("/api/status", httplib::Headers { { "X-Pacemaker-Token", "s3cret-token" } })) == 200);
    CHECK(rig.status(rig.cli->Get("/api/status", httplib::Headers { { "Authorization", "Bearer s3cret-token" } })) == 200);
    CHECK(rig.status(rig.cli->Get("/api/status", httplib::Headers { { "Authorization", "Bearer nope" } })) == 401);
    CHECK(rig.status(rig.cli->Post("/api/action/tap", "", "application/json")) == 401);
    CHECK(rig.status(rig.cli->Post("/api/action/tap?token=s3cret-token", "", "application/json")) == 200);
    CHECK(rig.status(rig.cli->Put("/api/state", "{}", "application/json")) == 401);
    CHECK(rig.status(rig.cli->Get("/api/events")) == 401);
    auto err = rig.cli->Get("/api/status");
    Json j; REQUIRE(err && Json::parse(err->body, j));
    CHECK(!j["ok"].getBool(true));
    // clearing the token opens the API again
    rig.web->setToken("");
    CHECK(rig.status(rig.cli->Get("/api/status")) == 200);
}

TEST_CASE(Web_BadInputGivesClientErrorsAndNeverKillsTheServer)
{
    WebRig rig(2);
    const char* bodies[] = { "", "null", "[]", "[1,2,3]", "\"str\"", "123", "{", "}{", "{\"a\":", "\xff\xfe\x00garbage",
                             "{\"params\":[]}", "{\"params\":{\"referenceBpm\":\"fast\"}}", "{\"session\":{\"channels\":\"x\"}}",
                             "{\"session\":{\"songs\":{\"songs\":5}}}", "{\"stateVersion\":\"two\"}", "{\"params\":{\"meterNum\":-5,\"meterDen\":7}}" };
    const char* posts[] = { "/api/state/patch", "/api/params", "/api/calibration/loopback/start", "/api/calibration/hardware/start",
                            "/api/action/nudge", "/api/action/follow", "/api/driftlog/stop" };
    for (const char* path : posts)
        for (const char* b : bodies) {
            auto r = rig.cli->Post(path, std::string(b), "application/json");
            CHECK_MSG(r && r->status < 500, std::string("POST ") + path + " body '" + b + "' -> " + (r ? std::to_string(r->status) : "no response"));
        }
    for (const char* b : bodies) {
        auto r = rig.cli->Put("/api/state", std::string(b), "application/json");
        CHECK_MSG(r && r->status < 500, std::string("PUT /api/state body '") + b + "'");
        auto s = rig.cli->Put("/api/songs", std::string(b), "application/json");
        CHECK_MSG(s && s->status < 500, std::string("PUT /api/songs body '") + b + "'");
    }
    // deeply nested JSON is rejected by the parser, not by a stack overflow
    const std::string deep(100000, '[');
    auto d = rig.cli->Put("/api/state", deep, "application/json");
    CHECK(d && d->status == 400);
    // oversized body
    const std::string big(3 * 1024 * 1024, 'x');
    auto o = rig.cli->Put("/api/state", big, "application/json");
    CHECK_MSG(!o || (o->status >= 400 && o->status < 500), "oversized body");
    // odd URLs and methods
    for (const char* u : { "/api/", "/api/nope", "/../etc/passwd", "/api/action/", "/api/action/%00", "//", "/api/song/", "/api/status/extra" }) {
        httplib::Client c("127.0.0.1", rig.web->port());
        auto r = c.Get(u);
        CHECK_MSG(!r || r->status < 500, std::string("GET ") + u);
    }
    httplib::Client dc("127.0.0.1", rig.web->port());
    auto del = dc.Delete("/api/state");
    CHECK(del && del->status >= 400 && del->status < 500);
    // still alive and consistent
    int code = 0;
    CHECK(rig.getJson("/api/status", &code).has("state") && code == 200);
}

TEST_CASE(Web_ManyClientsAndRestart)
{
    WebRig rig(2);
    std::vector<std::thread> ths;
    std::atomic<int> okCount { 0 };
    for (int t = 0; t < 8; ++t)
        ths.emplace_back([&] {
            httplib::Client c("127.0.0.1", rig.web->port());
            c.set_read_timeout(5, 0);
            for (int i = 0; i < 25; ++i) { auto r = c.Get("/api/status"); if (r && r->status == 200) ++okCount; if (i % 5 == 0) c.Post("/api/action/tap"); }
        });
    for (auto& t : ths) t.join();
    CHECK(okCount.load() == 8 * 25);
    // stop and restart on a new port
    rig.web->stop();
    CHECK(!rig.web->running());
    REQUIRE(rig.web->start(0, "127.0.0.1"));
    httplib::Client c("127.0.0.1", rig.web->port());
    auto r = c.Get("/api/status");
    CHECK(r && r->status == 200);
    // binding an occupied port reports an error instead of crashing
    WebUi second(rig.core);
    CHECK(!second.start(rig.web->port(), "127.0.0.1"));
    CHECK(!second.lastError().empty());
}

TEST_CASE(Web_MetronomeStatusGivesAScheduleThatMatchesTheBeatGrid)
{
    // The phone page extrapolates beats from next.inMs + tempo.periodMs. With real-time feeding the next beat must fall
    // within one period, and two polls must agree on the absolute time of the same beat.
    WebRig live(40, false, nullptr, "", 55.0);                   // locks on 55 s of drums (offline host times)
    live.feedRealtime(2.0);                                      // now host times follow the steady clock
    auto statusAt = [&](double* absMs) {
        const Json st = live.web->statusJson();
        *absMs = st["serverMs"].getNumber() + st["next"]["inMs"].getNumber(-1e9);
        return st;
    };
    double abs1 = 0, abs2 = 0;
    Json s1 = statusAt(&abs1);
    REQUIRE(s1["tempo"]["valid"].getBool());
    const double period = s1["tempo"]["periodMs"].getNumber();
    CHECK(period > 400 && period < 600);
    const double in1 = s1["next"]["inMs"].getNumber(-1e9);
    CHECK_MSG(in1 >= -5.0 && in1 <= period + 5.0, "inMs = " + std::to_string(in1) + " period " + std::to_string(period));
    CHECK(s1["next"]["beat"].getInt() >= 1 && s1["next"]["beat"].getInt() <= s1["beatsPerBar"].getInt());
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    live.feedRealtime(0.05);
    Json s2 = statusAt(&abs2);
    if (s1["next"]["beat"].getInt() == s2["next"]["beat"].getInt() && s1["next"]["bar"].getInt() == s2["next"]["bar"].getInt())
        CHECK_MSG(std::fabs(abs1 - abs2) < 25.0, "same beat predicted " + std::to_string(abs1 - abs2) + " ms apart");
    else
        CHECK_MSG(std::fabs(std::fabs(abs2 - abs1) - period) < 40.0, "next beat is one period later");
}

#endif // PM_HAVE_WEB && PM_HAVE_SERVICES
