// Tests for the outputs library: W1 to W4, ClockMap, MIDI clock, OSC, Sync, Link policy (ES-02 section 9, RS-07 section 7).
#include "TestFramework.h"
#include "pacemaker/ClockMap.h"
#include "pacemaker/DriftLog.h"
#include "pacemaker/Json.h"
#include "pacemaker/LinkPolicy.h"
#include "pacemaker/MidiClock.h"
#include "pacemaker/Osc.h"
#include "pacemaker/Settings.h"
#include "pacemaker/SyncRenderer.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>

using namespace pacemaker;

namespace {
constexpr double kSr = 48000.0;

BeatMapSnapshot snap(double bpm, int64_t origin, int beatInBar = 1, int bar = 1, EngineState st = EngineState::Locked, uint32_t seq = 1)
{
    BeatMapSnapshot s;
    s.bpm = bpm; s.beatOriginSample = origin; s.nextBeatSample = origin + (int64_t) std::llround(60.0 / bpm * kSr);
    s.beatInBar = beatInBar; s.barNumber = bar; s.confidence = 0.9f; s.state = (uint8_t) st; s.sequence = seq;
    return s;
}
// Clean map: host microseconds = sample / 48.
void cleanMap(ClockMap& m, int blocks = 40)
{
    m.prepare(kSr);
    for (int i = 0; i < blocks; ++i) m.addBlock((int64_t) i * 256, (int64_t) std::llround(i * 256 * 1e6 / kSr));
}
}

TEST_CASE(W1_JsonRoundTrip)
{
    Json j; std::string err;
    REQUIRE(Json::parse(R"({"a":[1,2.5,-3e2,true,false,null],"s":"x\"y\\z\n\u00e9\ud83d\ude00","o":{"k":{}}})", j, &err));
    CHECK(j.get("a").items().size() == 6);
    CHECK(j.get("a").items()[2].asNumber() == -300.0);
    CHECK(j.get("s").asString() == "x\"y\\z\n\xC3\xA9\xF0\x9F\x98\x80");
    Json k; REQUIRE(Json::parse(j.dump(), k));
    CHECK(k.dump() == j.dump());
    const char* bad[] = { "", "{", "[1,", "{\"a\"}", "tru", "\"\\ud800\"", "[1] x", "{\"a\":01x}", "\"\x01\"", "[,]" };
    for (const char* b : bad) { Json t; CHECK_MSG(!Json::parse(b, t), b); }
    std::string deep(200, '['); Json t;
    CHECK(!Json::parse(deep, t));
}

TEST_CASE(W2_SettingsMergeAndMigration)
{
    Settings s;
    CHECK(s.profile == "stage" && s.meterNum == 4);
    Json p; REQUIRE(Json::parse(R"({"engine":{"meterNum":99,"sensitivity":7,"pattern":"waltz34","bogus":1},"outputs":{"osc":{"port":70000,"root":"x/","profile":"RESOLUME"}},"unknown":{}})", p));
    std::string err;
    REQUIRE(s.merge(p, &err));
    CHECK(s.meterNum == 16 && s.sensitivity == 1.0 && s.pattern == "waltz34");
    CHECK(s.outputs.osc.port == 65535 && s.outputs.osc.root == "/x" && s.outputs.osc.profile == "resolume");
    Json bad; REQUIRE(Json::parse(R"({"engine":{"meterNum":"four"}})", bad));
    Settings before = s;
    CHECK(!s.merge(bad, &err));
    CHECK(s.meterNum == before.meterNum);   // all or nothing
    // Round trip
    Settings t; REQUIRE(t.merge(s.toJson()));
    CHECK(t.toJson().dump() == s.toJson().dump());
    // Future version loads with a warning, never crashes (A4)
    Json fut; REQUIRE(Json::parse(R"({"stateVersion":99,"engine":{"profile":"rehearsal"},"newThing":[1,2]})", fut));
    Settings u; REQUIRE(u.merge(fut));
    CHECK(u.profile == "rehearsal" && !u.warnings.empty());
    // Presets change parameters only
    Settings v; v.outputs.osc.enabled = true; v.calibration.inputLatencySamples = 77;
    CHECK(applyPreset(v, "3/4 waltz"));
    CHECK(v.meterNum == 3 && v.pattern == "waltz34" && v.outputs.osc.enabled && v.calibration.inputLatencySamples == 77);
    CHECK(!applyPreset(v, "nonsense"));
}

