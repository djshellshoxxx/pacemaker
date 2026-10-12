// Latency model and calibration procedures (ES-03).
#include "pacemaker/core/Calibration.h"
#include "pacemaker/core/Csv.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace pacemaker { namespace core {

double medianOf(std::vector<double> v)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// ---- Section 3 ----

const char* micPlacementName(MicPlacement p)
{
    switch (p) {
        case MicPlacement::Close: return "Close";
        case MicPlacement::Overhead: return "Overhead";
        case MicPlacement::Room: return "Room";
        case MicPlacement::Custom: return "Custom";
    }
    return "Close";
}

bool micPlacementFromName(const std::string& name, MicPlacement& out)
{
    const std::string k = normaliseKey(name);
    for (MicPlacement p : { MicPlacement::Close, MicPlacement::Overhead, MicPlacement::Room, MicPlacement::Custom })
        if (k == normaliseKey(micPlacementName(p))) { out = p; return true; }
    return false;
}

double placementDistanceMetres(MicPlacement p)
{
    switch (p) {
        case MicPlacement::Close: return 0.1;
        case MicPlacement::Overhead: return 1.0;
        case MicPlacement::Room: return 3.0;
        case MicPlacement::Custom: return 0.0;
    }
    return 0.0;
}

double acousticDelayMs(double metres) { return std::max(0.0, metres) / kSpeedOfSound * 1000.0; }

RoleLatency RoleLatency::defaultsFor(InputRole role)
{
    RoleLatency r;
    if (role == InputRole::Overhead) r.placement = MicPlacement::Overhead;
    if (role == InputRole::Trigger) { r.distanceMetres = 0.0; r.moduleLatencyMs = kDefaultModuleLatencyMs; }
    return r;
}

LatencyModel::LatencyModel()
{
    for (int i = 0; i < kNumInputRoles; ++i) roles[i] = RoleLatency::defaultsFor((InputRole) i);
}

double LatencyModel::roleDelaySamples(InputRole role) const
{
    const int i = (int) role;
    if (i < 0 || i >= kNumInputRoles) return 0.0;
    const RoleLatency& r = roles[i];
    // Trigger (and MIDI) roles have no acoustic path, only the module latency.
    const double ms = (role == InputRole::Trigger ? 0.0 : acousticDelayMs(r.distance())) + r.moduleLatencyMs;
    return ms * 0.001 * sampleRate;
}

double LatencyModel::correctionSamples(InputRole role) const
{
    return detectorDelaySamples + inputLatencySamples() + roleDelaySamples(role);
}

int64_t LatencyModel::correct(int64_t raw, InputRole role) const
{
    return raw - (int64_t) std::llround(correctionSamples(role));
}

// ---- Section 4 ----

int LoopbackConfig::clickSamples() const { return std::max(1, (int) std::lround(clickMs * 0.001 * sampleRate)); }

int64_t LoopbackConfig::clickOnsetSample(int k) const
{
    return (int64_t) std::llround((leadInMs + k * spacingMs) * 0.001 * sampleRate);
}

int64_t LoopbackConfig::totalSamples() const
{
    return clickOnsetSample(numClicks - 1) + clickSamples()
         + (int64_t) std::llround((maxRoundTripMs + windowMs) * 0.001 * sampleRate);
}

std::vector<float> makeClickTemplate(double sampleRate, double clickMs)
{
    const int n = std::max(1, (int) std::lround(clickMs * 0.001 * sampleRate));
    std::vector<float> t((size_t) n);
    uint32_t s = 0x1234567u;
    for (auto& v : t) { s = s * 1664525u + 1013904223u; v = (s >> 31) ? 1.0f : -1.0f; }
    return t;
}

std::vector<float> renderLoopbackStimulus(const LoopbackConfig& cfg)
{
    std::vector<float> out((size_t) cfg.totalSamples(), 0.0f);
    const auto click = makeClickTemplate(cfg.sampleRate, cfg.clickMs);
    for (int k = 0; k < cfg.numClicks; ++k) {
        const int64_t at = cfg.clickOnsetSample(k);
        for (size_t m = 0; m < click.size() && at + (int64_t) m < (int64_t) out.size(); ++m) out[(size_t) at + m] = click[m];
    }
    return out;
}

