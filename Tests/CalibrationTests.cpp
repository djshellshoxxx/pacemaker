// Tests for Json, Csv and ES-03 calibration (section 9: L1-L3).
#include "TestFramework.h"
#include "pacemaker/core/Calibration.h"
#include "pacemaker/core/CalibrationStore.h"
#include "pacemaker/core/Csv.h"
#include "pacemaker/core/Json.h"
#include <cmath>
#include <deque>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

using namespace pacemaker;
using namespace pacemaker::core;

namespace {

std::string tempPath(const std::string& name)
{
    return (std::filesystem::temp_directory_path() / ("pacemaker_cal_" + name)).string();
}

// Pink noise (Paul Kellet's economy filter) with unit-ish RMS.
std::vector<float> pinkNoise(size_t n, uint32_t seed)
{
    std::mt19937 rng(seed);
    std::normal_distribution<float> g(0.0f, 1.0f);
    std::vector<float> out(n);
    float b0 = 0, b1 = 0, b2 = 0;
    double e = 0;
    for (auto& v : out) {
        const float w = g(rng);
        b0 = 0.99765f * b0 + w * 0.0990460f;
        b1 = 0.96300f * b1 + w * 0.2965164f;
        b2 = 0.57000f * b2 + w * 1.0526913f;
        v = b0 + b1 + b2 + w * 0.1848f;
        e += (double) v * v;
    }
    const float scale = (float) (1.0 / std::sqrt(e / (double) n));
    for (auto& v : out) v *= scale;
    return out;
}

// Duplex device: input = output delayed by reportedOut + hidden + reportedIn, plus noise.
// Runs the calibrator block by block exactly as an audio callback would.
LoopbackResult runDevice(int reportedIn, int reportedOut, int hidden, float noiseRms, HiddenSplit split,
                         int block = 128)
{
    LoopbackCalibrator cal;
    cal.start();
    const int delay = reportedIn + reportedOut + hidden;
    std::deque<float> line((size_t) delay, 0.0f);
    const auto noise = pinkNoise((size_t) cal.config().totalSamples() + 4096, 7);
    std::vector<float> in((size_t) block), out((size_t) block);
    size_t t = 0;
    while (cal.state() == LoopbackCalibrator::State::Running) {
        for (int i = 0; i < block; ++i) {
            in[(size_t) i] = line.front() + noiseRms * noise[t + (size_t) i];
            line.pop_front();
        }
        cal.process(in.data(), out.data(), block);
        for (int i = 0; i < block; ++i) line.push_back(out[(size_t) i]);
        t += (size_t) block;
    }
    return cal.analyse(reportedIn, reportedOut, split);
}

} // namespace

TEST_CASE(CalJsonRoundTrip)
{
    Json o = Json::object();
    o.set("zeta", 1);
    o.set("alpha", 2.5);
    o.set("name", "Snare \"2\"\n\xC3\xA9");
    o.set("list", Json::array());
    o.set("flag", true);
    o.set("none", nullptr);
    Json& l = const_cast<Json&>(o["list"]);
    l.push(1); l.push("x"); l.push(Json::object());
    o.set("big", (int64_t) 1234567890123LL);
    o.set("size", (size_t) 7);
    for (int indent : { -1, 2 }) {
        auto r = Json::parse(o.dump(indent));
        REQUIRE_MSG(r.ok, r.error);
        CHECK(r.value == o);
        CHECK(r.value.members()[0].first == "zeta"); // insertion order kept
    }
    CHECK(o.dump() == "{\"zeta\":1,\"alpha\":2.5,\"name\":\"Snare \\\"2\\\"\\n\xC3\xA9\",\"list\":[1,\"x\",{}],"
                      "\"flag\":true,\"none\":null,\"big\":1234567890123,\"size\":7}");
    CHECK(Json::parse("0.1").value.asNumber() == 0.1);
    CHECK(Json::parse("[1e3,-0.5,\"\\u00e9\\ud83d\\ude00\"]").value[2].asString() == "\xC3\xA9\xF0\x9F\x98\x80");
    // Typed getters with defaults.
    CHECK(o.getInt("zeta", 9) == 1);
    CHECK(o.getInt("name", 9) == 9);
    CHECK(o.getString("missing", "d") == "d");
    CHECK(o["list"][5].isNull());
    CHECK(o["zeta"]["x"].isNull());
    CHECK(o.getBool("flag", false));
    Json n(3.0);
    n.set("k", 1); // converts to object
    CHECK(n.isObject() && n.getInt("k", 0) == 1);
}