TEST_CASE(W3_SongMapCsv)
{
    SongMap m; std::string err;
    REQUIRE(m.fromCsv("name,bpm,meterNum,meterDen,pattern,countInBeats,programChange\nOpener,128,4,4,fouronfloor44,4,0\n\"Slow, one\",70,6,8,compound68,2,5\n", &err));
    REQUIRE(m.songs().size() == 2);
    CHECK(m.songs()[1].name == "Slow, one" && m.songs()[1].meterNum == 6 && m.songs()[1].meterDen == 8);
    CHECK(m.find("opener") == 0 && m.findProgram(5) == 1 && m.find("zzz") == -1);
    Settings s; CHECK(m.apply(1, s));
    CHECK(s.referenceBpm == 70 && s.meterNum == 6 && s.pattern == "compound68");
    CHECK(!m.apply(9, s));
    CHECK(!m.fromCsv("name,bpm\n,120\n", &err));
    SongMap n; REQUIRE(n.fromJson(m.toJson()));
    CHECK(n.songs().size() == 2);
}

TEST_CASE(W4_DriftLogReportAndMidi)
{
    DriftLog d;
    d.setSongName(0, "Opener");
    for (int i = 0; i < 8; ++i) {
        DriftRow r; r.timestampUs = i * 500000; r.bpm = 120.0 + (i == 7 ? 4.0 : 0.0); r.confidence = 0.9f;
        r.state = (int) EngineState::Locked; r.bar = 1 + i / 4; r.beat = i % 4 + 1; r.song = 0; d.add(r);
    }
    DriftRow h; h.timestampUs = 4000000; h.state = (int) EngineState::Hold; h.event = 's'; h.song = 0; d.add(h);
    DriftRow h2 = h; h2.timestampUs = 6000000; h2.state = (int) EngineState::Locked; d.add(h2);
    DriftRow rl; rl.timestampUs = 6100000; rl.event = 'r'; rl.song = 0; d.add(rl);
    const std::string csv = d.toCsv();
    CHECK(csv.rfind("timestampUs,bpm,confidence,state,bar,beat,event\n", 0) == 0);
    CHECK(std::count(csv.begin(), csv.end(), '\n') == 12);
    const Json rep = d.report();
    REQUIRE(rep.items().size() == 1);
    const Json& r0 = rep.items()[0];
    CHECK(r0.get("name").asString() == "Opener" && r0.get("relocks").asInt() == 1 && r0.get("beats").asInt() == 8);
    CHECK(std::fabs(r0.get("secondsInHold").asNumber() - 2.0) < 1e-6);
    CHECK(std::fabs(r0.get("maxBpm").asNumber() - 124.0) < 1e-9);
    const std::string mid = d.tempoMapMidi();
    CHECK(mid.compare(0, 4, "MThd") == 0 && mid.find("MTrk") == 14);
    CHECK(mid.find("\xFF\x51\x03") != std::string::npos);
    CHECK(mid.compare(mid.size() - 4, 4, std::string("\x00\xFF\x2F\x00", 4)) == 0);
    DriftLog big; DriftRow x; for (size_t i = 0; i < DriftLog::kMaxRows + 10; ++i) big.add(x);
    CHECK(big.size() == DriftLog::kMaxRows);
}