LoopbackResult analyseLoopback(const float* x, size_t n, const LoopbackConfig& cfg,
                               double reportedIn, double reportedOut, HiddenSplit split)
{
    LoopbackResult r;
    r.reportedSumSamples = reportedIn + reportedOut;
    const auto t = makeClickTemplate(cfg.sampleRate, cfg.clickMs);
    const int64_t M = (int64_t) t.size();
    double et = 0;
    for (float v : t) et += (double) v * v;
    auto corr = [&](int64_t at) {
        if (at < 0 || at + M > (int64_t) n) return 0.0;
        double c = 0;
        for (int64_t m = 0; m < M; ++m) c += (double) t[(size_t) m] * x[at + m];
        return c;
    };
    if (!x || n == 0) { r.message = "no capture"; return r; }

    // Coarse lag: sum of |correlation| over all clicks (polarity independent).
    const int64_t maxLag = (int64_t) std::llround(cfg.maxRoundTripMs * 0.001 * cfg.sampleRate);
    int64_t coarse = 0;
    double best = -1;
    for (int64_t L = 0; L <= maxLag; ++L) {
        double s = 0;
        for (int k = 0; k < cfg.numClicks; ++k) s += std::fabs(corr(cfg.clickOnsetSample(k) + L));
        if (s > best) { best = s; coarse = L; }
    }

    // Fine search per click in a window centred on the coarse lag (section 4 step 3).
    const int64_t win = (int64_t) std::llround(cfg.windowMs * 0.001 * cfg.sampleRate);
    std::vector<double> lags;
    for (int k = 0; k < cfg.numClicks; ++k) {
        const int64_t e = cfg.clickOnsetSample(k);
        const int64_t w0 = std::max<int64_t>(e, e + coarse - win / 2);
        double pk = 0;
        int64_t at = -1;
        for (int64_t j = 0; j + M <= win; ++j) {
            const double c = std::fabs(corr(w0 + j));
            if (c > pk) { pk = c; at = w0 + j; }
        }
        if (at < 0) continue;
        double ex = 0;
        for (int64_t m = 0; m < M; ++m) ex += (double) x[at + m] * x[at + m];
        const double ncc = ex > 0 ? pk / std::sqrt(et * ex) : 0.0;
        if (ncc < cfg.detectThreshold) continue;
        r.lags.push_back((int) (at - e));
        lags.push_back((double) (at - e));
    }
    r.clicksDetected = (int) lags.size();
    if (lags.empty()) { r.message = "no clicks detected: check the loopback patch"; return r; }

    // Median, reject outliers, median of the rest (step 3).
    const double med = medianOf(lags);
    std::vector<double> kept;
    for (double l : lags) if (std::fabs(l - med) <= cfg.outlierSamples) kept.push_back(l);
    r.clicksKept = (int) kept.size();
    r.roundTripSamples = medianOf(kept);
    if (!kept.empty())
        r.spreadSamples = (int) (*std::max_element(kept.begin(), kept.end()) - *std::min_element(kept.begin(), kept.end()));

    // Step 4: hidden may be negative when the driver over-reports.
    r.hiddenSamples = r.roundTripSamples - r.reportedSumSamples;
    if (split == HiddenSplit::AllOnInput) { r.hiddenInputSamples = r.hiddenSamples; r.hiddenOutputSamples = 0; }
    else { r.hiddenInputSamples = r.hiddenSamples * 0.5; r.hiddenOutputSamples = r.hiddenSamples - r.hiddenInputSamples; }

    // Step 5: pass/fail.
    r.pass = r.clicksKept >= cfg.minClicks && r.spreadSamples <= cfg.maxSpreadSamples;
    char b[200];
    std::snprintf(b, sizeof b, "%s: round trip %.1f, reported %.1f, hidden %.1f samples (%d/%d clicks, spread %d)",
                  r.pass ? "PASS" : "FAIL", r.roundTripSamples, r.reportedSumSamples, r.hiddenSamples,
                  r.clicksKept, cfg.numClicks, r.spreadSamples);
    r.message = b;
    return r;
}

LoopbackCalibrator::LoopbackCalibrator(const LoopbackConfig& cfg)
    : cfg_(cfg), stimulus_(renderLoopbackStimulus(cfg)), capture_(stimulus_.size(), 0.0f)
{
}

void LoopbackCalibrator::start()
{
    state_.store(State::Idle, std::memory_order_release);
    std::fill(capture_.begin(), capture_.end(), 0.0f);
    pos_.store(0, std::memory_order_relaxed);
    state_.store(State::Running, std::memory_order_release);
}

void LoopbackCalibrator::cancel() { state_.store(State::Idle, std::memory_order_release); }

double LoopbackCalibrator::progress() const
{
    if (state() == State::Captured) return 1.0;
    return capture_.empty() ? 0.0 : (double) pos_.load(std::memory_order_relaxed) / (double) capture_.size();
}

