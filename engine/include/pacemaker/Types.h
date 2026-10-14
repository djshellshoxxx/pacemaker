// Pacemaker engine: public value types (ES-01 section 2).
#pragma once
#include <cstdint>

namespace pacemaker {

enum class InputRole : uint8_t { Any, Kick, Snare, HiHat, Overhead, Trigger };
enum class ProfileType : uint8_t { Stage, Rehearsal, Custom };
enum class EngineState : uint8_t { Idle, CountIn, Locking, Locked, Hold, Chase };
// Quintuple54 is 5/4 grouped 3+2; Septuple78 is 7/8 with the grouping chosen by
// EngineConfig::grouping78; Learned uses EngineConfig::learnedPattern (or Engine::setLearnedPattern).
enum class BarPattern : uint8_t { Rock44, HalfTime44, FourOnFloor44, Waltz34, Compound68, Quintuple54, Septuple78, Learned };
enum class Grouping78 : uint8_t { G223, G322, G232 };   // 7/8 as 2+2+3, 3+2+2, 2+3+2

// Band tags are bit flags so that merged onsets can carry several.
namespace BandTag {
constexpr uint8_t None = 0, KickLike = 1, SnareLike = 2, Other = 4;
}

struct Meter {
    int beatsPerBar = 4;   // 1..16
    int beatUnit = 4;      // 4 or 8
};

struct TempoRange {
    double minBpm = 50.0;
    double maxBpm = 220.0;
};

struct FollowProfile {
    ProfileType type = ProfileType::Stage;
    double alpha = 0.25;          // phase gain
    double beta = 0.06;           // period gain
    double chaseBeta = 0.20;      // period gain while in Chase
    double holdConfidence = 0.35; // informational threshold
    double maxSlewMs = 6.0;       // published phase slew per beat
    double kernelSeconds = 5.0;   // tempogram kernel length

    static FollowProfile stage() { return FollowProfile{}; }
    static FollowProfile rehearsal()
    {
        FollowProfile p;
        p.type = ProfileType::Rehearsal;
        p.alpha = 0.40; p.beta = 0.12; p.chaseBeta = 0.35;
        p.holdConfidence = 0.25; p.maxSlewMs = 12.0; p.kernelSeconds = 3.0;
        return p;
    }
};

constexpr int kMaxRoles = 4;

// Bar pattern recorded by Learn mode (ES-01 section 7): per bar position scores in about [-0.5, 1]
// as used by the bar tracker. Plain data so the host can store it with the session.
struct LearnedPattern {
    int beats = 0;                // 0 = no pattern; otherwise 1..16 and must equal the meter's beatsPerBar
    float kick[16] {}, snare[16] {}, strong[16] {};
};

struct EngineConfig {
    double sampleRate = 48000.0;  // 44100 .. 192000
    int maxBlockSize = 2048;
    int numRoles = 1;             // 1..4
    InputRole roles[kMaxRoles] = { InputRole::Any, InputRole::Any, InputRole::Any, InputRole::Any };
    Meter meter;
    TempoRange tempoRange;
    double referenceBpm = 0.0;    // 0 = none
    FollowProfile profile;
    BarPattern pattern = BarPattern::Rock44;
    float sensitivity = 0.5f;     // 0..1, scales the peak-picking delta
    Grouping78 grouping78 = Grouping78::G223;   // used by BarPattern::Septuple78
    LearnedPattern learnedPattern;              // used by BarPattern::Learned
    bool autoOctave = false;      // switch octave when ambiguity persists (ES-01 section 5)
};

struct DiscreteOnset {
    int64_t sample = 0;
    InputRole role = InputRole::Trigger;
    float velocity = 1.0f;        // 0..127 (MIDI) or 0..1; values <= 1 are taken as normalised
};

namespace SnapshotFlags {
constexpr uint8_t Relock = 1, OctaveAmbiguity = 2, BarConfirmed = 4;
}

struct BeatMapSnapshot {
    double bpm = 0.0;
    int64_t beatOriginSample = 0;
    int64_t nextBeatSample = 0;
    int beatInBar = 1;
    int barNumber = 0;
    float confidence = 0.0f;
    uint8_t state = 0;            // EngineState
    uint8_t flags = 0;            // SnapshotFlags
    uint32_t sequence = 0;
};

struct OnsetEvent {
    int64_t sample;
    InputRole role;
    float strength;
    uint8_t bandTag;
};

struct BeatEvent {
    int64_t sample;
    int beatInBar;
    int barNumber;
    float confidence;
    uint8_t state;
    uint8_t relock;
};

struct EngineEvent {
    enum Type : uint8_t { Onset, Beat } type;
    union {
        OnsetEvent onset;
        BeatEvent beat;
    };
};

} // namespace pacemaker