TEST_CASE(ClockMapSmoothsJitter)
{
    ClockMap m; m.prepare(kSr);
    // True clock runs 100 ppm fast; callbacks arrive with +-2 ms jitter.
    uint64_t rng = 12345;
    auto noise = [&] { rng = rng * 6364136223846793005ull + 1442695040888963407ull; return ((double) (rng >> 33) / 2147483648.0 - 0.5) * 4000.0; };
    for (int i = 0; i < 600; ++i) {
        const double ideal = i * 256 * 1e6 / kSr * 1.0001 + 5000.0;
        m.addBlock((int64_t) i * 256, (int64_t) std::llround(ideal + noise()));
    }
    const double s = 599 * 256;
    const double truth = s * 1e6 / kSr * 1.0001 + 5000.0;
    CHECK(std::fabs(m.hostTimeForSample(s) - truth) < 250.0);   // jitter was +-2000 us
    CHECK(std::fabs(m.sampleForHostTime(m.hostTimeForSample(s)) - s) < 0.01);
    m.setInputCompUs(1000.0);
    CHECK(std::fabs(m.hostTimeForSample(s) - (truth - 1000.0)) < 250.0);
}

TEST_CASE(BeatGridArithmetic)
{
    BeatMapSnapshot s = snap(120, 48000, 3, 5);
    BeatGrid g = gridFromSnapshot(s, 4);
    CHECK(g.beatInBarAt(0) == 3 && g.beatInBarAt(2) == 1 && g.beatInBarAt(-3) == 4 && g.beatInBarAt(-4) == 3);
    CHECK(g.barAt(0) == 5 && g.barAt(2) == 6 && g.barAt(-3) == 4 && g.barAt(-2) == 5);
    CHECK(std::fabs(g.barPhaseAt(48000 + 12000) - 0.625) < 1e-9);   // beat 3 is index 2; half a beat later: 2.5 / 4
}

TEST_CASE(MidiClockTicksAndTransport)
{
    ClockMap map; cleanMap(map);
    MidiClockGenerator gen;
    std::vector<MidiMessage> all;
    const BeatMapSnapshot s = snap(120, 48000);   // beat at 1,000,000 us, period 500,000 us
    for (int64_t now = 900000; now < 4000000; now += 20000) gen.generate(s, 4, map, now, now + 40000, all);
    REQUIRE(!all.empty());
    CHECK(all[0].bytes[0] == 0xFA && std::llabs(all[0].hostUs - 1000000) <= 2);
    std::vector<int64_t> ticks;
    for (const auto& m : all) if (m.bytes[0] == 0xF8) ticks.push_back(m.hostUs);
    CHECK(ticks.size() >= 140 && ticks.size() <= 150);   // ~3 s * 48 ticks/s
    double worst = 0;
    for (size_t i = 1; i < ticks.size(); ++i) worst = std::max(worst, std::fabs((double) (ticks[i] - ticks[i - 1]) - 500000.0 / 24.0));
    CHECK_MSG(worst <= 2.0, "worst interval error us");
    CHECK(std::is_sorted(ticks.begin(), ticks.end()));
    // Tick 0 of every beat coincides with the beat.
    for (size_t i = 0; i < ticks.size(); i += 24) { const int64_t r = (ticks[i] - 1000000) % 500000; CHECK(std::min(r, 500000 - r) <= 2); }
    // Idle sends Stop once.
    std::vector<MidiMessage> stopMsgs;
    gen.generate(snap(120, 48000, 1, 1, EngineState::Idle), 4, map, 4100000, 4140000, stopMsgs);
    gen.generate(snap(120, 48000, 1, 1, EngineState::Idle), 4, map, 4120000, 4160000, stopMsgs);
    CHECK(stopMsgs.size() == 1 && stopMsgs[0].bytes[0] == 0xFC);
}

