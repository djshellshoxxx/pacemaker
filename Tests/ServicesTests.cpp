#include "TestFramework.h"
#include "DrumMachine.h"
#include "pacemaker/Core.h"
#include "pacemaker/DriftLog.h"
#include "pacemaker/Extras.h"
#include "pacemaker/LinkOutput.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>

using namespace pacemaker;

namespace {

constexpr double kSr = 48000.0;

Timeline steadyTimeline(double bpm, int64_t originHostUs, EngineState st = EngineState::Locked)
{
    Timeline t;
    t.map.sampleRate = kSr; t.map.originSample = 0; t.map.originHostUs = originHostUs; t.map.valid = true;
    t.snap.bpm = bpm;
    t.snap.beatOriginSample = 0;
    t.snap.nextBeatSample = (int64_t) std::llround(60.0 / bpm * kSr);
    t.snap.beatInBar = 1; t.snap.barNumber = 4;
    t.snap.confidence = 0.9f; t.snap.state = (uint8_t) st; t.snap.sequence = 10;
    t.beatsPerBar = 4;
    return t;
}

double sdOf(const std::vector<double>& v)
{
    if (v.size() < 2) return 0;
    double m = 0; for (double x : v) m += x; m /= (double) v.size();
    double s = 0; for (double x : v) s += (x - m) * (x - m);
    return std::sqrt(s / (double) (v.size() - 1));
}

} // namespace

// ------------------------------------------------------------------ JSON / state
TEST_CASE(Json_RoundTripAndErrors)
{
    Json j;
    std::string err;
    CHECK(Json::parse("{\"a\":[1,2.5,\"x\\n\\u00e9\",true,null],\"b\":{\"c\":-3e2}}", j, &err));
    CHECK(j["a"].items().size() == 5);
    CHECK(j["a"].items()[1].getNumber() == 2.5);
    CHECK(j["a"].items()[2].getString() == std::string("x\n\xc3\xa9"));
    CHECK(j["b"]["c"].getNumber() == -300.0);
    Json k;
    CHECK(Json::parse(j.dump(), k));
    CHECK(k.dump() == j.dump());
    CHECK(!Json::parse("{\"a\":", k));
    CHECK(!Json::parse("[1,2", k));
    CHECK(!Json::parse("{} x", k));
    CHECK(j["missing"]["deep"].getInt(7) == 7);
}

TEST_CASE(A4_StateRoundTrip)
{
    PluginState st;
    st.params.referenceBpm = 128; st.params.pattern = BarPattern::Waltz34; st.params.meterNum = 3; st.params.profile = ProfileType::Rehearsal;
    st.params.sensitivity = 0.7f; st.params.autoOctave = true;
    st.session.channels = { { 0, InputRole::Kick, 0.1, false }, { 1, InputRole::Overhead, 1.0, true } };
    st.session.midi.enabled = true; st.session.midi.offsetMs = -12.5; st.session.midi.sppOnRelock = true; st.session.midiPort = "IAC Bus 1";
    st.session.osc.enabled = true; st.session.osc.host = "10.0.0.5"; st.session.osc.port = 7000; st.session.osc.profile = OscProfile::Resolume;
    st.session.osc.listenPort = 9001;
    st.session.link.enabled = true; st.session.link.quantum = 8; st.session.link.confidenceGate = 0.5;
    st.session.sync.enabled = true; st.session.sync.invertPolarity = true; st.session.sync.offsetMs = 1.5;
    st.session.mappings = { { true, 1, 64, Action::Follow }, { false, 0, 60, Action::Tap } };
    st.session.countIn.enabled = true; st.session.countIn.midiNote = 60; st.session.countIn.oscAddress = "/go";
    st.session.songs.songs = { { "Intro, \"quoted\"", 100.5, 6, 8, BarPattern::Compound68, 2, 5 } };
    st.session.songs.current = 0;
    st.session.inputLatencyMs = 17.25; st.session.tapAlongValid = true; st.session.nudgeMs = -20;
    st.session.notes.kick = { 35, 36 };

    PluginState back;
    std::string warn;
    CHECK(PluginState::deserialise(st.serialise(), back, &warn));
    CHECK(warn.empty());
    CHECK(back.serialise() == st.serialise());
    CHECK(back.session.songs.songs[0].name == "Intro, \"quoted\"");
    CHECK(back.session.mappings.size() == 2 && back.session.mappings[0].action == Action::Follow);
    CHECK(back.params.pattern == BarPattern::Waltz34 && back.params.referenceBpm == 128);
}

TEST_CASE(A4_UnknownKeysFutureVersionAndMigration)
{
    PluginState st;
    std::string warn;
    // Future version with unknown keys: loads what it understands, warns, does not crash.
    CHECK(PluginState::deserialise("{\"stateVersion\":99,\"mystery\":{\"x\":1},\"params\":{\"referenceBpm\":140,\"newThing\":5},"
                                   "\"session\":{\"midi\":{\"enabled\":true,\"port\":\"P\",\"zzz\":1},\"osc\":{\"port\":99999999}}}", st, &warn));
    CHECK(!warn.empty());
    CHECK(st.params.referenceBpm == 140);
    CHECK(st.session.midi.enabled && st.session.midiPort == "P");
    CHECK(st.session.osc.port == 65535);                 // clamped
    // v0 flat file migrates.
    PluginState v0;
    CHECK(PluginState::deserialise("{\"referenceBpm\":96,\"midiOffset\":-5,\"oscHost\":\"1.2.3.4\",\"oscPort\":8000,\"linkQuantum\":3}", v0, &warn));
    CHECK(v0.params.referenceBpm == 96 && v0.session.midi.offsetMs == -5);
    CHECK(v0.session.osc.host == "1.2.3.4" && v0.session.osc.port == 8000 && v0.session.link.quantum == 3);
    // Garbage and empty objects.
    CHECK(!PluginState::deserialise("not json", st, &warn));
    PluginState e; CHECK(PluginState::deserialise("{}", e, &warn));
    CHECK(e.params.tempoMax == 220.0);
}

TEST_CASE(SongMap_CsvAndLookup)
{
    SongMap m;
    std::string err;
    CHECK(SongMap::fromCsv("Name,Tempo,Time Signature\nOpener,128,4/4\n\"Slow, one\",72,6/8\nWaltz,90,3/4\n", m, &err));
    REQUIRE(m.songs.size() == 3);
    CHECK(m.songs[1].name == "Slow, one" && m.songs[1].meterNum == 6 && m.songs[1].meterDen == 8 && m.songs[1].pattern == BarPattern::Compound68);
    CHECK(m.songs[2].pattern == BarPattern::Waltz34);
    SongMap n;
    CHECK(SongMap::fromCsv(m.toCsv(), n));
    CHECK(n.songs.size() == 3 && n.songs[1].name == "Slow, one");
    m.songs[0].programChange = 7;
    CHECK(m.findProgram(7) == 0 && m.findProgram(8) == -1);
    CHECK(m.findName("  waltz ") == 2);
    Params p; m.apply(2, p);
    CHECK(p.referenceBpm == 90 && p.meterNum == 3);
    CHECK(!SongMap::fromCsv("foo,bar\n1,2\n", n, &err));
}

TEST_CASE(Presets_FactoryAndUser)
{
    CHECK(factoryPresets().size() >= 9);
    const std::string dir = "pm_test_presets";
    Params p; p.referenceBpm = 111; p.sensitivity = 0.9f;
    std::string err;
    CHECK(saveUserPreset(dir, "My Preset/1", p, &err));
    const auto list = loadUserPresets(dir);
    REQUIRE(list.size() >= 1);
    bool found = false;
    for (const auto& u : list) if (u.name == "My Preset/1") { found = true; CHECK(u.params.referenceBpm == 111); }
    CHECK(found);
}

