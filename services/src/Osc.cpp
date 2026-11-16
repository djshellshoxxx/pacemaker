#include "pacemaker/Osc.h"
#include "pacemaker/MidiClock.h"
#include "pacemaker/Platform.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(_WIN32)
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <winsock2.h>
 #include <ws2tcpip.h>
 #pragma comment(lib, "ws2_32.lib")
#else
 #include <arpa/inet.h>
 #include <netdb.h>
 #include <sys/socket.h>
 #include <unistd.h>
#endif

namespace pacemaker {

namespace {
void pad4(std::vector<uint8_t>& o) { while (o.size() % 4) o.push_back(0); }
void putStr(std::vector<uint8_t>& o, const std::string& s) { o.insert(o.end(), s.begin(), s.end()); o.push_back(0); pad4(o); }
void put32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 3; i >= 0; --i) o.push_back((uint8_t) (v >> (8 * i))); }
void put64(std::vector<uint8_t>& o, uint64_t v) { for (int i = 7; i >= 0; --i) o.push_back((uint8_t) (v >> (8 * i))); }
uint32_t get32(const uint8_t* p) { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3]; }
}

uint64_t ntpTimetag(int64_t unixMicros)
{
    const uint64_t secs = (uint64_t) (unixMicros / 1000000) + 2208988800ull;
    const uint64_t frac = (uint64_t) (((unixMicros % 1000000) * 4294967296.0) / 1.0e6);
    return (secs << 32) | (frac & 0xFFFFFFFFull);
}
int64_t unixMicrosFromTimetag(uint64_t tag)
{
    const int64_t secs = (int64_t) (tag >> 32) - 2208988800ll;
    const double frac = (double) (tag & 0xFFFFFFFFull) / 4294967296.0;
    return secs * 1000000 + (int64_t) std::llround(frac * 1.0e6);
}

void OscMessage::encode(std::vector<uint8_t>& o) const
{
    putStr(o, address);
    std::string tt = ",";
    for (const auto& a : args) tt += a.type;
    putStr(o, tt);
    for (const auto& a : args) {
        if (a.type == 'i') put32(o, (uint32_t) a.i);
        else if (a.type == 'f') { uint32_t b; std::memcpy(&b, &a.f, 4); put32(o, b); }
        else putStr(o, a.s);
    }
}

std::vector<uint8_t> encodeOscPacket(const OscMessage& m) { std::vector<uint8_t> o; m.encode(o); return o; }

std::vector<uint8_t> encodeOscBundle(uint64_t timetag, const std::vector<OscMessage>& msgs)
{
    std::vector<uint8_t> o;
    putStr(o, "#bundle");
    put64(o, timetag);
    for (const auto& m : msgs) {
        std::vector<uint8_t> e; m.encode(e);
        put32(o, (uint32_t) e.size());
        o.insert(o.end(), e.begin(), e.end());
    }
    return o;
}

static bool decodeMessage(const uint8_t* d, size_t n, OscMessage& m)
{
    size_t i = 0;
    auto rdStr = [&](std::string& s) {
        size_t st = i; while (i < n && d[i]) ++i;
        if (i >= n) return false;
        s.assign((const char*) d + st, i - st);
        i = (i + 4) & ~(size_t) 3;
        return true;
    };
    std::string tt;
    if (!rdStr(m.address) || !rdStr(tt) || tt.empty() || tt[0] != ',') return false;
    for (size_t k = 1; k < tt.size(); ++k) {
        OscArg a; a.type = tt[k];
        if (a.type == 'i') { if (i + 4 > n) return false; a.i = (int32_t) get32(d + i); i += 4; }
        else if (a.type == 'f') { if (i + 4 > n) return false; uint32_t b = get32(d + i); std::memcpy(&a.f, &b, 4); i += 4; }
        else if (a.type == 's') { if (!rdStr(a.s)) return false; }
        else return false;
        m.args.push_back(a);
    }
    return true;
}

