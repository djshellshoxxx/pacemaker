// pacemaker_headless: the Pacemaker Box service (RS-03 section 2). JUCE console app, no GUI modules.
#include "AudioBridge.h"
#include "Config.h"
#include "Indicators.h"
#include "Log.h"
#include "Offline.h"
#include "PortManager.h"
#include "UserSettingsStore.h"
#include "pacemaker/Core.h"
#include "pacemaker/WebUi.h"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>
#include <map>
#include <mutex>
#include <thread>

#if defined(__linux__)
#include <dirent.h>
#include <pthread.h>
#include <sched.h>
#endif
#include <sys/stat.h>

using namespace pacemaker;
using namespace pmhl;

#ifndef PACEMAKER_VERSION_STRING
#define PACEMAKER_VERSION_STRING "0.1.0"
#endif

namespace {

constexpr const char* kDefaultConfig = "/etc/pacemaker/config.json";

void onSignal(int) { g_stopRequested.store(true); }

void usage()
{
    std::printf(
        "pacemaker_headless " PACEMAKER_VERSION_STRING " - Pacemaker Box service\n\n"
        "usage: pacemaker_headless [--config FILE] [options]\n"
        "  --config FILE        JSON configuration (default %s; built-in defaults when that file is absent)\n"
        "  --check-config       validate the configuration file and exit (0 = ok, 2 = error)\n"
        "  --list-devices       print audio devices (with channels, rates, buffers) and MIDI ports, then exit\n"
        "  --wav-in FILE        feed the core from a WAV file instead of a sound card\n"
        "      --realtime       pace to the wall clock and drive the real outputs (default: as fast as possible)\n"
        "      --linger SEC     keep the web UI up this long after the file ends (-1 = until SIGTERM)\n"
        "      --out-dir DIR    where the drift log, tempo map and report are written (default .)\n"
        "  --port N             override web.port (0 = no web UI)\n"
        "  --selftest           synthetic drums through the core, checks lock, web API, outputs; exit 0 on success\n"
        "  --bench [SECONDS]    report engine + outputs CPU on this machine (default 60 s of audio)\n"
        "      --bench-outputs L  outputs enabled during --bench: midi,osc,link,sync (default all)\n"
        "  --block N            block size for --wav-in / --bench (default 256)\n"
        "  --stderr             also log to stderr\n"
        "  --version, --help\n",
        kDefaultConfig);
}

// ------------------------------------------------------------------ realtime scheduling
struct ScopedRealtime {
    bool ok = false;
#if defined(__linux__)
    explicit ScopedRealtime(int prio)
    {
        if (prio <= 0) return;
        sched_param sp {};
        sp.sched_priority = std::min(prio, sched_get_priority_max(SCHED_FIFO));
        ok = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) == 0;
    }
    ~ScopedRealtime()
    {
        if (!ok) return;
        sched_param sp {};
        pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);
    }
#else
    explicit ScopedRealtime(int) {}
#endif
};

void logRealtimeThreads()
{
#if defined(__linux__)
    if (DIR* d = opendir("/proc/self/task")) {
        std::string found;
        while (dirent* e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            char path[96]; std::snprintf(path, sizeof path, "/proc/self/task/%s/stat", e->d_name);
            if (std::FILE* f = std::fopen(path, "r")) {
                char line[1024] = {};
                if (std::fgets(line, sizeof line, f)) {
                    const char* rp = std::strrchr(line, ')');          // fields after the comm: state ppid ... policy is field 41 overall
                    int field = 2; long policy = 0, prio = 0;
                    for (const char* p = rp ? rp + 1 : line; *p; ++p) {
                        if (*p == ' ') { ++field; if (field == 40) prio = std::atol(p + 1); if (field == 41) { policy = std::atol(p + 1); break; } }
                    }
                    if (policy == SCHED_FIFO || policy == SCHED_RR) {
                        std::string comm(line + (std::strchr(line, '(') ? std::strchr(line, '(') - line + 1 : 0));
                        comm = comm.substr(0, comm.find(')'));
                        found += (found.empty() ? "" : ", ") + comm + "[tid " + e->d_name + "](prio " + std::to_string(prio) + ")";
                    }
                }
                std::fclose(f);
            }
        }
        closedir(d);
        PM_LOGI("realtime threads: %s", found.empty() ? "none (check LimitRTPRIO / limits.d rtprio)" : found.c_str());
    }
#endif
}