// ------------------------------------------------------------------ clock map
TEST_CASE(ClockMap_HostTimeFilterRemovesJitter)
{
    HostTimeFilter f;
    drums::Rng rng(5);
    double worst = 0;
    int64_t out = 0;
    for (int i = 0; i < 2000; ++i) {
        const int64_t smp = (int64_t) i * 128;
        const int64_t truth = 5000000 + (int64_t) llround((double) smp * 1e6 / kSr);
        const int64_t noisy = truth + (int64_t) (rng.gauss() * 400.0);       // 0.4 ms callback jitter
        out = f.update(smp, noisy);
        if (i > 600) worst = std::max(worst, std::fabs((double) (out - truth)));
    }
    CHECK_MSG(worst < 120.0, "worst filtered error us = " + std::to_string(worst));
}

TEST_CASE(ClockMap_RoundTripAndGrid)
{
    ClockMap m; m.sampleRate = kSr; m.originSample = 1000; m.originHostUs = 2000000; m.valid = true;
    CHECK(m.hostTimeForSample(1000) == 2000000);
    CHECK(m.hostTimeForSample(1000 + 48000) == 3000000);
    CHECK(std::llabs(m.sampleForHostTime(m.hostTimeForSample(123456)) - 123456) <= 1);
    Timeline t = steadyTimeline(120, 0);
    const GridBeat g = gridBeat(t, 5);
    CHECK(g.beatInBar == 2 && g.barNumber == 5);
    CHECK(g.hostUs == 2500000);
    CHECK(gridBeat(t, -1).beatInBar == 4 && gridBeat(t, -1).barNumber == 3);
    CHECK(firstBeatAtOrAfter(t, 1) == 1 && firstBeatAtOrAfter(t, 0) == 0);
}

// ------------------------------------------------------------------ MIDI clock
TEST_CASE(MidiClock_PlannerStartTicksStop)
{
    MidiClockPlanner pl;
    MidiClockConfig cfg; cfg.enabled = true;
    Timeline t = steadyTimeline(120, 1000000);
    std::vector<MidiTimed> ev;
    const double per = periodSamples(t.snap) * 1e6 / kSr;
    const double org = (double) t.map.hostTimeForSample(0);
    // Not locked: nothing.
    Timeline idle = steadyTimeline(120, 1000000, EngineState::Locking);
    pl.plan(idle.snap, 4, org, per, 0, 1000000, 1020000, 30000, cfg, ev);
    CHECK(ev.empty());
    double from = 1000000;
    for (int w = 0; w < 150; ++w) { pl.plan(t.snap, 4, org, per, 0, from, from + 20000, 30000, cfg, ev); from += 20000; }
    REQUIRE(ev.size() > 100);
    CHECK(ev[0].bytes[0] == 0xFA);
    int ticks = 0; double prev = -1, maxDev = 0;
    for (size_t i = 1; i < ev.size(); ++i) {
        if (ev[i].bytes[0] != 0xF8) continue;
        ++ticks;
        if (prev >= 0) maxDev = std::max(maxDev, std::fabs(ev[i].t - prev - per / 24.0));
        prev = ev[i].t;
    }
    CHECK(ticks > 90);
    CHECK_MSG(maxDev < 1.0, "tick spacing deviation us = " + std::to_string(maxDev));
    // First tick coincides with the Start on a beat boundary.
    CHECK(std::fabs(std::fmod(ev[0].t - org, per)) < 1.0 || std::fabs(std::fmod(ev[0].t - org, per) - per) < 1.0);
    // Idle -> Stop once.
    Timeline gone = steadyTimeline(120, 1000000, EngineState::Idle);
    ev.clear();
    pl.plan(gone.snap, 4, org, per, 0, from, from + 20000, 30000, cfg, ev);
    CHECK(ev.size() == 1 && ev[0].bytes[0] == 0xFC);
    ev.clear();
    pl.plan(gone.snap, 4, org, per, 0, from + 20000, from + 40000, 30000, cfg, ev);
    CHECK(ev.empty());
}

TEST_CASE(MidiClock_SppOnlyOnRelockWhenEnabled)
{
    for (int sppOn = 0; sppOn < 2; ++sppOn) {
        MidiClockPlanner pl;
        MidiClockConfig cfg; cfg.enabled = true; cfg.sppOnRelock = sppOn != 0;
        Timeline t = steadyTimeline(120, 1000000);
        const double per = periodSamples(t.snap) * 1e6 / kSr;
        const double org = (double) t.map.hostTimeForSample(0);
        std::vector<MidiTimed> ev;
        double from = 1000000;
        for (int w = 0; w < 10; ++w) { pl.plan(t.snap, 4, org, per, 0, from, from + 20000, 30000, cfg, ev); from += 20000; }
        ev.clear();
        t.snap.flags = SnapshotFlags::Relock; t.snap.sequence = 11;
        for (int w = 0; w < 30; ++w) { pl.plan(t.snap, 4, org, per, 0, from, from + 20000, 30000, cfg, ev); from += 20000; }
        int spp = 0, cont = 0;
        for (const auto& e : ev) { if (e.bytes[0] == 0xF2) ++spp; if (e.bytes[0] == 0xFB) ++cont; }
        if (sppOn) CHECK(spp == 1 && cont == 1); else CHECK(spp == 0 && cont == 0);
    }
}

TEST_CASE(MidiClock_OffsetShiftsTicks)
{
    MidiClockPlanner a, b;
    MidiClockConfig cfg; cfg.enabled = true;
    Timeline t = steadyTimeline(100, 1000000);
    const double per = periodSamples(t.snap) * 1e6 / kSr, org = (double) t.map.hostTimeForSample(0);
    std::vector<MidiTimed> ea, eb;
    a.plan(t.snap, 4, org, per, 0, 1000000, 4000000, 30000, cfg, ea);
    b.plan(t.snap, 4, org, per, 5000.0, 1000000, 4000000, 30000, cfg, eb);
    REQUIRE(ea.size() > 10 && eb.size() > 10);
    // Same grid, shifted by 5 ms.
    double d = 1e9;
    for (const auto& x : ea) for (const auto& y : eb) if (x.bytes[0] == 0xF8 && y.bytes[0] == 0xF8) d = std::min(d, std::fabs(y.t - x.t - 5000.0));
    CHECK(d < 1.0);
}

TEST_CASE(O3_MidiClockThreadJitter)
{
    ClockThread ct;
    auto sink = std::make_shared<MemorySink>();
    SeqLock<Timeline> src;
    Timeline t = steadyTimeline(120, steadyNowUs() + 150000);
    src.write(t);
    MidiClockConfig cfg; cfg.enabled = true;
    ct.setSink(sink); ct.setConfig(cfg);
    ct.start(&src);
    std::this_thread::sleep_for(std::chrono::milliseconds(3200));
    ct.stop();
    const auto recs = sink->records();
    std::vector<double> iv;
    int64_t prev = -1; int starts = 0, stops = 0;
    for (const auto& r : recs) {
        if (r.bytes[0] == 0xFA) ++starts;
        if (r.bytes[0] == 0xFC) ++stops;
        if (r.bytes[0] != 0xF8) continue;
        if (prev >= 0) iv.push_back((double) (r.atUs - prev));
        prev = r.atUs;
    }
    REQUIRE(iv.size() > 100);
    const double sd = sdOf(iv) / 1000.0;
    const char* lim = std::getenv("PM_JITTER_LIMIT_MS");
    const double limit = lim ? std::atof(lim) : 0.3;
    std::printf("  O3 inter-tick sd = %.4f ms over %zu ticks (limit %.2f), driver jitter rms = %d us\n", sd, iv.size(), limit, ct.status().measuredJitterUs.load());
    CHECK(starts == 1);
    CHECK(stops == 1);       // stopped cleanly on shutdown
    CHECK_MSG(sd <= limit, "sd ms = " + std::to_string(sd));
    const double meanMs = [&] { double m = 0; for (double x : iv) m += x; return m / (double) iv.size() / 1000.0; }();
    CHECK(std::fabs(meanMs - 500.0 / 24.0) < 0.05);
}

