#include "pacemaker/Osc.h"
#include "pacemaker/Settings.h"
#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace pacemaker {

namespace {
void pad(std::vector<uint8_t>& v) { while (v.size() % 4) v.push_back(0); }
void putStr(std::vector<uint8_t>& v, const std::string& s) { v.insert(v.end(), s.begin(), s.end()); v.push_back(0); pad(v); }
void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 3; i >= 0; --i) v.push_back((uint8_t) (x >> (8 * i))); }
bool get32(const std::vector<uint8_t>& d, size_t& p, uint32_t& x)
{
    if (p + 4 > d.size()) return false;
    x = ((uint32_t) d[p] << 24) | ((uint32_t) d[p + 1] << 16) | ((uint32_t) d[p + 2] << 8) | d[p + 3]; p += 4; return true;
}
bool getStr(const std::vector<uint8_t>& d, size_t& p, std::string& s)
{
    size_t e = p;
    while (e < d.size() && d[e]) ++e;
    if (e >= d.size()) return false;
    s.assign((const char*) d.data() + p, e - p);
    p = (e + 4) & ~(size_t) 3;
    return p <= d.size();
}
bool decodeMessage(const std::vector<uint8_t>& d, size_t p, size_t end, std::vector<OscMessage>& out)
{
    std::vector<uint8_t> sub(d.begin() + (long) p, d.begin() + (long) end);
    size_t q = 0; OscMessage m; std::string tags;
    if (!getStr(sub, q, m.address) || !getStr(sub, q, tags) || tags.empty() || tags[0] != ',') return false;
    for (size_t i = 1; i < tags.size(); ++i) {
        uint32_t x;
        if (tags[i] == 'i') { if (!get32(sub, q, x)) return false; m.args.push_back(OscArg::I((int32_t) x)); }
        else if (tags[i] == 'f') { if (!get32(sub, q, x)) return false; float f; std::memcpy(&f, &x, 4); m.args.push_back(OscArg::F(f)); }
        else if (tags[i] == 's') { std::string s; if (!getStr(sub, q, s)) return false; m.args.push_back(OscArg::S(s)); }
        else return false;
    }
    out.push_back(std::move(m));
    return true;
}
} // namespace

std::vector<uint8_t> oscEncode(const OscMessage& m)
{
    std::vector<uint8_t> v;
    putStr(v, m.address);
    std::string tags = ",";
    for (const auto& a : m.args) tags += a.kind == OscArg::Int ? 'i' : a.kind == OscArg::Float ? 'f' : 's';
    putStr(v, tags);
    for (const auto& a : m.args) {
        if (a.kind == OscArg::Int) put32(v, (uint32_t) a.i);
        else if (a.kind == OscArg::Float) { uint32_t x; std::memcpy(&x, &a.f, 4); put32(v, x); }
        else putStr(v, a.s);
    }
    return v;
}

std::vector<uint8_t> oscBundle(uint64_t tt, const std::vector<OscMessage>& msgs)
{
    std::vector<uint8_t> v;
    putStr(v, "#bundle");
    put32(v, (uint32_t) (tt >> 32)); put32(v, (uint32_t) (tt & 0xFFFFFFFFu));
    for (const auto& m : msgs) { const auto e = oscEncode(m); put32(v, (uint32_t) e.size()); v.insert(v.end(), e.begin(), e.end()); }
    return v;
}

uint64_t ntpTimetag(int64_t hostUs)
{
    const int64_t us = hostUs + ntpOffsetUs();
    const uint64_t sec = (uint64_t) (us / 1000000), frac = (uint64_t) (us % 1000000);
    return (sec << 32) | ((frac << 32) / 1000000ull);
}

bool oscDecode(const std::vector<uint8_t>& d, std::vector<OscMessage>& out, uint64_t* timetag)
{
    if (d.size() < 4 || d.size() % 4) return false;
    if (d[0] != '#') return decodeMessage(d, 0, d.size(), out);
    size_t p = 8; uint32_t hi, lo;
    if (d.size() < 16 || std::memcmp(d.data(), "#bundle", 8) != 0) return false;
    if (!get32(d, p, hi) || !get32(d, p, lo)) return false;
    if (timetag) *timetag = ((uint64_t) hi << 32) | lo;
    while (p < d.size()) {
        uint32_t len;
        if (!get32(d, p, len) || len % 4 || p + len > d.size()) return false;
        if (!decodeMessage(d, p, p + len, out)) return false;
        p += len;
    }
    return true;
}

