// Settings overlay: Inputs, Outputs, Triggers, Tools, About (RS-01 section 4, ES-02, ES-03, RS-04).
#include "Panels.h"
#include "PanelWidgets.h"
#include "pacemaker/Core.h"
#include <set>

namespace pacemaker::ui {

namespace {

const InputRole kRoles[] = { InputRole::Any, InputRole::Kick, InputRole::Snare, InputRole::HiHat, InputRole::Overhead, InputRole::Trigger };

juce::StringArray roleNames()
{
    juce::StringArray a; for (auto r : kRoles) a.add(roleLabel(r)); return a;
}

int roleIndex(InputRole r) { for (int i = 0; i < 6; ++i) if (kRoles[i] == r) return i; return 0; }

juce::String noteListToString(const std::set<int>& s)
{
    juce::StringArray a; for (int n : s) a.add(juce::String(n)); return a.joinIntoString(", ");
}

std::set<int> parseNoteList(const juce::String& t)
{
    std::set<int> out;
    juce::StringArray parts; parts.addTokens(t, ", ;", "");
    for (auto& p : parts) if (p.trim().isNotEmpty() && p.containsOnly("0123456789")) out.insert(juce::jlimit(0, 127, p.getIntValue()));
    return out;
}

juce::String mappingText(const MidiMapping& m)
{
    return juce::String(m.isCC ? "CC " : "Note ") + juce::String(m.number) + (m.channel == 0 ? ", any channel" : ", channel " + juce::String(m.channel));
}

juce::String actionTitle(Action a)
{
    switch (a) {
    case Action::Follow: return "Follow on/off";
    case Action::Tap: return "Tap";
    case Action::DownbeatNow: return "Downbeat now";
    case Action::NudgeMinus: return "Nudge -";
    case Action::NudgePlus: return "Nudge +";
    case Action::Relock: return "Relock";
    case Action::Half: return "Half tempo";
    case Action::Double: return "Double tempo";
    case Action::ShiftMinus: return "Shift bar -";
    case Action::ShiftPlus: return "Shift bar +";
    case Action::NextSong: return "Next song";
    case Action::PrevSong: return "Previous song";
    default: return actionKey(a);
    }
}

juce::String versionString()
{
#if defined(JUCE_APPLICATION_VERSION_STRING)
    return JUCE_APPLICATION_VERSION_STRING;
#elif defined(JucePlugin_VersionString)
    return JucePlugin_VersionString;
#else
    return "0.1.0";
#endif
}

const char* kLicenceText =
    "Pacemaker is closed-source commercial software, licensed per user (RS-06). It listens to a live drummer and drives "
    "tempo to Ableton Link, MIDI clock, OSC and a Sync pulse bus.\n\n"
    "Third-party components\n"
    "JUCE 8 - used under the JUCE licence (Starter / Indie / Pro, juce.com/legal).\n"
    "Ableton Link - GPLv2+ or Ableton's proprietary licence; kept behind a build flag and only active when compiled in.\n"
    "asio-standalone (Link dependency) - Boost Software Licence 1.0.\n"
    "VST3 is a trademark of Steinberg Media Technologies GmbH; the VST3 SDK is used under the Steinberg licence agreement.\n"
    "CLAP SDK and clap-juce-extensions - MIT.\n"
    "cpp-httplib (remote page) - MIT.\n\n"
    "No GPL / AGPL / non-commercial onset or beat tracking code is linked; the tracking engine is original.";

} // namespace

struct SettingsPanel::Impl : juce::AsyncUpdater {
    SettingsPanel& owner;
    UiContext& ctx;
    int tab;
    juce::OwnedArray<juce::TextButton> tabButtons;
    FormPage page;
    juce::String presetName;          // typed name in the presets box
    int factorySel = 0, userSel = 0;
    juce::StringArray portCache;
    bool portsLoaded = false;

    Impl(SettingsPanel& o, UiContext& c, int t) : owner(o), ctx(c), tab(juce::jlimit(0, 4, t))
    {
        const char* names[] = { "Inputs", "Outputs", "Triggers", "Tools", "About" };
        for (int i = 0; i < 5; ++i) {
            auto* b = tabButtons.add(new juce::TextButton(names[i]));
            b->setClickingTogglesState(false); b->setRadioGroupId(77);
            b->onClick = [this, i] { selectTab(i); };
            owner.addAndMakeVisible(b);
        }
        owner.addAndMakeVisible(page);
        selectTab(tab);
    }

