#include "pacemaker/Core.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace pacemaker {

struct Core::Rig {
    struct Slot {
        InputRole role = InputRole::Any;
        int nCh = 0;
        int ch[kMaxChannels] {};
        int delay = 0;                    // compensation delay in samples (dmax - acoustic)
        std::vector<float> ring;
        int w = 0;
        std::vector<float> buf;
    };
    Engine engine;
    EngineConfig cfg;
    Slot slots[kMaxRoles];
    int nSlots = 0;
    int dmax = 0;
    int modLat = 0;
    int64_t base = 0;                     // core clock sample corresponding to engine sample 0
    NoteTable notes;
    std::vector<MidiMapping> maps;
    int beatsPerBar = 4;
};

namespace {
inline int64_t usOf(int64_t samples, double sr) { return (int64_t) std::llround((double) samples * 1.0e6 / sr); }
}

Core::Core() : arbiter_(1)
{
    for (auto& a : slotRole_) a = 0;
    for (auto& l : level_) l = 0.0f;
    for (auto& t : trace_) t = 0.0f;
    syncCfg_.write(SyncConfig {});
    countInRT_.write(CountInRT {});
    Timeline tl; timeline_.write(tl);
    // Per-process random-ish id for redundancy tie-breaks.
    arbiter_.setId((uint32_t) (steadyNowUs() & 0x7FFFFFFF) | 1u);
}

Core::~Core()
{
    hkStop_ = true;
    if (hk_.joinable()) hk_.join();
    oscIn_.stop();
    clock_.stop();
    oscOut_.stop();
    stopDriftLog();
}

// ---------------------------------------------------------------- configuration
void Core::prepare(double sr, int maxBlock, int numCh)
{
    {
        std::lock_guard<std::mutex> l(cfgMutex_);
        sampleRate_ = sr; maxBlock_ = std::max(16, maxBlock); numChannels_ = std::max(0, numCh);
        prepared_ = true;
        sync_.prepare(sr);
        loopback_.prepare(sr);
        traceBlock_ = std::max(1, (int) (sr / 100.0));
        filter_.reset();
        installRig(buildRig(params_, session_));
        applySessionToOutputs(session_);
    }
    if (!hk_.joinable()) {
        clock_.start(&timeline_);
        oscOut_.start(&timeline_);
        hkStop_ = false;
        hk_ = std::thread([this] { housekeeping(); });
    }
}

std::unique_ptr<Core::Rig> Core::buildRig(const Params& p, const SessionSettings& s)
{
    auto r = std::make_unique<Rig>();
    EngineConfig ec;
    ec.sampleRate = sampleRate_; ec.maxBlockSize = maxBlock_;
    ec.meter.beatsPerBar = p.meterNum; ec.meter.beatUnit = p.meterDen;
    ec.tempoRange.minBpm = p.tempoMin; ec.tempoRange.maxBpm = p.tempoMax;
    ec.referenceBpm = p.referenceBpm;
    ec.profile = p.followProfile();
    ec.pattern = p.pattern;
    ec.sensitivity = p.sensitivity;
    ec.autoOctave = p.autoOctave;
    ec.grouping78 = (Grouping78) std::clamp(p.grouping78, 0, 2);
    if (s.learnedValid) ec.learnedPattern = s.learned;

    std::vector<ChannelAssign> chans;
    for (const auto& c : s.channels) if (!c.mute && c.channel < numChannels_ && c.channel < kMaxChannels) chans.push_back(c);
    if (chans.empty() && s.channels.empty() && numChannels_ > 0) { ChannelAssign d; d.channel = 0; d.role = InputRole::Any; d.distanceM = 0.0; chans.push_back(d); }

    int acoustic[kMaxRoles] {};
    for (const auto& c : chans) {
        int si = -1;
        for (int k = 0; k < r->nSlots; ++k) if (r->slots[k].role == c.role) { si = k; break; }
        if (si < 0) {
            if (r->nSlots < kMaxRoles) { si = r->nSlots++; r->slots[si].role = c.role; acoustic[si] = roleDelaySamples(c.role, c.distanceM, 0.0, sampleRate_); }
            else si = r->nSlots - 1;
        }
        Rig::Slot& sl = r->slots[si];
        if (sl.nCh < kMaxChannels) sl.ch[sl.nCh++] = c.channel;
    }
    if (r->nSlots == 0) { r->nSlots = 1; r->slots[0].role = InputRole::Any; }   // MIDI / trigger only
    for (int k = 0; k < r->nSlots; ++k) r->dmax = std::max(r->dmax, acoustic[k]);
    for (int k = 0; k < r->nSlots; ++k) {
        Rig::Slot& sl = r->slots[k];
        sl.delay = r->dmax - acoustic[k];
        sl.ring.assign((size_t) r->dmax + 1, 0.0f);
        sl.buf.assign((size_t) maxBlock_ + 16, 0.0f);
        ec.roles[k] = sl.role;
    }
    ec.numRoles = r->nSlots;
    r->modLat = (int) std::lround(s.moduleLatencyMs * 0.001 * sampleRate_);
    r->notes = s.notes; r->maps = s.mappings;
    r->beatsPerBar = p.meterNum;
    r->cfg = ec;
    r->engine.prepare(ec);
    r->engine.setFollow(follow_.load());
    return r;
}

