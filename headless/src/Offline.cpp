#include "Offline.h"
#include "DrumMachine.h"
#include "Indicators.h"
#include "Log.h"
#include "Oled.h"
#include "httplib.h"
#include "pacemaker/Core.h"
#include "pacemaker/WebUi.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <juce_audio_formats/juce_audio_formats.h>
#include <thread>

#if defined(__unix__) || defined(__APPLE__)
#include <time.h>
#endif

namespace pmhl {

using namespace pacemaker;
using Clock = std::chrono::steady_clock;

std::atomic<bool> g_stopRequested { false };

namespace {

double cpuSeconds(clockid_t id)
{
    timespec ts {};
    clock_gettime(id, &ts);
    return (double) ts.tv_sec + (double) ts.tv_nsec * 1e-9;
}

struct FeedResult { int64_t samples = 0; double maxBlockUs = 0, sumBlockUs = 0; int64_t blocks = 0; std::vector<float> blockUs; };

// Feeds [from, to) of the channel data to the core. fast = faster than real time with synthetic host times and the
// offline flag (outputs suppressed); otherwise paced to the wall clock with host time taken from the filter.
FeedResult feed(Core& core, const std::vector<const float*>& chans, double sr, int block, int64_t from, int64_t to,
                bool realtime, int64_t hostBaseUs, bool measure, float* syncL = nullptr, float* syncR = nullptr)
{
    FeedResult r;
    const auto t0 = Clock::now();
    std::vector<const float*> ptrs(chans.size());
    for (int64_t pos = from; pos < to && !g_stopRequested.load(); pos += block) {
        const int n = (int) std::min<int64_t>(block, to - pos);
        const double audioSec = (double) (pos - from + n) / sr;
        if (realtime) std::this_thread::sleep_until(t0 + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(audioSec)));
        else std::this_thread::sleep_until(t0 + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(audioSec * 0.01)));
        for (size_t c = 0; c < chans.size(); ++c) ptrs[c] = chans[c] + pos;
        CoreHostInfo h;
        h.offline = !realtime;
        h.hostTimeUs = realtime ? -1 : hostBaseUs + (int64_t) std::llround((double) pos * 1e6 / sr);
        const auto b0 = Clock::now();
        core.process(ptrs.data(), (int) ptrs.size(), n, h, nullptr, 0, syncL, syncR, nullptr, nullptr, 0);
        if (measure) {
            const double us = std::chrono::duration<double, std::micro>(Clock::now() - b0).count();
            r.maxBlockUs = std::max(r.maxBlockUs, us); r.sumBlockUs += us; ++r.blocks; r.blockUs.push_back((float) us);
        }
        r.samples += n;
    }
    return r;
}

std::string stateWord(const Core& c) { return stateName(c.timeline().snap.state); }

// ------------------------------------------------------------------ WAV
bool readWav(const std::string& path, std::vector<std::vector<float>>& out, double& sr, std::string& err)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd(fm.createReaderFor(juce::File(path)));
    if (!rd) { err = "cannot read '" + path + "' (WAV/AIFF/FLAC expected)"; return false; }
    if (rd->lengthInSamples <= 0 || rd->lengthInSamples > 60LL * 60 * 192000) { err = "unreasonable file length"; return false; }
    const int nch = (int) std::min<unsigned>(rd->numChannels, (unsigned) kMaxChannels);
    sr = rd->sampleRate;
    const int64_t len = rd->lengthInSamples;
    juce::AudioBuffer<float> buf(nch, (int) std::min<int64_t>(len, 1 << 16));
    out.assign((size_t) nch, std::vector<float>((size_t) len));
    for (int64_t pos = 0; pos < len; pos += buf.getNumSamples()) {
        const int n = (int) std::min<int64_t>(buf.getNumSamples(), len - pos);
        rd->read(&buf, 0, n, pos, true, nch > 1);
        for (int c = 0; c < nch; ++c) std::copy(buf.getReadPointer(c), buf.getReadPointer(c) + n, out[(size_t) c].begin() + pos);
    }
    return true;
}

