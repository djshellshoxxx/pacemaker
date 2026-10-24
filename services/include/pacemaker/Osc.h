// OSC encoding, planning and output thread (ES-02 section 4).
#pragma once
#include "pacemaker/ClockMap.h"
#include "pacemaker/OutputStatus.h"
#include "pacemaker/SpscQueue.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace pacemaker {

struct OscArg {
    char type = 'i';       // i, f, s
    int32_t i = 0;
    float f = 0;
    std::string s;
    static OscArg Int(int32_t v) { OscArg a; a.type = 'i'; a.i = v; return a; }
    static OscArg Float(float v) { OscArg a; a.type = 'f'; a.f = v; return a; }
    static OscArg Str(std::string v) { OscArg a; a.type = 's'; a.s = std::move(v); return a; }
};

struct OscMessage {
    std::string address;
    std::vector<OscArg> args;
    void encode(std::vector<uint8_t>& out) const;
};

constexpr uint64_t kOscImmediate = 1;
uint64_t ntpTimetag(int64_t unixMicros);
int64_t unixMicrosFromTimetag(uint64_t tag);
std::vector<uint8_t> encodeOscPacket(const OscMessage& m);
std::vector<uint8_t> encodeOscBundle(uint64_t timetag, const std::vector<OscMessage>& msgs);

struct DecodedOsc {
    bool bundle = false;
    uint64_t timetag = 0;
    std::vector<OscMessage> messages;
};
bool decodeOsc(const uint8_t* data, size_t n, DecodedOsc& out);   // bundles one level deep

enum class OscProfile : uint8_t { Generic, Resolume, MagicQ };

struct OscConfig {
    bool enabled = false;
    std::string host = "127.0.0.1";
    int port = 9000;
    std::string root = "/pacemaker";
    OscProfile profile = OscProfile::Generic;
    bool alsoGeneric = true;       // keep the generic messages when a receiver profile is selected
    std::string tapAddress = "/tap";   // MagicQ
    double offsetMs = 0.0;
    double leadMs = 10.0;          // bundles are sent this long before the beat they announce
    int listenPort = 0;            // inbound control (song select, follow, tap ...); 0 = off
};

struct OscPacket {
    int64_t sendAtUs = 0;          // steady-clock time at which to put it on the wire
    int64_t beatHostUs = 0;        // for beat bundles: predicted beat time (steady clock), else 0
    std::vector<uint8_t> bytes;
};

class OscPlanner {
public:
    // Plans everything due at `nowUs` (call every few ms). `wallOffsetUs` = wall clock minus steady clock.
    void plan(const Timeline& tl, int64_t nowUs, int64_t wallOffsetUs, const OscConfig& cfg, std::vector<OscPacket>& out);
    void reset() { *this = OscPlanner(); }
private:
    double lastTempo_ = -1;
    int64_t lastTempoAt_ = 0, lastPhaseAt_ = 0, lastConfAt_ = 0;
    int lastState_ = -1;
    uint32_t lastRelockSeq_ = 0;
    int64_t lastBeatAbs_ = INT64_MIN;
    int64_t lastBeatHost_ = INT64_MIN;
};

class UdpSender {
public:
    UdpSender() = default;
    ~UdpSender() { close(); }
    bool open(const std::string& host, int port, std::string* err = nullptr);
    void close();
    bool send(const uint8_t* data, size_t n);
    bool isOpen() const { return sock_ != -1; }
private:
    intptr_t sock_ = -1;
    std::vector<uint8_t> addr_;
};

class OscTransport {
public:
    virtual ~OscTransport() = default;
    virtual bool send(const uint8_t* data, size_t n) = 0;
};

class OscThread {
public:
    ~OscThread() { stop(); }
    void start(TimelineSource src);
    void stop();
    void setConfig(const OscConfig& c);
    void setTransportForTest(std::shared_ptr<OscTransport> t);   // bypasses UDP
    // Immediate one-shot message (count-in launcher, section suggestions).
    void sendNow(const OscMessage& m);
    // Lock-free path for the audio thread (single producer): address (<=63 chars) with up to two numbers.
    struct Quick { char addr[64] {}; int32_t i0 = 0, i1 = 0; float f0 = 0; int nInts = 0; bool hasFloat = false; };
    void postFromAudio(const Quick& q) { quickQ_.push(q); }
    OutputStatus& status() { return status_; }
private:
    SpscQueue<Quick, 64> quickQ_;
    void run();
    TimelineSource src_ = nullptr;
    std::thread th_;
    std::atomic<bool> stop_ { false };
    std::mutex m_;
    OscConfig cfg_;
    bool cfgDirty_ = true;
    std::shared_ptr<OscTransport> testTransport_;
    std::vector<OscMessage> oneShots_;
    OutputStatus status_;
};

// Inbound OSC control (RS-01 section 6): /pacemaker/song i, /song/name s, /follow i, /tap, /downbeat, /relock,
// /half, /double, /nudge f. The callback runs on the receive thread.
class OscInput {
public:
    using Handler = std::function<void(const OscMessage&)>;
    ~OscInput() { stop(); }
    bool start(int port, Handler h, std::string* err = nullptr);
    void stop();
    bool running() const { return th_.joinable(); }
private:
    std::thread th_;
    std::atomic<bool> stop_ { false };
    intptr_t sock_ = -1;
    Handler handler_;
};

} // namespace pacemaker