    ~Impl() override { cancelPendingUpdate(); }
    void handleAsyncUpdate() override { rebuild(); }
    void edit(const std::function<void(SessionSettings&)>& f) { ctx.core.editSession(f); }
    void editParams(const std::function<void(Params&)>& f) { auto p = ctx.core.params(); f(p); ctx.core.setParams(p); }

    void selectTab(int t)
    {
        tab = t;
        for (int i = 0; i < tabButtons.size(); ++i) tabButtons[i]->setToggleState(i == t, juce::dontSendNotification);
        rebuild();
    }

    void rebuild()
    {
        const int scroll = page.getViewPositionY();
        auto& f = page.form;
        f.clear();
        switch (tab) {
        case Inputs: buildInputs(f); break;
        case Outputs: buildOutputs(f); break;
        case Triggers: buildTriggers(f); break;
        case Tools: buildTools(f); break;
        default: buildAbout(f); break;
        }
        page.relayout();
        page.setViewPosition(0, scroll);
    }

    // ------------------------------------------------------------ Inputs
    void buildInputs(Form& f)
    {
        const auto s = ctx.core.session();
        f.heading("Input channels");
        f.note("Each connected input channel has a role and, for microphones, a distance. The distance adds the acoustic delay "
               "(2.9 ms per metre) to that channel. Trigger inputs and MIDI are always 0 m.");
        if (s.channels.empty()) {
            f.note("No input channels are connected. Connect inputs in the host (plugin) or in Devices (standalone).", colours::amber);
        }
        const auto names = ctx.inputChannelNames ? ctx.inputChannelNames() : juce::StringArray();
        for (int i = 0; i < (int) s.channels.size(); ++i) {
            const auto& c = s.channels[(size_t) i];
            const juce::String nm = c.channel >= 0 && c.channel < names.size() ? names[c.channel] : "Channel " + juce::String(c.channel + 1);
            auto* role = f.own<juce::ComboBox>();
            const auto rn = roleNames();
            for (int k = 0; k < rn.size(); ++k) role->addItem(rn[k], k + 1);
            role->setSelectedId(roleIndex(c.role) + 1, juce::dontSendNotification);
            auto* dist = f.own<juce::ComboBox>();
            dist->addItem("Close  0.1 m", 1); dist->addItem("Overhead  1 m", 2); dist->addItem("Room  3 m", 3); dist->addItem("Custom", 4);
            auto* metres = f.own<juce::TextEditor>();
            metres->setFont(sans(15.0f)); metres->setInputRestrictions(6, "0123456789."); metres->setIndents(6, 6);
            metres->setText(juce::String(c.distanceM, 2), false);
            metres->setTooltip("Microphone distance in metres");
            auto* mute = f.own<juce::ToggleButton>("Mute");
            mute->setToggleState(c.mute, juce::dontSendNotification);

            auto presetFor = [](double m) { return std::abs(m - 0.1) < 0.005 ? 1 : std::abs(m - 1.0) < 0.005 ? 2 : std::abs(m - 3.0) < 0.005 ? 3 : 4; };
            dist->setSelectedId(presetFor(c.distanceM), juce::dontSendNotification);
            const bool isTrigger = c.role == InputRole::Trigger;
            dist->setEnabled(!isTrigger); metres->setEnabled(!isTrigger);

            role->onChange = [this, i, role, dist, metres] {
                const auto r = kRoles[juce::jlimit(0, 5, role->getSelectedId() - 1)];
                const bool trig = r == InputRole::Trigger;
                dist->setEnabled(!trig); metres->setEnabled(!trig);
                updateChannel(i, [r](ChannelAssign& a) { a.role = r; });
            };
            dist->onChange = [this, i, dist, metres] {
                const double v[] = { 0.1, 1.0, 3.0 };
                const int id = dist->getSelectedId();
                if (id >= 1 && id <= 3) { metres->setText(juce::String(v[id - 1], 2), false); updateChannel(i, [m = v[id - 1]](ChannelAssign& a) { a.distanceM = m; }); }
                else metres->grabKeyboardFocus();
            };
            auto commit = [this, i, dist, metres, presetFor] {
                const double m = juce::jlimit(0.0, 50.0, metres->getText().getDoubleValue());
                metres->setText(juce::String(m, 2), false);
                dist->setSelectedId(presetFor(m), juce::dontSendNotification);
                updateChannel(i, [m](ChannelAssign& a) { a.distanceM = m; });
            };
            metres->onReturnKey = commit; metres->onFocusLost = commit;
            mute->onClick = [this, i, mute] { const bool v = mute->getToggleState(); updateChannel(i, [v](ChannelAssign& a) { a.mute = v; }); };
            f.row(nm, { { role, 120 }, { dist, 150 }, { metres, 64 }, { mute, 80 } });
        }
        f.gap(6);
        f.heading("Latency and roles");
        f.slider("Module latency", 0.0, 20.0, 0.1, s.moduleLatencyMs, " ms", [this](double v) { edit([v](SessionSettings& x) { x.moduleLatencyMs = v; }); });
        f.note("Trigger-to-MIDI delay of an e-drum module (typically 2 to 5 ms). Applied to Trigger inputs.");
        f.toggle("Automatic role map", s.autoRoleMap, [this](bool v) { edit([v](SessionSettings& x) { x.autoRoleMap = v; }); },
                 "Derive roles from the connected buses");
        f.button("", "Reset role map to default", [this] { resetRoleMap(); }, 240);
        f.note(ctx.standalone ? "Sets every channel to Any, 0.1 m, unmuted. Change inputs and roles in Devices."
                              : "Turns the automatic role map back on. The roles are rebuilt from the host buses the next time the bus layout changes.");
    }