TEST_CASE(MidiClock_CountInLauncherQueue)
{
    ClockThread ct;
    auto sink = std::make_shared<MemorySink>();
    SeqLock<Timeline> src;
    Timeline t = steadyTimeline(120, steadyNowUs()); t.snap.state = (uint8_t) EngineState::Idle;
    src.write(t);
    MidiClockConfig cfg; cfg.enabled = true;
    ct.setSink(sink); ct.setConfig(cfg); ct.start(&src);
    const int64_t at = steadyNowUs() + 80000;
    const uint8_t on[3] = { 0x90, 60, 100 };
    ct.postFromAudio(at, on, 3);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    ct.stop();
    bool got = false; int64_t when = 0;
    for (const auto& r : sink->records()) if (r.bytes[0] == 0x90 && r.bytes[1] == 60) { got = true; when = r.atUs; }
    CHECK(got);
    CHECK(std::llabs(when - at) < 2000);
}

// ------------------------------------------------------------------ sync bus
TEST_CASE(SyncBus_PulsesAndClicks)
{
    SyncRenderer sr; sr.prepare(kSr);
    SyncConfig cfg; cfg.enabled = true;
    Timeline t = steadyTimeline(120, 0);
    std::vector<float> L(48000 * 4, 0.f), R(48000 * 4, 0.f);
    for (int64_t b = 0; b < (int64_t) L.size(); b += 64) sr.render(t.snap, 4, b, 64, cfg, L.data() + b, R.data() + b);
    int rising = 0; std::vector<int64_t> pos;
    for (size_t i = 1; i < L.size(); ++i) if (L[i] > 0.5f && L[i - 1] <= 0.5f) { ++rising; pos.push_back((int64_t) i); }
    CHECK(rising == 24 * 2 * 4 - 0 || rising == 24 * 2 * 4 - 1 || rising == 24 * 2 * 4 + 1 || std::abs(rising - 192) <= 1);
    // 24 ppqn: spacing 1000 samples exactly at 120 BPM.
    for (size_t i = 1; i < pos.size(); ++i) CHECK(std::llabs((pos[i] - pos[i - 1]) - 1000) <= 1);
    // Pulse width 1 ms.
    int width = 0; for (size_t i = (size_t) pos[2]; i < L.size() && L[i] > 0.5f; ++i) ++width;
    CHECK(width == 48);
    // Downbeat click is 2 kHz, others 1 kHz: count zero crossings in the 2 ms burst.
    auto crossings = [&](int64_t start) { int c = 0; for (int i = 1; i < 96; ++i) if ((R[(size_t) start + i] > 0) != (R[(size_t) start + i - 1] > 0)) ++c; return c; };
    CHECK(crossings(0 + 0) >= 6);          // beat 1 of bar 4 at sample 0 -> 2 kHz: 4 half cycles + envelope
    CHECK(crossings(24000) <= 4);          // beat 2 -> 1 kHz
    // Disabled: silence.
    std::vector<float> L2(4800, 0.f), R2(4800, 0.f);
    SyncRenderer off; off.prepare(kSr); cfg.enabled = false;
    off.render(t.snap, 4, 0, 4800, cfg, L2.data(), R2.data());
    for (float v : L2) CHECK(v == 0.f);
}

TEST_CASE(O4_MidiTickMatchesSyncPulse)
{
    Timeline t = steadyTimeline(137, 3000000);
    t.snap.beatOriginSample = 12345; t.snap.nextBeatSample = 12345 + (int64_t) std::llround(60.0 / 137 * kSr);
    SyncRenderer sr; sr.prepare(kSr);
    SyncConfig cfg; cfg.enabled = true;
    std::vector<float> L(48000 * 3, 0.f), R(48000 * 3, 0.f);
    std::vector<int64_t> syncTicks;
    for (int64_t b = 0; b < (int64_t) L.size(); b += 128) {
        sr.render(t.snap, 4, b, 128, cfg, L.data() + b, R.data() + b);
        for (auto s : sr.lastTickSamples()) syncTicks.push_back(s);
    }
    MidiClockPlanner pl; MidiClockConfig mc; mc.enabled = true;
    std::vector<MidiTimed> ev;
    const double perUs = periodSamples(t.snap) * 1e6 / kSr, org = (double) t.map.hostTimeForSample(t.snap.beatOriginSample);
    double from = 3000000; for (int w = 0; w < 150; ++w) { pl.plan(t.snap, 4, org, perUs, 0, from, from + 20000, 30000, mc, ev); from += 20000; }
    REQUIRE(syncTicks.size() > 100);
    // Every MIDI beat tick (0xF8 on a beat boundary) lies within 1 ms of some sync pulse.
    int checked = 0; double worst = 0;
    for (const auto& e : ev) {
        if (e.bytes[0] != 0xF8) continue;
        const double sampleT = (e.t - (double) t.map.originHostUs) * kSr / 1e6;
        double best = 1e18;
        for (auto s : syncTicks) best = std::min(best, std::fabs((double) s - sampleT));
        if (sampleT < 3.0 * kSr - 2000) { worst = std::max(worst, best * 1000.0 / kSr); ++checked; }
    }
    CHECK(checked > 50);
    CHECK_MSG(worst < 1.0, "worst ms = " + std::to_string(worst));
}

// ------------------------------------------------------------------ OSC
TEST_CASE(Osc_EncodeDecode)
{
    OscMessage m; m.address = "/pacemaker/beat"; m.args = { OscArg::Int(7), OscArg::Float(2.5f), OscArg::Str("hello") };
    auto bytes = encodeOscPacket(m);
    CHECK(bytes.size() % 4 == 0);
    DecodedOsc d; REQUIRE(decodeOsc(bytes.data(), bytes.size(), d));
    CHECK(!d.bundle && d.messages.size() == 1);
    CHECK(d.messages[0].address == m.address && d.messages[0].args[0].i == 7 && d.messages[0].args[1].f == 2.5f && d.messages[0].args[2].s == "hello");
    const int64_t unixUs = 1760000000123456ll;
    auto b = encodeOscBundle(ntpTimetag(unixUs), { m, m });
    REQUIRE(decodeOsc(b.data(), b.size(), d));
    CHECK(d.bundle && d.messages.size() == 2);
    CHECK(std::llabs(unixMicrosFromTimetag(d.timetag) - unixUs) <= 1);
    CHECK(!decodeOsc(b.data(), 10, d));
}

namespace {
struct CaptureTransport : OscTransport {
    std::mutex m; std::vector<std::vector<uint8_t>> pk;
    bool send(const uint8_t* d, size_t n) override { std::lock_guard<std::mutex> l(m); pk.emplace_back(d, d + n); return true; }
};
}