TEST_CASE(MidiClockOffsetAndSpp)
{
    ClockMap map; cleanMap(map);
    MidiClockConfig c; c.offsetMs = 10.0; c.sppOnRelock = true;
    MidiClockGenerator gen(c);
    std::vector<MidiMessage> out;
    BeatMapSnapshot s = snap(120, 48000, 2, 3);
    for (int64_t now = 900000; now < 1600000; now += 20000) gen.generate(s, 4, map, now, now + 40000, out);
    CHECK(out[0].bytes[0] == 0xFA);
    CHECK(std::llabs(out[0].hostUs - 1010000) <= 2 || std::llabs(out[0].hostUs - 1510000) <= 2);   // offset applied
    s.flags = SnapshotFlags::Relock; s.sequence = 7;
    out.clear();
    gen.generate(s, 4, map, 1600000, 1640000, out);
    gen.generate(s, 4, map, 1620000, 1660000, out);   // same sequence again: no second SPP
    int spp = 0, cont = 0; for (const auto& m : out) { spp += m.bytes[0] == 0xF2; cont += m.bytes[0] == 0xFB; }
    CHECK(spp == 1 && cont == 1);
    // Without sppOnRelock nothing is sent.
    MidiClockGenerator quiet; std::vector<MidiMessage> q;
    for (int64_t now = 900000; now < 1600000; now += 20000) quiet.generate(s, 4, map, now, now + 40000, q);
    for (const auto& m : q) CHECK(m.bytes[0] != 0xF2 && m.bytes[0] != 0xFB);
}

TEST_CASE(OscEncodeDecode)
{
    OscMessage m { "/pacemaker/beat", { OscArg::I(3), OscArg::F(120.5), OscArg::S("hi") } };
    const auto enc = oscEncode(m);
    CHECK(enc.size() % 4 == 0);
    std::vector<OscMessage> d; REQUIRE(oscDecode(enc, d));
    REQUIRE(d.size() == 1 && d[0].address == m.address && d[0].args.size() == 3);
    CHECK(d[0].args[0].i == 3 && d[0].args[1].f == 120.5f && d[0].args[2].s == "hi");
    const auto b = oscBundle(0x0102030405060708ull, { m, m });
    std::vector<OscMessage> db; uint64_t tt = 0; REQUIRE(oscDecode(b, db, &tt));
    CHECK(db.size() == 2 && tt == 0x0102030405060708ull);
    std::vector<uint8_t> broken(b.begin(), b.end() - 3);
    std::vector<OscMessage> x; CHECK(!oscDecode(broken, x));
    CHECK(!oscDecode({ 1, 2, 3 }, x));
}

TEST_CASE(O5_OscBeatBundles)
{
    ClockMap map; cleanMap(map);
    OscGenerator gen;
    std::vector<OscPacket> out;
    const BeatMapSnapshot s = snap(120, 48000);
    for (int64_t now = 900000; now < 3500000; now += 5000) gen.generate(s, 4, map, now, out);
    std::vector<uint64_t> tts; std::vector<std::pair<int, int>> beats; int tempo = 0, phase = 0, conf = 0, state = 0, down = 0;
    for (const auto& p : out) {
        std::vector<OscMessage> ms; uint64_t tt = 0;
        REQUIRE(oscDecode(p.bytes, ms, &tt));
        if (p.label == "beat") {
            tts.push_back(tt);
            for (const auto& m : ms) {
                if (m.address == "/pacemaker/beat") beats.push_back({ m.args[0].i, m.args[1].i });
                if (m.address == "/pacemaker/downbeat") ++down;
            }
            CHECK(p.sendAtUs <= (int64_t) 0 + 3500000);
        }
        tempo += p.label == "tempo"; phase += p.label == "phase"; conf += p.label == "confidence"; state += p.label == "state";
    }
    REQUIRE(tts.size() >= 4);
    CHECK(std::is_sorted(tts.begin(), tts.end()));
    // Timetags are the predicted beat times on the NTP clock (monotonic, 500 ms apart, within 1 ms).
    for (size_t i = 1; i < tts.size(); ++i) {
        const double dt = ((double) (tts[i] - tts[i - 1])) / 4294967296.0;
        CHECK(std::fabs(dt - 0.5) < 0.001);
    }
    const double first = (double) (tts[0] >> 32) + (double) (tts[0] & 0xFFFFFFFFu) / 4294967296.0;
    const double expect = (1000000.0 + (double) ntpOffsetUs()) * 1e-6;
    CHECK(std::fabs(first - expect) < 0.001 || std::fabs(first - expect - 0.5) < 0.001);
    for (size_t i = 1; i < beats.size(); ++i) CHECK(beats[i].second == beats[i - 1].second % 4 + 1);
    CHECK(down >= 1 && tempo >= 3 && phase > 200 && conf >= 20 && state == 1);
    // Resolume profile
    OscConfig rc; rc.profile = "resolume"; OscGenerator rg(rc); std::vector<OscPacket> ro;
    for (int64_t now = 900000; now < 1600000; now += 5000) rg.generate(s, 4, map, now, ro);
    bool sawTempo = false, sawResync = false;
    for (const auto& p : ro) { std::vector<OscMessage> ms; oscDecode(p.bytes, ms);
        for (const auto& m : ms) { sawTempo |= m.address == "/composition/tempocontroller/tempo"; sawResync |= m.address == "/composition/tempocontroller/resync"; } }
    CHECK(sawTempo && sawResync);
}