    void updateChannel(int idx, const std::function<void(ChannelAssign&)>& fn)
    {
        edit([&](SessionSettings& s) {
            if (idx >= 0 && idx < (int) s.channels.size()) fn(s.channels[(size_t) idx]);
            s.autoRoleMap = false;
        });
        if (ctx.standalone) {                     // keep the per-device preference in step (enabled prefs map to channels in order)
            const auto s = ctx.core.session();
            if (idx >= 0 && idx < (int) s.channels.size()) {
                std::vector<InputPref*> enabled;
                for (auto& p : ctx.user.inputPrefs) if (p.enabled) enabled.push_back(&p);
                std::sort(enabled.begin(), enabled.end(), [](InputPref* a, InputPref* b) { return a->deviceChannel < b->deviceChannel; });
                if (idx < (int) enabled.size()) {
                    enabled[(size_t) idx]->role = s.channels[(size_t) idx].role;
                    enabled[(size_t) idx]->distanceM = s.channels[(size_t) idx].distanceM;
                    enabled[(size_t) idx]->mute = s.channels[(size_t) idx].mute;
                    ctx.saveUser();
                }
            }
        }
    }

    void resetRoleMap()
    {
        edit([this](SessionSettings& s) {
            s.autoRoleMap = true;
            if (ctx.standalone) for (auto& c : s.channels) { c.role = InputRole::Any; c.distanceM = 0.1; c.mute = false; }
        });
        if (ctx.standalone) {
            for (auto& p : ctx.user.inputPrefs) { p.role = InputRole::Any; p.distanceM = 0.1; p.mute = false; }
            ctx.saveUser();
        }
        triggerAsyncUpdate();
    }