void Core::installRig(std::unique_ptr<Rig> r)
{
    Rig* raw = r.get();
    rigs_.push_back(std::move(r));
    // Keep the last three rigs alive: the audio thread may still be inside the previous one.
    while (rigs_.size() > 3) rigs_.erase(rigs_.begin());
    slotCount_ = raw->nSlots;
    for (int k = 0; k < 4; ++k) slotRole_[k] = k < raw->nSlots ? (int) raw->slots[k].role : 0;
    driftBeatsPerBar_ = raw->cfg.meter.beatsPerBar; driftBeatUnit_ = raw->cfg.meter.beatUnit;
    uiRig_ = raw;
    pending_ = raw;
}

bool Core::needsRebuild(const Params& a, const Params& b, const SessionSettings& sa, const SessionSettings& sb) const
{
    if (a.profile != b.profile || a.alpha != b.alpha || a.beta != b.beta || a.tempoMin != b.tempoMin || a.tempoMax != b.tempoMax
        || a.referenceBpm != b.referenceBpm || a.meterNum != b.meterNum || a.meterDen != b.meterDen || a.pattern != b.pattern
        || a.autoOctave != b.autoOctave || a.grouping78 != b.grouping78 || a.sensitivity != b.sensitivity) return true;
    if (sa.moduleLatencyMs != sb.moduleLatencyMs || sa.channels.size() != sb.channels.size()) return true;
    for (size_t i = 0; i < sa.channels.size(); ++i) {
        const auto &x = sa.channels[i], &y = sb.channels[i];
        if (x.channel != y.channel || x.role != y.role || x.distanceM != y.distanceM || x.mute != y.mute) return true;
    }
    if (sa.notes.kick != sb.notes.kick || sa.notes.snare != sb.notes.snare || sa.notes.hat != sb.notes.hat || sa.notes.anyNote != sb.notes.anyNote) return true;
    if (sa.mappings.size() != sb.mappings.size()) return true;
    for (size_t i = 0; i < sa.mappings.size(); ++i) {
        const auto &x = sa.mappings[i], &y = sb.mappings[i];
        if (x.isCC != y.isCC || x.channel != y.channel || x.number != y.number || x.action != y.action) return true;
    }
    return false;
}

void Core::applySessionToOutputs(const SessionSettings& s)
{
    clock_.setConfig(s.midi);
    pluginMidiCfg_.write(s.midi);
    oscOut_.setConfig(s.osc);
    link_.setSettings(s.link);
    syncCfg_.write(s.sync);
    CountInRT ci; ci.enabled = s.countIn.enabled; ci.note = s.countIn.midiNote; ci.channel = s.countIn.midiChannel;
    const std::string ciAddr = s.countIn.oscAddress.empty() ? s.osc.root + "/countin" : s.countIn.oscAddress;
    std::snprintf(ci.osc, sizeof ci.osc, "%s", ciAddr.c_str());
    countInRT_.write(ci);
    inputLatencyMs_ = s.inputLatencyMs;
    nudgeMs_ = s.nudgeMs;
    suppressOffline_ = !s.offlineOutputsEnabled;
    driftOffline_ = s.driftLogOffline;
    sectionSuggest_ = s.sectionSuggestions;
    redundancyOn_ = s.redundancyEnabled && !s.redundancyPeer.empty();
    if (hbPeer_ != s.redundancyPeer) {
        hbSender_.close(); hbPeer_ = s.redundancyPeer;
        const auto colon = hbPeer_.rfind(':');
        if (colon != std::string::npos) hbSender_.open(hbPeer_.substr(0, colon), std::atoi(hbPeer_.c_str() + colon + 1));
    }
    const int want = s.osc.listenPort;
    if (want != oscListenPort_ || (want > 0 && !oscIn_.running())) {
        oscIn_.stop(); oscListenPort_ = want;
        if (want > 0) oscIn_.start(want, [this](const OscMessage& m) { handleOsc(m); });
    }
}