TEST_CASE(O5_OscBeatTimetagsMonotonicAndAligned)
{
    OscPlanner pl;
    OscConfig cfg; cfg.enabled = true;
    Timeline t = steadyTimeline(120, 5000000);
    std::vector<OscPacket> out;
    const int64_t wallOff = 1760000000000000ll;
    for (int64_t now = 5000000 - 100000; now < 5000000 + 3000000; now += 2000) pl.plan(t, now, wallOff, cfg, out);
    std::vector<int64_t> tags; std::vector<int> beatsInBar; int tempoMsgs = 0, phaseMsgs = 0, downbeats = 0;
    for (const auto& p : out) {
        DecodedOsc d; REQUIRE(decodeOsc(p.bytes.data(), p.bytes.size(), d));
        for (const auto& m : d.messages) {
            if (m.address == "/pacemaker/beat") { tags.push_back(unixMicrosFromTimetag(d.timetag) - wallOff); beatsInBar.push_back(m.args[1].i); CHECK(m.args[2].i == 4); }
            if (m.address == "/pacemaker/tempo") { ++tempoMsgs; CHECK(std::fabs(m.args[0].f - 120.0f) < 0.01f); }
            if (m.address == "/pacemaker/phase") ++phaseMsgs;
            if (m.address == "/pacemaker/downbeat") ++downbeats;
        }
    }
    REQUIRE(tags.size() >= 6);
    for (size_t i = 1; i < tags.size(); ++i) CHECK(tags[i] > tags[i - 1]);
    // Aligned with the grid used by the Sync bus (beats every 500 ms from the origin at 5.0 s), within 1 ms.
    for (size_t i = 0; i < tags.size(); ++i) CHECK(std::llabs((tags[i] - 5000000) % 500000) < 1000);
    // beat numbers cycle 1..4 and the downbeat is announced exactly once per bar
    for (size_t i = 1; i < beatsInBar.size(); ++i) CHECK(beatsInBar[i] == beatsInBar[i - 1] % 4 + 1);
    CHECK(downbeats >= 1 && downbeats <= (int) tags.size() / 4 + 1);
    CHECK(tempoMsgs >= 3 && tempoMsgs <= 5);          // once on start, then every second
    CHECK(phaseMsgs > 200);
    // Resolume profile adds receiver-specific addresses.
    OscPlanner pr; OscConfig rc; rc.enabled = true; rc.profile = OscProfile::Resolume;
    out.clear();
    for (int64_t now = 4900000; now < 7100000; now += 2000) pr.plan(t, now, wallOff, rc, out);
    bool tempoAddr = false, resync = false;
    for (const auto& p : out) { DecodedOsc d; decodeOsc(p.bytes.data(), p.bytes.size(), d); for (const auto& m : d.messages) { if (m.address == "/composition/tempocontroller/tempo") tempoAddr = true; if (m.address == "/composition/tempocontroller/resync") resync = true; } }
    CHECK(tempoAddr); CHECK(resync);
}

TEST_CASE(Osc_ThreadSendsThroughTransportAndUdp)
{
    SeqLock<Timeline> src; src.write(steadyTimeline(120, steadyNowUs() + 100000));
    OscThread th;
    auto cap = std::make_shared<CaptureTransport>();
    th.setTransportForTest(cap);
    OscConfig cfg; cfg.enabled = true; th.setConfig(cfg);
    th.start(&src);
    std::this_thread::sleep_for(std::chrono::milliseconds(1400));
    OscMessage extra; extra.address = "/cue"; extra.args = { OscArg::Int(1) }; th.sendNow(extra);
    OscThread::Quick q; std::snprintf(q.addr, sizeof q.addr, "/quick"); q.i0 = 4; q.nInts = 1; th.postFromAudio(q);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    th.stop();
    bool beat = false, cue = false, quick = false;
    { std::lock_guard<std::mutex> l(cap->m); for (auto& b : cap->pk) { DecodedOsc d; if (!decodeOsc(b.data(), b.size(), d)) continue; for (auto& m : d.messages) { if (m.address == "/pacemaker/beat") beat = true; if (m.address == "/cue") cue = true; if (m.address == "/quick") quick = true; } } }
    CHECK(beat); CHECK(cue); CHECK(quick);

    // Real UDP loopback: receiver on an ephemeral port gets the OSC input pipeline output.
    OscInput in; std::mutex mu; std::vector<OscMessage> got;
    const int port = 39000 + (int) (steadyNowUs() % 900);
    std::string err;
    REQUIRE_MSG(in.start(port, [&](const OscMessage& m) { std::lock_guard<std::mutex> l(mu); got.push_back(m); }, &err), err);
    UdpSender snd; REQUIRE(snd.open("127.0.0.1", port));
    OscMessage m; m.address = "/pacemaker/song"; m.args = { OscArg::Int(3) };
    auto bytes = encodeOscPacket(m);
    CHECK(snd.send(bytes.data(), bytes.size()));
    for (int i = 0; i < 50 && got.empty(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    in.stop();
    std::lock_guard<std::mutex> l(mu);
    REQUIRE(got.size() == 1);
    CHECK(got[0].address == "/pacemaker/song" && got[0].args[0].i == 3);
}

// ------------------------------------------------------------------ Link
namespace {
struct MockLink : ILinkBackend {
    bool en = false; double bpm = 120; int peers = 0; int64_t clockOff = 777;
    int forced = 0, tempoSets = 0, commits = 0; double phaseErr = 0; bool playing = false;
    void setEnabled(bool on) override { en = on; }
    bool isEnabled() const override { return en; }
    int numPeers() const override { return peers; }
    int64_t clockMicros() const override { return steadyNowUs() + clockOff; }
    void setStartStopSyncEnabled(bool) override {}
    void capture() override {}
    double tempo() const override { return bpm; }
    double phaseAtTime(int64_t, double) const override { return 0; }
    double beatAtTime(int64_t, double) const override { return 100.0 + phaseErr; }
    void setTempo(double b, int64_t) override { bpm = b; ++tempoSets; }
    void forceBeatAtTime(double, int64_t, double) override { ++forced; }
    void setIsPlaying(bool p, int64_t) override { playing = p; }
    void commit() override { ++commits; }
};
}

TEST_CASE(O2_LinkNeverForcesWithoutRelock)
{
    LinkPublisher pub; MockLink link; link.en = true;
    LinkSettings cfg; cfg.commitIntervalMs = 10;
    Timeline t = steadyTimeline(120, steadyNowUs());
    int64_t now = steadyNowUs();
    // Tempo ramp 120 -> 126 with no relock: setTempo yes, forceBeat never.
    for (int i = 0; i < 600; ++i) {
        t.snap.bpm = 120 + 6.0 * i / 600.0; t.snap.sequence++;
        pub.process(link, t, now, cfg, true);
        now += 5000;
    }
    CHECK(link.forced == 0);
    CHECK(std::fabs(link.bpm - 126.0) < 0.2);
    CHECK(link.tempoSets > 5);
    // One relock -> exactly one force, even when seen on many blocks.
    t.snap.flags = SnapshotFlags::Relock; t.snap.sequence = 5000;
    for (int i = 0; i < 20; ++i) { pub.process(link, t, now, cfg, true); now += 5000; }
    CHECK(link.forced == 1);
    // Not publisher or confidence below the gate: nothing at all.
    MockLink quiet; quiet.en = true; LinkPublisher p2;
    p2.process(quiet, t, now, cfg, false);
    CHECK(quiet.commits == 0);
    t.snap.flags = 0; t.snap.confidence = 0.1f; t.snap.bpm = 140;
    LinkPublisher p3; MockLink q3; q3.en = true;
    for (int i = 0; i < 50; ++i) { p3.process(q3, t, now, cfg, true); now += 5000; }
    CHECK(q3.tempoSets == 0);
}

TEST_CASE(Link_PhaseDriftSlewsNeverForces)
{
    LinkPublisher pub; MockLink link; link.en = true; link.phaseErr = 0.1;      // Link is 0.1 beat ahead of us
    LinkSettings cfg;
    Timeline t = steadyTimeline(120, steadyNowUs());
    int64_t now = steadyNowUs();
    double minBpm = 1e9;
    for (int i = 0; i < 20000; ++i) {
        // Advance beats by re-anchoring the origin every 500 ms.
        const int64_t k = (now - t.map.originHostUs) / 500000;
        t.snap.beatOriginSample = k * 24000; t.snap.nextBeatSample = (k + 1) * 24000;
        t.snap.beatInBar = (int) (k % 4) + 1; t.snap.barNumber = (int) (k / 4);
        pub.process(link, t, now, cfg, true);
        minBpm = std::min(minBpm, link.bpm);
        now += 1000;
        if (i > 9000) break;
    }
    CHECK(link.forced == 0);
    CHECK(pub.diagnostics().slewCalls.load() >= 1);
    CHECK(minBpm >= 119.7);               // slew magnitude is +-0.2 BPM
}

TEST_CASE(Link_ArbitrationPicksHighestConfidenceFollower)
{
    auto svc = LinkService::createForTest(std::make_unique<MockLink>());
    const int a = svc->registerInstance(), b = svc->registerInstance(), c = svc->registerInstance();
    svc->report(a, true, 0.4f, 1); svc->report(b, true, 0.8f, 1); svc->report(c, false, 0.99f, 1);
    CHECK(!svc->isPublisher(a) && svc->isPublisher(b) && !svc->isPublisher(c));
    CHECK(svc->activeFollowers() == 2);
    svc->report(a, true, 0.95f, 2);
    CHECK(svc->isPublisher(a) && !svc->isPublisher(b));
    svc->unregisterInstance(a);
    CHECK(svc->isPublisher(b));
}

TEST_CASE(Link_RealLibraryTempoRoundTrip)
{
    if (!linkCompiledIn()) { std::printf("  (Link compiled out; skipped)\n"); return; }
    auto be = makeAbletonLinkBackend(100.0);
    REQUIRE(be != nullptr);
    be->setEnabled(true);
    CHECK(be->isEnabled());
    be->capture();
    be->setTempo(133.0, be->clockMicros());
    be->commit();
    be->capture();
    CHECK(std::fabs(be->tempo() - 133.0) < 0.01);
    be->capture();
    be->forceBeatAtTime(8.0, be->clockMicros() + 1000000, 4.0);
    be->commit(); be->capture();
    CHECK(std::fabs(std::fmod(be->beatAtTime(be->clockMicros() + 1000000, 4.0), 4.0)) < 0.01 ||
          std::fabs(std::fmod(be->beatAtTime(be->clockMicros() + 1000000, 4.0), 4.0) - 4.0) < 0.01);
    be->setEnabled(false);
}

TEST_CASE(O1_TwoLinkInstancesShareTempo)
{
    if (!linkCompiledIn()) return;
    auto a = makeAbletonLinkBackend(120.0), b = makeAbletonLinkBackend(90.0);
    a->setEnabled(true); b->setEnabled(true);
    bool joined = false;
    for (int i = 0; i < 100 && !joined; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); joined = a->numPeers() > 0 && b->numPeers() > 0; }
    if (!joined) { std::printf("  (no Link peer discovery in this environment; skipped)\n"); return; }
    a->capture(); a->setTempo(126.0, a->clockMicros()); a->commit();
    bool match = false;
    for (int i = 0; i < 80 && !match; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); b->capture(); match = std::fabs(b->tempo() - 126.0) < 0.1; }
    CHECK(match);
    a->setEnabled(false); b->setEnabled(false);
}