bool decodeOsc(const uint8_t* d, size_t n, DecodedOsc& out)
{
    out = {};
    if (n >= 16 && std::memcmp(d, "#bundle", 8) == 0) {
        out.bundle = true;
        out.timetag = ((uint64_t) get32(d + 8) << 32) | get32(d + 12);
        size_t i = 16;
        while (i + 4 <= n) {
            const size_t len = get32(d + i); i += 4;
            if (i + len > n) return false;
            OscMessage m; if (!decodeMessage(d + i, len, m)) return false;
            out.messages.push_back(std::move(m)); i += len;
        }
        return true;
    }
    OscMessage m; if (!decodeMessage(d, n, m)) return false;
    out.messages.push_back(std::move(m));
    return true;
}

void OscPlanner::plan(const Timeline& tl, int64_t nowUs, int64_t wallOffsetUs, const OscConfig& cfg, std::vector<OscPacket>& out)
{
    if (!cfg.enabled || !tl.map.valid || tl.suppressed) return;
    const BeatMapSnapshot& s = tl.snap;
    const double perSamples = periodSamples(s);
    if (s.bpm <= 0.0 || perSamples <= 0.0) return;
    const bool generic = cfg.profile == OscProfile::Generic || cfg.alsoGeneric;
    const std::string R = cfg.root;
    auto emit = [&](const std::vector<OscMessage>& msgs, int64_t at, int64_t beatHost) {
        if (msgs.empty()) return;
        OscPacket p; p.sendAtUs = at; p.beatHostUs = beatHost;
        p.bytes = beatHost ? encodeOscBundle(ntpTimetag(beatHost + wallOffsetUs + (int64_t) (cfg.offsetMs * 1000.0)), msgs)
                           : (msgs.size() == 1 ? encodeOscPacket(msgs[0]) : encodeOscBundle(kOscImmediate, msgs));
        out.push_back(std::move(p));
    };

    // /tempo on change (deadband 0.01) and every second.
    if (std::fabs(s.bpm - lastTempo_) > 0.01 || nowUs - lastTempoAt_ >= 1000000) {
        std::vector<OscMessage> m;
        if (generic) m.push_back({ R + "/tempo", { OscArg::Float((float) s.bpm) } });
        if (cfg.profile == OscProfile::Resolume) {
            const float norm = (float) std::clamp((s.bpm - 20.0) / 480.0, 0.0, 1.0);
            m.push_back({ "/composition/tempocontroller/tempo", { OscArg::Float(norm) } });
        }
        emit(m, nowUs, 0);
        lastTempo_ = s.bpm; lastTempoAt_ = nowUs;
    }
    if (nowUs - lastPhaseAt_ >= 10000) {
        const double perUs = perSamples * 1.0e6 / tl.map.sampleRate;
        const double originUs = (double) tl.map.hostTimeForSample(s.beatOriginSample);
        const double beats = (double) (s.beatInBar - 1) + ((double) nowUs - originUs) / perUs;
        const double bpb = std::max(1, tl.beatsPerBar);
        double ph = std::fmod(beats, bpb) / bpb; if (ph < 0) ph += 1.0;
        if (generic) emit({ { R + "/phase", { OscArg::Float((float) ph) } } }, nowUs, 0);
        lastPhaseAt_ = nowUs;
    }
    if (nowUs - lastConfAt_ >= 100000) {
        if (generic) emit({ { R + "/confidence", { OscArg::Float(s.confidence) } } }, nowUs, 0);
        lastConfAt_ = nowUs;
    }
    if (lastState_ != (int) s.state) {
        static const char* names[] = { "IDLE", "COUNT-IN", "LOCKING", "LOCKED", "HOLD", "CHASE" };
        if (generic) emit({ { R + "/state", { OscArg::Str(names[std::min<int>(s.state, 5)]) } } }, nowUs, 0);
        lastState_ = (int) s.state;
    }
    if ((s.flags & SnapshotFlags::Relock) && s.sequence != lastRelockSeq_) {
        lastRelockSeq_ = s.sequence;
        if (generic) emit({ { R + "/relock", { OscArg::Int(s.barNumber), OscArg::Int(s.beatInBar) } } }, nowUs, 0);
    }

    // Beat bundles, announced cfg.leadMs ahead with the predicted beat time as the NTP timetag.
    const int64_t lead = (int64_t) (cfg.leadMs * 1000.0);
    int64_t k = firstBeatAtOrAfter(tl, nowUs + (cfg.offsetMs < 0 ? (int64_t) (-cfg.offsetMs * 1000.0) : 0));
    for (int guard = 0; guard < 4; ++guard, ++k) {
        const GridBeat g = gridBeat(tl, k);
        const int64_t t = g.hostUs + (int64_t) (cfg.offsetMs * 1000.0);
        if (t - nowUs > lead) break;
        const int bpb = std::max(1, tl.beatsPerBar);
        const int64_t abs = (int64_t) g.barNumber * bpb + (g.beatInBar - 1);
        if (abs == lastBeatAbs_ || t <= lastBeatHost_) continue;
        std::vector<OscMessage> m;
        if (generic) {
            m.push_back({ R + "/beat", { OscArg::Int(g.barNumber), OscArg::Int(g.beatInBar), OscArg::Int(bpb) } });
            if (g.beatInBar == 1) m.push_back({ R + "/downbeat", { OscArg::Int(g.barNumber) } });
        }
        if (cfg.profile == OscProfile::Resolume && g.beatInBar == 1)
            m.push_back({ "/composition/tempocontroller/resync", { OscArg::Int(1) } });
        if (cfg.profile == OscProfile::MagicQ) m.push_back({ cfg.tapAddress, { OscArg::Int(1) } });
        emit(m, std::max(nowUs, t - lead), g.hostUs);
        lastBeatAbs_ = abs; lastBeatHost_ = t;
    }
}

