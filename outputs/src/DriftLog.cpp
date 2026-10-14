#include "pacemaker/DriftLog.h"
#include "pacemaker/Settings.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace pacemaker {

void DriftLog::add(const DriftRow& r)
{
    rows_.push_back(r);
    if (rows_.size() > kMaxRows) rows_.pop_front();
}

void DriftLog::setSongName(int index, const std::string& n)
{
    for (auto& p : songNames_) if (p.first == index) { p.second = n; return; }
    songNames_.push_back({ index, n });
}

std::string DriftLog::toCsv() const
{
    std::string s = "timestampUs,bpm,confidence,state,bar,beat,event\n";
    char b[160];
    for (const auto& r : rows_) {
        std::snprintf(b, sizeof b, "%lld,%.3f,%.3f,%s,%d,%d,%c\n", (long long) r.timestampUs, r.bpm, (double) r.confidence,
                      stateName((EngineState) r.state), r.bar, r.beat, r.event);
        s += b;
    }
    return s;
}

Json DriftLog::report() const
{
    struct Acc { double minB = 1e9, maxB = 0, sum = 0; int n = 0, relocks = 0; double hold = 0; int64_t lastT = 0; int lastState = -1;
                 std::vector<double> bpms; };
    std::map<int, Acc> acc;
    for (const auto& r : rows_) {
        Acc& a = acc[r.song];
        if (a.lastState == (int) EngineState::Hold && r.timestampUs > a.lastT) a.hold += (double) (r.timestampUs - a.lastT) * 1e-6;
        a.lastT = r.timestampUs; a.lastState = r.state;
        if (r.event == 'r') ++a.relocks;
        if (r.event != 'b' || r.bpm <= 0) continue;
        a.minB = std::min(a.minB, r.bpm); a.maxB = std::max(a.maxB, r.bpm); a.sum += r.bpm; ++a.n; a.bpms.push_back(r.bpm);
    }
    Json out = Json::array();
    for (auto& kv : acc) {
        Acc& a = kv.second;
        Json o = Json::object();
        std::string name = kv.first < 0 ? "(no song)" : "song " + std::to_string(kv.first + 1);
        for (const auto& p : songNames_) if (p.first == kv.first) name = p.second;
        const double mean = a.n ? a.sum / a.n : 0.0;
        double maxDev = 0;
        for (double b : a.bpms) maxDev = std::max(maxDev, std::fabs(b - mean));
        o["name"] = name; o["minBpm"] = a.n ? a.minB : 0.0; o["maxBpm"] = a.maxB; o["meanBpm"] = mean;
        o["maxDeviationBpm"] = maxDev; o["secondsInHold"] = a.hold; o["relocks"] = a.relocks; o["beats"] = a.n;
        out.push(o);
    }
    return out;
}

std::string DriftLog::tempoMapMidi() const
{
    auto be32 = [](std::string& s, uint32_t v) { for (int i = 3; i >= 0; --i) s += (char) ((v >> (8 * i)) & 0xFF); };
    auto vlq = [](std::string& s, uint32_t v) {
        char tmp[5]; int n = 0; tmp[n++] = (char) (v & 0x7F);
        while ((v >>= 7) != 0) tmp[n++] = (char) (0x80 | (v & 0x7F));
        while (n--) s += tmp[n];
    };
    std::string trk;
    double lastBpm = 0.0;
    int64_t t0 = -1, prevUs = 0;
    uint32_t prevTick = 0;
    double ticks = 0.0;
    for (const auto& r : rows_) {
        if (r.event != 'b' || r.bpm < 20.0) continue;
        if (t0 < 0) t0 = r.timestampUs;
        else ticks += (double) (r.timestampUs - prevUs) * 1e-6 * (lastBpm / 60.0) * 480.0;   // at the tempo in force
        prevUs = r.timestampUs;
        if (std::fabs(r.bpm - lastBpm) > 0.05) {
            const uint32_t tick = (uint32_t) std::llround(ticks);
            vlq(trk, tick - prevTick); prevTick = tick;
            const uint32_t usq = (uint32_t) std::llround(60e6 / r.bpm);
            trk += "\xFF\x51\x03";
            trk += (char) ((usq >> 16) & 0xFF); trk += (char) ((usq >> 8) & 0xFF); trk += (char) (usq & 0xFF);
            lastBpm = r.bpm;
        }
    }
    trk += std::string("\x00\xFF\x2F\x00", 4);
    std::string f = "MThd";
    be32(f, 6); f += std::string("\x00\x00\x00\x01\x01\xE0", 6);   // format 0, 1 track, 480 ppq
    f += "MTrk"; be32(f, (uint32_t) trk.size()); f += trk;
    return f;
}

} // namespace pacemaker