    // ------------------------------------------------------------ Outputs
    void buildOutputs(Form& f)
    {
        const auto s = ctx.core.session();
        if (linkCompiledIn()) {
            f.heading("Ableton Link");
            f.toggle("Enable Link", s.link.enabled, [this](bool v) { edit([v](SessionSettings& x) { x.link.enabled = v; }); });
            f.slider("Quantum (beats)", 1, 16, 1, s.link.quantum, "", [this](double v) { edit([v](SessionSettings& x) { x.link.quantum = (int) v; }); });
            f.toggle("Start / stop sync", s.link.startStopSync, [this](bool v) { edit([v](SessionSettings& x) { x.link.startStopSync = v; }); });
            f.toggle("Publish tempo", s.link.publishTempo, [this](bool v) { edit([v](SessionSettings& x) { x.link.publishTempo = v; }); });
            f.toggle("Publish phase", s.link.publishPhase, [this](bool v) { edit([v](SessionSettings& x) { x.link.publishPhase = v; }); });
            f.slider("Confidence gate", 0.0, 1.0, 0.01, s.link.confidenceGate, "", [this](double v) { edit([v](SessionSettings& x) { x.link.confidenceGate = v; }); });
            f.slider("Commit interval", 20, 1000, 10, s.link.commitIntervalMs, " ms", [this](double v) { edit([v](SessionSettings& x) { x.link.commitIntervalMs = v; }); });
            f.slider("Tempo deadband", 0.0, 2.0, 0.01, s.link.tempoDeadband, " BPM", [this](double v) { edit([v](SessionSettings& x) { x.link.tempoDeadband = v; }); });
            f.note("Tempo is only published while confidence is above the gate. Changes smaller than the deadband are ignored.");
            f.gap(6);
        }

        f.heading("MIDI clock");
        f.toggle("Send MIDI clock", s.midi.enabled, [this](bool v) { edit([v](SessionSettings& x) { x.midi.enabled = v; }); });
        if (MidiPortManager::virtualPortsSupported()) {
            f.toggle("Virtual port", s.midiVirtualPort, [this](bool v) {
                edit([v](SessionSettings& x) { x.midiVirtualPort = v; if (v && x.midiPort.empty()) x.midiPort = "Pacemaker Clock"; });
                applyPort(); triggerAsyncUpdate();
            }, "Create a virtual MIDI port");
        }
        if (s.midiVirtualPort && MidiPortManager::virtualPortsSupported()) {
            f.text("Virtual port name", juce::String(s.midiPort), [this](const juce::String& t) {
                const auto n = t.trim().isEmpty() ? std::string("Pacemaker Clock") : t.trim().toStdString();
                edit([n](SessionSettings& x) { x.midiPort = n; }); applyPort();
            });
        } else {
            if (!portsLoaded) { portCache = MidiPortManager::availablePorts(); portsLoaded = true; }
            juce::StringArray items; items.add("(none)");
            items.addArray(portCache);
            int sel = 0;
            if (!s.midiPort.empty()) {
                sel = items.indexOf(juce::String(s.midiPort));
                if (sel < 0) { items.add(juce::String(s.midiPort) + " (not found)"); sel = items.size() - 1; }
            }
            auto* box = f.own<juce::ComboBox>();
            for (int i = 0; i < items.size(); ++i) box->addItem(items[i], i + 1);
            box->setSelectedId(sel + 1, juce::dontSendNotification);
            box->onChange = [this, box, items] {
                const int i = box->getSelectedId() - 1;
                const std::string n = i <= 0 ? std::string() : items[i].endsWith(" (not found)") ? items[i].dropLastCharacters(12).toStdString() : items[i].toStdString();
                edit([n](SessionSettings& x) { x.midiPort = n; x.midiVirtualPort = false; });
                ctx.user.lastMidiPort = n; ctx.saveUser(); applyPort();
            };
            f.row("MIDI output port", { { box, 300 }, { f.makeButton("Refresh", [this] { portsLoaded = false; triggerAsyncUpdate(); }), 96 } });
        }
        f.info("Port status", ctx.ports.statusText().isEmpty() ? juce::String("not open") : ctx.ports.statusText(), colours::dim);
#if JUCE_WINDOWS
        f.note("Windows has no built-in virtual MIDI ports. Install the free loopMIDI driver, create a port there, then pick it above "
               "and select it as the MIDI sync input in your DAW or lighting software.");
        f.button("", "Get loopMIDI", [] { juce::URL("https://www.tobias-erichsen.de/software/loopmidi.html").launchInDefaultBrowser(); }, 200);
#endif
        f.slider("Clock offset", -300, 300, 0.5, s.midi.offsetMs, " ms", [this](double v) { edit([v](SessionSettings& x) { x.midi.offsetMs = v; }); });
        f.toggle("Song position on relock", s.midi.sppOnRelock, [this](bool v) { edit([v](SessionSettings& x) { x.midi.sppOnRelock = v; }); }, "Send Song Position Pointer");
        f.gap(6);

        f.heading("OSC");
        f.toggle("Send OSC", s.osc.enabled, [this](bool v) { edit([v](SessionSettings& x) { x.osc.enabled = v; }); });
        f.text("Host", juce::String(s.osc.host), [this](const juce::String& t) { edit([n = t.trim().toStdString()](SessionSettings& x) { if (!n.empty()) x.osc.host = n; }); }, 260);
        f.text("Port", juce::String(s.osc.port), [this](const juce::String& t) { edit([v = juce::jlimit(1, 65535, t.getIntValue())](SessionSettings& x) { x.osc.port = v; }); }, 100, "0123456789", 5);
        f.text("Root address", juce::String(s.osc.root), [this](const juce::String& t) { edit([n = t.trim().toStdString()](SessionSettings& x) { x.osc.root = n.empty() ? "/pacemaker" : (n[0] == '/' ? n : "/" + n); }); }, 260);
        f.combo("Receiver profile", { "Generic", "Resolume", "MagicQ" }, (int) s.osc.profile, [this](int i) {
            edit([i](SessionSettings& x) { x.osc.profile = (OscProfile) i; }); triggerAsyncUpdate();
        }, 200);
        if (s.osc.profile == OscProfile::MagicQ)
            f.text("Tap address", juce::String(s.osc.tapAddress), [this](const juce::String& t) { edit([n = t.trim().toStdString()](SessionSettings& x) { if (!n.empty()) x.osc.tapAddress = n; }); }, 260);
        if (s.osc.profile != OscProfile::Generic)
            f.toggle("Also send generic", s.osc.alsoGeneric, [this](bool v) { edit([v](SessionSettings& x) { x.osc.alsoGeneric = v; }); }, "Keep the generic messages as well");
        f.slider("OSC offset", -300, 300, 0.5, s.osc.offsetMs, " ms", [this](double v) { edit([v](SessionSettings& x) { x.osc.offsetMs = v; }); });
        f.slider("Lead", 0, 100, 1, s.osc.leadMs, " ms", [this](double v) { edit([v](SessionSettings& x) { x.osc.leadMs = v; }); });
        f.text("Listen port", juce::String(s.osc.listenPort), [this](const juce::String& t) { edit([v = juce::jlimit(0, 65535, t.getIntValue())](SessionSettings& x) { x.osc.listenPort = v; }); }, 100, "0123456789", 5);
        f.note("Listen port 0 turns inbound OSC control off. Bundles are sent 'lead' ms before the beat they announce.");
        f.gap(6);

        f.heading("Sync pulse bus");
        f.toggle("Enable Sync", s.sync.enabled, [this](bool v) { edit([v](SessionSettings& x) { x.sync.enabled = v; }); });
        f.toggle("Invert polarity", s.sync.invertPolarity, [this](bool v) { edit([v](SessionSettings& x) { x.sync.invertPolarity = v; }); });
        f.slider("Sync offset", -300, 300, 0.5, s.sync.offsetMs, " ms", [this](double v) { edit([v](SessionSettings& x) { x.sync.offsetMs = v; }); });
        f.slider("Level", 0.0, 1.0, 0.01, s.sync.level, "", [this](double v) { edit([v](SessionSettings& x) { x.sync.level = (float) v; }); });
        f.gap(6);

        f.heading("Count-in launcher");
        f.toggle("Enable", s.countIn.enabled, [this](bool v) { edit([v](SessionSettings& x) { x.countIn.enabled = v; }); }, "Fire a cue at bar 1 after a count-in");
        f.slider("MIDI note (-1 = off)", -1, 127, 1, s.countIn.midiNote, "", [this](double v) { edit([v](SessionSettings& x) { x.countIn.midiNote = (int) v; }); });
        f.slider("MIDI channel", 1, 16, 1, s.countIn.midiChannel, "", [this](double v) { edit([v](SessionSettings& x) { x.countIn.midiChannel = (int) v; }); });
        f.text("OSC address", juce::String(s.countIn.oscAddress), [this](const juce::String& t) { edit([n = t.trim().toStdString()](SessionSettings& x) { x.countIn.oscAddress = n; }); }, 300);
    }