TEST_CASE(CalJsonMalformed)
{
    const char* bad[] = { "", "{", "[1,]", "{\"a\" 1}", "{\"a\":1,}", "tru", "\"abc", "\"\\x\"", "\"\\u12\"",
                          "01", "-", "1.", "1e", "[1] 2", "{\"a\":[}", "nul", "\"a\nb\"", "{1:2}", "\xEF\xBB",
                          "[\"\\ud800\"", "1e999999" };
    for (const char* b : bad) {
        auto r = Json::parse(b);
        if (std::string(b) == "1e999999") { CHECK(r.ok); continue; } // clamps to 0, still valid syntax
        CHECK_MSG(!r.ok, b);
        CHECK(!r.error.empty() || r.ok);
    }
    auto r = Json::parse("{\n  \"a\": 1,\n  \"b\": x\n}");
    CHECK(!r.ok && r.errorLine == 3 && r.errorColumn == 8 && r.errorOffset == 19);
    // Deep nesting fails cleanly instead of overflowing the stack.
    CHECK(!Json::parse(std::string(100000, '[')).ok);
    // Random garbage and truncations never crash.
    std::mt19937 rng(1);
    const std::string good = "{\"a\":[1,2.5e-3,{\"b\":\"c\\u0041\"}],\"d\":true,\"e\":null}";
    for (int i = 0; i < 3000; ++i) {
        std::string s = good;
        const int muts = 1 + (int) (rng() % 4);
        for (int m = 0; m < muts; ++m) s[rng() % s.size()] = (char) (rng() % 256);
        if (rng() % 3 == 0) s.resize(rng() % s.size());
        auto pr = Json::parse(s);
        if (pr.ok) CHECK(Json::parse(pr.value.dump()).ok);
    }
    CHECK(Json::parse("\xEF\xBB\xBF{\"a\":1}").ok);
    CHECK(!loadJsonFile(tempPath("does_not_exist.json")).ok);
}

TEST_CASE(CalCsvParse)
{
    auto rows = parseCsv("a,\"b,c\",\"d \"\"q\"\"\"\r\n\r\n1,2\n\"multi\nline\",x");
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].size() == 3 && rows[0][1] == "b,c" && rows[0][2] == "d \"q\"");
    CHECK(rows[1].size() == 2);
    CHECK(rows[2][0] == "multi\nline");
    CHECK(parseCsv("a;b;c\n1;2,5;3")[1][1] == "2,5");
    CHECK(parseCsv("a\tb\n1\t2")[1][1] == "2");
    const CsvRow r { "plain", "has,comma", "has \"quote\"", "" };
    CHECK(parseCsv(csvLine(r))[0] == r);
    double d = 0;
    CHECK(parseDouble(" 0.3 ", d) && d == 0.3);
    CHECK(parseDouble("120,5", d) && d == 120.5);
    CHECK(parseDouble("-1.5e2", d) && d == -150.0);
    CHECK(!parseDouble("12abc", d) && !parseDouble("", d) && !parseDouble(".", d));
    int i = 0;
    CHECK(parseInt("42", i) && i == 42);
    CHECK(!parseInt("9e99", i));
    auto t = parseCsvTable("\xEF\xBB\xBF Song Name , BPM\nA,120\nB");
    CHECK(t.column("song_name") == 0 && t.column({ "tempo", "bpm" }) == 1);
    CHECK(t.field(1, 1).empty() && t.field(0, 1) == "120" && t.field(9, 0).empty());
    CHECK(parseCsv("").empty());
}

