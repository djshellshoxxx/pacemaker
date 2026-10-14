// Latency calibration tests L1 to L3 plus tap-along, hardware offset and storage (ES-03 section 9).
#include "TestFramework.h"
#include "pacemaker/Calibration.h"
#include <cmath>
#include <random>

using namespace pacemaker;

namespace {
constexpr double kSr = 48000.0;

LoopbackResult simulate(int reportedIn, int reportedOut, int hidden, double noiseRms, unsigned seed = 3)
{
    const LoopbackPlan plan = planLoopback(kSr);
    std::vector<float> train, rec((size_t) plan.totalSamples, 0.0f);
    renderLoopbackTrain(plan, kSr, train);
    const int delay = reportedIn + reportedOut + hidden;
    std::mt19937 rng(seed); std::normal_distribution<float> nd(0.f, (float) noiseRms);
    for (size_t i = 0; i < rec.size(); ++i) rec[i] = (i >= (size_t) delay ? train[i - (size_t) delay] : 0.f) + (noiseRms > 0 ? nd(rng) : 0.f);
    return analyseLoopback(rec, plan, kSr, reportedIn, reportedOut);
}
}

TEST_CASE(L1_LoopbackFindsHiddenLatency)
{
    const LoopbackResult r = simulate(96, 64, 37, 0.0);
    CHECK(r.ok);
    CHECK(std::fabs(r.hiddenSamples - 37.0) <= 1.0);
    CHECK(r.detected == 8 && r.spreadSamples <= 1.0);
    // Over-reporting drivers give a negative hidden term, which is still the right correction.
    const LoopbackResult n = simulate(200, 100, 0, 0.0);
    CHECK(n.ok && std::fabs(n.hiddenSamples - 0.0) <= 1.0);
    const LoopbackResult o = simulate(300, 300, 0, 0.0);   // reported sum exceeds the measured one by 0? delay is 600, reported 600
    CHECK(std::fabs(o.hiddenSamples) <= 1.0);
}

TEST_CASE(L2_LoopbackSurvivesNoise)
{
    // Click peak is 1.0; noise RMS 0.1 is -20 dB relative to the click.
    for (unsigned seed = 1; seed <= 4; ++seed) {
        const LoopbackResult r = simulate(96, 64, 37, 0.1, seed);
        CHECK_MSG(r.ok, r.message);
        CHECK(std::fabs(r.hiddenSamples - 37.0) <= 1.0);
    }
}

TEST_CASE(LoopbackFailsCleanly)
{
    const LoopbackPlan plan = planLoopback(kSr);
    std::vector<float> silence((size_t) plan.totalSamples, 0.0f);
    const LoopbackResult r = analyseLoopback(silence, plan, kSr, 0, 0);
    CHECK(!r.ok && r.detected == 0 && !r.message.empty());
    std::vector<float> shortBuf(100, 0.0f);
    CHECK(!analyseLoopback(shortBuf, plan, kSr, 0, 0).ok);   // truncated capture does not crash
}

TEST_CASE(L3_AcousticDelayPresets)
{
    const double close = acousticDelaySamples(distancePreset("close"), kSr);
    const double over = acousticDelaySamples(distancePreset("overhead"), kSr);
    CHECK(std::fabs(over / kSr * 1000.0 - 2.9) < 0.05);     // 2.9 ms per metre
    // An onset rendered 1 m away lands later by (over - close); the correction puts both on the same sample.
    const double trueHit = 100000.0;
    const double rawClose = trueHit + close, rawOver = trueHit + over;
    CHECK(std::fabs(correctOnsetSample(rawClose, 0, 0, close) - correctOnsetSample(rawOver, 0, 0, over)) <= 1.0);
    CHECK(std::fabs(correctOnsetSample(rawOver, 384, 120, over) - (trueHit - 384 - 120)) <= 1.0);
}

TEST_CASE(TapAlongAndHardwareOffset)
{
    std::vector<double> onsets;
    for (int i = 0; i < 32; ++i) onsets.push_back(1.0 + i * 0.5 + 0.018 + ((i * 7) % 5 - 2) * 0.001);   // 18 ms late, small spread
    const TapAlongResult t = tapAlong(onsets, 120.0, 1.0);
    CHECK(t.ok && std::fabs(t.medianMs - 18.0) < 2.0 && t.used == 32);
    CHECK(!tapAlong({ 1.0, 1.5 }, 120.0, 1.0).ok);
    std::vector<double> hits, hw;
    for (int i = 0; i < 16; ++i) { hits.push_back(2.0 + i * 0.5); hw.push_back(2.0 + i * 0.5 + 0.012); }   // hardware click 12 ms late
    CHECK(std::fabs(hardwareClockOffsetMs(hw, hits) - (-12.0)) < 0.5);
}

TEST_CASE(CalibrationStoreRoundTrip)
{
    CalibrationStore s;
    s.set({ "Scarlett 2i2", 48000, 128 }, { 18.5, 18.5 });
    CalibrationStore t; t.fromJson(s.toJson());
    CalibrationEntry e; CHECK(t.get({ "Scarlett 2i2", 48000, 128 }, e) && e.hiddenInputSamples == 18.5);
    CHECK(!t.get({ "Scarlett 2i2", 44100, 128 }, e));
    t.fromJson(Json(42)); CHECK(t.size() == 0);   // garbage is ignored
}
