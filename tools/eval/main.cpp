// pacemaker_eval: run the tracking engine over a WAV file and print beats and tempo.
//
//   pacemaker_eval file.wav [--block N] [--bpm REF] [--roles kick,snare,any,...] [--rehearsal]
//                  [--onsets] [--json OUT.json] [--csv OUT.csv] [--quiet]
//   pacemaker_eval gen --out PREFIX [--bpm B] [--bars N] [--seed S] [--walk SIGMA] [--jitter MS]
//                  [--hihat] [--countin] [--ramp-to B] [--fill-bars N] [--silence S]
//
// `gen` renders a synthetic drum track with the in-repo drum machine (Tests/DrumMachine.h) to
// PREFIX.wav (mono float32) and writes its annotation to PREFIX.beats, one "timeSeconds beatInBar"
// line per played beat (RS-05 section 2).
//
// --json writes one machine-readable result file (beats, downbeats, state changes, tempo track);
// --csv writes the beat list as "time,beat_in_bar,bar,confidence,state,relock". tools/eval/score.py
// reads the JSON.
//
// Output lines:  "beat <timeSeconds> <beatInBar> <confidence>"  for every published beat
//                "tempo <timeSeconds> <bpm> <state> <confidence>"  once per second
//                "onset <timeSeconds> <role> <strength> <tag>"  with --onsets
#include "DrumMachine.h"
#include "pacemaker/Engine.h"
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace pacemaker;

namespace {

struct Wav { double sampleRate = 0; int channels = 0; std::vector<std::vector<float>> data; };

bool readWav(const char* path, Wav& w)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) return false;
    auto u16 = [&](size_t o) { return (uint16_t) ((uint8_t) bytes[o] | ((uint8_t) bytes[o + 1] << 8)); };
    auto u32 = [&](size_t o) { return (uint32_t) u16(o) | ((uint32_t) u16(o + 2) << 16); };
    size_t pos = 12; int format = 0, bits = 0; size_t dataPos = 0, dataLen = 0;
    while (pos + 8 <= bytes.size()) {
        const uint32_t len = u32(pos + 4);
        if (std::memcmp(bytes.data() + pos, "fmt ", 4) == 0) {
            format = u16(pos + 8); w.channels = u16(pos + 10); w.sampleRate = u32(pos + 12); bits = u16(pos + 22);
            if (format == 0xFFFE && len >= 26) format = u16(pos + 8 + 24);
        } else if (std::memcmp(bytes.data() + pos, "data", 4) == 0) {
            dataPos = pos + 8; dataLen = std::min<size_t>(len, bytes.size() - dataPos);
        }
        pos += 8 + len + (len & 1);
    }
    if (w.channels <= 0 || dataPos == 0) return false;
    const int bytesPer = bits / 8;
    const size_t frames = dataLen / (size_t) (bytesPer * w.channels);
    w.data.assign(w.channels, std::vector<float>(frames));
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < w.channels; ++c) {
            const size_t o = dataPos + (i * w.channels + c) * bytesPer;
            float v = 0;
            if (format == 3 && bits == 32) { std::memcpy(&v, bytes.data() + o, 4); }
            else if (bits == 16) v = (float) (int16_t) u16(o) / 32768.0f;
            else if (bits == 24) { int32_t s = (int32_t) (((uint32_t) (uint8_t) bytes[o]) << 8 | ((uint32_t) (uint8_t) bytes[o + 1]) << 16 | ((uint32_t) (uint8_t) bytes[o + 2]) << 24); v = (float) (s >> 8) / 8388608.0f; }
            else if (bits == 32) v = (float) (int32_t) u32(o) / 2147483648.0f;
            else return false;
            w.data[c][i] = v;
        }
    return true;
}

InputRole parseRole(const std::string& s)
{
    if (s == "kick") return InputRole::Kick;
    if (s == "snare") return InputRole::Snare;
    if (s == "hihat") return InputRole::HiHat;
    if (s == "overhead") return InputRole::Overhead;
    if (s == "trigger") return InputRole::Trigger;
    return InputRole::Any;
}

const char* stateName(uint8_t s)
{
    static const char* names[] = { "Idle", "CountIn", "Locking", "Locked", "Hold", "Chase" };
    return s < 6 ? names[s] : "?";
}



bool writeWavMono(const std::string& path, const std::vector<float>& x, int sr)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    auto w32 = [&](uint32_t v) { f.write((const char*) &v, 4); };
    auto w16 = [&](uint16_t v) { f.write((const char*) &v, 2); };
    const uint32_t bytes = (uint32_t) (x.size() * 4);
    f.write("RIFF", 4); w32(36 + bytes); f.write("WAVEfmt ", 8);
    w32(16); w16(3); w16(1); w32((uint32_t) sr); w32((uint32_t) sr * 4); w16(4); w16(32);
    f.write("data", 4); w32(bytes);
    f.write((const char*) x.data(), (std::streamsize) bytes);
    return (bool) f;
}