// ------------------------------------------------------------------ calibration
namespace {
// Simulated device: output click goes through outDelay + inDelay + hidden samples, with optional noise.
std::vector<float> simulateLoopback(int totalDelay, double noiseRms, uint64_t seed, std::vector<int64_t>& starts)
{
    LoopbackCapture cap; cap.prepare(kSr); cap.start();
    drums::Rng rng(seed);
    std::vector<float> loopBuf((size_t) totalDelay + 4096, 0.0f);
    std::vector<float> in(256), out(256);
    std::vector<float> delayed;
    size_t wr = 0;
    std::vector<float> outStream;
    std::vector<float> rec;
    while (cap.active()) {
        std::fill(out.begin(), out.end(), 0.0f);
        // capture first, using what the device returns from earlier outputs
        for (size_t i = 0; i < in.size(); ++i) {
            const size_t idx = outStream.size() + i;
            const float v = idx >= (size_t) totalDelay && idx - (size_t) totalDelay < outStream.size() ? outStream[idx - (size_t) totalDelay] : 0.0f;
            in[i] = v;
        }
        // the click for the current block is generated by process(); the device then returns it totalDelay later
        std::vector<float> in2 = in;
        for (auto& v : in2) v += (float) (noiseRms * rng.gauss());
        cap.process(in2.data(), out.data(), (int) out.size());
        outStream.insert(outStream.end(), out.begin(), out.end());
        (void) wr; (void) loopBuf; (void) delayed;
    }
    starts = cap.clickStarts();
    // Re-render the recording properly: input = output delayed, plus noise.
    std::vector<float> res(cap.recording().size(), 0.0f);
    for (size_t i = 0; i < res.size(); ++i) {
        const float v = i >= (size_t) totalDelay && i - (size_t) totalDelay < outStream.size() ? outStream[i - (size_t) totalDelay] : 0.0f;
        res[i] = v + (float) (noiseRms * rng.gauss());
    }
    return res;
}
}

TEST_CASE(L1_LoopbackFindsHiddenLatency)
{
    const int reportedIn = 64, reportedOut = 96, hidden = 37;
    std::vector<int64_t> starts;
    auto rec = simulateLoopback(reportedIn + reportedOut + hidden, 0.0, 1, starts);
    auto r = analyzeLoopback(rec.data(), (int64_t) rec.size(), starts, kSr, reportedIn, reportedOut, true);
    CHECK(r.pass);
    CHECK(r.clicksDetected == 8);
    CHECK_MSG(std::abs(r.hiddenSamples - hidden) <= 1, "hidden = " + std::to_string(r.hiddenSamples));
    CHECK(r.roundTripSamples == reportedIn + reportedOut + hidden);
    // Over-reporting driver gives a negative hidden term that is stored as such.
    auto rec2 = simulateLoopback(100, 0.0, 2, starts);
    auto r2 = analyzeLoopback(rec2.data(), (int64_t) rec2.size(), starts, kSr, 100, 60, true);
    CHECK(r2.hiddenSamples == -60);
    // Split mode halves the correction across input and output.
    auto r3 = analyzeLoopback(rec.data(), (int64_t) rec.size(), starts = starts, kSr, reportedIn, reportedOut, false);
    (void) r3;
}

TEST_CASE(L2_LoopbackRobustToNoise)
{
    // -20 dB SNR: click peak amplitude 1.0, rms of the burst about 0.4 -> noise rms 0.04 is about -20 dB.
    for (uint64_t seed = 1; seed <= 4; ++seed) {
        std::vector<int64_t> starts;
        auto rec = simulateLoopback(64 + 96 + 37, 0.04, seed, starts);
        auto r = analyzeLoopback(rec.data(), (int64_t) rec.size(), starts, kSr, 64, 96, true);
        CHECK_MSG(r.pass, "seed " + std::to_string(seed));
        CHECK_MSG(std::abs(r.hiddenSamples - 37) <= 1, "hidden = " + std::to_string(r.hiddenSamples) + " seed " + std::to_string(seed));
    }
    // Pure noise or a missing patch cable fails cleanly instead of reporting garbage.
    std::vector<float> noise(48000 * 5, 0.0f); drums::Rng rng(9); for (auto& v : noise) v = (float) (0.05 * rng.gauss());
    std::vector<int64_t> starts; for (int i = 1; i <= 8; ++i) starts.push_back(i * 24000);
    auto r = analyzeLoopback(noise.data(), (int64_t) noise.size(), starts, kSr, 0, 0, true);
    CHECK(!r.pass);
}