void LoopbackCalibrator::process(const float* in, float* out, int numSamples)
{
    if (state_.load(std::memory_order_acquire) != State::Running) return;
    const int64_t total = (int64_t) capture_.size();
    int64_t p = pos_.load(std::memory_order_relaxed);
    for (int i = 0; i < numSamples; ++i, ++p) {
        const bool inside = p < total;
        if (inside) capture_[(size_t) p] = in ? in[i] : 0.0f;
        if (out) out[i] = inside ? stimulus_[(size_t) p] : 0.0f;
    }
    pos_.store(p, std::memory_order_relaxed);
    if (p >= total) state_.store(State::Captured, std::memory_order_release);
}

LoopbackResult LoopbackCalibrator::analyse(double reportedIn, double reportedOut, HiddenSplit split) const
{
    if (state() != State::Captured) { LoopbackResult r; r.message = "capture not complete"; return r; }
    return analyseLoopback(capture_.data(), capture_.size(), cfg_, reportedIn, reportedOut, split);
}

// ---- Section 5 ----

TapAlongCalibrator::TapAlongCalibrator(double sampleRate, int beatsPerBar, int bars, bool clickLoopback)
    : sr_(sampleRate), beatsPerBar_(std::max(1, beatsPerBar)), bars_(std::max(1, bars)), clickLoopback_(clickLoopback)
{
}

void TapAlongCalibrator::reset() { started_ = false; count_ = 0; startPpq_ = lastPpq_ = 0; }

void TapAlongCalibrator::setHostPosition(int64_t blockStartSample, double ppq, double bpm, bool playing)
{
    blockSample_ = blockStartSample; blockPpq_ = ppq; playing_ = playing;
    if (bpm > 0) bpm_ = bpm;
    if (started_ && playing) lastPpq_ = std::max(lastPpq_, ppq);
}

void TapAlongCalibrator::addOnset(int64_t sample)
{
    if (!playing_ || count_ >= kMaxOnsets) return;
    const double ppq = blockPpq_ + (double) (sample - blockSample_) / sr_ * bpm_ / 60.0;
    if (!started_) { started_ = true; startPpq_ = lastPpq_ = std::floor(ppq + 0.5) - 0.5; }
    if (ppq - startPpq_ > beatsPerBar_ * bars_) return;
    lastPpq_ = std::max(lastPpq_, ppq);
    errorsMs_[count_++] = (ppq - std::floor(ppq + 0.5)) * 60000.0 / bpm_;
}

double TapAlongCalibrator::beatsCovered() const { return started_ ? lastPpq_ - startPpq_ : 0.0; }
bool TapAlongCalibrator::complete() const { return beatsCovered() >= beatsPerBar_ * bars_; }

TapAlongResult TapAlongCalibrator::result() const
{
    TapAlongResult r;
    r.clickLoopback = clickLoopback_;
    r.count = count_;
    std::vector<double> e(errorsMs_, errorsMs_ + count_);
    if (e.size() < 4) { r.message = "not enough hits"; return r; }
    r.valid = true;
    r.medianErrorMs = medianOf(e);
    r.medianErrorSamples = r.medianErrorMs * 0.001 * sr_;
    for (auto& v : e) v = std::fabs(v - r.medianErrorMs);
    r.madMs = medianOf(e);
    char b[160];
    if (clickLoopback_)
        std::snprintf(b, sizeof b, "DAW input latency %.1f ms", r.medianErrorMs);
    else
        std::snprintf(b, sizeof b, "Your hits land %.1f ms %s the DAW beat", std::fabs(r.medianErrorMs),
                      r.medianErrorMs >= 0 ? "after" : "before");
    r.message = b;
    return r;
}

// ---- Section 6 ----

ClockOffsetResult measureHardwareClockOffset(const std::vector<int64_t>& clicks, const std::vector<int64_t>& hitsIn,
                                             double sampleRate, double maxPairMs)
{
    ClockOffsetResult r;
    if (sampleRate <= 0) return r;
    std::vector<int64_t> hits(hitsIn);
    std::sort(hits.begin(), hits.end());
    std::vector<double> d;
    for (int64_t c : clicks) {
        auto it = std::lower_bound(hits.begin(), hits.end(), c);
        double bestMs = 1e18;
        if (it != hits.end()) bestMs = (double) (c - *it) / sampleRate * 1000.0;
        if (it != hits.begin()) {
            const double ms = (double) (c - *(it - 1)) / sampleRate * 1000.0;
            if (std::fabs(ms) < std::fabs(bestMs)) bestMs = ms;
        }
        if (std::fabs(bestMs) <= maxPairMs) d.push_back(bestMs);
    }
    r.pairs = (int) d.size();
    if (r.pairs < 4) return r;
    r.valid = true;
    r.medianOffsetMs = medianOf(d);
    std::sort(d.begin(), d.end());
    r.spreadMs = d[d.size() * 3 / 4] - d[d.size() / 4];
    r.suggestedMidiClockOffsetMs = -r.medianOffsetMs;
    return r;
}

}} // namespace pacemaker::core