PluginState Core::state() const
{
    std::lock_guard<std::mutex> l(cfgMutex_);
    PluginState st; st.params = params_; st.params.follow = follow_.load(); st.session = session_;
    st.session.songs.current = currentSong_.load();
    return st;
}

void Core::setState(const PluginState& st)
{
    std::lock_guard<std::mutex> l(cfgMutex_);
    const bool rebuild = prepared_ && needsRebuild(params_, st.params, session_, st.session);
    params_ = st.params; session_ = st.session;
    follow_ = params_.follow;
    currentSong_ = session_.songs.current;
    if (prepared_) {
        if (rebuild) installRig(buildRig(params_, session_));
        applySessionToOutputs(session_);
        if (uiRig_.load()) uiRig_.load()->engine.setFollow(follow_.load());
    }
}

Params Core::params() const { std::lock_guard<std::mutex> l(cfgMutex_); Params p = params_; p.follow = follow_.load(); return p; }
void Core::setParams(const Params& p)
{
    std::lock_guard<std::mutex> l(cfgMutex_);
    const bool rebuild = prepared_ && needsRebuild(params_, p, session_, session_);
    params_ = p;
    follow_ = p.follow;
    if (prepared_) {
        if (rebuild) installRig(buildRig(params_, session_));
        if (uiRig_.load()) uiRig_.load()->engine.setFollow(follow_.load());
    }
}
SessionSettings Core::session() const { std::lock_guard<std::mutex> l(cfgMutex_); return session_; }
void Core::setSession(const SessionSettings& s)
{
    std::lock_guard<std::mutex> l(cfgMutex_);
    const bool rebuild = prepared_ && needsRebuild(params_, params_, session_, s);
    session_ = s;
    if (prepared_) { if (rebuild) installRig(buildRig(params_, session_)); applySessionToOutputs(session_); }
}
void Core::editSession(const std::function<void(SessionSettings&)>& fn)
{
    SessionSettings s = session(); fn(s); setSession(s);
}

void Core::setDeviceLatency(int rin, int rout, int hin, int hout)
{
    reportedIn_ = rin; reportedOut_ = rout; hiddenIn_ = hin; hiddenOut_ = hout;
    inputLatencySamples_ = rin + hin;
    outputLatencySamples_ = rout + hout;
}

// ---------------------------------------------------------------- actions
void Core::tap() { doAction(Action::Tap); }
void Core::downbeatNow() { doAction(Action::DownbeatNow); }
void Core::relock() { doAction(Action::Relock); }
void Core::half() { doAction(Action::Half); }
void Core::doubleTime() { doAction(Action::Double); }
void Core::shiftBar(int d) { doAction(d < 0 ? Action::ShiftMinus : Action::ShiftPlus); }

void Core::doAction(Action a)
{
    Rig* r = uiRig_.load();
    if (!r) return;
    switch (a) {
    case Action::Follow: setFollow(!follow_.load()); break;
    case Action::Tap: r->engine.tap(r->engine.samplesProcessed()); break;
    case Action::DownbeatNow: r->engine.downbeatNow(); break;
    case Action::Relock: r->engine.relock(); break;
    case Action::Half: r->engine.halfTime(); break;
    case Action::Double: r->engine.doubleTime(); break;
    case Action::ShiftMinus: r->engine.shiftBar(-1); break;
    case Action::ShiftPlus: r->engine.shiftBar(1); break;
    case Action::NudgeMinus: nudge(-10.0); break;
    case Action::NudgePlus: nudge(10.0); break;
    case Action::NextSong: songDelta_.fetch_add(1); break;
    case Action::PrevSong: songDelta_.fetch_sub(1); break;
    default: break;
    }
}

void Core::audioDoAction(Action a, int64_t atSample)
{
    Rig* r = active_;
    if (!r) return;
    switch (a) {
    case Action::Tap: r->engine.tap(atSample); break;
    case Action::Follow: { const bool f = !follow_.load(); follow_ = f; r->engine.setFollow(f); break; }
    case Action::NudgeMinus: nudgeMs_ = std::clamp(nudgeMs_.load() - 10.0, -100.0, 100.0); break;
    case Action::NudgePlus: nudgeMs_ = std::clamp(nudgeMs_.load() + 10.0, -100.0, 100.0); break;
    default: doAction(a); break;
    }
}

void Core::nudge(double ms) { nudgeMs_ = std::clamp(nudgeMs_.load() + ms, -100.0, 100.0); }
void Core::setFollow(bool f)
{
    follow_ = f;
    if (Rig* r = uiRig_.load()) r->engine.setFollow(f);
}
void Core::learnPattern()
{
    if (Rig* r = uiRig_.load()) { learnRev_ = r->engine.learnedRevision(); learnWatch_ = true; r->engine.learnPattern(); }
}
bool Core::engineLearning() const { Rig* r = uiRig_.load(); return r && r->engine.learning(); }

