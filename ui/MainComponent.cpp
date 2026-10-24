#include "MainComponent.h"
#include <cmath>

namespace pacemaker::ui {

using namespace colours;

namespace {

const char* stateWord(int s)
{
    static const char* w[] = { "LISTENING", "COUNT-IN", "LOCKING", "LOCKED", "HOLD", "CHASE" };
    return w[juce::jlimit(0, 5, s)];
}
juce::Colour stateColour(int s)
{
    switch (s) { case 3: return green; case 4: return amber; case 5: return amber; case 2: return cyan; case 1: return cyan; default: return dim; }
}
juce::Colour roleColour(InputRole r)
{
    switch (r) { case InputRole::Kick: return juce::Colour(0xffff7a59); case InputRole::Snare: return juce::Colour(0xff4cc9f0);
                 case InputRole::HiHat: return juce::Colour(0xffe9d758); case InputRole::Overhead: return juce::Colour(0xffb388ff);
                 case InputRole::Trigger: return juce::Colour(0xff3ddc97); default: return juce::Colour(0xffd7e3ea); }
}
const char* roleName(InputRole r)
{
    switch (r) { case InputRole::Kick: return "Kick"; case InputRole::Snare: return "Snare"; case InputRole::HiHat: return "Hat";
                 case InputRole::Overhead: return "Overhead"; case InputRole::Trigger: return "Trigger"; default: return "Any"; }
}
const char* actionLabel(Action a)
{
    switch (a) { case Action::Tap: return "Tap"; case Action::DownbeatNow: return "Downbeat now"; case Action::NudgeMinus: return "Nudge -";
                 case Action::NudgePlus: return "Nudge +"; case Action::Half: return "Half"; case Action::Double: return "Double";
                 case Action::Relock: return "Relock"; case Action::Follow: return "Follow"; default: return ""; }
}
juce::String mappingText(const SessionSettings& s, Action a)
{
    for (const auto& m : s.mappings)
        if (m.action == a) return juce::String(m.isCC ? "CC " : "Note ") + juce::String(m.number) + (m.channel ? " ch " + juce::String(m.channel) : juce::String(" any ch"));
    return {};
}

} // namespace

// =============================================================== Header
struct MainComponent::Header : juce::Component {
    MainComponent& m;
    juce::TextButton follow { "FOLLOW" };
    explicit Header(MainComponent& mc) : m(mc)
    {
        addAndMakeVisible(follow);
        follow.setClickingTogglesState(true);
        follow.setTooltip("Follow the drummer. Off holds the tempo (footswitch: Lock). Shortcut: F");
        follow.onClick = [this] { m.ctx_.core.setFollow(follow.getToggleState()); };
        follow.setWantsKeyboardFocus(true);
    }
    void resized() override { follow.setBounds(getWidth() - 190, 14, 174, 64); }
    void paint(juce::Graphics& g) override
    {
        const auto& md = m.model_;
        const auto& s = md.tl.snap;
        g.setColour(panel); g.fillRect(getLocalBounds());
        g.setColour(line); g.drawHorizontalLine(getHeight() - 1, 0, (float) getWidth());
        // tempo
        const bool have = s.bpm > 0;
        g.setColour(have ? text : dim);
        g.setFont(mono(76.0f, true));
        g.drawText(have ? juce::String(s.bpm, 1) : juce::String("---.-"), 12, 4, 270, 84, juce::Justification::centredLeft);
        g.setColour(dim); g.setFont(sans(16.0f, true));
        g.drawText("BPM", 14, 84, 60, 20, juce::Justification::centredLeft);
        // meter, bar:beat
        g.setColour(text); g.setFont(mono(22.0f, true));
        const int bpb = md.tl.beatsPerBar;
        g.drawText(juce::String(md.params.meterNum) + "/" + juce::String(md.params.meterDen) + "   Bar " + juce::String(have ? s.barNumber : 0)
                       + "  Beat " + juce::String(have ? s.beatInBar : 0) + "/" + juce::String(bpb),
                   290, 62, 360, 28, juce::Justification::centredLeft);
        // state word
        g.setColour(stateColour(s.state)); g.setFont(sans(40.0f, true));
        g.drawText(stateWord(s.state), 290, 12, 340, 48, juce::Justification::centredLeft);
        // confidence arc
        const juce::Rectangle<float> arcR(650.0f, 12.0f, 84.0f, 84.0f);
        const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
        juce::Path track, val;
        track.addCentredArc(arcR.getCentreX(), arcR.getCentreY(), 36.0f, 36.0f, 0.0f, a0, a1, true);
        g.setColour(grid); g.strokePath(track, juce::PathStrokeType(8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float c = juce::jlimit(0.0f, 1.0f, s.confidence);
        if (c > 0.01f) {
            val.addCentredArc(arcR.getCentreX(), arcR.getCentreY(), 36.0f, 36.0f, 0.0f, a0, a0 + (a1 - a0) * c, true);
            g.setColour(c > 0.6f ? green : c > 0.35f ? amber : red);
            g.strokePath(val, juce::PathStrokeType(8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour(text); g.setFont(mono(18.0f, true));
        g.drawText(juce::String((int) std::lround(c * 100.0f)) + "%", arcR.toNearestInt().translated(0, -2), juce::Justification::centred);
        g.setColour(dim); g.setFont(sans(14.0f));
        g.drawText("confidence", 640, 94, 104, 18, juce::Justification::centred);
        // pulse icon: flashes on the downbeat
        const bool flash = md.nowUs < md.downbeatFlashUntilUs;
        const juce::Rectangle<float> ic(762.0f, 26.0f, 44.0f, 44.0f);
        g.setColour(flash ? green : grid); g.fillEllipse(ic);
        g.setColour(flash ? juce::Colours::white : dim);
        juce::Path pulse; pulse.startNewSubPath(ic.getX() + 6, ic.getCentreY()); pulse.lineTo(ic.getX() + 14, ic.getCentreY());
        pulse.lineTo(ic.getX() + 19, ic.getY() + 9); pulse.lineTo(ic.getX() + 25, ic.getBottom() - 8); pulse.lineTo(ic.getX() + 30, ic.getCentreY());
        pulse.lineTo(ic.getRight() - 6, ic.getCentreY());
        g.strokePath(pulse, juce::PathStrokeType(2.5f));
        // badges
        int x = 12; const int y = getHeight() - 24;
        auto badge = [&](const juce::String& t, juce::Colour c2) {
            g.setFont(sans(14.0f, true));
            const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), t) + 18;
            g.setColour(c2.withAlpha(0.2f)); g.fillRoundedRectangle((float) x, (float) y, (float) w, 20.0f, 10.0f);
            g.setColour(c2); g.drawRoundedRectangle((float) x, (float) y, (float) w, 20.0f, 10.0f, 1.0f);
            g.drawText(t, x, y, w, 20, juce::Justification::centred);
            x += w + 8;
        };
        if (s.flags & SnapshotFlags::OctaveAmbiguity) badge("OCTAVE? USE HALF / DOUBLE", amber);
        if (md.linkFighting) badge("ANOTHER APP IS SETTING TEMPO", amber);
        if (md.linkFollowers > 1) badge("2 INSTANCES WITH FOLLOW ON", amber);
        if (md.standby) badge("STANDBY (OTHER UNIT PUBLISHING)", cyan);
        if (!md.params.follow) badge("HOLDING: FOLLOW IS OFF", amber);
        if (md.tl.suppressed && !md.standby) badge("OUTPUTS SUPPRESSED (OFFLINE)", dim);
        if (md.deviceLost) {
            g.setColour(red); g.fillRect(0, 0, getWidth(), 22);
            g.setColour(juce::Colours::white); g.setFont(sans(15.0f, true));
            g.drawText("AUDIO DEVICE LOST. Outputs hold the last tempo; MIDI clock keeps running.", 12, 0, getWidth() - 24, 22, juce::Justification::centredLeft);
        }
    }
    void update() { follow.setToggleState(m.model_.params.follow, juce::dontSendNotification); repaint(); }
};

// =============================================================== Trace
struct MainComponent::Trace : juce::Component {
    MainComponent& m;
    float trace[kTraceLen] {};
    explicit Trace(MainComponent& mc) : m(mc) {}
    void paint(juce::Graphics& g) override
    {
        const auto& md = m.model_;
        const auto b = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff0a1115)); g.fillRect(b);
        g.setColour(line); g.drawRect(b, 1.0f);
        const double past = 6.0e6, fut = 2.0e6;
        auto xOf = [&](double t) { return (float) ((t - ((double) md.nowUs - past)) / (past + fut)) * b.getWidth(); };
        if (!md.tl.map.valid || md.tl.snap.bpm <= 0) {
            g.setColour(dim); g.setFont(sans(16.0f));
            g.drawText("Waiting for the drummer. Play, or tap.", getLocalBounds(), juce::Justification::centred);
        } else {
            const double perUs = periodSamples(md.tl.snap) * 1.0e6 / md.tl.map.sampleRate;
            // expectancy windows (future beats) and the predicted grid
            int64_t k = firstBeatAtOrAfter(md.tl, md.nowUs - (int64_t) past);
            for (int guard = 0; guard < 200; ++guard, ++k) {
                const GridBeat gb = gridBeat(md.tl, k);
                if ((double) gb.hostUs > (double) md.nowUs + fut) break;
                const float x = xOf((double) gb.hostUs);
                if (gb.hostUs >= md.nowUs) {
                    const float hw = (float) (0.15 * perUs / (past + fut)) * b.getWidth();
                    g.setColour(green.withAlpha(0.06f)); g.fillRect(x - hw, 0.0f, 2 * hw, b.getHeight());
                }
                g.setColour(gb.beatInBar == 1 ? green.withAlpha(0.75f) : grid.brighter(0.25f));
                g.drawLine(x, 0.0f, x, b.getHeight(), gb.beatInBar == 1 ? 2.0f : 1.0f);
                if (gb.beatInBar == 1) { g.setColour(dim); g.setFont(mono(14.0f)); g.drawText(juce::String(gb.barNumber), (int) x + 4, 2, 40, 16, juce::Justification::left); }
            }
        }
        // novelty trace (heart-monitor)
        juce::Path p;
        const float base = b.getBottom() - 6.0f, amp = b.getHeight() * 0.7f;
        for (int i = 0; i < kTraceLen; ++i) {
            const double t = (double) md.nowUs - (double) (kTraceLen - 1 - i) * 10000.0;
            const float x = xOf(t), y = base - trace[i] * amp;
            if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
        g.setColour(cyan.withAlpha(0.85f)); g.strokePath(p, juce::PathStrokeType(1.6f));
        // onset spikes
        for (const auto& o : md.onsets) {
            const float x = xOf((double) o.hostUs);
            if (x < 0 || x > b.getWidth()) continue;
            g.setColour(roleColour(o.role));
            g.drawLine(x, base, x, base - juce::jlimit(0.15f, 1.0f, o.strength) * amp, 2.0f);
        }
        // now marker
        const float nx = xOf((double) md.nowUs);
        g.setColour(text.withAlpha(0.5f)); g.drawLine(nx, 0.0f, nx, b.getHeight(), 1.0f);
    }
    void update(Core& core) { core.copyTrace(trace, kTraceLen); repaint(); }
};

// =============================================================== Inputs
struct MainComponent::Inputs : juce::Component {
    MainComponent& m;
    explicit Inputs(MainComponent& mc) : m(mc) {}

    struct Tile { juce::Rectangle<int> r, role, dist, mute; int chIndex; };
    std::vector<Tile> tiles() const
    {
        std::vector<Tile> t;
        const auto& chs = m.model_.session.channels;
        const int n = (int) chs.size();
        if (n == 0) return t;
        const int w = juce::jmin(190, getWidth() / n);
        for (int i = 0; i < n; ++i) {
            Tile x; x.chIndex = i;
            x.r = juce::Rectangle<int>(i * w + 2, 2, w - 4, getHeight() - 4);
            x.role = x.r.withHeight(24).reduced(6, 0);
            x.dist = juce::Rectangle<int>(x.r.getX() + 6, x.r.getBottom() - 26, 70, 22);
            x.mute = juce::Rectangle<int>(x.r.getRight() - 36, x.r.getBottom() - 26, 30, 22);
            t.push_back(x);
        }
        return t;
    }
    void paint(juce::Graphics& g) override
    {
        const auto& md = m.model_;
        const auto ts = tiles();
        if (ts.empty()) { g.setColour(dim); g.setFont(sans(15.0f)); g.drawText("No input channels connected.", getLocalBounds(), juce::Justification::centred); return; }
        for (const auto& t : ts) {
            const auto& c = md.session.channels[(size_t) t.chIndex];
            g.setColour(panel); g.fillRoundedRectangle(t.r.toFloat(), 6.0f);
            g.setColour(line); g.drawRoundedRectangle(t.r.toFloat(), 6.0f, 1.0f);
            const bool led = md.nowUs < md.onsetLedUntilUs[juce::jlimit(0, 7, (int) c.role)];
            g.setColour(c.mute ? dim : roleColour(c.role)); g.setFont(sans(16.0f, true));
            g.drawText(juce::String(roleName(c.role)) + (c.mute ? " (muted)" : ""), t.role, juce::Justification::centredLeft);
            g.setColour(led && !c.mute ? roleColour(c.role) : grid);
            g.fillEllipse((float) t.r.getRight() - 24.0f, (float) t.r.getY() + 8.0f, 14.0f, 14.0f);
            // level meter
            const float lv = c.channel >= 0 && c.channel < kMaxChannels ? juce::jlimit(0.0f, 1.0f, md.levels[c.channel]) : 0.0f;
            const float db = lv > 1e-4f ? 20.0f * std::log10(lv) : -80.0f;
            const float frac = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
            const auto mr = juce::Rectangle<float>((float) t.r.getX() + 8.0f, (float) t.r.getY() + 32.0f, (float) t.r.getWidth() - 16.0f, 10.0f);
            g.setColour(grid); g.fillRoundedRectangle(mr, 4.0f);
            g.setColour(frac > 0.95f ? red : frac > 0.8f ? amber : green);
            g.fillRoundedRectangle(mr.withWidth(mr.getWidth() * frac), 4.0f);
            // channel label
            g.setColour(dim); g.setFont(sans(14.0f));
            const juce::String nm = c.channel < md.channelNames.size() ? md.channelNames[c.channel] : juce::String("Ch ") + juce::String(c.channel + 1);
            g.drawText(nm, t.r.getX() + 8, t.r.getY() + 46, t.r.getWidth() - 16, 18, juce::Justification::centredLeft);
            // chips
            g.setColour(panel2); g.fillRoundedRectangle(t.dist.toFloat(), 4.0f); g.fillRoundedRectangle(t.mute.toFloat(), 4.0f);
            g.setColour(text); g.setFont(sans(14.0f, true));
            g.drawText(juce::String(c.distanceM, 1) + " m", t.dist, juce::Justification::centred);
            g.setColour(c.mute ? red : dim); g.drawText("M", t.mute, juce::Justification::centred);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        for (const auto& t : tiles()) {
            if (!t.r.contains(e.getPosition())) continue;
            const int idx = t.chIndex;
            auto edit = [this, idx](std::function<void(ChannelAssign&)> f) {
                m.ctx_.core.editSession([&](SessionSettings& s) { if (idx < (int) s.channels.size()) f(s.channels[(size_t) idx]); s.autoRoleMap = false; });
            };
            if (t.mute.contains(e.getPosition())) { edit([](ChannelAssign& c) { c.mute = !c.mute; }); return; }
            if (t.role.contains(e.getPosition())) {
                juce::PopupMenu menu;
                const InputRole roles[] = { InputRole::Any, InputRole::Kick, InputRole::Snare, InputRole::HiHat, InputRole::Overhead, InputRole::Trigger };
                for (auto r : roles) menu.addItem((int) r + 1, roleName(r));
                menu.showMenuAsync(juce::PopupMenu::Options(), [edit](int res) { if (res > 0) edit([res](ChannelAssign& c) { c.role = (InputRole) (res - 1); }); });
                return;
            }
            if (t.dist.contains(e.getPosition())) {
                juce::PopupMenu menu;
                menu.addItem(1, "Close (0.1 m, 0.3 ms)"); menu.addItem(2, "Overhead (1 m, 2.9 ms)"); menu.addItem(3, "Room (3 m, 8.7 ms)");
                menu.addItem(4, "Custom...");
                juce::Component::SafePointer<Inputs> sp(this);
                menu.showMenuAsync(juce::PopupMenu::Options(), [sp, edit](int res) {
                    if (res >= 1 && res <= 3) { const double d[] = { 0.1, 1.0, 3.0 }; edit([&](ChannelAssign& c) { c.distanceM = d[res - 1]; }); }
                    if (res == 4 && sp != nullptr) {
                        auto* w = new juce::AlertWindow("Microphone distance", "Distance from the source in metres", juce::MessageBoxIconType::NoIcon);
                        w->addTextEditor("d", "0.5");
                        w->addButton("OK", 1); w->addButton("Cancel", 0);
                        w->enterModalState(true, juce::ModalCallbackFunction::create([w, edit](int r) {
                            if (r == 1) { const double v = juce::jlimit(0.0, 20.0, w->getTextEditorContents("d").getDoubleValue()); edit([v](ChannelAssign& c) { c.distanceM = v; }); }
                        }), true);
                    }
                });
                return;
            }
        }
    }
};

// =============================================================== Outputs
struct MainComponent::Outputs : juce::Component {
    MainComponent& m;
    struct Tile : juce::Component {
        juce::ToggleButton enable;
        juce::TextEditor offset;
        juce::String title, status;
        bool hasOffset = true, warn = false;
        std::function<void()> onTitleClick;
        Tile() { addAndMakeVisible(enable); addChildComponent(offset); offset.setInputRestrictions(7, "-0123456789."); offset.setJustification(juce::Justification::centredRight); }
        void resized() override
        {
            enable.setBounds(6, 4, 30, 28);
            offset.setBounds(getWidth() - 88, getHeight() - 30, 54, 24);
            offset.setVisible(hasOffset);
        }
        void paint(juce::Graphics& g) override
        {
            g.setColour(panel); g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 6.0f);
            g.setColour(warn ? amber : line); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 6.0f, 1.0f);
            g.setColour(text); g.setFont(sans(16.0f, true));
            g.drawText(title, 40, 6, getWidth() - 48, 24, juce::Justification::centredLeft);
            g.setColour(warn ? amber : dim); g.setFont(sans(14.0f));
            g.drawFittedText(status, 10, 34, getWidth() - 20, getHeight() - 70 + (hasOffset ? 0 : 24), juce::Justification::topLeft, 2);
            if (hasOffset) { g.setColour(dim); g.drawText("offset ms", 10, getHeight() - 30, 80, 24, juce::Justification::centredLeft); }
        }
        void mouseUp(const juce::MouseEvent& e) override { if (e.getPosition().y < 32 && e.getPosition().x > 40 && onTitleClick) onTitleClick(); }
    };
    Tile link, midi, osc, sync;
    explicit Outputs(MainComponent& mc) : m(mc)
    {
        link.title = "Ableton Link"; midi.title = "MIDI clock"; osc.title = "OSC"; sync.title = "Sync bus";
        link.hasOffset = false;
        for (Tile* t : { &link, &midi, &osc, &sync }) addAndMakeVisible(*t);
        link.onTitleClick = [this] { m.showSettings(SettingsPanel::Outputs); };
        midi.onTitleClick = osc.onTitleClick = sync.onTitleClick = link.onTitleClick;
        link.enable.onClick = [this] { m.ctx_.core.editSession([&](SessionSettings& s) { s.link.enabled = link.enable.getToggleState(); }); };
        midi.enable.onClick = [this] { m.ctx_.core.editSession([&](SessionSettings& s) { s.midi.enabled = midi.enable.getToggleState(); }); };
        osc.enable.onClick = [this] { m.ctx_.core.editSession([&](SessionSettings& s) { s.osc.enabled = osc.enable.getToggleState(); }); };
        sync.enable.onClick = [this] { m.ctx_.core.editSession([&](SessionSettings& s) { s.sync.enabled = sync.enable.getToggleState(); }); };
        auto hook = [this](Tile& t, std::function<void(SessionSettings&, double)> f) {
            t.offset.onReturnKey = t.offset.onFocusLost = [this, &t, f] {
                const double v = juce::jlimit(-300.0, 300.0, t.offset.getText().getDoubleValue());
                m.ctx_.core.editSession([&](SessionSettings& s) { f(s, v); });
            };
        };
        hook(midi, [](SessionSettings& s, double v) { s.midi.offsetMs = v; });
        hook(osc, [](SessionSettings& s, double v) { s.osc.offsetMs = v; });
        hook(sync, [](SessionSettings& s, double v) { s.sync.offsetMs = v; });
        midi.enable.setTooltip("Send MIDI clock (24 ppqn) with Start/Stop to the selected port");
        link.enable.setTooltip("Publish tempo and phase on Ableton Link");
    }
    void resized() override
    {
        const bool hasLink = linkCompiledIn();
        link.setVisible(hasLink);
        const int n = hasLink ? 4 : 3;
        const int w = getWidth() / n;
        int x = 0;
        if (hasLink) { link.setBounds(x, 0, w, getHeight()); x += w; }
        midi.setBounds(x, 0, w, getHeight()); x += w;
        osc.setBounds(x, 0, w, getHeight()); x += w;
        sync.setBounds(x, 0, getWidth() - x, getHeight());
    }
    void update()
    {
        const auto& s = m.model_.session;
        auto& c = m.ctx_.core;
        link.enable.setToggleState(s.link.enabled, juce::dontSendNotification);
        midi.enable.setToggleState(s.midi.enabled, juce::dontSendNotification);
        osc.enable.setToggleState(s.osc.enabled, juce::dontSendNotification);
        sync.enable.setToggleState(s.sync.enabled, juce::dontSendNotification);
        auto setOff = [](Tile& t, double v) { if (!t.offset.hasKeyboardFocus(true)) t.offset.setText(juce::String(v, 1), false); };
        setOff(midi, s.midi.offsetMs); setOff(osc, s.osc.offsetMs); setOff(sync, s.sync.offsetMs);
        const auto& ls = c.link().status();
        link.status = linkCompiledIn() ? (c.link().available() ? juce::String(ls.peers.load()) + " peer(s), " + juce::String(ls.detail()) + ", quantum " + juce::String(s.link.quantum)
                                                               : juce::String("Link is not available"))
                                       : juce::String();
        link.warn = m.model_.linkFighting || m.model_.linkFollowers > 1;
        const auto& ms = c.midiClock().status();
        midi.status = m.ctx_.ports.statusText() + (ms.connected.load() ? ", jitter " + juce::String(ms.measuredJitterUs.load()) + " us" : juce::String());
        midi.warn = s.midi.enabled && !ms.connected.load();
        const auto& os = c.osc().status();
        osc.status = juce::String(os.detail()) + (os.errors.load() ? ", errors " + juce::String(os.errors.load()) : juce::String());
        osc.warn = s.osc.enabled && !os.lastError().empty();
        sync.status = m.ctx_.standalone ? "L: 24 ppqn pulse, R: beat click. Route outputs in Devices." : "Enable the Sync output bus in the host.";
        for (Tile* t : { &link, &midi, &osc, &sync }) t->repaint();
    }
};

// =============================================================== Transport
struct MainComponent::Transport : juce::Component {
    MainComponent& m;
    struct ActionButton : juce::TextButton {
        Transport& owner; Action action;
        ActionButton(Transport& o, Action a, const juce::String& label) : juce::TextButton(label), owner(o), action(a) {}
        void mouseDown(const juce::MouseEvent& e) override
        {
            if (!e.mods.isPopupMenu()) { juce::TextButton::mouseDown(e); return; }
            auto& core = owner.m.ctx_.core;
            juce::PopupMenu menu;
            menu.addItem(1, "MIDI learn (next note or CC)...");
            const auto cur = mappingText(owner.m.model_.session, action);
            menu.addItem(2, cur.isEmpty() ? juce::String("No MIDI mapping") : "Clear mapping (" + cur + ")", cur.isNotEmpty());
            if (core.midiLearnPending() != Action::None) menu.addItem(3, "Cancel learn");
            menu.showMenuAsync(juce::PopupMenu::Options(), [this](int r) {
                auto& c = owner.m.ctx_.core;
                if (r == 1) c.armMidiLearn(action); else if (r == 2) c.clearMapping(action); else if (r == 3) c.cancelMidiLearn();
            });
        }
    };
    std::vector<std::unique_ptr<ActionButton>> btns;
    explicit Transport(MainComponent& mc) : m(mc)
    {
        struct D { Action a; const char* label; const char* tip; };
        const D defs[] = { { Action::Tap, "Tap", "Set the beat now (T)" }, { Action::DownbeatNow, "Downbeat now", "The current beat is beat 1 (D)" },
                           { Action::NudgeMinus, "Nudge -", "Shift published beats 10 ms earlier ([)" }, { Action::NudgePlus, "Nudge +", "Shift published beats 10 ms later (])" },
                           { Action::Half, "Half", "Halve the tempo (H)" }, { Action::Double, "Double", "Double the tempo (J)" },
                           { Action::Relock, "Relock", "Re-acquire phase and bar position (R)" } };
        for (const auto& d : defs) {
            auto b = std::make_unique<ActionButton>(*this, d.a, d.label);
            b->setTooltip(juce::String(d.tip) + ". Right-click for MIDI learn.");
            const Action a = d.a;
            b->onClick = [this, a] { m.ctx_.core.doAction(a); };
            addAndMakeVisible(*b);
            btns.push_back(std::move(b));
        }
    }
    void resized() override
    {
        const int n = (int) btns.size(); const int w = getWidth() / n;
        for (int i = 0; i < n; ++i) btns[(size_t) i]->setBounds(i * w + 3, 3, w - 6, getHeight() - 6);
    }
    void update()
    {
        const Action pending = m.ctx_.core.midiLearnPending();
        for (auto& b : btns) {
            const auto map = mappingText(m.model_.session, b->action);
            const juce::String base = actionLabel(b->action);
            b->setButtonText(pending == b->action ? "LEARNING..." : (map.isNotEmpty() ? base + "  [" + map + "]" : base));
            b->setToggleState(pending == b->action, juce::dontSendNotification);
        }
    }
};

// =============================================================== Footer
struct MainComponent::Footer : juce::Component {
    MainComponent& m;
    juce::ComboBox profile, pattern;
    juce::TextEditor refBpm;
    juce::TextButton calibrate { "Calibrate" }, songs { "Songs" }, report { "Report" }, stage { "Stage" }, settings { "Settings" }, devices { "Devices" }, learn { "Learn bar" };
    juce::Label lProfile { {}, "Profile" }, lRef { {}, "Reference BPM" }, lPattern { {}, "Pattern" };
    juce::ToggleButton log { "Drift log" };
    bool badgeShown = false;
    std::vector<BarPattern> patternIds;
    explicit Footer(MainComponent& mc) : m(mc)
    {
        for (auto* c : std::initializer_list<juce::Component*>{ &profile, &pattern, &refBpm, &calibrate, &songs, &report, &stage, &settings, &devices, &learn, &lProfile, &lRef, &lPattern, &log })
            addAndMakeVisible(*c);
        profile.addItem("Stage", 1); profile.addItem("Rehearsal", 2); profile.addItem("Custom", 3);
        for (const auto& p : patternList()) { pattern.addItem(p.label, (int) patternIds.size() + 1); patternIds.push_back(p.pattern); }
        refBpm.setInputRestrictions(6, "0123456789.");
        refBpm.setTooltip("Reference tempo from the song map. 0 = none. Kills octave and half-time errors.");
        refBpm.setJustification(juce::Justification::centred);
        profile.onChange = [this] { const auto id = profile.getSelectedId(); edit([id](Params& p) { p.profile = id == 2 ? ProfileType::Rehearsal : id == 3 ? ProfileType::Custom : ProfileType::Stage; }); };
        pattern.onChange = [this] { const int i = pattern.getSelectedId() - 1; if (i >= 0 && i < (int) patternIds.size()) edit([this, i](Params& p) { p.pattern = patternIds[(size_t) i]; }); };
        refBpm.onReturnKey = refBpm.onFocusLost = [this] { const double v = juce::jlimit(0.0, 300.0, refBpm.getText().getDoubleValue()); edit([v](Params& p) { p.referenceBpm = v; }); };
        calibrate.onClick = [this] { m.showCalibration(); };
        songs.onClick = [this] { m.showSongs(); };
        report.onClick = [this] { m.showReport(); };
        stage.onClick = [this] { m.setStageMode(true); };
        settings.onClick = [this] { m.showSettings(0); };
        devices.onClick = [this] { if (m.ctx_.showDevicePanel) m.ctx_.showDevicePanel(); };
        learn.setTooltip("Learn the kick/snare pattern over the next two bars (bar tracker Learn mode)");
        learn.onClick = [this] { m.ctx_.core.learnPattern(); };
        log.onClick = [this] {
            if (log.getToggleState()) m.ctx_.core.startDriftLog(); else m.ctx_.core.stopDriftLog();
        };
        devices.setVisible(m.ctx_.standalone);
    }
    void edit(std::function<void(Params&)> f) { auto p = m.ctx_.core.params(); f(p); m.ctx_.core.setParams(p); }
    void resized() override
    {
        auto area = getLocalBounds().reduced(6, 4);
        auto row1 = area.removeFromTop(area.getHeight() / 2), row2 = area;
        auto place = [&](juce::Rectangle<int>& r, juce::Component& c, int w) { c.setBounds(r.removeFromLeft(w).reduced(2, 1)); };
        place(row1, lProfile, 56); place(row1, profile, 112); place(row1, lPattern, 58); place(row1, pattern, 190);
        place(row1, lRef, 112); place(row1, refBpm, 64); place(row1, learn, 96);
        place(row2, calibrate, 100); place(row2, songs, 80); place(row2, report, 80);
        if (m.ctx_.standalone) place(row2, devices, 90);
        settings.setBounds(row2.removeFromRight(100).reduced(2, 1));
        stage.setBounds(row2.removeFromRight(80).reduced(2, 1));
        log.setBounds(row2.removeFromRight(110));
    }
    void paint(juce::Graphics& g) override
    {
        g.setColour(panel); g.fillRect(getLocalBounds());
        g.setColour(line); g.drawHorizontalLine(0, 0, (float) getWidth());
        if (!m.ctx_.hasCalibration()) {
            g.setColour(amber); g.setFont(sans(14.0f, true));
            const int x = calibrate.getRight() + 0;
            g.fillEllipse((float) calibrate.getX() + 4, (float) calibrate.getY() - 2, 10, 10);
            (void) x;
        }
    }
    void update()
    {
        const auto& p = m.model_.params;
        profile.setSelectedId(p.profile == ProfileType::Rehearsal ? 2 : p.profile == ProfileType::Custom ? 3 : 1, juce::dontSendNotification);
        for (size_t i = 0; i < patternIds.size(); ++i) if (patternIds[i] == p.pattern) pattern.setSelectedId((int) i + 1, juce::dontSendNotification);
        if (!refBpm.hasKeyboardFocus(true)) refBpm.setText(p.referenceBpm > 0 ? juce::String(p.referenceBpm, 1) : juce::String("0"), false);
        calibrate.setTooltip(m.ctx_.hasCalibration() ? "Latency calibration (done)" : "No calibration for this device yet: timing is uncompensated");
        const bool show = !m.ctx_.hasCalibration();
        if (show != badgeShown) { badgeShown = show; repaint(); }
        log.setToggleState(m.ctx_.core.driftLogPath().empty() ? false : log.getToggleState(), juce::dontSendNotification);
    }
};

// =============================================================== MainComponent
MainComponent::MainComponent(UiContext& ctx) : ctx_(ctx)
{
    setLookAndFeel(&laf_);
    header_ = std::make_unique<Header>(*this);
    trace_ = std::make_unique<Trace>(*this);
    inputs_ = std::make_unique<Inputs>(*this);
    outputs_ = std::make_unique<Outputs>(*this);
    transport_ = std::make_unique<Transport>(*this);
    footer_ = std::make_unique<Footer>(*this);
    for (juce::Component* c : std::initializer_list<juce::Component*>{ header_.get(), trace_.get(), inputs_.get(), outputs_.get(), transport_.get(), footer_.get() })
        addAndMakeVisible(*c);
    addChildComponent(closeBtn_);
    closeBtn_.onClick = [this] { closePanel(); };
    setWantsKeyboardFocus(true);
    stage_ = ctx.user.stageMode;
    model_.params = ctx.core.params();
    model_.session = ctx.core.session();
    if (ctx.inputChannelNames) model_.channelNames = ctx.inputChannelNames();
    setSize(preferredWidth(), preferredHeight());
    startTimerHz(30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    panel_.reset();
    setLookAndFeel(nullptr);
}

void MainComponent::timerCallback()
{
    auto& core = ctx_.core;
    model_.nowUs = steadyNowUs();
    model_.tl = core.timeline();
    model_.params = core.params();
    model_.deviceLost = core.deviceLost();
    model_.standby = core.standby();
    model_.linkFighting = core.link().publisher().diagnostics().fightingPeers.load();
    model_.linkFollowers = core.link().followers();
    if (++sessionRefreshTicks_ >= 3) {
        sessionRefreshTicks_ = 0;
        model_.session = core.session();
        if (ctx_.inputChannelNames) model_.channelNames = ctx_.inputChannelNames();
    }
    for (int c = 0; c < kMaxChannels; ++c) model_.levels[c] = core.inputLevel(c);
    EngineEvent e;
    while (core.popUiEvent(e)) {
        if (e.type == EngineEvent::Onset) {
            const int64_t t = model_.tl.map.hostTimeForSample(e.onset.sample);
            model_.onsets.push_back({ t, e.onset.strength, e.onset.role });
            model_.onsetLedUntilUs[juce::jlimit(0, 7, (int) e.onset.role)] = model_.nowUs + 90000;
        } else if (e.beat.beatInBar == 1) {
            model_.downbeatFlashUntilUs = model_.nowUs + 140000;
        }
    }
    while (!model_.onsets.empty() && model_.onsets.front().hostUs < model_.nowUs - 8000000) model_.onsets.pop_front();
    header_->update();
    trace_->update(core);
    if (!stage_) { inputs_->repaint(); outputs_->update(); footer_->update(); }
    transport_->update();
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(bg);
}

struct MainComponent::Backdrop : juce::Component {
    juce::Rectangle<int> frame; juce::String title;
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black.withAlpha(0.6f));
        g.setColour(panel2); g.fillRoundedRectangle(frame.toFloat(), 8.0f);
        g.setColour(line); g.drawRoundedRectangle(frame.toFloat(), 8.0f, 1.0f);
        g.setColour(text); g.setFont(sans(20.0f, true));
        g.drawText(title, frame.getX() + 16, frame.getY() + 4, frame.getWidth() - 80, 32, juce::Justification::centredLeft);
    }
    void mouseDown(const juce::MouseEvent&) override {}
};

void MainComponent::resized()
{
    auto r = getLocalBounds();
    header_->setBounds(r.removeFromTop(120));
    if (!stage_) {
        footer_->setBounds(r.removeFromBottom(84));
        outputs_->setBounds(r.removeFromBottom(100));
        inputs_->setBounds(r.removeFromBottom(92));
    }
    transport_->setBounds(r.removeFromBottom(stage_ ? 90 : 56));
    trace_->setBounds(r.reduced(0, 2));
    const bool show = !stage_;
    inputs_->setVisible(show); outputs_->setVisible(show); footer_->setVisible(show);
    if (panel_) {
        panelFrame_ = getLocalBounds().reduced(juce::jmax(16, getWidth() / 20), juce::jmax(12, getHeight() / 24));
        backdrop_->setBounds(getLocalBounds()); backdrop_->frame = panelFrame_; backdrop_->title = panel_->title(); backdrop_->repaint();
        panel_->setBounds(panelFrame_.reduced(2).withTrimmedTop(40));
        closeBtn_.setBounds(panelFrame_.getRight() - 44, panelFrame_.getY() + 4, 36, 32);
    }
}

void MainComponent::showPanel(std::unique_ptr<PanelBase> p)
{
    closePanel();
    panel_ = std::move(p);
    panel_->onClose = [this] { juce::MessageManager::callAsync([sp = juce::Component::SafePointer<MainComponent>(this)] { if (sp) sp->closePanel(); }); };
    backdrop_ = std::make_unique<Backdrop>();
    addAndMakeVisible(*backdrop_);
    addAndMakeVisible(*panel_);
    closeBtn_.setVisible(true); closeBtn_.toFront(false);
    resized();
    repaint();
}

void MainComponent::closePanel()
{
    if (!panel_) return;
    panel_.reset(); backdrop_.reset();
    closeBtn_.setVisible(false);
    repaint();
    grabKeyboardFocus();
}

void MainComponent::showSettings(int tab) { showPanel(std::make_unique<SettingsPanel>(ctx_, tab)); }
void MainComponent::showSongs() { showPanel(std::make_unique<SongMapPanel>(ctx_)); }
void MainComponent::showReport() { showPanel(std::make_unique<ReportPanel>(ctx_)); }
void MainComponent::showCalibration()
{
    // "No modal dialogs while Locked except calibration, which asks for confirmation" (RS-01 section 8).
    if (model_.tl.snap.state == (uint8_t) EngineState::Locked) {
        juce::Component::SafePointer<MainComponent> sp(this);
        juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::QuestionIcon)
                                         .withTitle("Calibrate while locked?")
                                         .withMessage("Pacemaker is locked to the band. Calibration plays test clicks and may disturb tracking.")
                                         .withButton("Calibrate").withButton("Cancel"),
                                     [sp](int r) { if (r == 1 && sp) sp->showPanel(std::make_unique<CalibrationWizard>(sp->ctx_)); });
        return;
    }
    showPanel(std::make_unique<CalibrationWizard>(ctx_));
}

