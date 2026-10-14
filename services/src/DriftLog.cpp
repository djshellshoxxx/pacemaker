#include "pacemaker/DriftLog.h"
#include "pacemaker/Settings.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sstream>
#if __has_include(<filesystem>)
 #include <filesystem>
#endif

namespace pacemaker {

const char* stateName(int s)
{
    static const char* n[] = { "IDLE", "COUNT-IN", "LOCKING", "LOCKED", "HOLD", "CHASE" };
    return (s >= 0 && s < 6) ? n[s] : "?";
}

std::string driftHeader() { return "timestampUs,bpm,confidence,state,bar,beat,event\n"; }

std::string driftRowToCsv(const DriftRow& r)
{
    char b[160];
    std::snprintf(b, sizeof b, "%lld,%.3f,%.3f,%s,%d,%d,", (long long) r.timestampUs, r.bpm, r.confidence, stateName(r.state), r.bar, r.beat);
    std::string ev;
    for (char c : r.event) ev += (c == ',' || c == '\n' || c == '\r') ? ' ' : c;
    return std::string(b) + ev + "\n";
}

std::string DriftLogger::defaultDirectory()
{
#if defined(_WIN32)
    const char* h = std::getenv("USERPROFILE");
    return std::string(h ? h : ".") + "\\Documents\\Pacemaker";
#else
    const char* h = std::getenv("HOME");
    return std::string(h ? h : ".") + "/Documents/Pacemaker";
#endif
}

bool DriftLogger::open(const std::string& dirIn, std::string* pathOut)
{
    std::lock_guard<std::mutex> l(m_);
    if (open_) return true;
    const std::string dir = dirIn.empty() ? defaultDirectory() : dirIn;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::time_t t = std::time(nullptr);
    char name[64];
    std::tm tmv {};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    std::strftime(name, sizeof name, "pacemaker-%Y%m%d-%H%M%S.csv", &tmv);
    path_ = dir + "/" + name;
    f_.open(path_, std::ios::binary | std::ios::trunc);
    if (!f_) return false;
    f_ << driftHeader();
    open_ = true;
    if (pathOut) *pathOut = path_;
    return true;
}

void DriftLogger::close()
{
    std::lock_guard<std::mutex> l(m_);
    if (open_) { f_.flush(); f_.close(); open_ = false; }
}

void DriftLogger::write(const DriftRow& r)
{
    std::lock_guard<std::mutex> l(m_);
    if (!open_) return;
    f_ << driftRowToCsv(r);
    if (++sinceFlush_ >= 16 || !r.event.empty()) { f_.flush(); sinceFlush_ = 0; }
}

bool parseDriftCsv(const std::string& csv, std::vector<DriftRow>& rows)
{
    rows.clear();
    std::istringstream in(csv);
    std::string line;
    if (!std::getline(in, line) || line.rfind("timestampUs", 0) != 0) return false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::vector<std::string> f; std::string cur;
        for (char c : line) { if (c == ',') { f.push_back(cur); cur.clear(); } else cur += c; }
        f.push_back(cur);
        if (f.size() < 7) continue;
        DriftRow r;
        r.timestampUs = std::atoll(f[0].c_str()); r.bpm = std::atof(f[1].c_str()); r.confidence = std::atof(f[2].c_str());
        r.state = 0; for (int s = 0; s < 6; ++s) if (f[3] == stateName(s)) r.state = s;
        r.bar = std::atoi(f[4].c_str()); r.beat = std::atoi(f[5].c_str()); r.event = f[6];
        rows.push_back(r);
    }
    return true;
}