void Core::selectSong(int index)
{
    std::lock_guard<std::mutex> l(cfgMutex_);
    if (index < 0 || index >= (int) session_.songs.songs.size()) return;
    session_.songs.apply(index, params_);
    session_.songs.current = index;
    currentSong_ = index;
    if (prepared_) installRig(buildRig(params_, session_));
    if (driftLog_.isOpen() || !driftRows_.empty()) {
        DriftRow row; row.timestampUs = wallNowUs(); row.event = "song:" + session_.songs.songs[(size_t) index].name;
        row.state = timeline_.read().snap.state;
        std::lock_guard<std::mutex> d(driftMutex_);
        if (driftLog_.isOpen()) driftLog_.write(row);
        driftRows_.push_back(row);
    }
}

void Core::nextSong(int delta)
{
    int n; int cur;
    { std::lock_guard<std::mutex> l(cfgMutex_); n = (int) session_.songs.songs.size(); cur = currentSong_.load(); }
    if (n <= 0) return;
    const int idx = ((cur < 0 ? (delta > 0 ? -1 : 0) : cur) + delta) % n;
    selectSong(idx < 0 ? idx + n : idx);
}

void Core::handleOsc(const OscMessage& m)
{
    const std::string root = [&] { std::lock_guard<std::mutex> l(cfgMutex_); return session_.osc.root; }();
    if (m.address.compare(0, root.size(), root) != 0) return;
    const std::string a = m.address.substr(root.size());
    auto argI = [&](size_t i, int d) { return i < m.args.size() ? (m.args[i].type == 'f' ? (int) m.args[i].f : m.args[i].i) : d; };
    if (a == "/song") pendingSong_ = argI(0, -1);
    else if (a == "/song/name" && !m.args.empty() && m.args[0].type == 's') {
        std::lock_guard<std::mutex> l(cfgMutex_);
        pendingSong_ = session_.songs.findName(m.args[0].s);
    }
    else if (a == "/follow") setFollow(argI(0, 1) != 0);
    else if (a == "/tap") doAction(Action::Tap);
    else if (a == "/downbeat") doAction(Action::DownbeatNow);
    else if (a == "/relock") doAction(Action::Relock);
    else if (a == "/half") doAction(Action::Half);
    else if (a == "/double") doAction(Action::Double);
    else if (a == "/nudge" && !m.args.empty()) nudge(m.args[0].type == 'f' ? m.args[0].f : (double) m.args[0].i);
    else if (a == "/redundancy/hb" && m.args.size() >= 3) {
        arbiter_.onPeer((uint32_t) m.args[0].i, m.args[1].f, m.args[2].i != 0, steadyNowUs());
    }
}

// ---------------------------------------------------------------- audio thread
void Core::handleMidi(Rig& r, const CoreMidiIn& m)
{
    const int type = m.status & 0xF0, ch = (m.status & 0x0F) + 1;
    auto mapped = [&](bool isCC, int number) {
        for (const auto& mm : r.maps)
            if (mm.isCC == isCC && mm.number == number && (mm.channel == 0 || mm.channel == ch)) return mm.action;
        return Action::None;
    };
    const int64_t at = r.engine.samplesProcessed() + m.offset;
    if (const int la = learnAction_.load(std::memory_order_relaxed)) {
        if ((type == 0x90 && m.d2 > 0) || (type == 0xB0 && m.d2 >= 64)) {
            MidiMapping lm; lm.isCC = type == 0xB0; lm.channel = ch; lm.number = m.d1; lm.action = (Action) la;
            learnedQ_.push(lm);
            learnAction_ = 0;
            return;
        }
    }
    if (type == 0x90 && m.d2 > 0) {
        const Action a = mapped(false, m.d1);
        if (a != Action::None) { audioDoAction(a, at); return; }
        InputRole role;
        if (r.notes.classify(m.d1, role)) {
            DiscreteOnset ev; ev.sample = std::max<int64_t>(0, at - r.modLat + r.dmax); ev.role = role; ev.velocity = (float) m.d2 / 127.0f;
            r.engine.pushEvent(ev);
        }
    } else if (type == 0xB0) {
        const Action a = mapped(true, m.d1);
        if (a != Action::None && m.d2 >= 64) audioDoAction(a, at);
    } else if (type == 0xC0) {
        const int idx = -2;  // resolved in housekeeping through the song map (program number only)
        (void) idx;
        pendingProgram_ = m.d1;
    }
}