    void applyPort()
    {
        const auto s = ctx.core.session();
        ctx.ports.setPort(juce::String(s.midiPort), s.midiVirtualPort);
    }

    // ------------------------------------------------------------ Triggers
    void buildTriggers(Form& f)
    {
        const auto s = ctx.core.session();
        f.heading("MIDI note table");
        f.note("MIDI notes from an e-drum module or pad controller are sorted into roles. Enter note numbers separated by commas.");
        f.text("Kick notes", noteListToString(s.notes.kick), [this](const juce::String& t) { edit([n = parseNoteList(t)](SessionSettings& x) { x.notes.kick = n; }); }, 300);
        f.text("Snare notes", noteListToString(s.notes.snare), [this](const juce::String& t) { edit([n = parseNoteList(t)](SessionSettings& x) { x.notes.snare = n; }); }, 300);
        f.text("Hi-hat notes", noteListToString(s.notes.hat), [this](const juce::String& t) { edit([n = parseNoteList(t)](SessionSettings& x) { x.notes.hat = n; }); }, 300);
        f.toggle("Any other note", s.notes.anyNote, [this](bool v) { edit([v](SessionSettings& x) { x.notes.anyNote = v; }); }, "Treat other notes as Trigger onsets");
        f.gap(6);

        f.heading("MIDI mappings");
        f.note("Right-click any control on the main screen to learn a note or CC for it. Existing mappings are listed here.");
        if (s.mappings.empty()) f.note("No mappings yet.");
        for (int i = 0; i < (int) s.mappings.size(); ++i) {
            const auto& m = s.mappings[(size_t) i];
            auto* l = f.own<juce::Label>(); l->setText(mappingText(m), juce::dontSendNotification); l->setFont(sans(15.0f));
            auto* del = f.makeButton("Delete", [this, i] {
                edit([i](SessionSettings& x) { if (i < (int) x.mappings.size()) x.mappings.erase(x.mappings.begin() + i); });
                triggerAsyncUpdate();
            });
            f.row(actionTitle(m.action), { { l, 220 }, { del, 90 } });
        }
        f.button("", "Suggest footswitch: CC64 Follow, CC65 Tap", [this] {
            edit([](SessionSettings& x) {
                auto addOne = [&x](int cc, Action a) {
                    for (auto& m : x.mappings) if (m.isCC && m.number == cc && m.action == a) return;
                    x.mappings.erase(std::remove_if(x.mappings.begin(), x.mappings.end(), [&](const MidiMapping& m) { return m.action == a; }), x.mappings.end());
                    MidiMapping m; m.isCC = true; m.number = cc; m.channel = 0; m.action = a; x.mappings.push_back(m);
                };
                addOne(64, Action::Follow); addOne(65, Action::Tap);
            });
            triggerAsyncUpdate();
        }, 360);
    }