// ---------------------------------------------------------------- UDP
bool UdpSender::open(const std::string& host, int port, std::string* err)
{
    close();
#if defined(_WIN32)
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    addrinfo hints {}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        if (err) *err = "cannot resolve " + host;
        return false;
    }
    const auto s = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
#if defined(_WIN32)
    if (s == INVALID_SOCKET) { freeaddrinfo(res); if (err) *err = "socket failed"; return false; }
#else
    if (s < 0) { freeaddrinfo(res); if (err) *err = "socket failed"; return false; }
#endif
    sock_ = (intptr_t) s;
    addr_.assign((const uint8_t*) res->ai_addr, (const uint8_t*) res->ai_addr + res->ai_addrlen);
    freeaddrinfo(res);
    return true;
}

void UdpSender::close()
{
    if (sock_ == -1) return;
#if defined(_WIN32)
    closesocket((SOCKET) sock_);
#else
    ::close((int) sock_);
#endif
    sock_ = -1;
}

bool UdpSender::send(const uint8_t* data, size_t n)
{
    if (sock_ == -1) return false;
#if defined(_WIN32)
    return ::sendto((SOCKET) sock_, (const char*) data, (int) n, 0, (const sockaddr*) addr_.data(), (int) addr_.size()) >= 0;
#else
    return ::sendto((int) sock_, data, n, 0, (const sockaddr*) addr_.data(), (socklen_t) addr_.size()) >= 0;
#endif
}

// ---------------------------------------------------------------- thread
void OscThread::start(TimelineSource src)
{
    stop();
    src_ = src; stop_ = false;
    th_ = std::thread([this] { run(); });
}
void OscThread::stop() { stop_ = true; if (th_.joinable()) th_.join(); }
void OscThread::setConfig(const OscConfig& c) { std::lock_guard<std::mutex> l(m_); cfg_ = c; cfgDirty_ = true; }
void OscThread::setTransportForTest(std::shared_ptr<OscTransport> t) { std::lock_guard<std::mutex> l(m_); testTransport_ = std::move(t); }
void OscThread::sendNow(const OscMessage& msg) { std::lock_guard<std::mutex> l(m_); if (oneShots_.size() < 64) oneShots_.push_back(msg); }