DriftReport analyzeDrift(const std::vector<DriftRow>& rows)
{
    DriftReport r;
    if (rows.empty()) return r;
    const int64_t t0 = rows.front().timestampUs;
    r.durationSec = (double) (rows.back().timestampUs - t0) * 1e-6;
    std::vector<double> bpms;
    for (size_t i = 0; i < rows.size(); ++i) {
        const DriftRow& d = rows[i];
        if (d.event == "relock") ++r.relockCount;
        const double dt = i + 1 < rows.size() ? (double) (rows[i + 1].timestampUs - d.timestampUs) * 1e-6 : 0.0;
        if (d.state == (int) EngineState::Hold) r.timeInHoldSec += dt;
        if (d.state == (int) EngineState::Locked || d.state == (int) EngineState::Chase) r.timeLockedSec += dt;
        if (d.event.empty() || d.event == "beat") {
            if (d.bpm > 0 && (d.state == (int) EngineState::Locked || d.state == (int) EngineState::Chase || d.state == (int) EngineState::Hold)) {
                bpms.push_back(d.bpm); ++r.beats;
            }
        }
    }
    if (!bpms.empty()) {
        double sum = 0; for (double b : bpms) sum += b;
        r.meanBpm = sum / (double) bpms.size();
        std::vector<double> s = bpms; std::sort(s.begin(), s.end());
        r.medianBpm = s[s.size() / 2]; r.minBpm = s.front(); r.maxBpm = s.back();
        for (double b : bpms) r.maxDeviationBpm = std::max(r.maxDeviationBpm, std::fabs(b - r.medianBpm));
    }
    // Downsample to at most 400 points.
    const size_t step = std::max<size_t>(1, rows.size() / 400);
    for (size_t i = 0; i < rows.size(); i += step)
        if (rows[i].bpm > 0) r.curve.emplace_back((double) (rows[i].timestampUs - t0) * 1e-6, rows[i].bpm);
    return r;
}

std::string renderDriftReportHtml(const DriftReport& r, const std::string& title)
{
    std::ostringstream o;
    o << "<!doctype html><html><head><meta charset=\"utf-8\"><title>" << title << "</title><style>"
         "body{background:#0b0f12;color:#d7e3ea;font:16px system-ui,sans-serif;margin:24px}"
         "h1{font-weight:600}table{border-collapse:collapse}td{padding:4px 16px 4px 0}"
         ".k{color:#7f98a6}svg{background:#0f161b;border:1px solid #1f2c35;margin-top:16px}</style></head><body>"
      << "<h1>" << title << "</h1><table>";
    auto row = [&](const char* k, const std::string& v) { o << "<tr><td class=\"k\">" << k << "</td><td>" << v << "</td></tr>"; };
    char b[64];
    std::snprintf(b, sizeof b, "%.1f s", r.durationSec); row("Duration", b);
    std::snprintf(b, sizeof b, "%.2f BPM (median %.2f)", r.meanBpm, r.medianBpm); row("Mean tempo", b);
    std::snprintf(b, sizeof b, "%.2f to %.2f BPM", r.minBpm, r.maxBpm); row("Range", b);
    std::snprintf(b, sizeof b, "%.2f BPM", r.maxDeviationBpm); row("Max deviation from median", b);
    std::snprintf(b, sizeof b, "%.1f s", r.timeInHoldSec); row("Time in Hold", b);
    std::snprintf(b, sizeof b, "%d", r.relockCount); row("Relocks", b);
    o << "</table>";
    if (r.curve.size() > 1) {
        double lo = 1e9, hi = -1e9, tmax = r.curve.back().first;
        for (auto& p : r.curve) { lo = std::min(lo, p.second); hi = std::max(hi, p.second); }
        if (hi - lo < 2.0) { const double m = 0.5 * (hi + lo); lo = m - 1.0; hi = m + 1.0; }
        const double W = 800, H = 240, pad = 30;
        o << "<svg width=\"" << W << "\" height=\"" << H << "\" viewBox=\"0 0 " << W << " " << H << "\"><polyline fill=\"none\" stroke=\"#3ddc97\" stroke-width=\"1.5\" points=\"";
        for (auto& p : r.curve) {
            const double x = pad + (tmax > 0 ? p.first / tmax : 0) * (W - 2 * pad);
            const double y = H - pad - (p.second - lo) / (hi - lo) * (H - 2 * pad);
            std::snprintf(b, sizeof b, "%.1f,%.1f ", x, y); o << b;
        }
        o << "\"/><text x=\"4\" y=\"" << pad << "\" fill=\"#7f98a6\" font-size=\"12\">" << (int) hi << "</text><text x=\"4\" y=\"" << H - pad
          << "\" fill=\"#7f98a6\" font-size=\"12\">" << (int) lo << "</text></svg>";
    }
    o << "</body></html>";
    return o.str();
}