    // ------------------------------------------------------------ Tools
    void buildTools(Form& f)
    {
        const auto s = ctx.core.session();
        const auto p = ctx.core.params();
        f.heading("Timing");
        f.slider("Output nudge", -100, 100, 0.5, s.nudgeMs, " ms", [this](double v) { edit([v](SessionSettings& x) { x.nudgeMs = v; }); });
        f.slider("Lookahead", 5, 200, 1, s.lookaheadMs, " ms", [this](double v) { edit([v](SessionSettings& x) { x.lookaheadMs = v; }); });
        f.toggle("Monitor input", s.monitor, [this](bool v) { edit([v](SessionSettings& x) { x.monitor = v; }); }, "Pass the input through to the main output");
        f.toggle("Offline outputs", s.offlineOutputsEnabled, [this](bool v) { edit([v](SessionSettings& x) { x.offlineOutputsEnabled = v; }); }, "Keep sending outputs during offline rendering");
        f.gap(6);

        f.heading("Logging and suggestions");
        f.text("Drift log folder", juce::String(s.driftLogDir), [this](const juce::String& t) { edit([n = t.trim().toStdString()](SessionSettings& x) { x.driftLogDir = n; }); }, 440);
        f.note("Empty = " + juce::String(DriftLogger::defaultDirectory()));
        f.toggle("Log offline renders", s.driftLogOffline, [this](bool v) { edit([v](SessionSettings& x) { x.driftLogOffline = v; }); }, "Write the drift log while rendering offline");
        f.toggle("Section suggestions", s.sectionSuggestions, [this](bool v) { edit([v](SessionSettings& x) { x.sectionSuggestions = v; }); }, "Send OSC /section on energy or pattern change");
        f.gap(6);

        f.heading("Redundancy");
        f.toggle("Dual instance", s.redundancyEnabled, [this](bool v) { edit([v](SessionSettings& x) { x.redundancyEnabled = v; }); }, "Arbitrate with a second Pacemaker");
        f.text("Peer host:port", juce::String(s.redundancyPeer), [this](const juce::String& t) { edit([n = t.trim().toStdString()](SessionSettings& x) { x.redundancyPeer = n; }); }, 260);
        f.gap(6);

        f.heading("Tracking");
        f.combo("Follow profile", { "Stage", "Rehearsal", "Custom" }, (int) p.profile, [this](int i) {
            editParams([i](Params& q) { q.profile = (ProfileType) i; }); triggerAsyncUpdate();
        }, 200);
        f.slider("Phase gain (alpha)", 0.02, 0.8, 0.01, p.alpha, "", [this](double v) { editParams([v](Params& q) { q.alpha = v; q.profile = ProfileType::Custom; }); });
        f.slider("Period gain (beta)", 0.005, 0.4, 0.005, p.beta, "", [this](double v) { editParams([v](Params& q) { q.beta = v; q.profile = ProfileType::Custom; }); });
        f.note("Moving a gain slider switches the profile to Custom. Higher gains follow faster and wobble more.");
        f.slider("Sensitivity", 0.0, 1.0, 0.01, p.sensitivity, "", [this](double v) { editParams([v](Params& q) { q.sensitivity = (float) v; }); });
        f.toggle("Auto octave", p.autoOctave, [this](bool v) { editParams([v](Params& q) { q.autoOctave = v; }); }, "Switch octave when the tempo stays ambiguous");
        f.slider("Tempo minimum", 30, 290, 1, p.tempoMin, " BPM", [this](double v) { editParams([v](Params& q) { q.tempoMin = v; if (q.tempoMax < v + 10) q.tempoMax = std::min(300.0, v + 10); }); });
        f.slider("Tempo maximum", 40, 300, 1, p.tempoMax, " BPM", [this](double v) { editParams([v](Params& q) { q.tempoMax = v; if (q.tempoMin > v - 10) q.tempoMin = std::max(30.0, v - 10); }); });
        f.slider("Meter numerator", 1, 16, 1, p.meterNum, "", [this](double v) { editParams([v](Params& q) { q.meterNum = (int) v; }); });
        {
            const int dens[] = { 2, 4, 8, 16 };
            int di = 1; for (int i = 0; i < 4; ++i) if (dens[i] == p.meterDen) di = i;
            f.combo("Meter denominator", { "2", "4", "8", "16" }, di, [this, dens](int i) { editParams([&](Params& q) { q.meterDen = dens[juce::jlimit(0, 3, i)]; }); }, 100);
        }
        f.combo("7/8 grouping", { "2 + 2 + 3", "3 + 2 + 2", "2 + 3 + 2" }, juce::jlimit(0, 2, p.grouping78), [this](int i) { editParams([i](Params& q) { q.grouping78 = i; }); }, 200);
        f.gap(6);

        buildPresets(f);
    }