TEST_CASE(O4_SyncPulseAlignsWithMidiTicks)
{
    ClockMap map; cleanMap(map);
    const BeatMapSnapshot s = snap(120, 48000, 1, 1);
    // Render 3 s in odd block sizes and in one go: identical (block size never matters).
    const int total = 3 * 48000;
    std::vector<float> L1((size_t) total), R1((size_t) total), L2((size_t) total), R2((size_t) total);
    renderSync(s, 4, kSr, 0, total, L1.data(), R1.data());
    for (int pos = 0; pos < total;) { const int n = std::min(137 + pos % 311, total - pos); renderSync(s, 4, kSr, pos, n, L2.data() + pos, R2.data() + pos); pos += n; }
    CHECK(L1 == L2);
    CHECK(R1 == R2);
    // Rising edges of the pulse versus generator ticks.
    std::vector<int> edges;
    for (int i = 1; i < total; ++i) if (L1[(size_t) i] > 1.0f && L1[(size_t) i - 1] <= 1.0f) edges.push_back(i);
    MidiClockGenerator gen; std::vector<MidiMessage> msgs;
    for (int64_t now = 900000; now < 4000000; now += 20000) gen.generate(s, 4, map, now, now + 40000, msgs);
    std::vector<double> tickSamples;
    for (const auto& m : msgs) if (m.bytes[0] == 0xF8) tickSamples.push_back((double) m.hostUs * kSr / 1e6);
    int checked = 0;
    for (int e : edges) {
        if (e < 48000) continue;
        double best = 1e9; for (double t : tickSamples) best = std::min(best, std::fabs(t - e));
        if (best >= 48.0) std::printf("  edge %d best %.1f\n", e, best);
        CHECK_MSG(best < 48.0, "pulse edge within 1 ms of a MIDI tick");   // O4
        ++checked;
    }
    CHECK(checked > 90);
    // Clicks: downbeat 2 kHz, other beats 1 kHz; peak level 0.8. Pulse peak +6 dBFS.
    float pk = 0, lp = 0; for (int i = 0; i < total; ++i) { pk = std::max(pk, std::fabs(R1[(size_t) i])); lp = std::max(lp, L1[(size_t) i]); }
    CHECK(pk > 0.5f && pk <= 0.81f && std::fabs(lp - 2.0f) < 1e-6f);
    // Idle renders silence.
    renderSync(snap(120, 48000, 1, 1, EngineState::Idle), 4, kSr, 0, 1000, L2.data(), R2.data());
    CHECK(*std::max_element(L2.begin(), L2.begin() + 1000) == 0.0f);
}

