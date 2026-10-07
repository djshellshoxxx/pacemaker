// Pacemaker tracking engine public interface (ES-01).
#pragma once
#include "Types.h"
#include "SpscQueue.h"
#include <atomic>
#include <memory>

namespace pacemaker {

class Engine {
public:
    Engine();
    explicit Engine(const EngineConfig& cfg);
    ~Engine();

    // Allocates all buffers. Not real-time safe. process() allocates nothing afterwards.
    void prepare(const EngineConfig& cfg);
    void reset();
    const EngineConfig& config() const;

    // Audio thread. roleChannels[r] has numSamples samples for role r (r < config().numRoles).
    void process(const float* const* roleChannels, int numSamples,
                 int64_t blockStartSample, int64_t blockHostTimeUs);

    // Audio thread. Discrete onset (e-drum MIDI, trigger, tap); sample in the engine clock.
    void pushEvent(DiscreteOnset ev);

    // Consumer thread. Drains OnsetEvent / BeatEvent records.
    bool popEvent(EngineEvent& out);
    size_t droppedEvents() const;

    // Any thread. Latest snapshot (seqlock).
    BeatMapSnapshot snapshot() const;

    // User actions. Applied at the next hop boundary on the audio thread.
    void tap(int64_t sample);
    void downbeatNow();
    void shiftBar(int delta);
    void relock();
    void halfTime();
    void doubleTime();
    void setFollow(bool follow);

    // Constant front-end delay subtracted from onset timestamps (ES-01 section 4).
    int detectorDelaySamples() const;
    int hopSize() const;
    int64_t samplesProcessed() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pacemaker
