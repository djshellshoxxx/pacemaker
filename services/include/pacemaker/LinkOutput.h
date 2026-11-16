// Ableton Link output (ES-02 section 2). The Link library sits behind PACEMAKER_WITH_LINK; everything else
// (publisher logic, arbitration) compiles without it and is tested with a mock backend.
#pragma once
#include "pacemaker/ClockMap.h"
#include "pacemaker/OutputStatus.h"
#include "pacemaker/Settings.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace pacemaker {

// Thin abstraction over ableton::Link so the publisher can be tested without a network.
class ILinkBackend {
public:
    virtual ~ILinkBackend() = default;
    virtual void setEnabled(bool on) = 0;
    virtual bool isEnabled() const = 0;
    virtual int numPeers() const = 0;
    virtual int64_t clockMicros() const = 0;                 // Link clock
    virtual void setStartStopSyncEnabled(bool on) = 0;
    // Audio-thread session state transaction.
    virtual void capture() = 0;
    virtual double tempo() const = 0;
    virtual double phaseAtTime(int64_t linkUs, double quantum) const = 0;
    virtual double beatAtTime(int64_t linkUs, double quantum) const = 0;
    virtual void setTempo(double bpm, int64_t linkUs) = 0;
    virtual void forceBeatAtTime(double beat, int64_t linkUs, double quantum) = 0;
    virtual void setIsPlaying(bool playing, int64_t linkUs) = 0;
    virtual void commit() = 0;
};

// nullptr when built with PACEMAKER_WITH_LINK=OFF.
std::unique_ptr<ILinkBackend> makeAbletonLinkBackend(double initialBpm);
constexpr bool linkCompiledIn()
{
#if defined(PACEMAKER_WITH_LINK) && PACEMAKER_WITH_LINK
    return true;
#else
    return false;
#endif
}

struct LinkDiagnostics {
    std::atomic<int> forceBeatCalls { 0 };
    std::atomic<int> setTempoCalls { 0 };
    std::atomic<int> slewCalls { 0 };
    std::atomic<bool> fightingPeers { false };
};

// Audio-thread logic.
class LinkPublisher {
public:
    void reset();
    // linkOffsetUs = backend.clockMicros() - steadyNowUs() captured by the caller.
    void process(ILinkBackend& link, const Timeline& tl, int64_t nowSteadyUs, const LinkSettings& cfg, bool isPublisher);
    LinkDiagnostics& diagnostics() { return diag_; }
private:
    uint32_t lastRelockSeq_ = 0;
    int64_t lastCommitUs_ = -1000000000;
    double lastSetTempo_ = 0;
    int64_t lastBeatAbs_ = INT64_MIN;
    int driftBeats_ = 0;
    int slewBeatsLeft_ = 0;
    double ourBpm_ = 0;
    bool playingSent_ = false;
    int fights_ = 0; int64_t fightWindowStart_ = 0;
    LinkDiagnostics diag_;
};

// Process-wide Link host shared by every Pacemaker instance (one ableton::Link per process).
class LinkService {
public:
    static std::shared_ptr<LinkService> acquire();       // refcounted singleton
    static std::shared_ptr<LinkService> createForTest(std::unique_ptr<ILinkBackend> b);
    ILinkBackend* backend() { return backend_.get(); }
    bool available() const { return backend_ != nullptr; }

    // Arbitration (message thread, ~500 ms): the instance with Follow on and the highest confidence publishes.
    int registerInstance();
    void unregisterInstance(int id);
    void report(int id, bool follow, float confidence, uint32_t sequence);
    bool isPublisher(int id) const;
    int activeFollowers() const;
private:
    struct Inst { int id; bool follow; float conf; uint32_t seq; };
    mutable std::mutex m_;
    std::vector<Inst> insts_;
    int nextId_ = 1;
    std::unique_ptr<ILinkBackend> backend_;
    std::atomic<int> publisher_ { 0 };
    void arbitrate();
};

// Per-instance wrapper used by the Core.
class LinkOutput {
public:
    LinkOutput();
    ~LinkOutput();
    void setSettings(const LinkSettings& s);
    // Audio thread.
    void process(const Timeline& tl, int64_t nowSteadyUs);
    // Message thread, every ~500 ms.
    void updateArbitration(const Timeline& tl);
    OutputStatus& status() { return status_; }
    LinkPublisher& publisher() { return pub_; }
    bool available() const { return service_ && service_->available(); }
    bool isPublisher() const { return service_ && service_->isPublisher(id_); }
    int followers() const { return service_ ? service_->activeFollowers() : 0; }
    void useServiceForTest(std::shared_ptr<LinkService> s);
private:
    std::shared_ptr<LinkService> service_;
    int id_ = 0;
    SeqLock<LinkSettings> settings_;
    std::atomic<bool> enabled_ { false };
    LinkPublisher pub_;
    OutputStatus status_;
};

} // namespace pacemaker
