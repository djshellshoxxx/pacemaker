#include "DevicePanel.h"
#include "AutoStart.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace pacemaker::ui {

using namespace colours;

namespace {
const char* roleNames[] = { "Any", "Kick", "Snare", "Hat", "Overhead", "Trigger" };
}

void applyInputPrefs(UiContext& ctx)
{
    if (!ctx.deviceManager) return;
    auto& dm = *ctx.deviceManager;
    auto setup = dm.getAudioDeviceSetup();
    juce::BigInteger mask;
    std::vector<ChannelAssign> chans;
    int compact = 0;
    // Preferences are indexed by device channel; the callback sees the enabled channels compacted in order.
    std::vector<InputPref> prefs = ctx.user.inputPrefs;
    std::sort(prefs.begin(), prefs.end(), [](const InputPref& a, const InputPref& b) { return a.deviceChannel < b.deviceChannel; });
    for (const auto& p : prefs) {
        if (!p.enabled) continue;
        mask.setBit(p.deviceChannel);
        ChannelAssign a; a.channel = compact++; a.role = p.role; a.distanceM = p.distanceM; a.mute = p.mute;
        chans.push_back(a);
    }
    if (mask.isZero()) mask.setBit(0), chans.push_back(ChannelAssign { 0, InputRole::Any, 0.0, false });
    if (setup.inputChannels != mask || setup.useDefaultInputChannels) {
        setup.inputChannels = mask; setup.useDefaultInputChannels = false;
        dm.setAudioDeviceSetup(setup, true);
    }
    ctx.core.editSession([&](SessionSettings& s) { s.channels = chans; s.autoRoleMap = false; });
}

void applyOutputRouting(UiContext& ctx)
{
    if (!ctx.bridge) return;
    AudioBridge::Routing r;
    r.syncLeft = ctx.user.syncOutLeft; r.syncRight = ctx.user.syncOutRight;
    r.calibrationOut = r.syncLeft >= 0 ? r.syncLeft : 0;
    ctx.bridge->setRouting(r);
}

void openConfiguredMidiInputs(UiContext& ctx)
{
    if (!ctx.deviceManager || !ctx.bridge) return;
    for (const auto& d : juce::MidiInput::getAvailableDevices()) {
        const bool want = std::find(ctx.user.midiInputs.begin(), ctx.user.midiInputs.end(), d.name.toStdString()) != ctx.user.midiInputs.end();
        ctx.deviceManager->setMidiInputDeviceEnabled(d.identifier, want);
        if (want) ctx.deviceManager->addMidiInputDeviceCallback(d.identifier, ctx.bridge);
        else ctx.deviceManager->removeMidiInputDeviceCallback(d.identifier, ctx.bridge);
    }
}

struct DevicePanel::Impl : juce::ChangeListener {
    UiContext& ctx;
    DevicePanel& owner;
    juce::Viewport view;
    struct Content : juce::Component {} content;
    juce::Label hdrAudio { {}, "Audio device" };
    std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;      // type, devices, rate, buffer
    juce::Label hdrIn { {}, "Input channels (every channel on its own, with a role and a microphone distance)" };
    struct Row {
        juce::ToggleButton on; juce::ComboBox role; juce::TextEditor dist; juce::ToggleButton mute { "Mute" }; juce::Label name;
    };
    std::vector<std::unique_ptr<Row>> rows;
    juce::Label hdrOut { {}, "Sync bus output pair (left = 24 ppqn pulse, right = beat click). Also carries the loopback calibration clicks." };
    juce::ComboBox syncL, syncR;
    juce::Label hdrMidi { {}, "MIDI inputs (e-drum modules, footswitches, program change)" };
    std::vector<std::unique_ptr<juce::ToggleButton>> midiToggles;
    juce::Label hdrApp { {}, "Application" };
    juce::ToggleButton autoStart { "Start Pacemaker when I log in" }, onTop { "Always on top" }, remote { "Phone page (visual metronome for the drummer) on port" };
    juce::TextEditor remotePort;
    juce::TextButton copyDiag { "Copy diagnostics" };

