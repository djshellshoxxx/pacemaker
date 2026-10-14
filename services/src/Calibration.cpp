#include "pacemaker/Calibration.h"
#include <algorithm>
#include <cmath>

namespace pacemaker {

int roleDelaySamples(InputRole role, double distanceMetres, double moduleLatencyMs, double sampleRate)
{
    if (role == InputRole::Trigger) return (int) std::lround(moduleLatencyMs * 0.001 * sampleRate);
    return (int) std::lround(distanceMetres / kSpeedOfSound * sampleRate);
}

std::vector<float> makeClickTemplate(double sampleRate)
{
    const int n = std::max(8, (int) std::lround(0.001 * sampleRate));
    std::vector<float> t((size_t) n);
    uint32_t x = 0x1234567u;
    for (int i = 0; i < n; ++i) {
        x = x * 1664525u + 1013904223u;
        const float noise = (float) ((x >> 8) & 0xFFFF) / 32768.0f - 1.0f;
        const float w = 0.5f - 0.5f * std::cos(2.0f * 3.14159265f * (float) (i + 1) / (float) (n + 1));
        t[(size_t) i] = noise * w;
    }
    float peak = 0; for (float v : t) peak = std::max(peak, std::fabs(v));
    for (float& v : t) v /= peak;   // 0 dBFS
    return t;
}

LoopbackResult analyzeLoopback(const float* rec, int64_t numRec, const std::vector<int64_t>& starts,
                               double sampleRate, int reportedIn, int reportedOut, bool allOnInput)
{
    LoopbackResult r;
    r.clicksTotal = (int) starts.size();
    r.reportedSumSamples = reportedIn + reportedOut;
    const std::vector<float> tmpl = makeClickTemplate(sampleRate);
    const int T = (int) tmpl.size();
    double tEnergy = 0; for (float v : tmpl) tEnergy += (double) v * v;
    const int win = (int) std::lround(0.040 * sampleRate);      // lag search range (spec: 20 ms; widened for slow drivers)

    std::vector<int> lags;
    std::vector<double> corr((size_t) win);
    for (int64_t c : starts) {
        if (c < 0 || c + win + T > numRec) continue;
        const float* x = rec + c;
        double best = -1e30; int bestLag = -1;
        for (int lag = 0; lag < win; ++lag) {
            double acc = 0;
            for (int i = 0; i < T; ++i) acc += (double) x[lag + i] * tmpl[(size_t) i];
            corr[(size_t) lag] = acc;
            if (acc > best) { best = acc; bestLag = lag; }
        }
        if (bestLag < 0 || best <= 0) continue;
        // Peak to sidelobe ratio over everything outside +-T samples of the peak.
        double ss = 0; int cnt = 0;
        for (int lag = 0; lag < win; ++lag) {
            if (std::abs(lag - bestLag) <= T) continue;
            ss += corr[(size_t) lag] * corr[(size_t) lag]; ++cnt;
        }
        const double rms = cnt ? std::sqrt(ss / cnt) : 0.0;
        if (rms > 0 && best / rms < 4.0) continue;               // buried in noise
        if (best < 0.05 * tEnergy) continue;                      // too quiet to be our click
        // Parabolic refinement is only used to round to the nearest sample.
        double frac = 0;
        if (bestLag > 0 && bestLag < win - 1) {
            const double a = corr[(size_t) bestLag - 1], b = corr[(size_t) bestLag], d = corr[(size_t) bestLag + 1];
            const double den = a - 2 * b + d;
            if (den < 0) frac = 0.5 * (a - d) / den;
        }
        lags.push_back((int) std::lround((double) bestLag + frac));
    }
    r.clicksDetected = (int) lags.size();
    if (lags.empty()) { r.message = "No clicks detected. Check the patch cable and levels."; return r; }

    std::vector<int> sorted = lags; std::sort(sorted.begin(), sorted.end());
    int med = sorted[sorted.size() / 2];
    std::vector<int> keep;
    for (int l : lags) if (std::abs(l - med) <= 2) keep.push_back(l);
    std::sort(keep.begin(), keep.end());
    med = keep[keep.size() / 2];
    r.roundTripSamples = med;
    r.spreadSamples = (double) (keep.back() - keep.front());
    r.hiddenSamples = med - r.reportedSumSamples;
    r.hiddenInputSamples = allOnInput ? r.hiddenSamples : r.hiddenSamples / 2;
    r.hiddenOutputSamples = r.hiddenSamples - r.hiddenInputSamples;
    r.pass = (int) keep.size() >= 6 && r.spreadSamples <= 4.0;
    r.message = r.pass ? "Calibration OK." : "Calibration unreliable: fewer than 6 clean clicks or spread above 4 samples.";
    return r;
}

// ---------------------------------------------------------------- capture
void LoopbackCapture::prepare(double sampleRate)
{
    sr_ = sampleRate;
    tmpl_ = makeClickTemplate(sampleRate);
    spacing_ = (int64_t) std::llround(0.5 * sampleRate);
    total_ = spacing_ * (kClicks + 1) + (int64_t) std::llround(0.1 * sampleRate);
    rec_.assign((size_t) total_, 0.0f);
    starts_.clear(); starts_.reserve(kClicks);
    state_ = 0;
}

void LoopbackCapture::start()
{
    std::fill(rec_.begin(), rec_.end(), 0.0f);
    starts_.clear();
    pos_ = 0; nextClick_ = 0;
    state_ = rec_.empty() ? 0 : 1;
}

void LoopbackCapture::process(const float* in, float* out, int n)
{
    if (state_ != 1) return;
    for (int i = 0; i < n && pos_ < total_; ++i, ++pos_) {
        rec_[(size_t) pos_] = in ? in[i] : 0.0f;
        // Click k starts at (k + 1) * spacing so that the first half second is a silent noise-floor lead-in.
        if (nextClick_ < kClicks && pos_ == (int64_t) (nextClick_ + 1) * spacing_) {
            starts_.push_back(pos_);
            ++nextClick_;
            clickLeft_ = (int) tmpl_.size(); clickIdx_ = 0;
        }
        if (clickLeft_ > 0 && out) { out[i] += tmpl_[(size_t) clickIdx_]; }
        if (clickLeft_ > 0) { ++clickIdx_; --clickLeft_; }
    }
    if (pos_ >= total_) state_ = 2;
}

// ---------------------------------------------------------------- storage
std::string CalibrationTable::key(const std::string& device, double sampleRate, int bufferSize)
{
    return device + "|" + std::to_string((int) std::lround(sampleRate)) + "|" + std::to_string(bufferSize);
}
bool CalibrationTable::get(const std::string& k, CalibrationEntry& e) const
{
    auto it = entries.find(k);
    if (it == entries.end()) return false;
    e = it->second; return true;
}
Json CalibrationTable::toJson() const
{
    Json o = Json::object();
    for (const auto& kv : entries) {
        Json e = Json::object();
        e.set("hiddenInputSamples", kv.second.hiddenInputSamples);
        e.set("hiddenOutputSamples", kv.second.hiddenOutputSamples);
        e.set("measuredAt", (int64_t) kv.second.measuredAt);
        o.set(kv.first, e);
    }
    return o;
}
CalibrationTable CalibrationTable::fromJson(const Json& j)
{
    CalibrationTable t;
    for (const auto& kv : j.members()) {
        CalibrationEntry e;
        e.hiddenInputSamples = kv.second["hiddenInputSamples"].getInt();
        e.hiddenOutputSamples = kv.second["hiddenOutputSamples"].getInt();
        e.measuredAt = (int64_t) kv.second["measuredAt"].getNumber();
        t.entries[kv.first] = e;
    }
    return t;
}

// ---------------------------------------------------------------- tap along
void TapAlong::onBlock(bool playing, double ppq, double bpm, int64_t sample, double sr)
{
    playing_ = playing; ppq0_ = ppq; bpm_ = bpm; sample0_ = sample; sr_ = sr;
    hostValid_ = playing && bpm > 0;
}

void TapAlong::onOnset(int64_t engineSample)
{
    if (!hostValid_ || errorsMs_.size() >= 4096) return;
    const double beats = ppq0_ + (double) (engineSample - sample0_) / sr_ * (bpm_ / 60.0);
    const double nearest = std::round(beats);
    const double errBeats = beats - nearest;
    if (std::fabs(errBeats) > 0.25) return;       // not near a beat: a sub-beat note
    errorsMs_.push_back(errBeats * 60000.0 / bpm_);
}

double TapAlong::medianErrorMs() const
{
    if (errorsMs_.empty()) return 0.0;
    std::vector<double> v = errorsMs_; std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// ---------------------------------------------------------------- hardware offset
bool HardwareOffsetEstimator::estimate(double sr, double periodMs, double& offsetMs) const
{
    std::vector<double> d;
    for (int64_t c : clicks_) {
        double best = 1e18; bool found = false;
        for (int64_t h : hits_) {
            const double dt = (double) (c - h) * 1000.0 / sr;
            if (std::fabs(dt) < std::fabs(best)) { best = dt; found = true; }
        }
        if (found && std::fabs(best) < 0.5 * periodMs) d.push_back(best);
    }
    if (d.size() < 8) return false;
    std::sort(d.begin(), d.end());
    offsetMs = d[d.size() / 2];
    return true;
}

} // namespace pacemaker
