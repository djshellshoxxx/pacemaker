#include "pacemaker/core/OutputSettings.h"
#include <algorithm>
#include <cmath>

namespace pacemaker { namespace core {

namespace {
double clampFinite(double v, double lo, double hi, double fallback)
{
    return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
}
}

double clampOffsetMs(double ms) { return clampFinite(ms, -300.0, 300.0, 0.0); }
double clampLookaheadMs(double ms) { return clampFinite(ms, 0.0, 200.0, 30.0); }

void sanitize(LinkSettings& s)
{
    s.quantum = std::round(clampFinite(s.quantum, 1.0, 16.0, 4.0));
    s.confidenceGate = clampFinite(s.confidenceGate, 0.0, 1.0, 0.35);
    s.commitIntervalMs = clampFinite(s.commitIntervalMs, 10.0, 1000.0, 50.0);
    s.deadbandBpm = clampFinite(s.deadbandBpm, 0.0, 5.0, 0.05);
}

void sanitize(MidiClockSettings& s) { s.offsetMs = clampOffsetMs(s.offsetMs); }

void sanitize(OscSettings& s)
{
    s.port = std::clamp(s.port, 1, 65535);
    s.offsetMs = clampOffsetMs(s.offsetMs);
    if (s.root.empty() || s.root[0] != '/') s.root = "/" + s.root;
    while (s.root.size() > 1 && s.root.back() == '/') s.root.pop_back();
    if (s.root == "/") s.root.clear();   // messages then go to "/tempo" etc.
    if (s.magicQTapAddress.empty() || s.magicQTapAddress[0] != '/') s.magicQTapAddress = "/" + s.magicQTapAddress;
}

void sanitize(SyncSettings& s) { s.offsetMs = clampOffsetMs(s.offsetMs); }

void sanitize(OutputSettings& s)
{
    sanitize(s.link); sanitize(s.midi); sanitize(s.osc); sanitize(s.sync);
    s.lookaheadMs = clampLookaheadMs(s.lookaheadMs);
}

}} // namespace pacemaker::core