// ---------------------------------------------------------------- SMF export
static void putVar(std::vector<uint8_t>& o, uint32_t v)
{
    uint8_t buf[5]; int n = 0;
    buf[n++] = v & 0x7F;
    while ((v >>= 7) > 0) buf[n++] = (uint8_t) ((v & 0x7F) | 0x80);
    while (n > 0) o.push_back(buf[--n]);
}
static void put32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 3; i >= 0; --i) o.push_back((uint8_t) (v >> (8 * i))); }
static void put16(std::vector<uint8_t>& o, uint16_t v) { o.push_back((uint8_t) (v >> 8)); o.push_back((uint8_t) v); }

std::vector<uint8_t> buildTempoMapMidi(const std::vector<DriftRow>& rows, int beatsPerBar, int beatUnit)
{
    const int ppq = 480;
    const int ticksPerBeat = beatUnit == 8 ? ppq / 2 : ppq;
    std::vector<uint8_t> trk;
    // Track name.
    const char* nm = "Pacemaker tempo map";
    putVar(trk, 0); trk.push_back(0xFF); trk.push_back(0x03); trk.push_back((uint8_t) std::strlen(nm));
    trk.insert(trk.end(), nm, nm + std::strlen(nm));
    // Time signature.
    putVar(trk, 0); trk.push_back(0xFF); trk.push_back(0x58); trk.push_back(4);
    trk.push_back((uint8_t) std::clamp(beatsPerBar, 1, 32)); trk.push_back(beatUnit == 8 ? 3 : 2); trk.push_back(24); trk.push_back(8);

    double lastBpm = -1;
    uint32_t lastTick = 0, tick = 0;
    int beatIdx = 0;
    bool first = true;
    for (const auto& r : rows) {
        if (!(r.event.empty() || r.event == "beat")) continue;
        if (r.bpm <= 0 || r.state < (int) EngineState::Locking) continue;
        if (first) { first = false; }
        else ++beatIdx;
        tick = (uint32_t) (beatIdx * ticksPerBeat);
        const double quarterBpm = beatUnit == 8 ? r.bpm * 0.5 : r.bpm;
        if (std::fabs(quarterBpm - lastBpm) > 0.05) {
            const uint32_t us = (uint32_t) std::lround(60000000.0 / std::max(1.0, quarterBpm));
            putVar(trk, tick - lastTick); lastTick = tick;
            trk.push_back(0xFF); trk.push_back(0x51); trk.push_back(3);
            trk.push_back((uint8_t) (us >> 16)); trk.push_back((uint8_t) (us >> 8)); trk.push_back((uint8_t) us);
            lastBpm = quarterBpm;
        }
    }
    if (lastBpm < 0) {   // nothing tracked: write a neutral 120 BPM so the file is still valid
        putVar(trk, 0); trk.push_back(0xFF); trk.push_back(0x51); trk.push_back(3);
        trk.push_back(0x07); trk.push_back(0xA1); trk.push_back(0x20);
    }
    putVar(trk, tick - lastTick); trk.push_back(0xFF); trk.push_back(0x2F); trk.push_back(0);

    std::vector<uint8_t> f;
    f.insert(f.end(), { 'M', 'T', 'h', 'd' }); put32(f, 6); put16(f, 0); put16(f, 1); put16(f, (uint16_t) ppq);
    f.insert(f.end(), { 'M', 'T', 'r', 'k' }); put32(f, (uint32_t) trk.size());
    f.insert(f.end(), trk.begin(), trk.end());
    return f;
}

bool exportTempoMapMidi(const std::vector<DriftRow>& rows, int beatsPerBar, int beatUnit, const std::string& path)
{
    const auto data = buildTempoMapMidi(rows, beatsPerBar, beatUnit);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write((const char*) data.data(), (std::streamsize) data.size());
    return (bool) f;
}

} // namespace pacemaker