// ------------------------------------------------------------------ state persistence
bool saveStateFile(const std::string& path, const PluginState& st)
{
    if (path.empty()) return false;
    const auto slash = path.find_last_of('/');
    if (slash != std::string::npos) {
        const std::string dir = path.substr(0, slash);
        for (size_t i = 1; i <= dir.size(); ++i) if (i == dir.size() || dir[i] == '/') ::mkdir(dir.substr(0, i).c_str(), 0755);
    }
    const std::string tmp = path + ".tmp";
    if (!writeTextFile(tmp, st.toJson().dump(2))) return false;
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

// ------------------------------------------------------------------ MIDI inputs (e-drum modules, footswitches)
class MidiInputs {
public:
    MidiInputs(juce::MidiInputCallback& cb, std::vector<std::string> patterns) : cb_(cb), patterns_(std::move(patterns)) {}
    ~MidiInputs() { closeAll(); }
    void closeAll() { open_.clear(); }
    void refresh()
    {
        const auto devices = juce::MidiInput::getAvailableDevices();
        for (auto it = open_.begin(); it != open_.end();) {
            bool present = false;
            for (const auto& d : devices) if (d.identifier == it->first) present = true;
            if (!present) { PM_LOGW("MIDI input '%s' disappeared", it->second.name.toRawUTF8()); it = open_.erase(it); } else ++it;
        }
        for (const auto& d : devices) {
            if (open_.count(d.identifier) || !wanted(d.name)) continue;
            if (auto in = juce::MidiInput::openDevice(d.identifier, &cb_)) {
                in->start();
                PM_LOGI("MIDI input opened: %s", d.name.toRawUTF8());
                open_[d.identifier] = { d.name, std::move(in) };
            }
        }
    }
    int count() const { return (int) open_.size(); }

private:
    bool wanted(const juce::String& name) const
    {
        const bool internal = name.containsIgnoreCase("Pacemaker") || name.containsIgnoreCase("Midi Through");
        for (const auto& p : patterns_) {
            if (p == "*") { if (!internal) return true; }
            else if (!p.empty() && name.containsIgnoreCase(juce::String(p))) return true;
        }
        return false;
    }
    struct Entry { juce::String name; std::unique_ptr<juce::MidiInput> in; };
    juce::MidiInputCallback& cb_;
    std::vector<std::string> patterns_;
    std::map<juce::String, Entry> open_;
};

// ------------------------------------------------------------------ audio device with retry / watchdog
class AudioRunner {
public:
    AudioRunner(AudioBridge& b, const HeadlessConfig& c) : bridge_(b), cfg_(c) {}
    ~AudioRunner() { close(); }

    int outputsNeeded() const { return std::max({ cfg_.audio.syncLeft, cfg_.audio.syncRight, cfg_.audio.calibrationOut }) + 1; }

    bool open(std::string& err)
    {
        auto& types = adm_.getAvailableDeviceTypes();
        if (!cfg_.audio.deviceType.empty()) {
            bool found = false;
            for (auto* t : types) if (t->getTypeName().equalsIgnoreCase(juce::String(cfg_.audio.deviceType))) { adm_.setCurrentAudioDeviceType(t->getTypeName(), true); found = true; }
            if (!found) { err = "audio device type '" + cfg_.audio.deviceType + "' is not available"; return false; }
        }
        auto* type = adm_.getCurrentDeviceTypeObject();
        if (!type) { err = "no audio device type"; return false; }
        type->scanForDevices();
        const auto ins = type->getDeviceNames(true), outs = type->getDeviceNames(false);
        if (ins.isEmpty()) { err = "no audio input devices found"; return false; }
        auto pick = [](const juce::StringArray& names, const std::string& want) -> juce::String {
            if (names.isEmpty()) return {};
            if (want.empty()) return names[0];
            for (const auto& n : names) if (n.equalsIgnoreCase(juce::String(want))) return n;
            for (const auto& n : names) if (n.containsIgnoreCase(juce::String(want))) return n;
            return {};
        };
        const juce::String inName = pick(ins, cfg_.audio.inputDevice);
        if (inName.isEmpty()) { err = "audio input device '" + cfg_.audio.inputDevice + "' not found"; return false; }
        const int nOut = outputsNeeded();
        juce::String outName;
        if (nOut > 0 || !cfg_.audio.outputDevice.empty()) {
            outName = pick(outs, cfg_.audio.outputDevice);
            if (outName.isEmpty()) outName = pick(outs, inName.toStdString());      // same card as the input
            if (outName.isEmpty() && cfg_.audio.outputDevice.empty() && !outs.isEmpty()) outName = outs[0];
        }
        int maxCh = 1;
        for (const auto& c : cfg_.state.session.channels) maxCh = std::max(maxCh, c.channel + 1);
        for (const auto& c : cfg_.audio.inputs) maxCh = std::max(maxCh, c.channel + 1);

        juce::AudioDeviceManager::AudioDeviceSetup s;
        adm_.getAudioDeviceSetup(s);
        s.inputDeviceName = inName; s.outputDeviceName = outName;
        s.sampleRate = cfg_.audio.sampleRate; s.bufferSize = cfg_.audio.bufferSize;
        s.useDefaultInputChannels = false; s.inputChannels.clear(); s.inputChannels.setRange(0, maxCh, true);
        s.useDefaultOutputChannels = false; s.outputChannels.clear();
        if (outName.isNotEmpty() && nOut > 0) s.outputChannels.setRange(0, nOut, true);
        const juce::String e = adm_.setAudioDeviceSetup(s, true);
        if (e.isNotEmpty()) { err = e.toStdString(); return false; }
        if (!added_) { adm_.addAudioCallback(&bridge_); added_ = true; }
        lastCallbacks_ = bridge_.callbackCount();
        lastProgress_ = std::chrono::steady_clock::now();
        PM_LOGI("audio device open: %s, %.0f Hz, buffer %d, %d input channel(s) requested, %d output channel(s)", inName.toRawUTF8(),
                bridge_.sampleRate(), bridge_.bufferSize(), maxCh, outName.isNotEmpty() ? nOut : 0);
        return true;
    }

    void close()
    {
        if (added_) { adm_.removeAudioCallback(&bridge_); added_ = false; }
        adm_.closeAudioDevice();
    }

    // Call about once per second. Reopens a device that vanished or stalled; retries every 5 s while it is missing.
    void tick()
    {
        const auto now = std::chrono::steady_clock::now();
        if (bridge_.running()) {
            if (bridge_.callbackCount() != lastCallbacks_) { lastCallbacks_ = bridge_.callbackCount(); lastProgress_ = now; return; }
            if (now - lastProgress_ < std::chrono::seconds(3)) return;
            PM_LOGW("audio callbacks stopped, closing the device");
            close();
        }
        if (now - lastTry_ < std::chrono::seconds(5)) return;
        lastTry_ = now;
        std::string err;
        if (open(err)) { if (wasDown_) PM_LOGI("audio device is back"); wasDown_ = false; }
        else { if (!wasDown_ || err != lastErr_) PM_LOGW("audio device unavailable: %s (retrying every 5 s)", err.c_str()); wasDown_ = true; lastErr_ = err; }
    }
    bool down() const { return wasDown_; }
    std::string lastError() const { return lastErr_; }

private:
    AudioBridge& bridge_;
    const HeadlessConfig& cfg_;
    juce::AudioDeviceManager adm_;
    bool added_ = false, wasDown_ = false;
    int lastCallbacks_ = 0;
    std::chrono::steady_clock::time_point lastTry_ {}, lastProgress_ {};
    std::string lastErr_;
};

// ------------------------------------------------------------------ --list-devices
int listDevices()
{
    juce::AudioDeviceManager adm;
    std::printf("Audio device types:\n");
    for (auto* type : adm.getAvailableDeviceTypes()) {
        type->scanForDevices();
        std::printf("[%s]\n", type->getTypeName().toRawUTF8());
        const auto ins = type->getDeviceNames(true), outs = type->getDeviceNames(false);
        auto describe = [&](const juce::String& n, bool input) {
            std::unique_ptr<juce::AudioIODevice> d(input ? type->createDevice({}, n) : type->createDevice(n, {}));
            if (!d) { std::printf("  %s %s (cannot open to query)\n", input ? "in " : "out", n.toRawUTF8()); return; }
            const auto chans = input ? d->getInputChannelNames() : d->getOutputChannelNames();
            std::printf("  %s \"%s\": %d ch, rates:", input ? "in " : "out", n.toRawUTF8(), chans.size());
            for (double r : d->getAvailableSampleRates()) std::printf(" %.0f", r);
            std::printf(", buffers:");
            int shown = 0; for (int b : d->getAvailableBufferSizes()) if (b >= 32 && b <= 2048 && shown++ < 8) std::printf(" %d", b);
            std::printf("\n");
        };
        for (const auto& n : ins) describe(n, true);
        for (const auto& n : outs) describe(n, false);
        if (ins.isEmpty() && outs.isEmpty()) std::printf("  (no devices)\n");
    }
    std::printf("MIDI inputs:\n");
    for (const auto& d : juce::MidiInput::getAvailableDevices()) std::printf("  \"%s\"\n", d.name.toRawUTF8());
    std::printf("MIDI outputs:\n");
    for (const auto& d : juce::MidiOutput::getAvailableDevices()) std::printf("  \"%s\"\n", d.name.toRawUTF8());
    std::printf("Virtual MIDI ports: %s\n", MidiPortManager::virtualPortsSupported() ? "supported" : "not supported");
    return 0;
}

// ------------------------------------------------------------------ the service
int runService(HeadlessConfig& cfg, int portOverride)
{
    if (portOverride >= 0) { cfg.web.port = portOverride; cfg.web.enabled = portOverride > 0; }

    // State precedence: config.state < saved state file (changes made in the web UI) < install-time keys in the config.
    PluginState st = cfg.state;
    bool loadedSaved = false;
    if (!cfg.stateFile.empty()) {
        std::string text, warn;
        PluginState saved;
        if (readTextFile(cfg.stateFile, text) && PluginState::deserialise(text, saved, &warn)) { st = saved; loadedSaved = true; PM_LOGI("loaded saved settings from %s", cfg.stateFile.c_str()); }
        else if (!text.empty()) PM_LOGW("ignoring unreadable state file %s", cfg.stateFile.c_str());
    }
    if (!cfg.audio.inputs.empty()) { st.session.channels = cfg.audio.inputs; st.session.autoRoleMap = false; }
    if (!cfg.midi.clockOut.empty()) {
        st.session.midiPort = cfg.midi.clockOut; st.session.midiVirtualPort = cfg.midi.virtualPort;
        if (!loadedSaved) st.session.midi.enabled = true;
    }
    if (st.session.driftLogDir.empty() && !cfg.stateFile.empty()) {
        const auto slash = cfg.stateFile.find_last_of('/');
        if (slash != std::string::npos) st.session.driftLogDir = cfg.stateFile.substr(0, slash) + "/drift";
    }
    cfg.state = st;

    Core core;
    auto ports = std::make_shared<MidiPortManager>();
    core.setMidiSink(ports);
    core.setState(st);
    ports->setPort(juce::String(st.session.midiPort), st.session.midiVirtualPort);
    int nIn = 1;
    for (const auto& c : st.session.channels) nIn = std::max(nIn, c.channel + 1);
    // Prepare first so the housekeeping / output threads exist before any realtime priority is raised.
    core.prepare(cfg.audio.sampleRate, std::max(cfg.audio.bufferSize, 2048), nIn);
    core.setPluginMidiOutput(false);
    bool driftStarted = !st.session.driftLogEnabled;      // the drift log file is created when the first music arrives

    UserSettingsStore store("Pacemaker");
    UserSettings us = store.load();
    std::mutex usMutex;
    AudioBridge bridge(core);
    bridge.setRouting({ cfg.audio.syncLeft, cfg.audio.syncRight, cfg.audio.calibrationOut >= 0 ? cfg.audio.calibrationOut : cfg.audio.syncLeft });
    bridge.setLatencyLookup([&](const juce::String& dev, double sr, int buf, int& hin, int& hout) {
        std::lock_guard<std::mutex> l(usMutex);
        CalibrationEntry e;
        if (!us.calibration.get(CalibrationTable::key(dev.toStdString(), sr, buf), e)) return false;
        hin = e.hiddenInputSamples; hout = e.hiddenOutputSamples;
        return true;
    });

    // ---- web UI
    WebUi web(core);
    WebUi::Hooks hk;
    hk.title = "Pacemaker Box";
    hk.tapAlongAvailable = false;
    hk.virtualMidiSupported = MidiPortManager::virtualPortsSupported();
    hk.midiOutPorts = [] { std::vector<std::string> v; for (const auto& n : MidiPortManager::availablePorts()) v.push_back(n.toStdString()); return v; };
    hk.midiInPorts = [] { std::vector<std::string> v; for (const auto& d : juce::MidiInput::getAvailableDevices()) v.push_back(d.name.toStdString()); return v; };
    hk.deviceInfo = [&bridge] {
        Json j = Json::object();
        j.set("name", bridge.deviceName().toStdString()).set("running", bridge.running()).set("sampleRate", bridge.sampleRate())
         .set("bufferSize", bridge.bufferSize()).set("inputLatency", bridge.reportedInputLatency()).set("outputLatency", bridge.reportedOutputLatency())
         .set("callbacks", bridge.callbackCount());
        return j;
    };
    hk.reportedLatency = [&bridge](int& i, int& o) { i = bridge.reportedInputLatency(); o = bridge.reportedOutputLatency(); };
    hk.beginLoopback = [&](int ch) {
        const auto r = bridge.routing();
        if (r.calibrationOut < 0) return false;
        core.startLoopback(ch);
        return true;
    };
    hk.loopbackFinished = [&](const LoopbackResult& r) {
        if (!r.pass) return;
        std::lock_guard<std::mutex> l(usMutex);
        CalibrationEntry e; e.hiddenInputSamples = r.hiddenInputSamples; e.hiddenOutputSamples = r.hiddenOutputSamples; e.measuredAt = (int64_t) wallNowUs() / 1000000;
        us.calibration.put(CalibrationTable::key(bridge.deviceName().toStdString(), bridge.sampleRate(), bridge.bufferSize()), e);
        us.lastInputDevice = bridge.deviceName().toStdString();
        store.save(us);
        PM_LOGI("loopback calibration stored: hidden in %d, out %d samples", r.hiddenInputSamples, r.hiddenOutputSamples);
    };
    hk.stateChanged = [&](const PluginState& s) {
        ports->setPort(juce::String(s.session.midiPort), s.session.midiVirtualPort);
        if (!cfg.stateFile.empty() && !saveStateFile(cfg.stateFile, s)) PM_LOGW("cannot save settings to %s", cfg.stateFile.c_str());
    };
    hk.extraStatus = [&](Json& j) {
        Json d = Json::object();
        d.set("name", bridge.deviceName().toStdString()).set("running", bridge.running()).set("sampleRate", bridge.sampleRate()).set("bufferSize", bridge.bufferSize());
        j.set("device", d).set("host", "headless");
    };
    web.setHooks(hk);
    web.setToken(cfg.web.token);
    if (cfg.web.enabled) {
        if (web.start(cfg.web.port, cfg.web.bind)) PM_LOGI("web UI listening on %s:%d%s", cfg.web.bind.c_str(), web.port(), cfg.web.token.empty() ? "" : " (token required for the API)");
        else PM_LOGE("web UI could not start: %s", web.lastError().c_str());
    }

    Indicators indicators(core);
    indicators.start(cfg);

    // ---- audio + MIDI inputs (realtime priority is held only while the device threads are created)
    MidiInputs midiIn(bridge, cfg.midi.inputs);
    AudioRunner audio(bridge, cfg);
    {
        ScopedRealtime rt(cfg.rtPriority);
        if (cfg.rtPriority > 0) { if (rt.ok) PM_LOGI("realtime priority %d granted for the audio thread", cfg.rtPriority); else PM_LOGW("realtime priority %d refused (set LimitRTPRIO / rtprio in limits.d); running at normal priority", cfg.rtPriority); }
        audio.tick();       // first open attempt; failures are logged and retried every 5 s from the main loop
    }
    midiIn.refresh();
    logRealtimeThreads();
    PM_LOGI("pacemaker_headless " PACEMAKER_VERSION_STRING " running");

    // ---- main loop
    auto lastSlow = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    auto lastTick = lastSlow;
    while (!g_stopRequested.load()) {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        const auto now = std::chrono::steady_clock::now();
        if (now - lastTick >= std::chrono::seconds(1)) {
            lastTick = now;
            if (!driftStarted && core.timeline().snap.state != (uint8_t) EngineState::Idle) {
                driftStarted = true;
                if (core.startDriftLog()) PM_LOGI("drift log: %s", core.driftLogPath().c_str()); else PM_LOGW("drift log could not be opened");
            }
            ScopedRealtime rt(cfg.rtPriority);        // device threads (re)created by a reopen inherit the priority
            audio.tick();
        }
        if (now - lastSlow >= std::chrono::seconds(5)) {
            lastSlow = now;
            ports->refresh();          // missing MIDI clock port: keep the name and retry (RS-03 section 8)
            midiIn.refresh();
        }
    }

    // ---- shutdown (SIGTERM / SIGINT): leave external gear stopped, then release everything
    PM_LOGI("signal received, shutting down");
    indicators.stop();
    web.stop();
    audio.close();
    midiIn.closeAll();
    core.midiClock().stop();                           // sends MIDI Stop (0xFC) if the clock was running
    core.stopDriftLog();
    if (!cfg.stateFile.empty()) saveStateFile(cfg.stateFile, core.state());
    PM_LOGI("bye");
    Log::get().close();
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    std::string configPath = kDefaultConfig;
    bool configGiven = false, checkOnly = false, listOnly = false, selftest = false, bench = false, toStderr = false;
    double benchSec = 60; std::string benchOutputs = "midi,osc,link,sync";
    int portOverride = -1, block = 256;
    WavOptions wav;
    auto need = [&](int& i) -> const char* {
        if (i + 1 >= argc) { std::fprintf(stderr, "pacemaker_headless: %s needs a value\n", argv[i]); std::exit(2); }
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") { usage(); return 0; }
        else if (a == "--version") { std::printf("pacemaker_headless " PACEMAKER_VERSION_STRING "\n"); return 0; }
        else if (a == "--config") { configPath = need(i); configGiven = true; }
        else if (a == "--check-config") checkOnly = true;
        else if (a == "--list-devices") listOnly = true;
        else if (a == "--wav-in") wav.wav = need(i);
        else if (a == "--realtime") wav.realtime = true;
        else if (a == "--linger") wav.lingerSec = std::atof(need(i));
        else if (a == "--out-dir") wav.outDir = need(i);
        else if (a == "--port") portOverride = std::atoi(need(i));
        else if (a == "--selftest") selftest = true;
        else if (a == "--bench") { bench = true; if (i + 1 < argc && argv[i + 1][0] != '-') benchSec = std::atof(argv[++i]); }
        else if (a == "--bench-outputs") benchOutputs = need(i);
        else if (a == "--block") block = std::max(32, std::atoi(need(i)));
        else if (a == "--stderr") toStderr = true;
        else { std::fprintf(stderr, "pacemaker_headless: unknown option '%s' (see --help)\n", a.c_str()); return 2; }
    }

    struct sigaction sa {};
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);

    juce::ScopedJuceInitialiser_GUI juceInit;        // message manager for device callbacks; no GUI modules are linked

    if (listOnly) return listDevices();
    if (selftest) { Log::get().open("", 0, 1, true); return runSelftest(); }
    if (bench) { Log::get().open("", 0, 1, true); return runBench(benchSec, block, benchOutputs); }

    HeadlessConfig cfg;
    std::string err;
    struct stat sbuf {};
    if (configGiven || ::stat(configPath.c_str(), &sbuf) == 0) {
        if (!HeadlessConfig::load(configPath, cfg, err)) { std::fprintf(stderr, "pacemaker_headless: %s\n", err.c_str()); return 2; }
    } else {
        std::fprintf(stderr, "pacemaker_headless: %s not found, using built-in defaults\n", configPath.c_str());
    }
    if (checkOnly) {
        std::printf("config ok: audio %s %.0f Hz buffer %d, %zu input mapping(s), web %s:%d%s, midi clock out '%s', gpio %s, oled %s, state file '%s'\n",
                    cfg.audio.deviceType.empty() ? "default" : cfg.audio.deviceType.c_str(), cfg.audio.sampleRate, cfg.audio.bufferSize,
                    cfg.audio.inputs.size(), cfg.web.bind.c_str(), cfg.web.port, cfg.web.token.empty() ? "" : " (token)", cfg.midi.clockOut.c_str(),
                    cfg.gpio.enabled ? "on" : "off", cfg.oled.enabled ? "on" : "off", cfg.stateFile.c_str());
        return 0;
    }
    Log::get().open(cfg.log.file, (long) cfg.log.maxKB * 1024, cfg.log.keep, toStderr || !wav.wav.empty() || cfg.log.file.empty());

    if (!wav.wav.empty()) {
        wav.webPort = portOverride;
        wav.block = block;
        const int rc = runWavIn(cfg, wav);
        Log::get().close();
        return rc;
    }
    return runService(cfg, portOverride);
}