// L1: hidden 37 samples on a simulated device is measured as 37 +-1.
TEST_CASE(CalLoopbackL1)
{
    auto r = runDevice(96, 160, 37, 0.0f, HiddenSplit::Equal);
    CHECK_MSG(r.pass, r.message);
    CHECK(r.clicksDetected == 8 && r.clicksKept == 8);
    CHECK_MSG(std::fabs(r.hiddenSamples - 37) <= 1, r.message);
    CHECK(std::fabs(r.hiddenInputSamples + r.hiddenOutputSamples - r.hiddenSamples) < 1e-9);
    CHECK(std::fabs(r.hiddenInputSamples - 18.5) <= 1);
    auto a = runDevice(32, 32, 37, 0.0f, HiddenSplit::AllOnInput, 64);
    CHECK(std::fabs(a.hiddenInputSamples - 37) <= 1 && a.hiddenOutputSamples == 0);
    // Over-reporting driver: hidden is negative and stored as such.
    auto neg = runDevice(200, 200, -50, 0.0f, HiddenSplit::Equal);
    CHECK(neg.pass && std::fabs(neg.hiddenSamples + 50) <= 1);
}

// L2: -20 dB SNR pink noise (noise RMS 0.1 against the 0 dBFS click).
TEST_CASE(CalLoopbackL2Noise)
{
    auto r = runDevice(96, 160, 37, 0.1f, HiddenSplit::Equal, 256);
    CHECK_MSG(r.pass, r.message);
    CHECK_MSG(std::fabs(r.hiddenSamples - 37) <= 1, r.message);
}

TEST_CASE(CalLoopbackFail)
{
    // No patch: only noise reaches the input.
    LoopbackConfig cfg;
    const auto noise = pinkNoise((size_t) cfg.totalSamples(), 3);
    auto r = analyseLoopback(noise.data(), noise.size(), cfg, 0, 0);
    CHECK(!r.pass);
    // Clicks dropped: only 4 of 8 arrive, so the run fails (< 6 clicks).
    auto stim = renderLoopbackStimulus(cfg);
    std::vector<float> cap(stim.size(), 0.0f);
    for (size_t i = 100; i < cap.size(); ++i) cap[i] = stim[i - 100];
    for (int k = 4; k < 8; ++k)
        for (int m = 0; m < 2000; ++m) cap[(size_t) cfg.clickOnsetSample(k) + (size_t) m] = 0;
    r = analyseLoopback(cap.data(), cap.size(), cfg, 0, 0);
    CHECK(!r.pass && r.clicksDetected == 4 && std::fabs(r.roundTripSamples - 100) < 0.5);
    // Not captured yet.
    LoopbackCalibrator c;
    CHECK(!c.analyse(0, 0).pass);
    CHECK(c.state() == LoopbackCalibrator::State::Idle);
}

// L3: an onset 1 m away with the Overhead preset lands with the Close role's onset +-1 sample.
TEST_CASE(CalAcousticL3)
{
    LatencyModel m;
    m.sampleRate = 48000;
    m.detectorDelaySamples = 256;
    m.reportedInputSamples = 64;
    m.hiddenInputSamples = 18.5;
    CHECK(std::fabs(acousticDelayMs(1.0) - 2.915) < 0.01);
    CHECK(m.roles[(int) InputRole::Overhead].placement == MicPlacement::Overhead);
    CHECK(m.roles[(int) InputRole::Trigger].moduleLatencyMs == kDefaultModuleLatencyMs);
    const int64_t truth = 100000;
    const double inLat = 256 + 64 + 18.5;
    auto rendered = [&](double metres) { return truth + (int64_t) std::llround(inLat + acousticDelayMs(metres) * 48.0); };
    const int64_t close = m.correct(rendered(0.1), InputRole::Kick);
    const int64_t over = m.correct(rendered(1.0), InputRole::Overhead);
    CHECK(std::llabs(close - truth) <= 1 && std::llabs(over - truth) <= 1 && std::llabs(over - close) <= 1);
    // Room and custom distances, trigger module latency only.
    m.roles[(int) InputRole::Snare] = { MicPlacement::Room, 0, 0 };
    CHECK(std::llabs(m.correct(rendered(3.0), InputRole::Snare) - truth) <= 1);
    m.roles[(int) InputRole::Snare] = { MicPlacement::Custom, 2.0, 0 };
    CHECK(std::llabs(m.correct(rendered(2.0), InputRole::Snare) - truth) <= 1);
    CHECK(std::fabs(m.roleDelaySamples(InputRole::Trigger) - 144.0) < 1e-9);
    MicPlacement p;
    CHECK(micPlacementFromName("overhead", p) && p == MicPlacement::Overhead && !micPlacementFromName("x", p));
}