    void buildPresets(Form& f)
    {
        f.heading("Presets");
        f.note("A preset stores only the tracking parameters (profile, gains, tempo range, meter, pattern, sensitivity). "
               "It never changes your session or your global settings.");
        const auto& fac = factoryPresets();
        juce::StringArray fn; for (auto& p : fac) fn.add(juce::String(p.name));
        if (fn.isEmpty()) fn.add("(none)");
        factorySel = juce::jlimit(0, fn.size() - 1, factorySel);
        auto* fbox = f.own<juce::ComboBox>();
        for (int i = 0; i < fn.size(); ++i) fbox->addItem(fn[i], i + 1);
        fbox->setSelectedId(factorySel + 1, juce::dontSendNotification);
        fbox->onChange = [this, fbox] { factorySel = fbox->getSelectedId() - 1; };
        f.row("Factory presets", { { fbox, 240 }, { f.makeButton("Load", [this, &fac] {
            if (factorySel >= 0 && factorySel < (int) fac.size()) ctx.core.setParams(fac[(size_t) factorySel].params);
            triggerAsyncUpdate();
        }), 90 } });

        const auto dir = ctx.store.presetsFolder().getFullPathName().toStdString();
        const auto user = loadUserPresets(dir);
        juce::StringArray un; for (auto& p : user) un.add(juce::String(p.name));
        if (un.isEmpty()) un.add("(no user presets)");
        userSel = juce::jlimit(0, un.size() - 1, userSel);
        auto* ubox = f.own<juce::ComboBox>();
        for (int i = 0; i < un.size(); ++i) ubox->addItem(un[i], i + 1);
        ubox->setSelectedId(userSel + 1, juce::dontSendNotification);
        ubox->onChange = [this, ubox] { userSel = ubox->getSelectedId() - 1; };
        ubox->setEnabled(!user.empty());
        auto* load = f.makeButton("Load", [this, user] {
            if (userSel >= 0 && userSel < (int) user.size()) ctx.core.setParams(user[(size_t) userSel].params);
            triggerAsyncUpdate();
        });
        auto* del = f.makeButton("Delete", [this, user] {
            if (userSel >= 0 && userSel < (int) user.size()) deleteUserPreset(user[(size_t) userSel].name);
            userSel = 0; triggerAsyncUpdate();
        });
        load->setEnabled(!user.empty()); del->setEnabled(!user.empty());
        f.row("My presets", { { ubox, 240 }, { load, 90 }, { del, 90 } });

        auto* name = f.own<juce::TextEditor>(); name->setFont(sans(15.0f)); name->setIndents(6, 6);
        name->setTextToShowWhenEmpty("Preset name", colours::dim); name->setText(presetName, false);
        name->onTextChange = [this, name] { presetName = name->getText(); };
        f.row("Save current as", { { name, 240 }, { f.makeButton("Save", [this] {
            const auto n = presetName.trim();
            if (n.isEmpty()) return;
            std::string err;
            saveUserPreset(ctx.store.presetsFolder().getFullPathName().toStdString(), n.toStdString(), ctx.core.params(), &err);
            presetName = {}; triggerAsyncUpdate();
        }), 90 } });
    }

