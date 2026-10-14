#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace pacemaker {

namespace {
constexpr int kBusMain = 0, kBusTrigger = 1, kBusKick = 2, kBusSnare = 3;
using Param = juce::AudioParameterFloat;

juce::StringArray patternLabels()
{
    juce::StringArray a;
    for (const auto& p : patternList()) a.add(p.label);
    return a;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout PacemakerProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    const Params d;
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "follow", 1 }, "Follow", d.follow));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "profile", 1 }, "Profile", StringArray { "Stage", "Rehearsal", "Custom" }, 0));
    l.add(std::make_unique<Param>(ParameterID { "alpha", 1 }, "Phase gain", NormalisableRange<float>(0.05f, 0.8f), (float) d.alpha));
    l.add(std::make_unique<Param>(ParameterID { "beta", 1 }, "Tempo gain", NormalisableRange<float>(0.01f, 0.5f), (float) d.beta));
    l.add(std::make_unique<Param>(ParameterID { "tempoMin", 1 }, "Tempo min", NormalisableRange<float>(30.0f, 300.0f, 1.0f), (float) d.tempoMin));
    l.add(std::make_unique<Param>(ParameterID { "tempoMax", 1 }, "Tempo max", NormalisableRange<float>(30.0f, 300.0f, 1.0f), (float) d.tempoMax));
    l.add(std::make_unique<Param>(ParameterID { "referenceBpm", 1 }, "Reference BPM", NormalisableRange<float>(0.0f, 300.0f, 0.1f), 0.0f));
    l.add(std::make_unique<AudioParameterInt>(ParameterID { "meterNum", 1 }, "Meter numerator", 1, 16, d.meterNum));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "meterDen", 1 }, "Meter denominator", StringArray { "4", "8" }, 0));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "pattern", 1 }, "Bar pattern", patternLabels(), 0));
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "autoOctave", 1 }, "Auto octave", d.autoOctave));
    l.add(std::make_unique<Param>(ParameterID { "sensitivity", 1 }, "Onset sensitivity", NormalisableRange<float>(0.0f, 1.0f), d.sensitivity));
    l.add(std::make_unique<Param>(ParameterID { "nudge", 1 }, "Nudge", NormalisableRange<float>(-1.0f, 1.0f), 0.0f));
    for (const char* id : { "tap", "downbeatNow", "relock", "halfTime", "doubleTime" })
        l.add(std::make_unique<AudioParameterBool>(ParameterID { id, 1 }, String(id), false));
    return l;
}

PacemakerProcessor::PacemakerProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Main", juce::AudioChannelSet::stereo(), true)
                               .withInput("Trigger", juce::AudioChannelSet::mono(), false)
                               .withInput("Kick", juce::AudioChannelSet::mono(), false)
                               .withInput("Snare", juce::AudioChannelSet::mono(), false)
                               .withOutput("Main", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Sync", juce::AudioChannelSet::stereo(), false)),
      user_(store_.load()),
      apvts_(*this, nullptr, "PARAMS", createLayout())
{
    for (auto* id : { "follow", "profile", "alpha", "beta", "tempoMin", "tempoMax", "referenceBpm", "meterNum", "meterDen", "pattern",
                      "autoOctave", "sensitivity", "nudge", "tap", "downbeatNow", "relock", "halfTime", "doubleTime" })
        apvts_.addParameterListener(id, this);
    core_.setMidiSink(std::shared_ptr<MidiSink>(&ports_, [](MidiSink*) {}));
    // CLAP, AU and LV2 hosts can route plugin MIDI output to hardware; VST3 cannot carry clock bytes.
    core_.setPluginMidiOutput(wrapperType != wrapperType_VST3 && wrapperType != wrapperType_Standalone);
    lastApplied_ = paramsFromApvts();
    core_.setParams(lastApplied_);
    startTimerHz(20);
}

PacemakerProcessor::~PacemakerProcessor()
{
    stopTimer();
    cancelPendingUpdate();
    store_.save(user_);
}

bool PacemakerProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    auto okIn = [](const juce::AudioChannelSet& s) { return s.isDisabled() || s == juce::AudioChannelSet::mono() || s == juce::AudioChannelSet::stereo(); };
    const auto& mainIn = l.getMainInputChannelSet();
    const auto& mainOut = l.getMainOutputChannelSet();
    if (!(mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo())) return false;
    if (!okIn(mainIn)) return false;
    for (int i = 1; i < l.inputBuses.size(); ++i) if (!okIn(l.inputBuses[i])) return false;
    const auto& sync = l.getChannelSet(false, 1);
    if (!(sync.isDisabled() || sync == juce::AudioChannelSet::stereo())) return false;
    // At least one audio input, or MIDI-only use with every input disabled.
    return true;
}