namespace {
struct MockLink : LinkSession {
    double bpm = 120.0; int sets = 0, forces = 0; double beatOffset = 0.0; double lastForced = -1;
    double tempo() override { return bpm; }
    void setTempo(double b, int64_t) override { bpm = b; ++sets; }
    void forceBeatAtTime(double beat, int64_t, double) override { ++forces; lastForced = beat; }
    double beatAtTime(int64_t, double) override { return 0.0; }
    int numPeers() override { return 1; }
};
}

TEST_CASE(O2_LinkPolicyNeverForcesWithoutRelock)
{
    ClockMap map; cleanMap(map);
    LinkPolicy pol; LinkConfig c; c.enabled = true; pol.configure(c);
    MockLink link;
    BeatMapSnapshot s = snap(124.0, 48000, 1, 1, EngineState::Locked, 1);
    for (int i = 0; i < 100; ++i) { s.sequence = (uint32_t) i + 1; pol.update(s, map, 1000000 + i * 10000, link); }
    CHECK(link.forces == 0 && pol.forceCount() == 0);
    CHECK(std::fabs(link.bpm - 124.0) < 0.3 && link.sets >= 1);
    // Relock forces exactly once per sequence.
    s.flags = SnapshotFlags::Relock; s.sequence = 500;
    pol.update(s, map, 3000000, link); pol.update(s, map, 3010000, link);
    CHECK(link.forces == 1);
    CHECK(link.lastForced == 0.0 * 4 + 1 * 4 + 0);   // bar 1, beat 1 with quantum 4
    // Low confidence or disabled: no tempo commit.
    MockLink quiet; LinkPolicy p2; p2.configure(c);
    BeatMapSnapshot weak = snap(140.0, 48000); weak.confidence = 0.1f;
    p2.update(weak, map, 1000000, quiet);
    CHECK(quiet.sets == 0);
    LinkConfig off; MockLink m3; LinkPolicy p3; p3.configure(off);
    p3.update(s, map, 1000000, m3);
    CHECK(m3.sets == 0 && m3.forces == 0);
}

TEST_CASE(O3_ClockThreadJitter)
{
    ClockMap map; map.prepare(kSr);
    const int64_t t0 = steadyNowUs() + 200000;
    for (int i = 0; i < 40; ++i) map.addBlock((int64_t) i * 256, t0 + (int64_t) std::llround(i * 256 * 1e6 / kSr));
    BeatMapSnapshot s = snap(120, 0);
    ClockThread th(map, [s] { return s; }, [] { return 4; });
    std::mutex mu; std::vector<int64_t> arrivals; int starts = 0, stops = 0;
    th.setSink(makeCallbackSink([&](const uint8_t* b, int, int64_t) {
        const int64_t at = steadyNowUs(); std::lock_guard<std::mutex> l(mu);
        if (b[0] == 0xF8) arrivals.push_back(at); else if (b[0] == 0xFA) ++starts; else if (b[0] == 0xFC) ++stops;
    }));
    th.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(2800));
    th.stop();
    std::lock_guard<std::mutex> l(mu);
    CHECK(starts == 1 && stops == 1);
    REQUIRE(arrivals.size() > 80);
    double mean = 0, sd = 0; const double nominal = 500000.0 / 24.0;
    std::vector<double> err;
    for (size_t i = 1; i < arrivals.size(); ++i) err.push_back((double) (arrivals[i] - arrivals[i - 1]) - nominal);
    for (double e : err) mean += e;
    mean /= (double) err.size();
    for (double e : err) sd += (e - mean) * (e - mean);
    sd = std::sqrt(sd / (double) err.size());
    std::printf("  tick interval sd = %.0f us over %zu ticks, jitter status %.0f us\n", sd, arrivals.size(), th.status().jitterUs.load());
    CHECK_MSG(sd < 1500.0, "tick interval standard deviation");   // CI hosts are noisy; target is 300 us on real hardware
    CHECK(std::fabs(mean) < 300.0);
}