    Impl(UiContext& c, DevicePanel& o) : ctx(c), owner(o)
    {
        o.addAndMakeVisible(view);
        view.setViewedComponent(&content, false);
        view.setScrollBarsShown(true, false);
        for (auto* l : { &hdrAudio, &hdrIn, &hdrOut, &hdrMidi, &hdrApp }) { l->setFont(sans(16.0f, true)); l->setColour(juce::Label::textColourId, green); content.addAndMakeVisible(*l); }
        selector = std::make_unique<juce::AudioDeviceSelectorComponent>(*ctx.deviceManager, 0, 64, 0, 64, false, false, true, false);
        content.addAndMakeVisible(*selector);
        ctx.deviceManager->addChangeListener(this);
        syncL.addItem("Off", 1); syncR.addItem("Off", 1);
        content.addAndMakeVisible(syncL); content.addAndMakeVisible(syncR);
        syncL.onChange = syncR.onChange = [this] {
            ctx.user.syncOutLeft = syncL.getSelectedId() - 2; ctx.user.syncOutRight = syncR.getSelectedId() - 2;
            ctx.core.editSession([&](SessionSettings& s) { s.sync.enabled = ctx.user.syncOutLeft >= 0 && ctx.user.syncOutRight >= 0 ? true : s.sync.enabled; });
            applyOutputRouting(ctx); ctx.saveUser();
        };
        for (auto* c2 : std::initializer_list<juce::Component*>{ &autoStart, &onTop, &remote, &remotePort, &copyDiag }) content.addAndMakeVisible(*c2);
        autoStart.setToggleState(ctx.user.autoStart, juce::dontSendNotification);
        onTop.setToggleState(ctx.user.alwaysOnTop, juce::dontSendNotification);
        remote.setToggleState(ctx.user.remotePageEnabled, juce::dontSendNotification);
        remotePort.setText(juce::String(ctx.user.remotePagePort), false); remotePort.setInputRestrictions(5, "0123456789");
        autoStart.onClick = [this] { ctx.user.autoStart = autoStart.getToggleState(); setAutoStart(ctx.user.autoStart); ctx.saveUser(); };
        onTop.onClick = [this] { ctx.user.alwaysOnTop = onTop.getToggleState(); if (ctx.setAlwaysOnTop) ctx.setAlwaysOnTop(ctx.user.alwaysOnTop); ctx.saveUser(); };
        remote.onClick = [this] { ctx.user.remotePageEnabled = remote.getToggleState(); ctx.user.remotePagePort = juce::jlimit(1024, 65535, remotePort.getText().getIntValue()); ctx.saveUser(); if (ctx.core.session().sectionSuggestions || true) {} };
#ifndef PACEMAKER_HAVE_WEB
        remote.setEnabled(false); remote.setTooltip("This build does not include the web page");
#endif
        copyDiag.onClick = [this] { juce::SystemClipboard::copyTextToClipboard(ctx.diagnostics ? ctx.diagnostics() : juce::String()); };
        rebuild();
    }
    ~Impl() override { ctx.deviceManager->removeChangeListener(this); }

    void changeListenerCallback(juce::ChangeBroadcaster*) override { rebuild(); owner.resized(); }