int generate(int argc, char** argv)
{
    std::string out; double bpm = 120, walk = 0.3, jitter = 10, rampTo = 0, silence = 0.5;
    int bars = 24, fillBars = 0; uint64_t seed = 1; bool hihat = false, countin = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? argv[++i] : (const char*) "0"; };
        if (a == "--out") out = next();
        else if (a == "--bpm") bpm = std::atof(next());
        else if (a == "--bars") bars = std::atoi(next());
        else if (a == "--seed") seed = (uint64_t) std::atoll(next());
        else if (a == "--walk") walk = std::atof(next());
        else if (a == "--jitter") jitter = std::atof(next());
        else if (a == "--ramp-to") rampTo = std::atof(next());
        else if (a == "--fill-bars") fillBars = std::atoi(next());
        else if (a == "--silence") silence = std::atof(next());
        else if (a == "--hihat") hihat = true;
        else if (a == "--countin") countin = true;
    }
    if (out.empty()) { std::fprintf(stderr, "gen: --out PREFIX required\n"); return 2; }
    const int sr = 48000;
    drums::Machine m((double) sr, seed);
    m.bpm = bpm; m.walkSigma = walk; m.jitterMs = jitter; m.hihat8ths = hihat;
    m.silence(silence);
    if (countin) m.countIn(4);
    if (rampTo > 0) { m.bars(std::max(1, bars / 3)); m.bars(std::max(1, bars / 3), false, false, rampTo); m.bars(std::max(1, bars - 2 * (bars / 3))); }
    else if (fillBars > 0) { m.bars(std::max(1, bars / 2)); m.bars(fillBars, true); m.bars(std::max(1, bars - bars / 2 - fillBars)); }
    else m.bars(bars);
    m.addNoiseFloor();
    if (!writeWavMono(out + ".wav", m.audio, sr)) { std::fprintf(stderr, "cannot write %s.wav\n", out.c_str()); return 1; }
    std::ofstream t(out + ".beats");
    if (!t) { std::fprintf(stderr, "cannot write %s.beats\n", out.c_str()); return 1; }
    char buf[64];
    for (const auto& b : m.beats) {
        if (!b.played) continue;
        std::snprintf(buf, sizeof buf, "%.4f %d\n", b.hitTime, b.beatInBar);
        t << buf;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc >= 2 && std::string(argv[1]) == "gen") return generate(argc, argv);
    if (argc < 2) { std::fprintf(stderr, "usage: pacemaker_eval file.wav [--block N] [--bpm REF] [--roles r1,r2] [--rehearsal] [--onsets] [--json F] [--csv F] [--quiet]\n       pacemaker_eval gen --out PREFIX [--bpm B] [--bars N] [--seed S] [--walk S] [--jitter MS] [--hihat] [--countin] [--ramp-to B] [--fill-bars N]\n"); return 2; }
    int block = 256; bool onsets = false, quiet = false; std::string jsonPath, csvPath;
    EngineConfig cfg;
    std::vector<InputRole> roles;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--block" && i + 1 < argc) block = std::atoi(argv[++i]);
        else if (a == "--bpm" && i + 1 < argc) cfg.referenceBpm = std::atof(argv[++i]);
        else if (a == "--rehearsal") cfg.profile = FollowProfile::rehearsal();
        else if (a == "--onsets") onsets = true;
        else if (a == "--quiet") quiet = true;
        else if (a == "--json" && i + 1 < argc) jsonPath = argv[++i];
        else if (a == "--csv" && i + 1 < argc) csvPath = argv[++i];
        else if (a == "--roles" && i + 1 < argc) {
            std::string list = argv[++i]; size_t p = 0;
            while (p <= list.size()) { size_t q = list.find(',', p); if (q == std::string::npos) q = list.size(); roles.push_back(parseRole(list.substr(p, q - p))); p = q + 1; }
        }
    }
    Wav wav;
    if (!readWav(argv[1], wav)) { std::fprintf(stderr, "cannot read %s (PCM 16/24/32 or float32 WAV expected)\n", argv[1]); return 1; }
    cfg.sampleRate = wav.sampleRate; cfg.maxBlockSize = block;
    cfg.numRoles = std::min(wav.channels, kMaxRoles);
    for (int r = 0; r < cfg.numRoles; ++r) cfg.roles[r] = r < (int) roles.size() ? roles[r] : InputRole::Any;
    Engine engine(cfg);

    const int64_t total = (int64_t) wav.data[0].size();
    std::vector<const float*> ch(cfg.numRoles);
    int64_t nextTempoPrint = 0;
    struct BeatRec { double t; int pos, bar; float conf; uint8_t state, relock; };
    struct TempoRec { double t, bpm; uint8_t state; float conf; };
    struct StateRec { double t; uint8_t from, to; };
    struct FlagRec { double t; uint8_t flags; };
    std::vector<BeatRec> beats; std::vector<TempoRec> tempo; std::vector<StateRec> changes; std::vector<FlagRec> flagChanges;
    uint8_t lastState = (uint8_t) EngineState::Idle, lastFlags = 0;
    for (int64_t pos = 0; pos < total; pos += block) {
        const int n = (int) std::min<int64_t>(block, total - pos);
        for (int r = 0; r < cfg.numRoles; ++r) ch[r] = wav.data[r].data() + pos;
        engine.process(ch.data(), n, pos, 0);
        const double now = (pos + n) / wav.sampleRate;
        EngineEvent ev;
        while (engine.popEvent(ev)) {
            if (ev.type == EngineEvent::Beat) {
                const double t = ev.beat.sample / wav.sampleRate;
                beats.push_back({ t, ev.beat.beatInBar, ev.beat.barNumber, ev.beat.confidence, ev.beat.state, ev.beat.relock });
                if (!quiet) std::printf("beat %.4f %d %.3f\n", t, ev.beat.beatInBar, ev.beat.confidence);
            } else if (onsets && !quiet)
                std::printf("onset %.4f %d %.3f %d\n", ev.onset.sample / wav.sampleRate, (int) ev.onset.role, ev.onset.strength, ev.onset.bandTag);
        }
        const BeatMapSnapshot s = engine.snapshot();
        if (s.state != lastState) { changes.push_back({ now, lastState, s.state }); lastState = s.state; }
        if (s.flags != lastFlags) { flagChanges.push_back({ now, s.flags }); lastFlags = s.flags; }
        if (pos + n >= nextTempoPrint) {
            tempo.push_back({ now, s.bpm, s.state, s.confidence });
            if (!quiet) std::printf("tempo %.2f %.2f %s %.3f\n", now, s.bpm, stateName(s.state), s.confidence);
            nextTempoPrint += (int64_t) wav.sampleRate;
        }
    }

    if (!csvPath.empty()) {
        std::ofstream f(csvPath);
        if (!f) { std::fprintf(stderr, "cannot write %s\n", csvPath.c_str()); return 1; }
        f << "time,beat_in_bar,bar,confidence,state,relock\n";
        char buf[160];
        for (const auto& b : beats) {
            std::snprintf(buf, sizeof buf, "%.4f,%d,%d,%.3f,%s,%d\n", b.t, b.pos, b.bar, b.conf, stateName(b.state), (int) b.relock);
            f << buf;
        }
    }
    if (!jsonPath.empty()) {
        std::ofstream f(jsonPath);
        if (!f) { std::fprintf(stderr, "cannot write %s\n", jsonPath.c_str()); return 1; }
        char buf[256];
        std::string path = argv[1]; std::string esc;
        for (char c : path) { if (c == '\\' || c == '"') esc += '\\'; esc += c; }
        std::snprintf(buf, sizeof buf, "{\n  \"version\": 1,\n  \"file\": \"%s\",\n  \"sample_rate\": %.0f,\n  \"block\": %d,\n  \"duration\": %.4f,\n", esc.c_str(), wav.sampleRate, block, total / wav.sampleRate);
        f << buf << "  \"beats\": [";
        for (size_t i = 0; i < beats.size(); ++i) {
            const auto& b = beats[i];
            std::snprintf(buf, sizeof buf, "%s\n    {\"t\": %.4f, \"beat_in_bar\": %d, \"bar\": %d, \"confidence\": %.3f, \"state\": \"%s\", \"relock\": %d}",
                          i ? "," : "", b.t, b.pos, b.bar, b.conf, stateName(b.state), (int) b.relock);
            f << buf;
        }
        f << "\n  ],\n  \"downbeats\": [";
        bool first = true;
        for (const auto& b : beats) if (b.pos == 1) { std::snprintf(buf, sizeof buf, "%s%.4f", first ? "" : ", ", b.t); f << buf; first = false; }
        f << "],\n  \"state_changes\": [";
        for (size_t i = 0; i < changes.size(); ++i) {
            std::snprintf(buf, sizeof buf, "%s\n    {\"t\": %.4f, \"from\": \"%s\", \"to\": \"%s\"}", i ? "," : "", changes[i].t, stateName(changes[i].from), stateName(changes[i].to));
            f << buf;
        }
        f << "\n  ],\n  \"flag_changes\": [";
        for (size_t i = 0; i < flagChanges.size(); ++i) {
            std::snprintf(buf, sizeof buf, "%s\n    {\"t\": %.4f, \"relock\": %d, \"octave_ambiguity\": %d, \"bar_confirmed\": %d}", i ? "," : "", flagChanges[i].t,
                          (flagChanges[i].flags & SnapshotFlags::Relock) != 0, (flagChanges[i].flags & SnapshotFlags::OctaveAmbiguity) != 0, (flagChanges[i].flags & SnapshotFlags::BarConfirmed) != 0);
            f << buf;
        }
        f << "\n  ],\n  \"tempo\": [";
        for (size_t i = 0; i < tempo.size(); ++i) {
            std::snprintf(buf, sizeof buf, "%s\n    {\"t\": %.2f, \"bpm\": %.2f, \"state\": \"%s\", \"confidence\": %.3f}", i ? "," : "", tempo[i].t, tempo[i].bpm, stateName(tempo[i].state), tempo[i].conf);
            f << buf;
        }
        f << "\n  ]\n}\n";
    }
    return 0;
}