int PacemakerProcessor::countFlatChannels() const
{
    int n = 0;
    for (int b = 0; b < getBusCount(true); ++b) if (auto* bus = getBus(true, b)) if (bus->isEnabled()) n += bus->getNumberOfChannels();
    return n;
}

juce::StringArray PacemakerProcessor::inputChannelNames() const
{
    juce::StringArray names;
    for (int b = 0; b < getBusCount(true); ++b) {
        auto* bus = getBus(true, b);
        if (!bus || !bus->isEnabled()) continue;
        for (int c = 0; c < bus->getNumberOfChannels(); ++c)
            names.add(bus->getName() + (bus->getNumberOfChannels() > 1 ? (c == 0 ? " L" : " R") : ""));
    }
    return names;
}

void PacemakerProcessor::rebuildDefaultRoles()
{
    auto s = core_.session();
    if (!s.autoRoleMap && !s.channels.empty()) return;
    std::vector<ChannelAssign> ch;
    int flat = 0;
    for (int b = 0; b < getBusCount(true); ++b) {
        auto* bus = getBus(true, b);
        if (!bus || !bus->isEnabled()) continue;
        for (int c = 0; c < bus->getNumberOfChannels(); ++c, ++flat) {
            ChannelAssign a; a.channel = flat; a.mute = false;
            if (b == kBusMain) { a.role = InputRole::Any; a.distanceM = 0.0; }
            else if (b == kBusTrigger) { a.role = InputRole::Kick; a.distanceM = 0.1; }
            else if (b == kBusKick) { a.role = InputRole::Kick; a.distanceM = 0.1; }
            else { a.role = InputRole::Snare; a.distanceM = 0.1; }
            ch.push_back(a);
        }
    }
    s.channels = ch;
    s.autoRoleMap = true;
    core_.setSession(s);
}

void PacemakerProcessor::prepareToPlay(double sr, int maxBlock)
{
    sampleRate_ = sr; maxBlock_ = juce::jmax(16, maxBlock);
    flatChannels_ = countFlatChannels();
    core_.prepare(sr, maxBlock_, flatChannels_);
    rebuildDefaultRoles();
    inPtrs_.assign((size_t) juce::jmax(1, flatChannels_) + 1, nullptr);
    midiIn_.reserve(512);
    midiOut_.assign(512, CoreMidiOut {});
    silence_.assign((size_t) maxBlock_ + 16, 0.0f);
    validHostTimeBlocks_ = 0;
}

Params PacemakerProcessor::paramsFromApvts() const
{
    Params p;
    auto f = [&](const char* id) { return apvts_.getRawParameterValue(id)->load(); };
    p.follow = f("follow") > 0.5f;
    const int prof = (int) std::lround(f("profile"));
    p.profile = prof == 1 ? ProfileType::Rehearsal : prof == 2 ? ProfileType::Custom : ProfileType::Stage;
    p.alpha = f("alpha"); p.beta = f("beta");
    p.tempoMin = f("tempoMin"); p.tempoMax = std::max((double) f("tempoMax"), p.tempoMin + 10.0);
    p.referenceBpm = f("referenceBpm");
    p.meterNum = (int) std::lround(f("meterNum"));
    p.meterDen = (int) std::lround(f("meterDen")) == 1 ? 8 : 4;
    const int pi = (int) std::lround(f("pattern"));
    p.pattern = patternList()[(size_t) juce::jlimit(0, (int) patternList().size() - 1, pi)].pattern;
    p.autoOctave = f("autoOctave") > 0.5f;
    p.sensitivity = f("sensitivity");
    return p;
}