void OscGenerator::generate(const BeatMapSnapshot& s, int beatsPerBar, const ClockMap& map, int64_t now, std::vector<OscPacket>& out)
{
    const EngineState st = (EngineState) s.state;
    const BeatGrid g = gridFromSnapshot(s, beatsPerBar);
    auto push = [&](int64_t sendAt, uint64_t tt, std::vector<uint8_t> bytes, const char* label) {
        out.push_back({ sendAt, std::move(bytes), tt, label });
    };
    const int64_t offUs = (int64_t) std::llround(cfg_.offsetMs * 1000.0);
    const bool resolume = cfg_.profile == "resolume", magicq = cfg_.profile == "magicq";

    if ((int) st != lastState_) {
        lastState_ = (int) st;
        push(now, 0, oscEncode({ addr("/state"), { OscArg::S(stateName(st)) } }), "state");
    }
    if (st == EngineState::Idle || !g.valid()) return;

    if (std::fabs(s.bpm - lastBpm_) > 0.01 || now - lastTempoUs_ >= 1000000) {
        lastBpm_ = s.bpm; lastTempoUs_ = now;
        if (resolume) push(now, 0, oscEncode({ "/composition/tempocontroller/tempo", { OscArg::F((s.bpm - 20.0) / 480.0) } }), "tempo");
        else push(now, 0, oscEncode({ addr("/tempo"), { OscArg::F(s.bpm) } }), "tempo");
    }
    if (now - lastConfUs_ >= 100000) {
        lastConfUs_ = now;
        push(now, 0, oscEncode({ addr("/confidence"), { OscArg::F(s.confidence) } }), "confidence");
    }
    if (now - lastPhaseUs_ >= 10000) {
        lastPhaseUs_ = now;
        const double smp = map.sampleForHostTime((double) now - (double) offUs);
        push(now, 0, oscEncode({ addr("/phase"), { OscArg::F(g.barPhaseAt(smp)) } }), "phase");
    }
    if ((s.flags & SnapshotFlags::Relock) && s.sequence != lastRelockSeq_) {
        lastRelockSeq_ = s.sequence;
        push(now, 0, oscEncode({ addr("/relock"), { OscArg::I(s.barNumber), OscArg::I(s.beatInBar) } }), "relock");
    }
    // Beats: send each one once, lookahead before it is due, with the predicted time as the bundle timetag.
    const int64_t horizon = now + (int64_t) std::llround(cfg_.lookaheadMs * 1000.0);
    int64_t k = (int64_t) std::floor(g.beatAt(map.sampleForHostTime((double) (now - offUs))));
    for (;; ++k) {
        const int64_t t = (int64_t) std::llround(map.hostTimeForSample(g.sampleOfBeat(k))) + offUs;
        if (t > horizon) break;
        if (t < now - 2000) continue;
        const int bar = g.barAt(k), beat = g.beatInBarAt(k);
        const int64_t key = (int64_t) bar * 64 + beat;
        if (key <= lastBeatKey_) continue;
        lastBeatKey_ = key;
        std::vector<OscMessage> ms;
        ms.push_back({ addr("/beat"), { OscArg::I(bar), OscArg::I(beat), OscArg::I(g.beatsPerBar) } });
        if (beat == 1) {
            ms.push_back({ addr("/downbeat"), { OscArg::I(bar) } });
            if (resolume) ms.push_back({ "/composition/tempocontroller/resync", { OscArg::I(1) } });
        }
        if (magicq) ms.push_back({ cfg_.magicqTapAddress, { OscArg::I(1) } });
        const uint64_t tt = ntpTimetag(t);
        push(std::max(now, t - (int64_t) std::llround(cfg_.lookaheadMs * 1000.0)), tt, oscBundle(tt, ms), "beat");
    }
}

UdpSender::~UdpSender() { close(); }

bool UdpSender::open(const std::string& host, int port, std::string* error)
{
    close();
    addrinfo hints {}, *res = nullptr;
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        if (error) *error = "cannot resolve " + host;
        return false;
    }
    std::memcpy(addr_, res->ai_addr, std::min<size_t>(res->ai_addrlen, sizeof addr_));
    addrLen_ = (unsigned) res->ai_addrlen;
    freeaddrinfo(res);
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) { if (error) *error = "socket failed"; return false; }
    return true;
}
bool UdpSender::send(const std::vector<uint8_t>& b)
{
    if (fd_ < 0) return false;
    const auto n = ::sendto(fd_, b.data(), b.size(), 0, (const sockaddr*) addr_, addrLen_);
    if (n != (long) b.size()) { ++errors_; return false; }
    return true;
}
void UdpSender::close() { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }

OscRunner::OscRunner(const ClockMap& m, std::function<BeatMapSnapshot()> s, std::function<int()> b)
    : map_(m), snapshot_(std::move(s)), beatsPerBar_(std::move(b)) {}
OscRunner::~OscRunner() { stop(); }

bool OscRunner::start(const std::string& host, int port, const OscConfig& cfg, std::string* error)
{
    stop();
    if (!sender_.open(host, port, error)) { status_.error = error ? *error : "open failed"; status_.connected = false; return false; }
    cfg_ = cfg; status_.error.clear(); status_.connected = true; status_.enabled = true; running_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
}
void OscRunner::stop()
{
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
    sender_.close(); status_.enabled = false; status_.connected = false;
}

void OscRunner::run()
{
    OscGenerator gen(cfg_);
    std::vector<OscPacket> pending, fresh;
    while (running_.load()) {
        const int64_t now = steadyNowUs();
        fresh.clear();
        gen.generate(snapshot_(), beatsPerBar_(), map_, now, fresh);
        pending.insert(pending.end(), fresh.begin(), fresh.end());
        size_t i = 0;
        for (; i < pending.size(); ++i) {
            if (pending[i].sendAtUs > steadyNowUs()) continue;
            if (sender_.send(pending[i].bytes)) status_.sent++;
            else status_.connected = false;
            pending[i].bytes.clear();
        }
        pending.erase(std::remove_if(pending.begin(), pending.end(), [](const OscPacket& p) { return p.bytes.empty(); }), pending.end());
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

} // namespace pacemaker