int Core::process(const float* const* in, int numCh, int n, const CoreHostInfo& host, const CoreMidiIn* midiIn, int numMidiIn,
                  float* syncL, float* syncR, float* calOut, CoreMidiOut* midiOut, int maxMidiOut)
{
    if (n <= 0) return 0;
    if (Rig* p = pending_.exchange(nullptr)) {
        active_ = p;
        p->base = clockSample_;
    }
    Rig* r = active_;
    if (!r) { clockSample_ += n; return 0; }
    n = std::min(n, maxBlock_);

    // ---- host time
    const int64_t hostUs = host.hostTimeUs >= 0 ? host.hostTimeUs : filter_.update(clockSample_, steadyNowUs());

    // ---- meters
    for (int c = 0; c < std::min(numCh, kMaxChannels); ++c) {
        float pk = 0; const float* x = in[c];
        if (x) for (int i = 0; i < n; ++i) pk = std::max(pk, std::fabs(x[i]));
        const float prev = level_[c].load(std::memory_order_relaxed) * 0.92f;
        level_[c].store(std::max(prev, pk), std::memory_order_relaxed);
    }

    // ---- loopback calibration
    if (loopback_.active()) {
        const float* x = loopbackChannel_ < numCh ? in[loopbackChannel_] : nullptr;
        loopback_.process(x, calOut, n);
    }

    // ---- role mix with acoustic compensation
    const float* roleBuf[kMaxRoles] {};
    for (int k = 0; k < r->nSlots; ++k) {
        Rig::Slot& sl = r->slots[k];
        float* b = sl.buf.data();
        std::fill(b, b + n, 0.0f);
        for (int j = 0; j < sl.nCh; ++j) {
            const int c = sl.ch[j];
            if (c >= numCh || !in[c]) continue;
            const float* x = in[c];
            for (int i = 0; i < n; ++i) b[i] += x[i];
        }
        if (sl.delay > 0 || r->dmax > 0) {
            const int len = (int) sl.ring.size();
            for (int i = 0; i < n; ++i) {
                sl.ring[(size_t) sl.w] = b[i];
                int rd = sl.w - sl.delay; if (rd < 0) rd += len;
                b[i] = sl.ring[(size_t) rd];
                if (++sl.w >= len) sl.w = 0;
            }
        }
        roleBuf[k] = b;
    }

    // ---- MIDI in (before the audio so discrete events precede the hop that consumes them)
    for (int i = 0; i < numMidiIn; ++i) handleMidi(*r, midiIn[i]);

    // ---- engine
    r->engine.process(roleBuf, n, clockSample_ - r->base, hostUs);

    // ---- block energy / trace
    {
        double e = 0; const float* b = roleBuf[0];
        for (int k = 0; k < r->nSlots; ++k) { const float* x = roleBuf[k]; for (int i = 0; i < n; ++i) e += (double) x[i] * x[i]; (void) b; }
        barEnergy_ += e; barEnergyN_ += (int64_t) n * r->nSlots;
        traceAcc_ += e; traceCount_ += n;
        while (traceCount_ >= traceBlock_) {
            const double ms = traceAcc_ / std::max(1, traceCount_ * r->nSlots);
            const double db = 10.0 * std::log10(ms + 1e-10);
            const float nov = (float) std::clamp((db - prevDb_) / 12.0, 0.0, 1.0);
            prevDb_ = db;
            const uint32_t h = traceHead_.load(std::memory_order_relaxed);
            trace_[h % kTraceLen].store(nov, std::memory_order_relaxed);
            traceHead_.store(h + 1, std::memory_order_release);
            traceAcc_ = 0; traceCount_ = 0;
        }
    }

    // ---- timeline
    Timeline tl;
    tl.snap = r->engine.snapshot();
    if (tl.snap.beatOriginSample != 0 || tl.snap.nextBeatSample != 0) { tl.snap.beatOriginSample += r->base; tl.snap.nextBeatSample += r->base; }
    tl.beatsPerBar = r->beatsPerBar;
    tl.follow = follow_.load() ? 1 : 0;
    ClockMap& m = tl.map;
    m.sampleRate = sampleRate_; m.originSample = clockSample_;
    m.originHostUs = hostUs - usOf(inputLatencySamples_.load() + r->dmax, sampleRate_)
                     - (int64_t) std::llround(inputLatencyMs_.load() * 1000.0) + (int64_t) std::llround(nudgeMs_.load() * 1000.0);
    m.outputLatencyUs = 0;
    m.valid = true;
    tl.publishedAtUs = steadyNowUs();
    tl.suppressed = ((host.offline && suppressOffline_.load()) || standby_.load()) ? 1 : 0;

    // ---- events
    EngineEvent ev;
    while (r->engine.popEvent(ev)) {
        if (ev.type == EngineEvent::Onset) ev.onset.sample += r->base; else ev.beat.sample += r->base;
        onEngineEvent(*r, ev, tl);
    }
    prevState_ = tl.snap.state;
    if (tl.snap.state == (uint8_t) EngineState::CountIn) countInSeen_ = true;

    if (tapAlongOn_.load()) tapAlong_.onBlock(host.playing && host.hasPpq, host.ppq, host.bpm, clockSample_, sampleRate_);

    if (deviceLost_.load() && tl.snap.state == (uint8_t) EngineState::Locked) tl.snap.state = (uint8_t) EngineState::Hold;
    timeline_.write(tl);

    // ---- sync bus
    int result = 0;
    if (syncL && syncR) {
        SyncConfig sc = syncCfg_.read();
        sc.offsetMs -= (double) (outputLatencySamples_.load() + (host.outputLatencyUs ? usOf(1, 1e6) * 0 : 0)) * 1000.0 / sampleRate_;
        BeatMapSnapshot s = tl.snap;
        // Sync pulses are placed in the engine's sample clock shifted by the input compensation so that they line
        // up with what the other outputs publish.
        const int64_t shift = (int64_t) inputLatencySamples_.load() + r->dmax + (int64_t) std::llround(inputLatencyMs_.load() * 0.001 * sampleRate_)
                              - (int64_t) std::llround(nudgeMs_.load() * 0.001 * sampleRate_);
        s.beatOriginSample -= shift; s.nextBeatSample -= shift;
        if (tl.suppressed) sc.enabled = false;
        sync_.render(s, tl.beatsPerBar, clockSample_, n, sc, syncL, syncR);
    }

    // ---- Link
    link_.process(tl, steadyNowUs());

    // ---- plugin MIDI clock output (CLAP / AU / LV2)
    if (wantMidiOut_.load() && midiOut && maxMidiOut > 0 && !tl.suppressed) {
        const MidiClockConfig mc = pluginMidiCfg_.read();
        planBuf_.clear();
        const double per = periodSamples(tl.snap);
        const double offS = mc.offsetMs * 0.001 * sampleRate_ - (double) (inputLatencySamples_.load() + r->dmax)
                            - inputLatencyMs_.load() * 0.001 * sampleRate_ + nudgeMs_.load() * 0.001 * sampleRate_;
        pluginMidi_.plan(tl.snap, tl.beatsPerBar, (double) tl.snap.beatOriginSample, per, offS, (double) clockSample_,
                         (double) (clockSample_ + n), 0.0, mc, planBuf_);
        for (const auto& e : planBuf_) {
            if (result >= maxMidiOut) break;
            CoreMidiOut& o = midiOut[result++];
            o.offset = std::clamp((int) std::llround(e.t) - (int) clockSample_, 0, n - 1);
            o.len = e.len; std::memcpy(o.bytes, e.bytes, 3);
        }
    }

    clockSample_ += n;
    return result;
}