void PacemakerProcessor::pushParamsToApvts(const Params& p)
{
    const juce::ScopedValueSetter<bool> guard(ignoreParamEvents_, true);
    auto set = [&](const char* id, float v) {
        if (auto* prm = apvts_.getParameter(id)) { const float n = prm->convertTo0to1(v); if (std::abs(prm->getValue() - n) > 1e-6f) prm->setValueNotifyingHost(n); }
    };
    set("follow", p.follow ? 1.0f : 0.0f);
    set("profile", p.profile == ProfileType::Rehearsal ? 1.0f : p.profile == ProfileType::Custom ? 2.0f : 0.0f);
    set("alpha", (float) p.alpha); set("beta", (float) p.beta);
    set("tempoMin", (float) p.tempoMin); set("tempoMax", (float) p.tempoMax);
    set("referenceBpm", (float) p.referenceBpm);
    set("meterNum", (float) p.meterNum); set("meterDen", p.meterDen == 8 ? 1.0f : 0.0f);
    int pi = 0; for (size_t i = 0; i < patternList().size(); ++i) if (patternList()[i].pattern == p.pattern) pi = (int) i;
    set("pattern", (float) pi);
    set("autoOctave", p.autoOctave ? 1.0f : 0.0f);
    set("sensitivity", p.sensitivity);
}

void PacemakerProcessor::parameterChanged(const juce::String& id, float value)
{
    if (ignoreParamEvents_) return;
    // Momentary controls fire on the rising edge and are cleared by the timer.
    auto edge = [&](std::atomic<int>& prev, Action a) { const int v = value > 0.5f ? 1 : 0; if (v && !prev.exchange(v)) core_.doAction(a); else if (!v) prev = 0; };
    if (id == "tap") { edge(tapPrev_, Action::Tap); return; }
    if (id == "downbeatNow") { edge(dbPrev_, Action::DownbeatNow); return; }
    if (id == "relock") { edge(relockPrev_, Action::Relock); return; }
    if (id == "halfTime") { edge(halfPrev_, Action::Half); return; }
    if (id == "doubleTime") { edge(dblPrev_, Action::Double); return; }
    if (id == "nudge") { if (value > 0.5f) core_.doAction(Action::NudgePlus); else if (value < -0.5f) core_.doAction(Action::NudgeMinus); return; }
    if (id == "follow") core_.setFollow(value > 0.5f);
    paramsDirty_ = true;
    triggerAsyncUpdate();
}

void PacemakerProcessor::handleAsyncUpdate()
{
    if (!paramsDirty_.exchange(false)) return;
    const Params p = paramsFromApvts();
    lastApplied_ = p;
    core_.setParams(p);
}

void PacemakerProcessor::timerCallback()
{
    // Core -> parameters (song changes, MIDI-mapped Follow): mirror to the host.
    const Params cp = core_.params();
    if (cp.referenceBpm != lastApplied_.referenceBpm || cp.meterNum != lastApplied_.meterNum || cp.meterDen != lastApplied_.meterDen
        || cp.pattern != lastApplied_.pattern || cp.follow != lastApplied_.follow || cp.profile != lastApplied_.profile
        || cp.sensitivity != lastApplied_.sensitivity || cp.tempoMin != lastApplied_.tempoMin || cp.tempoMax != lastApplied_.tempoMax
        || cp.autoOctave != lastApplied_.autoOctave || cp.alpha != lastApplied_.alpha || cp.beta != lastApplied_.beta) {
        lastApplied_ = cp;
        pushParamsToApvts(cp);
    }
    // Clear momentary parameters after ~100 ms so they can be triggered again.
    {
        const juce::ScopedValueSetter<bool> guard(ignoreParamEvents_, true);
        for (const char* id : { "tap", "downbeatNow", "relock", "halfTime", "doubleTime", "nudge" })
            if (auto* prm = apvts_.getParameter(id)) {
                const float v = prm->getValue();
                const bool isNudge = juce::String(id) == "nudge";
                if (isNudge ? std::abs(v - 0.5f) > 0.01f : v > 0.01f) {
                    if (ticks_ % 2 == 0) { prm->setValueNotifyingHost(isNudge ? 0.5f : 0.0f); }
                }
            }
        tapPrev_ = dbPrev_ = relockPrev_ = halfPrev_ = dblPrev_ = 0;
    }
    ++ticks_;
    // Output port management: follow the session, retry every ~5 s when missing.
    const auto s = core_.session();
    ports_.setPort(juce::String(s.midiPort), s.midiVirtualPort);
    if (ticks_ % 100 == 0) ports_.refresh();
}

void PacemakerProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0) return;

    // Gather the input channels of every enabled input bus (flat order = bus order).
    int nIn = 0;
    for (int b = 0; b < getBusCount(true); ++b) {
        auto* bus = getBus(true, b);
        if (!bus || !bus->isEnabled()) continue;
        auto bb = getBusBuffer(buffer, true, b);
        for (int c = 0; c < bb.getNumChannels() && nIn < (int) inPtrs_.size() - 1; ++c) inPtrs_[(size_t) nIn++] = bb.getReadPointer(c);
    }

    midiIn_.clear();
    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        if (m.getRawDataSize() < 1 || m.getRawDataSize() > 3 || m.getRawData()[0] >= 0xF0) continue;
        CoreMidiIn e; e.offset = juce::jlimit(0, n - 1, meta.samplePosition);
        e.status = m.getRawData()[0]; e.d1 = m.getRawDataSize() > 1 ? m.getRawData()[1] : 0; e.d2 = m.getRawDataSize() > 2 ? m.getRawData()[2] : 0;
        midiIn_.push_back(e);
    }

    CoreHostInfo h;
    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            h.playing = pos->getIsPlaying();
            if (auto ppq = pos->getPpqPosition()) { h.hasPpq = true; h.ppq = *ppq; }
            if (auto bpm = pos->getBpm()) h.bpm = *bpm;
            // Host time is used only when it is demonstrably on the steady clock; otherwise the HostTimeFilter fallback runs.
            if (auto ht = pos->getHostTimeNs()) {
                const int64_t hostUs = (int64_t) (*ht / 1000);
                if (std::llabs(hostUs - steadyNowUs()) < 100000) { if (validHostTimeBlocks_ < 16) ++validHostTimeBlocks_; }
                else validHostTimeBlocks_ = 0;
                if (validHostTimeBlocks_ >= 8) h.hostTimeUs = hostUs;
            }
        }
    }
    h.offline = isNonRealtime();

    float* syncL = nullptr; float* syncR = nullptr;
    if (auto* sb = getBus(false, 1); sb && sb->isEnabled()) {
        auto bb = getBusBuffer(buffer, false, 1);
        if (bb.getNumChannels() >= 2) { bb.clear(); syncL = bb.getWritePointer(0); syncR = bb.getWritePointer(1); }
    }
    // Main output bus: pass-through (inputs are already in the buffer) when Monitor is on, else silence.
    const bool monitor = core_.session().monitor;

    const int nm = core_.process(inPtrs_.data(), nIn, n, h, midiIn_.data(), (int) midiIn_.size(), syncL, syncR, nullptr, midiOut_.data(), (int) midiOut_.size());

    if (!monitor) { auto mb = getBusBuffer(buffer, false, 0); mb.clear(); }
    midi.clear();
    if (nm > 0) {
        for (int i = 0; i < nm; ++i) midi.addEvent(juce::MidiMessage(midiOut_[(size_t) i].bytes, midiOut_[(size_t) i].len), midiOut_[(size_t) i].offset);
    }
}

juce::AudioProcessorEditor* PacemakerProcessor::createEditor() { return new PacemakerEditor(*this); }

void PacemakerProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto tree = apvts_.copyState();
    juce::ValueTree pm("PM_SETTINGS");
    PluginState st = core_.state();
    pm.setProperty("json", juce::String(st.toJson().dump()), nullptr);
    tree.removeChild(tree.getChildWithName("PM_SETTINGS"), nullptr);
    tree.addChild(pm, -1, nullptr);
    if (auto xml = tree.createXml()) copyXmlToBinary(*xml, dest);
}

void PacemakerProcessor::setStateInformation(const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, size));
    if (!xml || !xml->hasTagName(apvts_.state.getType())) return;
    auto tree = juce::ValueTree::fromXml(*xml);
    const auto pm = tree.getChildWithName("PM_SETTINGS");
    if (pm.isValid()) tree.removeChild(pm, nullptr);
    {
        const juce::ScopedValueSetter<bool> guard(ignoreParamEvents_, true);
        apvts_.replaceState(tree);
    }
    PluginState st;
    st.params = paramsFromApvts();
    if (pm.isValid()) {
        std::string warn;
        PluginState loaded;
        if (PluginState::deserialise(pm.getProperty("json").toString().toStdString(), loaded, &warn)) {
            st.session = loaded.session;
            if (!warn.empty()) DBG("Pacemaker: " << warn);
        }
    }
    lastApplied_ = st.params;
    core_.setState(st);
    if (flatChannels_ > 0) rebuildDefaultRoles();
}

} // namespace pacemaker

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new pacemaker::PacemakerProcessor(); }
