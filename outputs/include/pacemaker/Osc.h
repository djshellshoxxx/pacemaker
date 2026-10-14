// OSC output (ES-02 section 4): encoder, deterministic generator, UDP sender and runner thread.
#pragma once
#include "ClockMap.h"
#include "MidiClock.h"
#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace pacemaker {

struct OscArg { enum Kind { Int, Float, Str } kind; int32_t i = 0; float f = 0; std::string s;
                static OscArg I(int v) { OscArg a; a.kind = Int; a.i = v; return a; }
                static OscArg F(double v) { OscArg a; a.kind = Float; a.f = (float) v; return a; }
                static OscArg S(std::string v) { OscArg a; a.kind = Str; a.s = std::move(v); return a; } };
struct OscMessage { std::string address; std::vector<OscArg> args; };

std::vector<uint8_t> oscEncode(const OscMessage& m);
// A bundle with an NTP timetag (seconds since 1900 in the high word) and one or more messages.
std::vector<uint8_t> oscBundle(uint64_t ntpTimetag, const std::vector<OscMessage>& msgs);
uint64_t ntpTimetag(int64_t hostUs);   // uses ntpOffsetUs()
// Decoder used by tests and the in-process monitor. Returns false on malformed input.
bool oscDecode(const std::vector<uint8_t>& data, std::vector<OscMessage>& out, uint64_t* timetag = nullptr);

struct OscConfig {
    std::string root = "/pacemaker";
    std::string profile = "generic";        // generic | resolume | magicq
    std::string magicqTapAddress = "/pb/1/tap";   // configurable; check the receiver's documentation
    double lookaheadMs = 30.0;
    double offsetMs = 0.0;
};

struct OscPacket { int64_t sendAtUs; std::vector<uint8_t> bytes; uint64_t timetag; std::string label; };

class OscGenerator {
public:
    explicit OscGenerator(OscConfig c = {}) : cfg_(std::move(c)) {}
    void configure(const OscConfig& c) { cfg_ = c; }
    void generate(const BeatMapSnapshot& s, int beatsPerBar, const ClockMap& map, int64_t nowUs, std::vector<OscPacket>& out);
private:
    std::string addr(const char* leaf) const { return cfg_.root + leaf; }
    OscConfig cfg_;
    int64_t lastBeatKey_ = INT64_MIN, lastTempoUs_ = 0, lastPhaseUs_ = 0, lastConfUs_ = 0;
    double lastBpm_ = -1.0;
    int lastState_ = -1;
    uint32_t lastRelockSeq_ = 0;
};

class UdpSender {
public:
    UdpSender() = default;
    ~UdpSender();
    bool open(const std::string& host, int port, std::string* error);
    bool send(const std::vector<uint8_t>& bytes);
    void close();
    uint64_t errors() const { return errors_; }
private:
    int fd_ = -1;
    alignas(16) unsigned char addr_[32] {};
    unsigned addrLen_ = 0;
    uint64_t errors_ = 0;
};

class OscRunner {
public:
    OscRunner(const ClockMap& map, std::function<BeatMapSnapshot()> snapshotFn, std::function<int()> beatsPerBarFn);
    ~OscRunner();
    bool start(const std::string& host, int port, const OscConfig& cfg, std::string* error);
    void stop();
    const OutputStatus& status() const { return status_; }
private:
    void run();
    const ClockMap& map_;
    std::function<BeatMapSnapshot()> snapshot_;
    std::function<int()> beatsPerBar_;
    UdpSender sender_;
    OscConfig cfg_;
    std::atomic<bool> running_ { false };
    std::thread thread_;
    OutputStatus status_;
};

} // namespace pacemaker