void Core::onEngineEvent(Rig& r, const EngineEvent& ev, const Timeline& tl)
{
    if (ev.type == EngineEvent::Onset) {
        if (tapAlongOn_.load()) tapAlong_.onOnset(ev.onset.sample);
        if (hwOn_.load()) {
            if ((int) ev.onset.role == hwRole_.load()) hw_.addClick(ev.onset.sample); else hw_.addHit(ev.onset.sample);
        }
    } else {
        // Count-in launcher: the first downbeat after a count-in.
        if (countInSeen_ && ev.beat.state == (uint8_t) EngineState::Locked) {
            if (ev.beat.beatInBar == 1) {
                countInSeen_ = false;
                const CountInRT ci = countInRT_.read();
                if (ci.enabled) {
                    const int64_t t = std::max(steadyNowUs(), tl.map.hostTimeForSample(ev.beat.sample));
                    if (ci.note >= 0) {
                        const uint8_t on[3] = { (uint8_t) (0x90 | ((ci.channel - 1) & 0x0F)), (uint8_t) ci.note, 100 };
                        const uint8_t off[3] = { (uint8_t) (0x80 | ((ci.channel - 1) & 0x0F)), (uint8_t) ci.note, 0 };
                        clock_.postFromAudio(t, on, 3);
                        clock_.postFromAudio(t + 50000, off, 3);
                    }
                    if (ci.osc[0]) {
                        OscThread::Quick q; std::snprintf(q.addr, sizeof q.addr, "%s", ci.osc); q.i0 = ev.beat.barNumber; q.nInts = 1;
                        oscOut_.postFromAudio(q);
                    }
                }
            }
        }
        // Section suggestions: one energy value per bar.
        if (ev.beat.beatInBar == 1 && sectionSuggest_.load() && barEnergyN_ > 0) {
            const int s = section_.onBar(barEnergy_ / (double) barEnergyN_);
            if (s != 0) {
                OscThread::Quick q; std::snprintf(q.addr, sizeof q.addr, "/pacemaker/section"); q.i0 = s; q.i1 = ev.beat.barNumber; q.nInts = 2;
                oscOut_.postFromAudio(q);
            }
        }
        if (ev.beat.beatInBar == 1) { barEnergy_ = 0; barEnergyN_ = 0; }
    }
    (void) r;
    evQ_.push(ev);
}