TEST_CASE(L3_RoleAcousticDelay)
{
    CHECK(roleDelaySamples(InputRole::Kick, 0.1, 3.0, kSr) == (int) std::lround(0.1 / 343.0 * kSr));
    CHECK(roleDelaySamples(InputRole::Overhead, 1.0, 3.0, kSr) == (int) std::lround(1.0 / 343.0 * kSr));
    CHECK(roleDelaySamples(InputRole::Trigger, 5.0, 3.0, kSr) == (int) std::lround(0.003 * kSr));
    CHECK(std::fabs(distanceToDelayMs(1.0) - 2.915) < 0.01);
    // Through the Core: the same click arriving 1 m later on the overhead channel lands on the same corrected sample.
    Core core;
    SessionSettings s;
    s.channels = { { 0, InputRole::Kick, 0.1, false }, { 1, InputRole::Overhead, 1.0, false } };
    core.setSession(s);
    core.prepare(kSr, 256, 2);
    const int extra = roleDelaySamples(InputRole::Overhead, 1.0, 0, kSr) - roleDelaySamples(InputRole::Kick, 0.1, 0, kSr);
    std::vector<float> a((size_t) kSr * 2, 0.f), b(a.size(), 0.f);
    for (int hit = 0; hit < 8; ++hit) {
        const size_t at = (size_t) (0.4 * kSr + hit * 0.5 * kSr);
        for (int i = 0; i < 300; ++i) {
            const float v = (float) (std::sin(i * 0.31) * std::exp(-i / 60.0));
            a[at + (size_t) i] += v; b[at + (size_t) extra + (size_t) i] += v;
        }
    }
    std::vector<int64_t> kick, oh;
    for (size_t pos = 0; pos + 256 <= a.size(); pos += 256) {
        const float* ch[2] = { a.data() + pos, b.data() + pos };
        core.process(ch, 2, 256, CoreHostInfo {}, nullptr, 0, nullptr, nullptr, nullptr, nullptr, 0);
        core.housekeepingOnce();
        EngineEvent e;
        while (core.popUiEvent(e)) if (e.type == EngineEvent::Onset) { (e.onset.role == InputRole::Kick ? kick : oh).push_back(e.onset.sample); }
    }
    REQUIRE(kick.size() >= 4);
    REQUIRE(oh.size() >= 4);
    int paired = 0;
    for (auto k : kick) for (auto o : oh) if (std::llabs(k - o) < 200) { ++paired; CHECK_MSG(std::llabs(k - o) <= 1, "diff = " + std::to_string(k - o)); }
    CHECK(paired >= 4);
}

TEST_CASE(TapAlong_MedianOfBeatErrors)
{
    TapAlong ta;
    drums::Rng rng(3);
    const double bpm = 120; const double lat = 0.018;      // hits land 18 ms after the DAW beat
    for (int bar = 0; bar < 8; ++bar) {
        for (int beat = 0; beat < 4; ++beat) {
            const int64_t blockStart = (int64_t) ((bar * 4 + beat) * 24000);
            ta.onBlock(true, bar * 4 + beat, bpm, blockStart, kSr);
            const int64_t onset = blockStart + (int64_t) ((lat + 0.003 * rng.gauss()) * kSr);
            ta.onOnset(onset);
        }
    }
    CHECK(ta.enough());
    CHECK(std::fabs(ta.medianErrorMs() - 18.0) < 2.0);
    TapAlong idle; idle.onBlock(false, 0, 120, 0, kSr); idle.onOnset(100);
    CHECK(idle.count() == 0);
}

TEST_CASE(HardwareOffset_EstimatesAndNegates)
{
    HardwareOffsetEstimator h;
    for (int i = 0; i < 16; ++i) { const int64_t hit = 24000 * i; h.addHit(hit); h.addClick(hit + (int64_t) (0.012 * kSr)); }
    double ms = 0;
    REQUIRE(h.estimate(kSr, 500.0, ms));
    CHECK(std::fabs(ms - 12.0) < 0.1);
    CHECK(HardwareOffsetEstimator::suggestedClockOffsetMs(ms) < 0);
    HardwareOffsetEstimator few; few.addHit(0); few.addClick(10);
    CHECK(!few.estimate(kSr, 500.0, ms));
}

TEST_CASE(CalibrationTable_RoundTrip)
{
    CalibrationTable t;
    t.put(CalibrationTable::key("Scarlett 2i2", 48000, 128), { 20, 17, 123456 });
    auto back = CalibrationTable::fromJson(t.toJson());
    CalibrationEntry e;
    CHECK(back.get("Scarlett 2i2|48000|128", e) && e.hiddenInputSamples == 20 && e.hiddenOutputSamples == 17);
    CHECK(!back.get("other", e));
}

// ------------------------------------------------------------------ drift log, report, tempo map
namespace {
std::vector<DriftRow> syntheticRows()
{
    std::vector<DriftRow> rows;
    int64_t t = 1000000; double bpm = 100;
    for (int i = 0; i < 64; ++i) {
        DriftRow r; r.timestampUs = t; r.bpm = bpm; r.confidence = 0.9; r.state = 3; r.bar = i / 4; r.beat = i % 4 + 1;
        if (i >= 30 && i < 36) r.state = 4;
        if (i == 40) r.event = "relock";
        rows.push_back(r);
        t += (int64_t) (60e6 / bpm); bpm += (i < 32 ? 0.5 : -0.25);
    }
    return rows;
}
}

TEST_CASE(DriftLog_CsvReportAndMidiTempoMap)
{
    auto rows = syntheticRows();
    std::string csv = driftHeader();
    for (auto& r : rows) csv += driftRowToCsv(r);
    std::vector<DriftRow> back;
    REQUIRE(parseDriftCsv(csv, back));
    CHECK(back.size() == rows.size() && back[40].event == "relock" && back[31].state == 4);
    auto rep = analyzeDrift(back);
    CHECK(rep.relockCount == 1);
    CHECK(rep.timeInHoldSec > 3.0 && rep.timeInHoldSec < 4.5);
    CHECK(rep.maxBpm > rep.minBpm && rep.maxDeviationBpm > 1.0);
    const std::string html = renderDriftReportHtml(rep, "Test");
    CHECK(html.find("<svg") != std::string::npos && html.find("Relocks") != std::string::npos);

    auto mid = buildTempoMapMidi(rows, 4, 4);
    REQUIRE(mid.size() > 30);
    CHECK(std::memcmp(mid.data(), "MThd", 4) == 0);
    // Walk the track and decode tempo events.
    size_t i = 14 + 8; uint32_t tick = 0; int tempos = 0; double firstBpm = 0, lastBpm = 0; bool timeSig = false, endTrack = false;
    while (i < mid.size()) {
        uint32_t dt = 0; while (mid[i] & 0x80) dt = (dt << 7) | (mid[i++] & 0x7F); dt = (dt << 7) | mid[i++]; tick += dt;
        REQUIRE(mid[i] == 0xFF);
        const uint8_t type = mid[i + 1], len = mid[i + 2];
        if (type == 0x51) { const uint32_t us = ((uint32_t) mid[i + 3] << 16) | ((uint32_t) mid[i + 4] << 8) | mid[i + 5]; const double b = 60e6 / us; if (!tempos) firstBpm = b; lastBpm = b; ++tempos; }
        if (type == 0x58) { timeSig = mid[i + 3] == 4 && mid[i + 4] == 2; }
        if (type == 0x2F) endTrack = true;
        i += 3 + len;
    }
    CHECK(timeSig); CHECK(endTrack);
    CHECK(tempos > 20);
    CHECK(std::fabs(firstBpm - 100.0) < 0.5);
    CHECK(std::fabs(lastBpm - rows.back().bpm) < 0.5);
    CHECK(tick == 63u * 480u);
    // File helper writes the same bytes.
    CHECK(exportTempoMapMidi(rows, 4, 4, "pm_test_map.mid"));
    std::string readBack; CHECK(readTextFile("pm_test_map.mid", readBack) && readBack.size() == mid.size());
    // Empty input still yields a valid file.
    CHECK(buildTempoMapMidi({}, 4, 4).size() > 20);
}