std::string joinPath(const std::string& dir, const std::string& f) { return dir.empty() || dir == "." ? f : (dir.back() == '/' ? dir + f : dir + "/" + f); }

} // namespace

// ====================================================================== --wav-in
int runWavIn(const HeadlessConfig& cfg, const WavOptions& opt)
{
    std::vector<std::vector<float>> audio;
    double sr = 48000; std::string err;
    if (!readWav(opt.wav, audio, sr, err)) { std::fprintf(stderr, "pacemaker_headless: %s\n", err.c_str()); return 1; }
    const int nch = (int) audio.size();
    const int64_t total = (int64_t) audio[0].size();
    PM_LOGI("wav-in: %s, %d ch, %.0f Hz, %.1f s, %s", opt.wav.c_str(), nch, sr, (double) total / sr, opt.realtime ? "realtime" : "offline");

    Core core;
    PluginState st = cfg.state;
    if (!cfg.audio.inputs.empty()) st.session.channels = cfg.audio.inputs;
    else if (st.session.channels.empty())
        for (int c = 0; c < nch; ++c) { ChannelAssign a; a.channel = c; a.role = InputRole::Any; a.distanceM = 0.0; st.session.channels.push_back(a); }
    st.session.driftLogDir = opt.outDir;
    st.session.driftLogOffline = true;
    core.setState(st);
    core.prepare(sr, std::max(opt.block, 2048), nch);

    WebUi web(core);
    WebUi::Hooks hk;
    hk.tapAlongAvailable = false;
    hk.title = "Pacemaker (file: " + opt.wav + ")";
    web.setHooks(hk);
    const int port = opt.webPort >= 0 ? opt.webPort : (cfg.web.enabled ? cfg.web.port : 0);
    if (port > 0 || opt.webPort == 0) {
        web.setToken(cfg.web.token);
        if (port > 0 && web.start(port, cfg.web.bind)) PM_LOGI("web UI on http://%s:%d/", cfg.web.bind.c_str(), web.port());
        else if (port > 0) PM_LOGW("web UI not started: %s", web.lastError().c_str());
    }

    if (!core.startDriftLog()) PM_LOGW("drift log could not be opened in %s", opt.outDir.c_str());
    std::vector<const float*> chans;
    for (auto& c : audio) chans.push_back(c.data());
    const int64_t hostBase = steadyNowUs();

    // 1 s chunks so progress is visible and SIGTERM is honoured quickly.
    const auto wall0 = Clock::now();
    FeedResult fr;
    const int64_t chunk = (int64_t) sr;
    for (int64_t pos = 0; pos < total && !g_stopRequested.load(); pos += chunk) {
        const int64_t to = std::min(total, pos + chunk);
        // pace relative to the start of this chunk; for realtime each chunk starts at its own wall-clock slot
        std::vector<const float*> cp(chans.size());
        for (size_t c = 0; c < chans.size(); ++c) cp[c] = chans[c];
        FeedResult one = feed(core, cp, sr, opt.block, pos, to, opt.realtime, hostBase, false);
        fr.samples += one.samples;
        if ((pos / chunk) % 10 == 0) PM_LOGI("t=%5.1fs %s", (double) pos / sr, core.statusSummary().c_str());
    }
    const double wall = std::chrono::duration<double>(Clock::now() - wall0).count();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));          // the core's housekeeping thread drains the event queue
    const Timeline tl = core.timeline();
    const DriftReport rep = core.driftReport();
    std::printf("wav-in done: %.1f s of audio in %.2f s wall (%.0fx), final %s %.2f BPM conf %.2f, beats %d, relocks %d, median BPM %.2f\n",
                (double) total / sr, wall, (double) total / sr / std::max(wall, 1e-3), stateName(tl.snap.state), tl.snap.bpm,
                (double) tl.snap.confidence, rep.beats, rep.relockCount, rep.medianBpm);

    if (opt.lingerSec != 0 && web.running()) {
        PM_LOGI("lingering %s with the web UI at http://127.0.0.1:%d/", opt.lingerSec < 0 ? "until SIGTERM" : (std::to_string((int) opt.lingerSec) + " s").c_str(), web.port());
        std::printf("web UI port %d\n", web.port()); std::fflush(stdout);
        const auto until = Clock::now() + std::chrono::duration<double>(opt.lingerSec < 0 ? 1e9 : opt.lingerSec);
        while (Clock::now() < until && !g_stopRequested.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    web.stop();
    core.stopDriftLog();
    int rc = 0;
    const std::string mapPath = joinPath(opt.outDir, "pacemaker-tempomap.mid");
    if (core.exportTempoMap(mapPath)) std::printf("tempo map: %s\n", mapPath.c_str()); else { std::fprintf(stderr, "cannot write %s\n", mapPath.c_str()); rc = 1; }
    const std::string logPath = core.driftLogPath();
    if (!logPath.empty()) std::printf("drift log: %s\n", logPath.c_str());
    const std::string repPath = joinPath(opt.outDir, "pacemaker-report.html");
    if (writeTextFile(repPath, renderDriftReportHtml(rep, "Pacemaker drift report"))) std::printf("report: %s\n", repPath.c_str());
    return rc;
}

// ====================================================================== --selftest
namespace {
struct Checker {
    int fails = 0, total = 0;
    void check(bool ok, const std::string& what)
    {
        ++total; if (!ok) ++fails;
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    }
};

bool jsonOf(const httplib::Result& r, Json& j) { return r && r->status == 200 && Json::parse(r->body, j); }
} // namespace

int runSelftest()
{
    Checker t;
    std::printf("pacemaker_headless selftest\n");

    // --- config parsing
    {
        HeadlessConfig c; std::string e;
        const bool ok = HeadlessConfig::parse(R"({"audio":{"bufferSize":128,"inputs":[{"channel":0,"role":"kick","distanceM":0.1},{"channel":1,"role":"snare"}]},
            "web":{"port":9090,"token":"x"},"midi":{"clockOut":"Pisound MIDI"},"state":{"params":{"referenceBpm":120}}})", c, e);
        t.check(ok && c.audio.bufferSize == 128 && c.audio.inputs.size() == 2 && c.audio.inputs[1].role == InputRole::Snare && c.web.port == 9090
                    && c.midi.clockOut == "Pisound MIDI" && c.state.params.referenceBpm == 120.0, "config: valid file parses");
        HeadlessConfig b; std::string e2;
        t.check(!HeadlessConfig::parse(R"({"audio":{"bufferSize":"big"}})", b, e2) && e2.find("bufferSize") != std::string::npos, "config: wrong type is rejected naming the key (" + e2 + ")");
        t.check(!HeadlessConfig::parse(R"({"audio":{"inputs":[{"channel":0,"role":"cowbell"}]}})", b, e2), "config: unknown role is rejected");
        t.check(!HeadlessConfig::parse("{ not json", b, e2), "config: syntax error is rejected");
    }

    // --- OLED renderer
    {
        uint8_t a[kOledBytes], b[kOledBytes];
        OledInfo i; i.valid = true; i.bpm = 120.5; i.state = "LOCKED"; i.bar = 12; i.beat = 3; i.confidence = 0.8f;
        renderOled(i, a);
        i.state = "HOLD"; i.bpm = 98.0; renderOled(i, b);
        t.check(oledLitPixels(a) > 150 && std::memcmp(a, b, sizeof a) != 0, "oled: tempo + state render, different content differs");
    }

    // --- engine + web, fast part
    const double kSr = 48000.0;
    drums::Machine dm(kSr, 7);
    dm.bpm = 120; dm.walkSigma = 0.1; dm.jitterMs = 3.0;
    dm.silence(1.0);                                    // lead-in: the machine writes at negative indices when jitter is negative at t = 0
    dm.bars(40);                                        // 80 s
    const int64_t total = (int64_t) dm.audio.size();

    Core core;
    SessionSettings ss;
    ss.channels = { { 0, InputRole::Any, 0.0, false } };
    ss.midi.enabled = true;
    auto sink = std::make_shared<MemorySink>();
    core.setSession(ss);
    core.setMidiSink(sink);
    core.prepare(kSr, 256, 1);

    WebUi web(core);
    WebUi::Hooks hk; hk.tapAlongAvailable = false;
    web.setHooks(hk);
    web.setToken("selftest-token");
    const bool started = web.start(0, "127.0.0.1");
    t.check(started && web.port() > 0, "web: server starts on an ephemeral port (" + std::to_string(web.port()) + ")");
    if (!started) return 1;
    httplib::Client cli("127.0.0.1", web.port());
    cli.set_read_timeout(3, 0); cli.set_connection_timeout(3, 0);
    httplib::Headers auth = { { "X-Pacemaker-Token", "selftest-token" } };

    std::vector<const float*> chans { dm.audio.data() };
    const int64_t hostBase = steadyNowUs();
    const int64_t fastEnd = (int64_t) (60.0 * kSr);
    feed(core, chans, kSr, 256, 0, fastEnd / 2, false, hostBase, false);
    {
        Json j;
        auto r = cli.Get("/api/status", auth);
        t.check(jsonOf(r, j) && j["tempo"].isObject() && j.has("state") && j["inputs"].isArray(), "web: /api/status answers mid-run with valid JSON");
    }
    feed(core, chans, kSr, 256, fastEnd / 2, fastEnd, false, hostBase, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const Timeline tl = core.timeline();
    const double truth = dm.beats[std::min(dm.beats.size() - 1, (size_t) (59.0 / 0.5))].bpm;
    char buf[160];
    std::snprintf(buf, sizeof buf, "engine: locks on synthetic drums (state %s, %.2f BPM vs %.2f, conf %.2f)", stateName(tl.snap.state), tl.snap.bpm, truth, (double) tl.snap.confidence);
    t.check(tl.snap.state == (uint8_t) EngineState::Locked && std::fabs(tl.snap.bpm - truth) < 0.04 * truth && tl.snap.confidence > 0.3f, buf);

    {
        Json j;
        auto r = cli.Get("/api/status", auth);
        const bool ok = jsonOf(r, j);
        t.check(ok && j["state"].getString() == "LOCKED" && std::fabs(j["tempo"]["bpm"].getNumber() - tl.snap.bpm) < 1.0 && j["confidence"].getNumber() > 0.3,
                "web: /api/status reports LOCKED with the engine tempo");
        auto noTok = cli.Get("/api/status");
        t.check(noTok && noTok->status == 401, "web: token required (401 without it)");
        auto p = cli.Get("/", auth);
        t.check(p && p->status == 200 && p->body.find("Pacemaker") != std::string::npos, "web: main page served");
        auto m = cli.Get("/metronome");
        t.check(m && m->status == 200 && m->body.find("wakeLock") != std::string::npos, "web: /metronome page served");
        auto a = cli.Post("/api/action/tap", auth);
        t.check(a && a->status == 200, "web: POST /api/action/tap");
        auto bad = cli.Post("/api/action/doesnotexist", auth);
        t.check(bad && bad->status == 404, "web: unknown action -> 404");
        auto garbage = cli.Put("/api/state", auth, "{{{ not json", "application/json");
        t.check(garbage && garbage->status == 400, "web: garbage state -> 400, no crash");
        auto mid = cli.Get("/api/tempomap.mid", auth);
        t.check(mid && mid->status == 200 && mid->body.compare(0, 4, "MThd") == 0, "web: tempo map is a MIDI file");
        auto rep = cli.Get("/api/report", auth);
        t.check(rep && rep->status == 200 && rep->body.find("<html") != std::string::npos, "web: drift report html");
        int events = 0; std::string acc;
        cli.Get("/api/events", auth, [&](const char* d, size_t n) { acc.append(d, n); events = 0; for (size_t p2 = 0; (p2 = acc.find("event: status", p2)) != std::string::npos; ++p2) ++events; return events < 3; });
        t.check(events >= 3, "web: SSE delivers status events (" + std::to_string(events) + ")");
    }

    // --- realtime part: outputs live (MIDI clock to the memory sink, LED pulses), then the SIGTERM shutdown sequence
    Indicators ind(core);
    HeadlessConfig lcfg; lcfg.gpio.enabled = true; lcfg.gpio.chip = "fake"; lcfg.gpio.pulseMs = 20;
    ind.start(lcfg);
    sink->clear();
    feed(core, chans, kSr, 256, fastEnd, fastEnd + (int64_t) (5.0 * kSr), true, 0, false);
    const auto recs = sink->records();
    int clocks = 0, starts = 0; for (const auto& r : recs) { if (r.bytes[0] == 0xF8) ++clocks; if (r.bytes[0] == 0xFA || r.bytes[0] == 0xFB) ++starts; }
    t.check(clocks >= 24 * 4, "outputs: MIDI clock running during realtime feed (" + std::to_string(clocks) + " ticks in 5 s)");
    t.check(ind.beatPulses() >= 3 && ind.downbeatPulses() >= 1, "indicators: LED beat / downbeat pulses (" + std::to_string(ind.beatPulses()) + "/" + std::to_string(ind.downbeatPulses()) + ")");
    ind.stop();
    web.stop();
    core.midiClock().stop();                                  // the SIGTERM path: clock thread leaves the hardware stopped
    const auto after = sink->records();
    t.check(!after.empty() && after.back().bytes[0] == 0xFC, "shutdown: MIDI Stop (0xFC) is the last byte sent");
    (void) starts;
    std::printf("selftest: %d/%d checks passed\n", t.total - t.fails, t.total);
    return t.fails == 0 ? 0 : 1;
}

// ====================================================================== --bench
int runBench(double seconds, int block, const std::string& outputs)
{
    seconds = std::clamp(seconds, 5.0, 3600.0);
    const double kSr = 48000.0;
    drums::Machine dm(kSr, 3);
    dm.splitRoles = true;
    dm.bpm = 128; dm.walkSigma = 0.2; dm.jitterMs = 4.0; dm.hihat8ths = true;
    dm.silence(1.0);
    dm.bars((int) std::ceil((seconds + 12.0) / (60.0 / 128.0 * 4.0)) + 1);      // + 12 s for the real-time part
    dm.addNoiseFloor(1e-4f);
    const int64_t total = std::min<int64_t>((int64_t) ((seconds + 11.0) * kSr), (int64_t) dm.roleAudio[0].size());
    const int64_t offlineEnd = (int64_t) (seconds * kSr);

    Core core;
    SessionSettings ss;
    ss.channels = { { 0, InputRole::Kick, 0.1, false }, { 1, InputRole::Snare, 0.1, false }, { 2, InputRole::HiHat, 1.0, false } };
    auto has = [&](const char* k) { return outputs.find(k) != std::string::npos; };
    ss.midi.enabled = has("midi");
    ss.osc.enabled = has("osc"); ss.osc.host = "127.0.0.1"; ss.osc.port = 9;      // discard port: real UDP sends, nobody listens
    ss.sync.enabled = has("sync");
    ss.link.enabled = has("link") && linkCompiledIn();
    core.setSession(ss);
    core.setMidiSink(std::make_shared<MemorySink>());
    core.prepare(kSr, block, 3);
    std::vector<const float*> chans { dm.roleAudio[0].data(), dm.roleAudio[1].data(), dm.roleAudio[2].data() };
    std::vector<float> L((size_t) block + 16), R((size_t) block + 16);

    std::printf("pacemaker_headless --bench: %.0f s of 3-channel drums, block %d, outputs: %s\n", seconds, block, outputs.c_str());

    // A: audio thread cost, faster than real time (this thread only). Outputs are suppressed by the offline flag, so
    // this isolates engine + tracker + sync rendering; it is the number that matters for the audio callback.
    const double tcpu0 = cpuSeconds(CLOCK_THREAD_CPUTIME_ID);
    const auto w0 = Clock::now();
    FeedResult fa;
    {
        std::vector<const float*> ptrs(3);
        const int64_t hostBase = steadyNowUs();
        for (int64_t pos = 0; pos + block <= offlineEnd; pos += block) {
            for (int c = 0; c < 3; ++c) ptrs[(size_t) c] = chans[(size_t) c] + pos;
            CoreHostInfo h; h.offline = true; h.hostTimeUs = hostBase + (int64_t) std::llround((double) pos * 1e6 / kSr);
            const auto b0 = Clock::now();
            core.process(ptrs.data(), 3, block, h, nullptr, 0, L.data(), R.data(), nullptr, nullptr, 0);
            const double us = std::chrono::duration<double, std::micro>(Clock::now() - b0).count();
            fa.maxBlockUs = std::max(fa.maxBlockUs, us); fa.sumBlockUs += us; ++fa.blocks; fa.blockUs.push_back((float) us);
            if ((fa.blocks & 1023) == 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));   // let housekeeping drain the event queues
        }
    }
    const double tcpu = cpuSeconds(CLOCK_THREAD_CPUTIME_ID) - tcpu0;
    const double wallA = std::chrono::duration<double>(Clock::now() - w0).count();
    const double audioSec = (double) fa.blocks * block / kSr;
    std::sort(fa.blockUs.begin(), fa.blockUs.end());
    const double budgetUs = block / kSr * 1e6;
    const double p99 = fa.blockUs.empty() ? 0 : fa.blockUs[(size_t) ((double) fa.blockUs.size() * 0.99)];
    std::printf("A. audio thread, offline: %.1f s audio in %.2f s wall, thread CPU %.3f s => %.2f%% of one core in real time\n",
                audioSec, wallA, tcpu, tcpu / audioSec * 100.0);
    std::printf("   per block (%d samples, budget %.0f us): mean %.1f us, p99 %.1f us, max %.1f us (%.1f%% of budget)\n",
                block, budgetUs, fa.sumBlockUs / std::max<int64_t>(1, fa.blocks), p99, fa.maxBlockUs, fa.maxBlockUs / budgetUs * 100.0);
    std::printf("   final: %s %.1f BPM conf %.2f\n", stateWord(core).c_str(), core.timeline().snap.bpm, (double) core.timeline().snap.confidence);

    // B: whole process in real time (audio + MIDI clock thread + OSC thread + Link + housekeeping), 10 s.
    const double realSec = std::min(10.0, (double) (total - fa.blocks * block) / kSr);
    double procPct = 0;
    if (realSec > 2.0) {
        const int64_t from = fa.blocks * block;
        const double pc0 = cpuSeconds(CLOCK_PROCESS_CPUTIME_ID);
        const auto w1 = Clock::now();
        feed(core, chans, kSr, block, from, from + (int64_t) (realSec * kSr), true, 0, false, L.data(), R.data());
        const double pcpu = cpuSeconds(CLOCK_PROCESS_CPUTIME_ID) - pc0;
        const double wallB = std::chrono::duration<double>(Clock::now() - w1).count();
        procPct = pcpu / wallB * 100.0;
        std::printf("B. whole process, real time: %.1f s wall, process CPU %.3f s => %.2f%% of one core (audio + clock + OSC + Link + housekeeping)\n", wallB, pcpu, procPct);
    }
    std::printf("   MIDI clock sent %llu, OSC sent %llu\n", (unsigned long long) core.midiClock().status().sent.load(), (unsigned long long) core.osc().status().sent.load());
    std::printf("RESULT audio_thread_pct=%.2f process_pct=%.2f max_block_pct_of_budget=%.1f\n", tcpu / audioSec * 100.0, procPct, fa.maxBlockUs / budgetUs * 100.0);
    std::printf("Budget (RS-03): engine + outputs <= 30%% of one Pi 4 core. A Pi 4 core is roughly 3-5x slower than a desktop core:\n"
                "multiply the percentages above by 5 for a conservative estimate, then confirm on the device with the same command.\n");
    return 0;
}

} // namespace pmhl