    void deleteUserPreset(const std::string& name)
    {
        const auto folder = ctx.store.presetsFolder();
        for (auto& file : folder.findChildFiles(juce::File::findFiles, false, "*.pmpreset")) {
            auto v = juce::JSON::parse(file);
            if (v.isObject() && v.getProperty("name", {}).toString().toStdString() == name) { file.deleteFile(); return; }
        }
    }

    // ------------------------------------------------------------ About
    void buildAbout(Form& f)
    {
        f.heading("Pacemaker");
        f.info("Version", versionString());
        f.info("Mode", ctx.standalone ? "Standalone" : "Plugin");
        f.info("Ableton Link", linkCompiledIn() ? "compiled in" : "not compiled in (this build has no Link output)", linkCompiledIn() ? colours::green : colours::amber);
        f.info("Built with", "JUCE " + juce::String(juce::SystemStats::getJUCEVersion()).fromFirstOccurrenceOf("JUCE v", false, false));
        f.info("System", juce::SystemStats::getOperatingSystemName());
        f.info("Settings folder", ctx.store.folder().getFullPathName());
        f.button("", "Copy diagnostics", [this] {
            juce::SystemClipboard::copyTextToClipboard(ctx.diagnostics ? ctx.diagnostics() : juce::String("Diagnostics are not available."));
        }, 200);
        f.note("Copies the status summary (tracking state, outputs, latency) to the clipboard for a support request.");
        f.gap(6);
        f.heading("Licence and third-party notices");
        f.note(kLicenceText, colours::text);
    }

    void layout()
    {
        auto r = owner.getLocalBounds().reduced(8, 4);
        auto top = r.removeFromTop(38);
        const int w = juce::jmin(120, top.getWidth() / 5);
        for (auto* b : tabButtons) b->setBounds(top.removeFromLeft(w).reduced(2, 2));
        r.removeFromTop(4);
        page.setBounds(r);
    }
};

SettingsPanel::SettingsPanel(UiContext& c, int initialTab) : PanelBase(c), impl_(std::make_unique<Impl>(*this, c, initialTab)) {}
SettingsPanel::~SettingsPanel() = default;
void SettingsPanel::resized() { impl_->layout(); }

} // namespace pacemaker::ui