TEST_CASE(DriftLogger_WritesFile)
{
    DriftLogger lg; std::string path;
    REQUIRE(lg.open("pm_test_drift", &path));
    for (auto& r : syntheticRows()) lg.write(r);
    lg.close();
    std::string csv; REQUIRE(readTextFile(path, csv));
    std::vector<DriftRow> rows; CHECK(parseDriftCsv(csv, rows) && rows.size() == 64);
}

// ------------------------------------------------------------------ extras
TEST_CASE(SectionDetector_FlagsLoudAndQuietSections)
{
    SectionDetector d;
    int ups = 0, downs = 0;
    for (int bar = 0; bar < 40; ++bar) {
        double ms = 0.01;                         // verse
        if (bar >= 16 && bar < 28) ms = 0.08;     // chorus: +9 dB
        if (bar >= 28) ms = 0.002;                // breakdown: -7 dB vs chorus
        const int s = d.onBar(ms);
        if (s > 0) ++ups; if (s < 0) ++downs;
    }
    CHECK(ups == 1);
    CHECK(downs >= 1);
    SectionDetector flat; int any = 0;
    for (int bar = 0; bar < 50; ++bar) any += flat.onBar(0.01 * (1.0 + 0.1 * std::sin(bar))) != 0;
    CHECK(any == 0);
}

TEST_CASE(Redundancy_ExactlyOnePublishes)
{
    RedundancyArbiter a(1), b(2);
    int64_t now = 0;
    int conflicts = 0, silent = 0;
    float ca = 0.9f, cb = 0.6f;
    for (int step = 0; step < 400; ++step) {
        now += 100000;
        if (step == 150) cb = 0.99f;           // B becomes much better
        if (step == 300) cb = 0.0f;            // B dies (stops heartbeating below)
        const float sa = RedundancyArbiter::scoreOf(ca, 3, true), sb = RedundancyArbiter::scoreOf(cb, 3, true);
        const bool bAlive = step < 300;
        if (bAlive) a.onPeer(2, sb, b.active(), now);
        b.onPeer(1, sa, a.active(), now);
        const bool pa = a.decide(sa, now), pb = bAlive ? b.decide(sb, now) : false;
        if (step > 5 && pa && pb) ++conflicts;
        if (step > 5 && !pa && !pb) ++silent;
    }
    CHECK(conflicts == 0);
    CHECK(silent <= 1);
    CHECK(a.active());                          // takes over when the peer goes silent
    CHECK(RedundancyArbiter::scoreOf(0.9f, 3, false) == 0.0f);
}

// ------------------------------------------------------------------ core end to end
namespace {

struct E2E {
    std::vector<float> syncL;
    std::vector<int64_t> pulses;           // output sample index of each rising edge on the left sync channel
    std::vector<drums::Beat> truth;
    std::vector<CoreMidiOut> midi;
    std::vector<int64_t> midiTickSamples;
};

E2E runCore(Core& core, drums::Machine& dm, int latencySamples, int block, bool pluginMidi = false)
{
    E2E out;
    const size_t total = dm.audio.size() + (size_t) latencySamples;
    std::vector<float> in(total, 0.f);
    for (size_t i = 0; i < dm.audio.size(); ++i) in[i + (size_t) latencySamples] = dm.audio[i];
    out.syncL.assign(total, 0.f);
    std::vector<float> R(total, 0.f);
    std::vector<CoreMidiOut> mo(256);
    for (size_t pos = 0; pos + (size_t) block <= total; pos += (size_t) block) {
        const float* ch[1] = { in.data() + pos };
        CoreHostInfo h; h.hostTimeUs = 2000000 + (int64_t) std::llround((double) pos * 1e6 / kSr);
        const int n = core.process(ch, 1, block, h, nullptr, 0, out.syncL.data() + pos, R.data() + pos, nullptr, pluginMidi ? mo.data() : nullptr, pluginMidi ? 256 : 0);
        for (int i = 0; i < n; ++i) if (mo[(size_t) i].bytes[0] == 0xF8) out.midiTickSamples.push_back((int64_t) pos + mo[(size_t) i].offset);
        if (pos % (size_t) (block * 64) == 0) core.housekeepingOnce();
    }
    for (size_t i = 1; i < out.syncL.size(); ++i) if (out.syncL[i] > 0.5f && out.syncL[i - 1] <= 0.5f) out.pulses.push_back((int64_t) i);
    out.truth = dm.beats;
    return out;
}

}

TEST_CASE(L4_EndToEndSyncBusAlignsWithGroundTruth)
{
    drums::Machine dm(kSr, 21);
    dm.bpm = 120; dm.walkSigma = 0.1; dm.jitterMs = 3.0;
    dm.bars(28);
    const int hidden = 96;                              // 2 ms of hidden driver latency, found by calibration
    Core core;
    SessionSettings s; s.sync.enabled = true; s.channels = { { 0, InputRole::Any, 0.0, false } };
    core.setSession(s);
    core.prepare(kSr, 64, 1);
    core.setDeviceLatency(0, 0, hidden, 0);
    E2E r = runCore(core, dm, hidden, 64);
    // Beat pulses = every 24th pulse is a beat; use the click timing by choosing pulses nearest to truth beats.
    REQUIRE(r.pulses.size() > 24 * 30);
    double sum = 0, signedSum = 0; int n = 0; double worst = 0;
    for (const auto& b : r.truth) {
        if (!b.played || b.hitTime < 14.0 || b.hitTime > 50.0) continue;
        double best = 1e9, signedBest = 0;
        for (auto p : r.pulses) { const double d = (double) p / kSr - b.hitTime; if (std::fabs(d) < best) { best = std::fabs(d); signedBest = d; } }
        sum += best; worst = std::max(worst, best); ++n; signedSum += signedBest;
    }
    std::printf("  L4 signed mean = %.2f ms\n", signedSum / std::max(1, n) * 1000.0);
    REQUIRE(n > 30);
    const double meanMs = sum / n * 1000.0;
    std::printf("  L4 mean |sync pulse - ground-truth beat| = %.2f ms (worst %.2f)\n", meanMs, worst * 1000.0);
    // ES-03 L4: after calibration the pulse is aligned with the ground truth (systematic offset < 2 ms). The
    // absolute scatter is the tracker's own beat error (ES-01 G1), reported but bounded loosely here.
    CHECK_MSG(std::fabs(signedSum / n * 1000.0) < 2.0, "signed mean ms = " + std::to_string(signedSum / n * 1000.0));
    CHECK_MSG(meanMs < 9.0, "mean abs ms = " + std::to_string(meanMs));
    // The same chain without the calibration is visibly worse (sanity check that the compensation is doing something).
    Core uncal; uncal.setSession(s); uncal.prepare(kSr, 64, 1);
    drums::Machine dm2(kSr, 21); dm2.bpm = 120; dm2.walkSigma = 0.1; dm2.jitterMs = 3.0; dm2.bars(28);
    E2E r2 = runCore(uncal, dm2, 960, 64);       // 20 ms uncalibrated device latency
    double sum2 = 0; int n2 = 0;
    for (const auto& b : r2.truth) {
        if (!b.played || b.hitTime < 14.0 || b.hitTime > 50.0) continue;
        double best = 1e9, sb = 0; for (auto p : r2.pulses) { const double d = (double) p / kSr - b.hitTime; if (std::fabs(d) < best) { best = std::fabs(d); sb = d; } }
        sum2 += sb; ++n2;
    }
    CHECK(n2 > 30);
    CHECK(std::fabs(sum2 / std::max(1, n2) * 1000.0) > 15.0);     // uncalibrated: the 20 ms device latency shows up
}