TEST_CASE(CalTapAlong)
{
    const double sr = 48000, bpm = 100;
    const double spb = 60.0 / bpm * sr;
    TapAlongCalibrator tap(sr, 4, 8);
    std::mt19937 rng(5);
    std::normal_distribution<double> jitter(0.0, 3.0);
    // Host starts at ppq 0 at sample 0; the player lands 25 ms late with 3 ms jitter.
    const int block = 512;
    std::vector<int64_t> hits;
    for (int b = 0; b < 40; ++b) hits.push_back((int64_t) std::llround(b * spb + (25.0 + jitter(rng)) * 48.0));
    size_t h = 0;
    for (int64_t s = 0; s < (int64_t) (40 * spb); s += block) {
        tap.setHostPosition(s, s / spb, bpm, true);
        while (h < hits.size() && hits[h] < s + block) tap.addOnset(hits[h++]);
    }
    CHECK(tap.complete());
    auto r = tap.result();
    CHECK(r.valid && r.count == 32);
    CHECK_MSG(std::fabs(r.medianErrorMs - 25.0) < 2.0, r.message);
    CHECK(std::fabs(r.medianErrorSamples - r.medianErrorMs * 48.0) < 1e-6);
    CHECK(r.message.find("after") != std::string::npos);

    // Click loopback variant: early hits are reported, no human jitter.
    TapAlongCalibrator click(sr, 4, 8, true);
    for (int b = 0; b < 40; ++b) {
        const int64_t s0 = (int64_t) (b * spb);
        click.setHostPosition(s0, b, bpm, true);
        click.addOnset(s0 + 480); // 10 ms
    }
    auto c = click.result();
    CHECK(c.clickLoopback && std::fabs(c.medianErrorMs - 10.0) < 0.01);
    // Stopped transport is ignored.
    TapAlongCalibrator stopped(sr);
    stopped.setHostPosition(0, 0, bpm, false);
    stopped.addOnset(100);
    CHECK(!stopped.result().valid);
}

TEST_CASE(CalHardwareClockOffset)
{
    const double sr = 48000;
    std::vector<int64_t> hits, clicks;
    for (int i = 0; i < 16; ++i) {
        hits.push_back(i * 24000);
        clicks.push_back(i * 24000 + 576 + (i % 3) * 10); // ~12 ms late
    }
    clicks.push_back(9999999); // unpaired
    auto r = measureHardwareClockOffset(clicks, hits, sr);
    CHECK(r.valid && r.pairs == 16);
    CHECK(std::fabs(r.medianOffsetMs - 12.208) < 0.01);
    CHECK(std::fabs(r.suggestedMidiClockOffsetMs + r.medianOffsetMs) < 1e-12);
    CHECK(!measureHardwareClockOffset({}, hits, sr).valid);
}

TEST_CASE(CalStore)
{
    CalibrationStore s;
    const DeviceKey a { "Focusrite USB", 48000, 64 }, b { "Focusrite USB", 48000, 128 };
    s.set(a, { 18.5, 18.5, 293, "2026-10-11" });
    s.set(b, { -4, 0, 0, "" });
    s.set(a, { 19, 18, 293, "" });
    CHECK(s.size() == 2 && s.find(a)->hiddenInputSamples == 19 && !s.has({ "Other", 48000, 64 }));
    const std::string path = tempPath("store/calibration.json");
    REQUIRE(s.save(path));
    CalibrationStore t;
    REQUIRE(t.load(path));
    CHECK(t.size() == 2 && t.find(b)->hiddenInputSamples == -4 && t.find(a)->hiddenOutputSamples == 18);
    CHECK(t.toJson() == s.toJson());
    // Malformed entries are skipped; invalid files leave the store unchanged.
    auto j = Json::parse("{\"devices\":[1,{\"deviceName\":\"x\"},{\"deviceName\":\"y\",\"sampleRate\":44100,\"extra\":1}]}");
    CHECK(CalibrationStore::fromJson(j.value).size() == 1);
    writeTextFile(path, "{broken");
    CHECK(!t.load(path) && t.size() == 2);
    CHECK(t.erase(a) && !t.erase(a));
    std::filesystem::remove_all(tempPath("store"));
}
