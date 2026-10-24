#include "pacemaker/Calibration.h"
#include <algorithm>
#include <cmath>

namespace pacemaker {

double distancePreset(const std::string& n)
{
    if (n == "overhead") return 1.0;
    if (n == "room") return 3.0;
    return 0.1;
}

std::vector<float> loopbackClick(double sr)
{
    const int len = std::max(8, (int) std::lround(0.001 * sr));
    std::vector<float> c((size_t) len);
    for (int i = 0; i < len; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * (i + 0.5) / len);
        c[(size_t) i] = (float) (w * (i % 2 == 0 ? 1.0 : -0.6));   // broadband-ish: sharp autocorrelation peak
    }
    return c;
}

LoopbackPlan planLoopback(double sr, int clicks, double spacing, double lead)
{
    LoopbackPlan p;
    for (int i = 0; i < clicks; ++i) p.clickStarts.push_back((int64_t) std::llround((lead + i * spacing) * sr));
    p.totalSamples = (int64_t) std::llround((lead + clicks * spacing + 0.25) * sr);
    return p;
}

void renderLoopbackTrain(const LoopbackPlan& plan, double sr, std::vector<float>& out)
{
    out.assign((size_t) plan.totalSamples, 0.0f);
    const auto c = loopbackClick(sr);
    for (int64_t s : plan.clickStarts)
        for (size_t i = 0; i < c.size() && s + (int64_t) i < plan.totalSamples; ++i) out[(size_t) s + i] = c[i];
}

LoopbackResult analyseLoopback(const std::vector<float>& in, const LoopbackPlan& plan, double sr, double repIn, double repOut)
{
    LoopbackResult r;
    const auto tpl = loopbackClick(sr);
    const int64_t T = (int64_t) tpl.size();
    const int64_t maxLag = (int64_t) std::lround(0.020 * sr);    // 20 ms search window after the click
    std::vector<double> lags;
    double tplEnergy = 0; for (float v : tpl) tplEnergy += (double) v * v;
    for (int64_t start : plan.clickStarts) {
        double best = 0, second = 0; int64_t bestLag = -1;
        double winEnergy = 0;
        for (int64_t i = 0; i < T && start + i < (int64_t) in.size(); ++i) winEnergy += (double) in[(size_t) (start + i)] * in[(size_t) (start + i)];
        for (int64_t lag = 0; lag <= maxLag; ++lag) {
            if (lag > 0) {   // slide the energy window
                const int64_t add = start + lag + T - 1, rem = start + lag - 1;
                if (add < (int64_t) in.size()) winEnergy += (double) in[(size_t) add] * in[(size_t) add];
                if (rem >= 0 && rem < (int64_t) in.size()) winEnergy -= (double) in[(size_t) rem] * in[(size_t) rem];
            }
            if (start + lag + T > (int64_t) in.size()) break;
            double dot = 0;
            for (int64_t i = 0; i < T; ++i) dot += (double) in[(size_t) (start + lag + i)] * tpl[(size_t) i];
            const double score = dot / (std::sqrt(std::max(winEnergy, 1e-12) * tplEnergy));   // normalised correlation
            if (score > best) { second = best; best = score; bestLag = lag; }
            else if (score > second && std::llabs(lag - bestLag) > 2) second = score;
        }
        // Accept only a clear, sharp peak: correlation above 0.5.
        if (bestLag >= 0 && best > 0.5) {
            // Parabolic refinement is skipped on purpose: the spec asks for +-1 sample.
            lags.push_back((double) bestLag);
        }
    }
    r.detected = (int) lags.size();
    if (lags.empty()) { r.message = "No clicks detected. Check the patch and levels."; return r; }
    std::vector<double> sorted = lags; std::sort(sorted.begin(), sorted.end());
    double med = sorted[sorted.size() / 2];
    if (sorted.size() % 2 == 0) med = 0.5 * (sorted[sorted.size() / 2 - 1] + sorted[sorted.size() / 2]);
    std::vector<double> kept;
    for (double l : lags) if (std::fabs(l - med) <= 2.0) kept.push_back(l);   // reject outliers
    if (kept.empty()) { r.message = "Inconsistent clicks."; return r; }
    std::sort(kept.begin(), kept.end());
    r.roundTripSamples = kept[kept.size() / 2];
    if (kept.size() % 2 == 0) r.roundTripSamples = std::floor(0.5 * (kept[kept.size() / 2 - 1] + kept[kept.size() / 2]) + 0.5);
    r.spreadSamples = kept.back() - kept.front();
    r.hiddenSamples = r.roundTripSamples - (repIn + repOut);
    r.ok = r.detected >= 6 && r.spreadSamples <= 4.0;
    r.message = r.ok ? "Calibration complete." : (r.detected < 6 ? "Too few clicks detected." : "Timing spread too large; try a larger buffer or check the cable.");
    return r;
}

TapAlongResult tapAlong(const std::vector<double>& onsets, double bpm, double origin)
{
    TapAlongResult r;
    if (bpm <= 0) return r;
    const double period = 60.0 / bpm;
    std::vector<double> errs;
    for (double t : onsets) {
        const double k = std::round((t - origin) / period);
        const double e = t - (origin + k * period);
        if (std::fabs(e) < 0.25 * period) errs.push_back(e * 1000.0);   // ignore off-beat hits
    }
    r.used = (int) errs.size();
    if (errs.size() < 8) return r;
    std::sort(errs.begin(), errs.end());
    r.medianMs = errs[errs.size() / 2];
    r.ok = true;
    return r;
}

double hardwareClockOffsetMs(const std::vector<double>& hw, const std::vector<double>& hits)
{
    std::vector<double> d;
    for (double h : hits) {
        double best = 1e9;
        for (double c : hw) if (std::fabs(c - h) < std::fabs(best)) best = c - h;
        if (std::fabs(best) < 0.3) d.push_back(best * 1000.0);
    }
    if (d.empty()) return 0.0;
    std::sort(d.begin(), d.end());
    return -d[d.size() / 2];
}

Json CalibrationStore::toJson() const
{
    Json o = Json::object();
    for (const auto& kv : table_) { Json e = Json::object(); e["in"] = kv.second.hiddenInputSamples; e["out"] = kv.second.hiddenOutputSamples; o[kv.first] = e; }
    return o;
}
void CalibrationStore::fromJson(const Json& j)
{
    table_.clear();
    if (!j.isObject()) return;
    for (const auto& kv : j.members()) table_[kv.first] = { kv.second.get("in").asNumber(), kv.second.get("out").asNumber() };
}

} // namespace pacemaker