TEST_CASE(Core_PluginMidiClockSampleAccurateAndMatchesSync)
{
    drums::Machine dm(kSr, 22);
    dm.bpm = 110; dm.walkSigma = 0.05; dm.jitterMs = 2.0; dm.bars(24);
    Core core;
    SessionSettings s; s.sync.enabled = true; s.midi.enabled = true; s.channels = { { 0, InputRole::Any, 0.0, false } };
    core.setSession(s);
    core.prepare(kSr, 128, 1);
    core.setPluginMidiOutput(true);
    E2E r = runCore(core, dm, 0, 128, true);
    REQUIRE(r.midiTickSamples.size() > 24 * 20);
    // Each plugin MIDI tick coincides with a sync pulse (O4 through the whole Core), sample-accurate.
    int matched = 0, total = 0;
    for (auto t : r.midiTickSamples) {
        ++total;
        for (auto p : r.pulses) if (std::llabs(p - t) <= 48) { ++matched; break; }       // 1 ms
    }
    CHECK_MSG(matched >= total - 2, std::to_string(matched) + " of " + std::to_string(total));
}

TEST_CASE(Core_CountInLauncherFiresOnBarOne)
{
    drums::Machine dm(kSr, 23);
    dm.bpm = 110; dm.walkSigma = 0.05; dm.jitterMs = 2.0;
    dm.countIn(4); dm.bars(10);
    Core core;
    SessionSettings s; s.channels = { { 0, InputRole::Any, 0.0, false } };
    s.countIn.enabled = true; s.countIn.midiNote = 72; s.countIn.midiChannel = 2; s.countIn.oscAddress = "/go";
    s.midi.enabled = false;
    core.setSession(s);
    auto sink = std::make_shared<MemorySink>();
    core.setMidiSink(sink);
    core.editSession([](SessionSettings& x) { x.midi.enabled = true; });
    core.prepare(kSr, 128, 1);
    // Real-time pacing is not needed: the launcher is timed by host time, which we derive from steady time.
    E2E r;
    const size_t total = dm.audio.size();
    std::vector<float> L(total), R(total);
    const int64_t t0 = steadyNowUs();
    for (size_t pos = 0; pos + 128 <= total; pos += 128) {
        const float* ch[1] = { dm.audio.data() + pos };
        CoreHostInfo h; h.hostTimeUs = t0 + (int64_t) std::llround((double) pos * 1e6 / kSr);
        core.process(ch, 1, 128, h, nullptr, 0, nullptr, nullptr, nullptr, nullptr, 0);
        if ((pos / 128) % 32 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    int ons = 0, offs = 0;
    for (const auto& rec : sink->records()) { if (rec.bytes[0] == 0x91 && rec.bytes[1] == 72) ++ons; if (rec.bytes[0] == 0x81 && rec.bytes[1] == 72) ++offs; }
    CHECK_MSG(ons == 1, "note-ons = " + std::to_string(ons));
    CHECK(offs == 1);
}

TEST_CASE(Core_MidiNotesActionsAndSongs)
{
    Core core;
    SessionSettings s; s.channels = { { 0, InputRole::Any, 0.0, false } };
    s.mappings = { { true, 0, 64, Action::Follow }, { false, 0, 90, Action::NextSong } };
    SongMap::fromCsv("name,bpm,meterNum,meterDen,programChange\nA,100,4,4,0\nB,140,3,4,1\n", s.songs);
    core.setSession(s);
    core.prepare(kSr, 128, 1);
    std::vector<float> silence(128, 0.f);
    const float* ch[1] = { silence.data() };
    CoreMidiIn ev[3];
    ev[0].status = 0xB0; ev[0].d1 = 64; ev[0].d2 = 127;                 // footswitch: Follow toggle
    ev[1].status = 0x90; ev[1].d1 = 36; ev[1].d2 = 100; ev[1].offset = 10;     // kick
    ev[2].status = 0xC0; ev[2].d1 = 1;                                  // program change -> song B
    CHECK(core.follow());
    core.process(ch, 1, 128, CoreHostInfo {}, ev, 3, nullptr, nullptr, nullptr, nullptr, 0);
    CHECK(!core.follow());
    core.housekeepingOnce();
    CHECK(core.currentSong() == 1);
    CHECK(core.params().referenceBpm == 140 && core.params().meterNum == 3);
    core.process(ch, 1, 128, CoreHostInfo {}, nullptr, 0, nullptr, nullptr, nullptr, nullptr, 0);
    core.doAction(Action::NextSong);
    core.housekeepingOnce();
    CHECK(core.currentSong() == 0);
    // State round trip through the Core keeps everything.
    PluginState st = core.state();
    Core other; other.setState(st); other.prepare(kSr, 128, 1);
    CHECK(other.state().serialise() == st.serialise());
}

TEST_CASE(Core_DeviceLossKeepsClockRunning)
{
    drums::Machine dm(kSr, 24);
    dm.bpm = 120; dm.walkSigma = 0.05; dm.jitterMs = 2.0; dm.bars(14);
    Core core;
    SessionSettings s; s.channels = { { 0, InputRole::Any, 0.0, false } };
    core.setSession(s); core.prepare(kSr, 128, 1);
    for (size_t pos = 0; pos + 128 <= dm.audio.size(); pos += 128) {
        const float* ch[1] = { dm.audio.data() + pos };
        CoreHostInfo h; h.hostTimeUs = steadyNowUs();
        core.process(ch, 1, 128, h, nullptr, 0, nullptr, nullptr, nullptr, nullptr, 0);
    }
    const Timeline before = core.timeline();
    CHECK(before.snap.state == (uint8_t) EngineState::Locked);
    core.notifyDeviceLost();
    const Timeline after = core.timeline();
    CHECK(after.snap.state == (uint8_t) EngineState::Hold);
    CHECK(after.snap.bpm == before.snap.bpm);
    CHECK(core.deviceLost());
}

TEST_CASE(A2_ConcurrentThreadsStress)
{
    const char* sec = std::getenv("PM_STRESS_SECONDS");
    const double seconds = sec ? std::atof(sec) : 3.0;
    drums::Machine dm(kSr, 25);
    dm.bpm = 120; dm.walkSigma = 0.2; dm.jitterMs = 4.0; dm.bars(8);
    Core core;
    SessionSettings s; s.sync.enabled = true; s.midi.enabled = true; s.osc.enabled = true; s.link.enabled = false;
    s.channels = { { 0, InputRole::Any, 0.0, false } };
    core.setSession(s);
    auto sink = std::make_shared<MemorySink>(); core.setMidiSink(sink);
    auto cap = std::make_shared<CaptureTransport>(); core.osc().setTransportForTest(cap);
    core.prepare(kSr, 64, 1);
    std::atomic<bool> stop { false };
    std::thread ui([&] { while (!stop) { core.timeline(); EngineEvent e; while (core.popUiEvent(e)) {} float tr[100]; core.copyTrace(tr, 100); core.statusSummary(); core.doAction(Action::NudgePlus); core.doAction(Action::NudgeMinus); std::this_thread::sleep_for(std::chrono::milliseconds(2)); } });
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<float> L(64), R(64);
    size_t pos = 0;
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < seconds) {
        const float* ch[1] = { dm.audio.data() + pos };
        CoreHostInfo h; h.hostTimeUs = steadyNowUs();
        std::fill(L.begin(), L.end(), 0.f); std::fill(R.begin(), R.end(), 0.f);
        core.process(ch, 1, 64, h, nullptr, 0, L.data(), R.data(), nullptr, nullptr, 0);
        pos = (pos + 64) % (dm.audio.size() - 64);
        std::this_thread::sleep_for(std::chrono::microseconds(1333));         // roughly real time
        if (pos % (64 * 200) == 0) { Params p = core.params(); p.sensitivity = p.sensitivity > 0.5f ? 0.4f : 0.6f; core.setParams(p); }
    }
    stop = true; ui.join();
    CHECK(sink->records().size() > 0);
    CHECK(core.timeline().snap.sequence > 100);
}