void Core::notifyDeviceLost()
{
    deviceLost_ = true;
    Timeline tl = timeline_.read();
    if (tl.snap.state == (uint8_t) EngineState::Locked || tl.snap.state == (uint8_t) EngineState::Chase) tl.snap.state = (uint8_t) EngineState::Hold;
    timeline_.write(tl);
}

void Core::copyTrace(float* dst, int n) const
{
    n = std::min(n, kTraceLen);
    const uint32_t h = traceHead_.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i) {
        const uint32_t idx = h - (uint32_t) n + (uint32_t) i;
        dst[i] = h >= (uint32_t) n || idx < h ? trace_[idx % kTraceLen].load(std::memory_order_relaxed) : 0.0f;
    }
}

// ---------------------------------------------------------------- calibration
void Core::startLoopback(int ch) { loopbackChannel_ = std::max(0, ch); loopback_.start(); }

LoopbackResult Core::finishLoopback(int rin, int rout, bool allOnInput)
{
    LoopbackResult res;
    if (!loopback_.ready()) { res.message = "capture not finished"; return res; }
    res = analyzeLoopback(loopback_.recording().data(), loopback_.recordedSamples(), loopback_.clickStarts(), loopback_.sampleRate(), rin, rout, allOnInput);
    loopback_.finish();
    return res;
}

void Core::startTapAlong() { std::lock_guard<std::mutex> l(calMutex_); tapAlong_.reset(); tapAlongOn_ = true; }
int Core::tapAlongCount() const { return tapAlong_.count(); }
double Core::tapAlongMedianMs() const { return tapAlong_.medianErrorMs(); }
void Core::acceptTapAlong()
{
    tapAlongOn_ = false;
    const double ms = tapAlong_.medianErrorMs();
    editSession([&](SessionSettings& s) { s.inputLatencyMs = ms; s.tapAlongValid = true; });
}

void Core::startHardwareCalibration(InputRole clickRole) { hw_.reset(); hwRole_ = (int) clickRole; hwOn_ = true; }
bool Core::hardwareOffset(double& ms)
{
    const Timeline tl = timeline_.read();
    const double perMs = tl.snap.bpm > 0 ? 60000.0 / tl.snap.bpm : 500.0;
    return hw_.estimate(sampleRate_, perMs, ms);
}
void Core::applyHardwareOffset(double measuredMs)
{
    hwOn_ = false;
    editSession([&](SessionSettings& s) { s.midi.offsetMs = std::clamp(HardwareOffsetEstimator::suggestedClockOffsetMs(measuredMs), -300.0, 300.0); });
}

// ---------------------------------------------------------------- drift log
bool Core::startDriftLog()
{
    std::string dir;
    { std::lock_guard<std::mutex> l(cfgMutex_); dir = session_.driftLogDir; }
    std::lock_guard<std::mutex> d(driftMutex_);
    return driftLog_.open(dir);
}
void Core::stopDriftLog() { std::lock_guard<std::mutex> d(driftMutex_); driftLog_.close(); }
std::string Core::driftLogPath() const { return driftLog_.path(); }
std::vector<DriftRow> Core::driftRows() const { std::lock_guard<std::mutex> d(const_cast<std::mutex&>(driftMutex_)); return driftRows_; }
bool Core::exportTempoMap(const std::string& path) const { return exportTempoMapMidi(driftRows(), driftBeatsPerBar_, driftBeatUnit_, path); }
DriftReport Core::driftReport() const { return analyzeDrift(driftRows()); }

// ---------------------------------------------------------------- housekeeping ("message thread" work)
void Core::clearMapping(Action a)
{
    editSession([&](SessionSettings& s) {
        s.mappings.erase(std::remove_if(s.mappings.begin(), s.mappings.end(), [&](const MidiMapping& m) { return m.action == a; }), s.mappings.end());
    });
}