void MainComponent::setStageMode(bool on)
{
    stage_ = on;
    ctx_.user.stageMode = on; ctx_.saveUser();
    resized();
    if (ctx_.requestResize) ctx_.requestResize();
    repaint();
}

void MainComponent::mouseDown(const juce::MouseEvent&) { grabKeyboardFocus(); }

bool MainComponent::keyPressed(const juce::KeyPress& k)
{
    if (panel_ && k == juce::KeyPress::escapeKey) { closePanel(); return true; }
    if (panel_) return false;
    auto& c = ctx_.core;
    if (k == juce::KeyPress::escapeKey) { if (stage_) { setStageMode(false); return true; } c.cancelMidiLearn(); return true; }
    const auto ch = juce::CharacterFunctions::toLowerCase(k.getTextCharacter());
    switch (ch) {
    case 't': c.doAction(Action::Tap); return true;
    case 'd': c.doAction(Action::DownbeatNow); return true;
    case 'r': c.doAction(Action::Relock); return true;
    case 'f': c.setFollow(!c.follow()); return true;
    case '[': c.doAction(Action::NudgeMinus); return true;
    case ']': c.doAction(Action::NudgePlus); return true;
    case 'h': c.doAction(Action::Half); return true;
    case 'j': c.doAction(Action::Double); return true;
    case 's': setStageMode(!stage_); return true;
    case ',': showSettings(0); return true;
    default: break;
    }
    return false;
}

} // namespace pacemaker::ui
