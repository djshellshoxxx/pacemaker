// pacemaker_eval: run the tracking engine over a WAV file and print beats and tempo.
//
//   pacemaker_eval file.wav [--block N] [--bpm REF] [--roles kick,snare,any,...] [--rehearsal]
//
// Output lines:  "beat <timeSeconds> <beatInBar> <confidence>"  for every published beat
//                "tempo <timeSeconds> <bpm> <state> <confidence>"  once per second
//                "onset <timeSeconds> <role> <strength> <tag>"  with --onsets
#include "pacemaker/Engine.h"
#include <cstdint>
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

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: pacemaker_eval file.wav [--block N] [--bpm REF] [--roles r1,r2] [--rehearsal] [--onsets]\n"); return 2; }
    int block = 256; bool onsets = false;
    EngineConfig cfg;
    std::vector<InputRole> roles;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--block" && i + 1 < argc) block = std::atoi(argv[++i]);
        else if (a == "--bpm" && i + 1 < argc) cfg.referenceBpm = std::atof(argv[++i]);
        else if (a == "--rehearsal") cfg.profile = FollowProfile::rehearsal();
        else if (a == "--onsets") onsets = true;
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
    for (int64_t pos = 0; pos < total; pos += block) {
        const int n = (int) std::min<int64_t>(block, total - pos);
        for (int r = 0; r < cfg.numRoles; ++r) ch[r] = wav.data[r].data() + pos;
        engine.process(ch.data(), n, pos, 0);
        EngineEvent ev;
        while (engine.popEvent(ev)) {
            if (ev.type == EngineEvent::Beat)
                std::printf("beat %.4f %d %.3f\n", ev.beat.sample / wav.sampleRate, ev.beat.beatInBar, ev.beat.confidence);
            else if (onsets)
                std::printf("onset %.4f %d %.3f %d\n", ev.onset.sample / wav.sampleRate, (int) ev.onset.role, ev.onset.strength, ev.onset.bandTag);
        }
        if (pos + n >= nextTempoPrint) {
            const BeatMapSnapshot s = engine.snapshot();
            std::printf("tempo %.2f %.2f %s %.3f\n", (pos + n) / wav.sampleRate, s.bpm, stateName(s.state), s.confidence);
            nextTempoPrint += (int64_t) wav.sampleRate;
        }
    }
    return 0;
}