void Core::housekeepingOnce()
{
    if (learnWatch_) if (Rig* r = uiRig_.load()) if (r->engine.learnedRevision() != learnRev_ && !r->engine.learning()) {
        LearnedPattern lp;
        if (r->engine.getLearnedPattern(lp)) {
            learnWatch_ = false;
            std::lock_guard<std::mutex> l(cfgMutex_);
            session_.learned = lp; session_.learnedValid = true;     // stored with the session; engine already uses it
            params_.pattern = BarPattern::Learned;
        }
    }
    MidiMapping lm;
    while (learnedQ_.pop(lm)) {
        editSession([&](SessionSettings& s) {
            s.mappings.erase(std::remove_if(s.mappings.begin(), s.mappings.end(), [&](const MidiMapping& m) {
                return m.action == lm.action || (m.isCC == lm.isCC && m.number == lm.number && (m.channel == lm.channel || m.channel == 0)); }), s.mappings.end());
            s.mappings.push_back(lm);
        });
    }
    if (const int d = songDelta_.exchange(0)) nextSong(d);
    if (const int pc = pendingProgram_.exchange(-1); pc >= 0) {
        int idx; { std::lock_guard<std::mutex> l(cfgMutex_); idx = session_.songs.findProgram(pc); }
        if (idx >= 0) pendingSong_ = idx;
    }
    if (const int s = pendingSong_.exchange(-1); s >= 0) selectSong(s);

    const Timeline tl = timeline_.read();
    const int64_t now = steadyNowUs();
    EngineEvent e;
    bool logOffline = driftOffline_.load();
    while (evQ_.pop(e)) {
        if (!uiQ_.push(e)) { EngineEvent drop; uiQ_.pop(drop); uiQ_.push(e); }     // drop oldest
        if (e.type != EngineEvent::Beat) continue;
        if (tl.suppressed && !logOffline && suppressOffline_.load() && false) continue;
        DriftRow row;
        row.timestampUs = tl.map.hostTimeForSample(e.beat.sample) + (wallNowUs() - now);
        row.bpm = tl.snap.bpm; row.confidence = e.beat.confidence; row.state = e.beat.state;
        row.bar = e.beat.barNumber; row.beat = e.beat.beatInBar;
        row.event = e.beat.relock ? "relock" : "";
        std::lock_guard<std::mutex> d(driftMutex_);
        if (driftRows_.size() < 200000) driftRows_.push_back(row);
        if (driftLog_.isOpen()) driftLog_.write(row);
    }
    if (tl.snap.state != lastLoggedState_ && tl.map.valid) {
        lastLoggedState_ = tl.snap.state;
        DriftRow row; row.timestampUs = wallNowUs(); row.bpm = tl.snap.bpm; row.confidence = tl.snap.confidence;
        row.state = tl.snap.state; row.bar = tl.snap.barNumber; row.beat = tl.snap.beatInBar; row.event = "state";
        std::lock_guard<std::mutex> d(driftMutex_);
        if (driftRows_.size() < 200000) driftRows_.push_back(row);
        if (driftLog_.isOpen()) driftLog_.write(row);
    }

    if (now - lastArbUs_ > 500000) { lastArbUs_ = now; link_.updateArbitration(tl); }

    if (redundancyOn_.load()) {
        const float myScore = RedundancyArbiter::scoreOf(tl.snap.confidence, tl.snap.state, tl.follow != 0);
        const bool active = arbiter_.decide(myScore, now);
        standby_ = !active;
        if (now - lastHbUs_ > 100000 && hbSender_.isOpen()) {
            lastHbUs_ = now;
            std::string root; { std::lock_guard<std::mutex> l(cfgMutex_); root = session_.osc.root; }
            OscMessage m; m.address = root + "/redundancy/hb";
            m.args = { OscArg::Int((int32_t) arbiter_.id()), OscArg::Float(myScore), OscArg::Int(active ? 1 : 0) };
            const auto bytes = encodeOscPacket(m);
            hbSender_.send(bytes.data(), bytes.size());
        }
    } else standby_ = false;
}

void Core::housekeeping()
{
    while (!hkStop_.load()) {
        housekeepingOnce();
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
}

std::string Core::statusSummary() const
{
    const Timeline tl = timeline_.read();
    char b[256];
    std::snprintf(b, sizeof b, "bpm=%.2f conf=%.2f state=%s bar=%d beat=%d dropped=%zu midiSent=%llu oscSent=%llu linkPeers=%d",
                  tl.snap.bpm, tl.snap.confidence, stateName(tl.snap.state), tl.snap.barNumber, tl.snap.beatInBar,
                  evQ_.dropped(), (unsigned long long) const_cast<Core*>(this)->clock_.status().sent.load(),
                  (unsigned long long) const_cast<Core*>(this)->oscOut_.status().sent.load(), const_cast<Core*>(this)->link_.status().peers.load());
    return b;
}

} // namespace pacemaker