    void rebuild()
    {
        auto* dev = ctx.deviceManager->getCurrentAudioDevice();
        const auto inNames = dev ? dev->getInputChannelNames() : juce::StringArray();
        const auto outNames = dev ? dev->getOutputChannelNames() : juce::StringArray();
        // Ensure prefs exist for every device input channel.
        for (int i = 0; i < inNames.size(); ++i) {
            bool have = false; for (const auto& p : ctx.user.inputPrefs) if (p.deviceChannel == i) have = true;
            if (!have) { InputPref p; p.deviceChannel = i; p.enabled = i == 0; p.role = InputRole::Any; p.distanceM = 0.0; ctx.user.inputPrefs.push_back(p); }
        }
        rows.clear();
        for (int i = 0; i < inNames.size(); ++i) {
            auto r = std::make_unique<Row>();
            InputPref* pref = nullptr; for (auto& p : ctx.user.inputPrefs) if (p.deviceChannel == i) pref = &p;
            r->name.setText(juce::String(i + 1) + ". " + inNames[i], juce::dontSendNotification);
            r->on.setToggleState(pref->enabled, juce::dontSendNotification);
            for (int k = 0; k < 6; ++k) r->role.addItem(roleNames[k], k + 1);
            r->role.setSelectedId((int) pref->role + 1, juce::dontSendNotification);
            r->dist.setText(juce::String(pref->distanceM, 2), false); r->dist.setInputRestrictions(6, "0123456789.");
            r->dist.setTooltip("Distance from the source in metres (343 m/s => 2.9 ms per metre)");
            r->mute.setToggleState(pref->mute, juce::dontSendNotification);
            auto apply = [this, i, rp = r.get()] {
                for (auto& p : ctx.user.inputPrefs) if (p.deviceChannel == i) {
                    p.enabled = rp->on.getToggleState(); p.role = (InputRole) (rp->role.getSelectedId() - 1);
                    p.distanceM = juce::jlimit(0.0, 20.0, (double) rp->dist.getText().getDoubleValue()); p.mute = rp->mute.getToggleState();
                }
                ctx.saveUser(); applyInputPrefs(ctx);
            };
            r->on.onClick = apply; r->role.onChange = apply; r->mute.onClick = apply;
            r->dist.onReturnKey = r->dist.onFocusLost = apply;
            for (auto* c2 : std::initializer_list<juce::Component*>{ &r->on, &r->role, &r->dist, &r->mute, &r->name }) content.addAndMakeVisible(*c2);
            rows.push_back(std::move(r));
        }
        syncL.clear(dontSend()); syncR.clear(dontSend());
        syncL.addItem("Off", 1); syncR.addItem("Off", 1);
        for (int i = 0; i < outNames.size(); ++i) { syncL.addItem(juce::String(i + 1) + ". " + outNames[i], i + 2); syncR.addItem(juce::String(i + 1) + ". " + outNames[i], i + 2); }
        syncL.setSelectedId(ctx.user.syncOutLeft + 2, dontSend()); syncR.setSelectedId(ctx.user.syncOutRight + 2, dontSend());
        midiToggles.clear();
        for (const auto& d : juce::MidiInput::getAvailableDevices()) {
            auto t = std::make_unique<juce::ToggleButton>(d.name);
            const std::string nm = d.name.toStdString();
            t->setToggleState(std::find(ctx.user.midiInputs.begin(), ctx.user.midiInputs.end(), nm) != ctx.user.midiInputs.end(), juce::dontSendNotification);
            auto* tp = t.get();
            t->onClick = [this, nm, tp] {
                auto& v = ctx.user.midiInputs;
                v.erase(std::remove(v.begin(), v.end(), nm), v.end());
                if (tp->getToggleState()) v.push_back(nm);
                ctx.saveUser(); openConfiguredMidiInputs(ctx);
            };
            content.addAndMakeVisible(*t);
            midiToggles.push_back(std::move(t));
        }
    }
    static juce::NotificationType dontSend() { return juce::dontSendNotification; }

    void layout()
    {
        const int w = juce::jmax(400, view.getWidth() - 16);
        int y = 4;
        auto place = [&](juce::Component& c, int h) { c.setBounds(8, y, w - 16, h); y += h + 4; };
        place(hdrAudio, 22);
        place(*selector, 260);
        place(hdrIn, 22);
        for (auto& r : rows) {
            auto b = juce::Rectangle<int>(8, y, w - 16, 28);
            r->on.setBounds(b.removeFromLeft(28)); r->name.setBounds(b.removeFromLeft(juce::jmax(150, w / 4)));
            r->role.setBounds(b.removeFromLeft(110).reduced(2, 0)); b.removeFromLeft(8);
            r->dist.setBounds(b.removeFromLeft(60).reduced(2, 0)); b.removeFromLeft(4);
            r->mute.setBounds(b.removeFromLeft(80));
            y += 32;
        }
        y += 6;
        place(hdrOut, 40);
        { auto b = juce::Rectangle<int>(8, y, w - 16, 28); syncL.setBounds(b.removeFromLeft(240)); b.removeFromLeft(8); syncR.setBounds(b.removeFromLeft(240)); y += 36; }
        place(hdrMidi, 22);
        if (midiToggles.empty()) y += 4;
        for (auto& t : midiToggles) { t->setBounds(8, y, w - 16, 26); y += 28; }
        y += 6;
        place(hdrApp, 22);
        place(autoStart, 26); place(onTop, 26);
        { auto b = juce::Rectangle<int>(8, y, w - 16, 28); remote.setBounds(b.removeFromLeft(380)); remotePort.setBounds(b.removeFromLeft(70)); y += 34; }
        place(copyDiag, 30);
        content.setSize(w, y + 8);
    }
};

DevicePanel::DevicePanel(UiContext& c) : PanelBase(c), impl_(std::make_unique<Impl>(c, *this)) {}
DevicePanel::~DevicePanel() = default;
void DevicePanel::resized()
{
    impl_->view.setBounds(getLocalBounds());
    impl_->layout();
}

} // namespace pacemaker::ui