void OscThread::run()
{
    ScopedTimerResolution timerRes;
    (void) timerRes;
    OscPlanner planner;
    UdpSender udp;
    OscConfig cfg;
    std::vector<OscPacket> pending, ready;
    std::shared_ptr<OscTransport> tt;
    while (!stop_.load()) {
        std::vector<OscMessage> shots;
        {
            std::lock_guard<std::mutex> l(m_);
            if (cfgDirty_) {
                cfg = cfg_; cfgDirty_ = false;
                udp.close();
                status_.enabled = cfg.enabled;
                if (cfg.enabled && !testTransport_) {
                    std::string err;
                    if (udp.open(cfg.host, cfg.port, &err)) { status_.connected = true; status_.clearError(); }
                    else { status_.connected = false; status_.setError(err); }
                }
                status_.setDetail(cfg.host + ":" + std::to_string(cfg.port));
            }
            tt = testTransport_;
            shots.swap(oneShots_);
        }
        const int64_t now = steadyNowUs();
        auto put = [&](const std::vector<uint8_t>& b) {
            bool ok = tt ? tt->send(b.data(), b.size()) : udp.send(b.data(), b.size());
            if (ok) status_.sent++; else status_.setError("send failed");
        };
        { Quick q; while (quickQ_.pop(q)) {
            OscMessage m; m.address = q.addr;
            if (q.nInts > 0) m.args.push_back(OscArg::Int(q.i0));
            if (q.nInts > 1) m.args.push_back(OscArg::Int(q.i1));
            if (q.hasFloat) m.args.push_back(OscArg::Float(q.f0));
            shots.push_back(std::move(m));
        } }
        if (cfg.enabled) for (const auto& m : shots) put(encodeOscPacket(m));
        if (src_ && cfg.enabled) {
            const Timeline tl = src_->read();
            planner.plan(tl, now, wallNowUs() - now, cfg, pending);
        }
        // Send what is due; keep what is not yet.
        for (auto it = pending.begin(); it != pending.end();) {
            if (it->sendAtUs <= now) { put(it->bytes); it = pending.erase(it); } else ++it;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

// ---------------------------------------------------------------- inbound
bool OscInput::start(int port, Handler h, std::string* err)
{
    stop();
#if defined(_WIN32)
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { if (err) *err = "socket failed"; return false; }
    DWORD tmo = 100; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*) &tmo, sizeof tmo);
#else
    int s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) { if (err) *err = "socket failed"; return false; }
    timeval tv {}; tv.tv_usec = 100000; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
    int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*) &one, sizeof one);
    sockaddr_in a {}; a.sin_family = AF_INET; a.sin_port = htons((uint16_t) port); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(s, (sockaddr*) &a, sizeof a) != 0) {
        if (err) *err = "cannot bind UDP port " + std::to_string(port);
#if defined(_WIN32)
        closesocket(s);
#else
        ::close(s);
#endif
        return false;
    }
    sock_ = (intptr_t) s; handler_ = std::move(h); stop_ = false;
    th_ = std::thread([this] {
        std::vector<uint8_t> buf(2048);
        while (!stop_.load()) {
#if defined(_WIN32)
            const int n = ::recv((SOCKET) sock_, (char*) buf.data(), (int) buf.size(), 0);
#else
            const ssize_t n = ::recv((int) sock_, buf.data(), buf.size(), 0);
#endif
            if (n <= 0) continue;
            DecodedOsc d;
            if (!decodeOsc(buf.data(), (size_t) n, d)) continue;
            for (const auto& m : d.messages) if (handler_) handler_(m);
        }
    });
    return true;
}

void OscInput::stop()
{
    stop_ = true;
    if (th_.joinable()) th_.join();
    if (sock_ != -1) {
#if defined(_WIN32)
        closesocket((SOCKET) sock_);
#else
        ::close((int) sock_);
#endif
        sock_ = -1;
    }
}

} // namespace pacemaker
