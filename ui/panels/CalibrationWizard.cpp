// Calibration wizard (RS-01 section 5, ES-03): loopback (standalone), tap along (plugin), hardware clock offset, mic distances.
#include "Panels.h"
#include "PanelWidgets.h"
#include <cmath>
#include <ctime>

namespace pacemaker::ui {

namespace {

enum class Page { Method, LoopbackSetup, LoopbackRun, LoopbackResult, TapAlong, Hardware, Mics };

struct DriverHint { const char* name; const char* trust; const char* typical; };
const DriverHint kDriverHints[] = {
    { "ASIO", "Reported latency is good", "2 to 4 ms at 64 samples" },
    { "CoreAudio", "Reported latency is fair; verify with a loopback run", "2 to 4 ms at 32 to 64 samples" },
    { "WASAPI exclusive", "Reported latency is good", "about 5 ms" },
    { "WASAPI shared", "Reported latency is poor; calibrate", "10 to 12 ms" },
    { "DirectSound", "Reported latency is useless; calibrate (or use ASIO)", "15 to 20 ms" },
    { "ALSA / JACK", "Depends on the driver", "3 to 8 ms" },
    { "PipeWire", "Depends on the configuration", "1 to 11 ms" },
};

// Which hint row matches the active JUCE device type (or -1).
int hintForDeviceType(const juce::String& t)
{
    if (t.containsIgnoreCase("ASIO")) return 0;
    if (t.containsIgnoreCase("CoreAudio")) return 1;
    if (t.containsIgnoreCase("Exclusive")) return 2;
    if (t.containsIgnoreCase("Windows Audio")) return 3;
    if (t.containsIgnoreCase("DirectSound")) return 4;
    if (t.containsIgnoreCase("ALSA") || t.containsIgnoreCase("JACK")) return 5;
    if (t.containsIgnoreCase("Pipe")) return 6;
    return -1;
}

juce::String ms(double samples, double sr) { return sr > 0 ? juce::String(samples * 1000.0 / sr, 2) + " ms" : juce::String("-"); }

} // namespace

struct CalibrationWizard::Impl : juce::Timer {
    // Four-step progress strip.
    struct Strip : juce::Component {
        int stage = 0; bool plugin = false;
        void paint(juce::Graphics& g) override
        {
            const char* names[] = { "1  Method", "2  Measure", "3  Result", "4  Mic distances" };
            const int n = 4, w = getWidth() / n;
            for (int i = 0; i < n; ++i) {
                auto r = juce::Rectangle<int>(i * w + 2, 2, w - 4, getHeight() - 4).toFloat();
                g.setColour(i == stage ? colours::green.darker(0.45f) : colours::panel);
                g.fillRoundedRectangle(r, 5.0f);
                g.setColour(i == stage ? colours::green : colours::line);
                g.drawRoundedRectangle(r, 5.0f, 1.0f);
                g.setColour(i == stage ? juce::Colours::white : colours::dim);
                g.setFont(sans(15.0f, i == stage));
                g.drawText(names[i], r.toNearestInt(), juce::Justification::centred);
            }
        }
    };

    CalibrationWizard& owner;
    UiContext& ctx;
    Strip strip;
    FormPage pageView;
    juce::TextButton backBtn { "Back" }, doneBtn { "Done" };
    Page page = Page::Method;

    // loopback state
    int inChoice = 0, outChoice = 0;
    bool allOnInput = false;
    AudioBridge::Routing savedRouting;
    bool routingChanged = false;
    int64_t runStartMs = 0;
    bool finishing = false;
    LoopbackResult result;
    double resultSr = 48000.0;
    int resultReportedIn = 0, resultReportedOut = 0;
    bool stored = false;
    juce::Label* liveA = nullptr; juce::Label* liveB = nullptr; juce::Label* liveC = nullptr;
    juce::TextButton* hwSet = nullptr;

    // hardware state
    int hwRoleChoice = 0;
    bool hwStarted = false;
    double hwMeasured = 0; bool hwValid = false;

    // tap along
    bool tapStarted = false;
    juce::String manualMs;

    juce::StringArray inNames; std::vector<int> outIdx; juce::StringArray outNames;

    Impl(CalibrationWizard& o, UiContext& c) : owner(o), ctx(c)
    {
        owner.addAndMakeVisible(strip); owner.addAndMakeVisible(pageView);
        owner.addAndMakeVisible(backBtn); owner.addAndMakeVisible(doneBtn);
        backBtn.onClick = [this] { goBack(); };
        doneBtn.onClick = [this] { restoreRouting(); if (owner.onClose) owner.onClose(); };
        if (ctx.bridge) savedRouting = ctx.bridge->routing();
        startTimerHz(10);
        show((Page) juce::SystemStats::getEnvironmentVariable("PM_STEP", "0").getIntValue()); /*TMPHACK*/
    }
    ~Impl() override { stopTimer(); restoreRouting(); }

    bool deviceRunning() const { return ctx.standalone && ctx.bridge != nullptr && ctx.bridge->running() && ctx.bridge->sampleRate() > 0; }

    void restoreRouting()
    {
        if (ctx.bridge && routingChanged) { ctx.bridge->setRouting(savedRouting); routingChanged = false; }
    }

    void goBack()
    {
        switch (page) {
        case Page::LoopbackRun: restoreRouting(); finishing = false; show(Page::LoopbackSetup); break;
        case Page::LoopbackResult: show(Page::LoopbackSetup); break;
        case Page::Method: break;
        default: show(Page::Method); break;
        }
    }

    // Rebuilding from inside a button callback would delete the button; defer.
    void showSoon(Page p)
    {
        juce::MessageManager::callAsync([sp = juce::Component::SafePointer<CalibrationWizard>(&owner), p] { if (sp) sp->impl_->show(p); });
    }

    void show(Page p)
    {
        page = p;
        liveA = liveB = liveC = nullptr; hwSet = nullptr;
        auto& f = pageView.form;
        f.clear();
        switch (p) {
        case Page::Method: buildMethod(f); break;
        case Page::LoopbackSetup: buildLoopbackSetup(f); break;
        case Page::LoopbackRun: buildLoopbackRun(f); break;
        case Page::LoopbackResult: buildLoopbackResult(f); break;
        case Page::TapAlong: buildTapAlong(f); break;
        case Page::Hardware: buildHardware(f); break;
        case Page::Mics: buildMics(f); break;
        }
        strip.stage = p == Page::Method ? 0 : p == Page::LoopbackResult ? 2 : p == Page::Mics ? 3 : 1;
        strip.repaint();
        backBtn.setVisible(p != Page::Method);
        pageView.relayout();
        pageView.setViewPosition(0, 0);
    }

    // ---------------------------------------------------------------- method
    juce::String deviceTypeName() const
    {
        if (ctx.deviceManager && ctx.deviceManager->getCurrentDeviceTypeObject()) return ctx.deviceManager->getCurrentDeviceTypeObject()->getTypeName();
        return {};
    }

    void buildMethod(Form& f)
    {
        f.heading("Choose a calibration method");
        f.note("Calibration measures the input latency that the audio driver does not report, so that Pacemaker's clicks, MIDI clock "
               "and Link beats land on the drummer's beat.");
        const auto sess = ctx.core.session();
        if (ctx.standalone) {
            if (deviceRunning()) {
                CalibrationEntry e; const bool have = ctx.user.calibration.get(ctx.calibrationKey().toStdString(), e);
                f.info("Audio device", ctx.bridge->deviceName() + ", " + juce::String(ctx.bridge->sampleRate() / 1000.0, 1) + " kHz, "
                                           + juce::String(ctx.bridge->bufferSize()) + " samples");
                f.info("Calibration", have ? "hidden latency in " + juce::String(e.hiddenInputSamples) + " / out " + juce::String(e.hiddenOutputSamples) + " samples"
                                           : "not calibrated for this device yet", have ? colours::green : colours::amber);
            } else {
                f.note("No audio device is running. Open Devices, choose an audio device, and come back. Loopback calibration needs a running device.", colours::amber);
                if (ctx.showDevicePanel) f.button("", "Open Devices", [this] { if (ctx.showDevicePanel) ctx.showDevicePanel(); if (owner.onClose) owner.onClose(); }, 180);
            }
            auto& b1 = f.button("Loopback", "Loopback calibration", [this] { showSoon(Page::LoopbackSetup); }, 240);
            b1.setEnabled(deviceRunning() && ctx.bridge != nullptr);
            f.note("Patch an output of your interface into one of its inputs with a cable. Pacemaker plays 8 clicks and measures the round trip to the sample. Accurate to about 1 sample.");
        } else {
            f.info("Input latency", juce::String(sess.inputLatencyMs, 1) + " ms" + (sess.tapAlongValid ? "  (measured)" : "  (not set)"),
                   sess.tapAlongValid ? colours::green : colours::amber);
            f.button("Tap along", "Tap along to the DAW", [this] { showSoon(Page::TapAlong); }, 240);
            f.note("Inside a DAW the input path is invisible, so you play along with the DAW metronome and Pacemaker measures how late your hits are.");
        }
        f.button("Hardware clock", "Hardware clock offset", [this] { showSoon(Page::Hardware); }, 240);
        f.note("For MIDI clock receivers: feed a hardware metronome (slaved to Pacemaker's clock) into a spare input and Pacemaker measures its offset from your hits.");
        f.button("Microphones", "Mic distances", [this] { showSoon(Page::Mics); }, 240);
        f.note("Set how far each microphone is from its drum so the sound travel time (2.9 ms per metre) is removed.");

        f.gap(8);
        f.heading("What to expect from your driver");
        const int mine = hintForDeviceType(deviceTypeName());
        for (int i = 0; i < 7; ++i) {
            const auto& h = kDriverHints[i];
            f.info(juce::String(h.name) + (i == mine ? "  (yours)" : ""), juce::String(h.typical) + ". " + h.trust, i == mine ? colours::green : colours::text);
        }
    }

    // ---------------------------------------------------------------- loopback
    void gatherChannels()
    {
        inNames.clear(); outNames.clear(); outIdx.clear();
        auto* dev = ctx.deviceManager ? ctx.deviceManager->getCurrentAudioDevice() : nullptr;
        if (dev) {
            const auto all = dev->getInputChannelNames(); const auto act = dev->getActiveInputChannels();
            for (int i = 0, k = 0; i < all.size(); ++i) if (act[i]) inNames.add("Input " + juce::String(++k) + "  (" + all[i] + ")");
            const auto outs = dev->getOutputChannelNames(); const auto oact = dev->getActiveOutputChannels();
            int pos = 0;
            for (int i = 0; i < outs.size(); ++i) if (oact[i]) { outIdx.push_back(pos); outNames.add("Output " + juce::String(i + 1) + "  (" + outs[i] + ")"); ++pos; }
        }
        if (inNames.isEmpty() && ctx.inputChannelNames) inNames = ctx.inputChannelNames();
        if (inNames.isEmpty()) inNames.add("Input 1");
        if (outNames.isEmpty()) { outNames.add("Output 1"); outIdx.push_back(0); }
        inChoice = juce::jlimit(0, inNames.size() - 1, inChoice);
        outChoice = juce::jlimit(0, outNames.size() - 1, outChoice);
        // Preselect the routing the host already uses.
        if (ctx.bridge && ctx.bridge->routing().calibrationOut >= 0)
            for (size_t i = 0; i < outIdx.size(); ++i) if (outIdx[i] == ctx.bridge->routing().calibrationOut) outChoice = (int) i;
    }

    void buildLoopbackSetup(Form& f)
    {
        f.heading("Loopback calibration");
        if (!deviceRunning()) {
            f.note("No audio device is running, so there is nothing to measure. Open Devices, choose an audio device and try again.", colours::amber);
            if (ctx.showDevicePanel) f.button("", "Open Devices", [this] { if (ctx.showDevicePanel) ctx.showDevicePanel(); if (owner.onClose) owner.onClose(); }, 180);
            return;
        }
        gatherChannels();
        f.info("Device", ctx.bridge->deviceName() + ", " + juce::String(ctx.bridge->sampleRate() / 1000.0, 1) + " kHz, buffer " + juce::String(ctx.bridge->bufferSize()));
        f.info("Driver reports", "input " + juce::String(ctx.bridge->reportedInputLatency()) + " + output " + juce::String(ctx.bridge->reportedOutputLatency()) + " samples");
        f.combo("Output for the clicks", outNames, outChoice, [this](int i) { outChoice = i; }, 340);
        f.combo("Input to record", inNames, inChoice, [this](int i) { inChoice = i; }, 340);
        f.toggle("", allOnInput, [this](bool v) { allOnInput = v; }, "Assign all of the hidden delay to the input");
        f.note("1.  Turn the output volume of the cable path down; the click is a full-scale 1 ms burst.\n"
               "2.  Connect the chosen output to the chosen input with a patch cable (or hold the kick mic at a monitor speaker).\n"
               "3.  Press Start. Eight clicks play, half a second apart, and take about five seconds.");
        auto& b = f.button("", "Start", [this] { startLoopback(); }, 160);
        b.setColour(juce::TextButton::buttonColourId, colours::green.darker(0.5f));
    }

    void startLoopback()
    {
        if (!deviceRunning()) { showSoon(Page::LoopbackSetup); return; }
        auto r = ctx.bridge->routing();
        if (!routingChanged) savedRouting = r;
        r.calibrationOut = outChoice >= 0 && outChoice < (int) outIdx.size() ? outIdx[(size_t) outChoice] : 0;
        ctx.bridge->setRouting(r); routingChanged = true;
        ctx.core.startLoopback(inChoice);
        runStartMs = (int64_t) juce::Time::getMillisecondCounter();
        finishing = false;
        showSoon(Page::LoopbackRun);
    }

    void buildLoopbackRun(Form& f)
    {
        f.heading("Measuring...");
        liveA = &f.info("Status", "Playing clicks");
        f.note("Keep the cable connected and stay quiet. If nothing happens within 15 seconds, check the patch cable and the output level.");
    }

    void pollLoopback()
    {
        if (page != Page::LoopbackRun || finishing) return;
        const double el = (double) ((int64_t) juce::Time::getMillisecondCounter() - runStartMs) / 1000.0;
        if (ctx.core.loopbackReady()) {
            finishing = true;
            if (liveA) liveA->setText("Analysing", juce::dontSendNotification);
            const int rin = ctx.bridge ? ctx.bridge->reportedInputLatency() : 0, rout = ctx.bridge ? ctx.bridge->reportedOutputLatency() : 0;
            const double sr = ctx.bridge ? ctx.bridge->sampleRate() : 48000.0;
            const bool all = allOnInput;
            Core* core = &ctx.core;
            juce::Thread::launch([sp = juce::Component::SafePointer<CalibrationWizard>(&owner), core, rin, rout, sr, all] {
                const auto res = core->finishLoopback(rin, rout, all);
                juce::MessageManager::callAsync([sp, res, rin, rout, sr] {
                    if (!sp) return;
                    auto& im = *sp->impl_;
                    im.result = res; im.resultSr = sr; im.resultReportedIn = rin; im.resultReportedOut = rout; im.stored = false;
                    im.restoreRouting();
                    im.show(Page::LoopbackResult);
                });
            });
        } else if (el > 15.0 || !deviceRunning()) {
            finishing = true;
            restoreRouting();
            result = LoopbackResult(); result.message = deviceRunning() ? "No clicks were recorded in time." : "The audio device stopped.";
            resultSr = ctx.bridge ? ctx.bridge->sampleRate() : 48000.0; stored = false;
            show(Page::LoopbackResult);
        } else if (liveA) {
            liveA->setText("Playing clicks (" + juce::String(el, 1) + " s)", juce::dontSendNotification);
        }
    }

    void buildLoopbackResult(Form& f)
    {
        const auto& r = result;
        f.heading(r.pass ? "Measurement passed" : "Measurement failed");
        f.info("Result", r.pass ? "PASS" : "FAIL", r.pass ? colours::green : colours::red);
        f.info("Clicks detected", juce::String(r.clicksDetected) + " of " + juce::String(r.clicksTotal > 0 ? r.clicksTotal : 8));
        f.info("Round trip", juce::String(r.roundTripSamples) + " samples  (" + ms(r.roundTripSamples, resultSr) + ")");
        f.info("Driver reported", juce::String(r.reportedSumSamples) + " samples  (" + ms(r.reportedSumSamples, resultSr) + ")");
        f.info("Hidden delta", juce::String(r.hiddenSamples) + " samples  (" + ms(r.hiddenSamples, resultSr) + ")", r.pass ? colours::cyan : colours::text);
        f.info("Spread", juce::String(r.spreadSamples, 1) + " samples");
        if (r.message.size()) f.note(juce::String(r.message));
        if (r.pass && r.hiddenSamples < 0) f.note("The driver over-reports its latency. The negative value is still the right correction and is stored as is.");
        if (!r.pass) f.note("A pass needs at least 6 detected clicks within 4 samples of each other. Check the cable, raise the output level and try again.", colours::amber);
        f.gap(6);
        auto* store = f.makeButton(stored ? "Stored" : "Store", [this] { storeResult(); });
        store->setEnabled(r.pass && !stored && ctx.bridge != nullptr);
        auto* again = f.makeButton("Repeat", [this] { showSoon(Page::LoopbackSetup); });
        auto* mics = f.makeButton("Next: mic distances", [this] { showSoon(Page::Mics); });
        f.row("", { { store, 130 }, { again, 130 }, { mics, 220 } }, 36);
        if (stored) f.note("Stored for " + ctx.calibrationKey() + " and applied.", colours::green);
    }

    void storeResult()
    {
        if (!ctx.bridge || stored) return;
        CalibrationEntry e;
        e.hiddenInputSamples = result.hiddenInputSamples; e.hiddenOutputSamples = result.hiddenOutputSamples;
        e.measuredAt = (int64_t) std::time(nullptr);
        ctx.user.calibration.put(ctx.calibrationKey().toStdString(), e);
        ctx.saveUser();
        ctx.core.setDeviceLatency(ctx.bridge->reportedInputLatency(), ctx.bridge->reportedOutputLatency(), e.hiddenInputSamples, e.hiddenOutputSamples);
        stored = true;
        showSoon(Page::LoopbackResult);
    }

    // ---------------------------------------------------------------- tap along
    void buildTapAlong(Form& f)
    {
        f.heading("Tap along to the DAW");
        f.note("1.  Start the DAW transport with the metronome on.\n"
               "2.  Play along on the kit with a steady beat for about 8 bars.\n"
               "3.  Watch the count rise, then press Accept.");
        if (!tapStarted) { ctx.core.startTapAlong(); tapStarted = true; }
        liveA = &f.info("Hits matched", "0 of 24");
        liveB = &f.info("Your hits land", "-");
        liveC = &f.info("Verdict", "waiting for hits", colours::dim);
        auto* acc = f.makeButton("Accept", [this] { ctx.core.acceptTapAlong(); tapStarted = false; showSoon(Page::TapAlong); });
        auto* re = f.makeButton("Restart", [this] { ctx.core.startTapAlong(); });
        f.row("", { { acc, 120 }, { re, 120 } }, 36);
        f.note("The median includes your own anticipation: most drummers play 10 to 30 ms ahead of a click. That is fine; Pacemaker then follows "
               "your feel. If you can route the DAW metronome into Pacemaker's input bus, the measured error is the pure DAW input latency "
               "without the human part, which is the preferred setup.");
        const auto s = ctx.core.session();
        f.info("Current input latency", juce::String(s.inputLatencyMs, 1) + " ms" + (s.tapAlongValid ? "" : "  (not set)"));
        f.text("Enter manually (ms)", juce::String(s.inputLatencyMs, 1), [this](const juce::String& t) {
            const double v = juce::jlimit(-100.0, 200.0, t.getDoubleValue());
            ctx.core.editSession([v](SessionSettings& x) { x.inputLatencyMs = v; x.tapAlongValid = true; });
        }, 100, "0123456789.-", 7);
    }

    void pollTap()
    {
        if (page != Page::TapAlong) return;
        const int n = ctx.core.tapAlongCount();
        const double med = ctx.core.tapAlongMedianMs();
        if (liveA) liveA->setText(juce::String(n) + " of 24" + (n >= 24 ? "  (enough)" : ""), juce::dontSendNotification);
        if (liveB) liveB->setText(n > 0 ? juce::String(std::abs(med), 1) + " ms " + (med >= 0 ? "after" : "before") + " the DAW beat" : juce::String("-"), juce::dontSendNotification);
        if (liveC) {
            liveC->setText(n < 8 ? "keep playing, 8 or more hits needed" : n < 24 ? "usable, more hits make it steadier" : "stable", juce::dontSendNotification);
            liveC->setColour(juce::Label::textColourId, n < 8 ? colours::amber : colours::green);
        }
    }

    // ---------------------------------------------------------------- hardware clock
    void buildHardware(Form& f)
    {
        f.heading("Hardware clock offset");
        f.note("1.  Make sure MIDI clock output is on and your hardware metronome or sequencer follows it.\n"
               "2.  Plug its click into a spare Pacemaker input and give that input a role below.\n"
               "3.  Play a steady beat. Pacemaker compares the hardware click with your hits.");
        const InputRole roles[] = { InputRole::Trigger, InputRole::Kick, InputRole::Snare, InputRole::HiHat, InputRole::Overhead, InputRole::Any };
        f.combo("Click is on role", { "Trigger", "Kick", "Snare", "Hi-hat", "Overhead", "Any" }, hwRoleChoice, [this](int i) { hwRoleChoice = i; hwStarted = false; }, 200);
        auto* start = f.makeButton("Start measuring", [this, roles] {
            ctx.core.startHardwareCalibration(roles[juce::jlimit(0, 5, hwRoleChoice)]); hwStarted = true; hwValid = false;
        });
        f.row("", { { start, 180 } }, 36);
        liveA = &f.info("Measured", hwStarted ? "collecting hits" : "press Start measuring", colours::dim);
        liveB = &f.info("Meaning", "-");
        auto* set = f.makeButton("Set offset", [this] {
            if (!hwValid) return;
            ctx.core.applyHardwareOffset(hwMeasured); hwStarted = false; showSoon(Page::Hardware);
        });
        set->setEnabled(false);
        hwSet = set;
        f.row("", { { set, 180 } }, 36);
        f.info("Current MIDI clock offset", juce::String(ctx.core.session().midi.offsetMs, 1) + " ms");
        f.note("Set offset writes the negative of the measured value into the MIDI clock offset (Settings, Outputs).");
        if (!ctx.core.session().midi.enabled) f.note("MIDI clock output is currently off.", colours::amber);
    }

    void pollHardware()
    {
        if (page != Page::Hardware || !hwStarted) return;
        double m = 0;
        hwValid = ctx.core.hardwareOffset(m);
        if (hwValid) hwMeasured = m;
        if (liveA) liveA->setText(hwValid ? juce::String(hwMeasured, 1) + " ms" : juce::String("collecting hits (needs a steady beat)"), juce::dontSendNotification);
        if (liveB) liveB->setText(hwValid ? (hwMeasured >= 0 ? "hardware click is late; offset will be " : "hardware click is early; offset will be ") + juce::String(-hwMeasured, 1) + " ms" : juce::String("-"), juce::dontSendNotification);
        if (hwSet) hwSet->setEnabled(hwValid);
    }

    // ---------------------------------------------------------------- mic distances
    void buildMics(Form& f)
    {
        f.heading("Microphone distances");
        f.note("Sound travels 2.9 ms per metre. Pick the distance of each microphone from its drum: Close 0.1 m (0.3 ms), "
               "Overhead 1 m (2.9 ms), Room 3 m (8.7 ms), or enter your own. Trigger and MIDI inputs have no acoustic delay.");
        const auto s = ctx.core.session();
        const auto names = ctx.inputChannelNames ? ctx.inputChannelNames() : juce::StringArray();
        if (s.channels.empty()) f.note("No input channels are connected yet.", colours::amber);
        for (int i = 0; i < (int) s.channels.size(); ++i) {
            const auto& c = s.channels[(size_t) i];
            const juce::String nm = (c.channel >= 0 && c.channel < names.size() ? names[c.channel] : "Channel " + juce::String(c.channel + 1)) + "  " + roleLabel(c.role);
            if (c.role == InputRole::Trigger) { f.info(nm, "Trigger / MIDI: 0 m, no acoustic delay", colours::dim); continue; }
            const double v[] = { 0.1, 1.0, 3.0 };
            const char* names2[] = { "Close", "Overhead", "Room", "Custom" };
            int sel = 3; for (int k = 0; k < 3; ++k) if (std::abs(c.distanceM - v[k]) < 0.005) sel = k;
            auto* ed = f.own<juce::TextEditor>(); ed->setFont(sans(15.0f)); ed->setIndents(6, 0); ed->setInputRestrictions(6, "0123456789.");
            ed->setText(juce::String(c.distanceM, 2), false);
            std::vector<std::pair<juce::Component*, int>> ctl;
            juce::Array<juce::TextButton*> chips;
            for (int k = 0; k < 4; ++k) {
                auto* b = f.own<juce::TextButton>(names2[k]);
                b->setClickingTogglesState(true); b->setRadioGroupId(300 + i); b->setToggleState(k == sel, juce::dontSendNotification);
                chips.add(b); ctl.push_back({ b, 92 });
            }
            ctl.push_back({ ed, 70 });
            for (int k = 0; k < 4; ++k)
                chips[k]->onClick = [this, i, k, ed, v] {
                    if (k < 3) { ed->setText(juce::String(v[k], 2), false); setDistance(i, v[k]); } else ed->grabKeyboardFocus();
                };
            auto commit = [this, i, ed, chips, v] {
                const double m = juce::jlimit(0.0, 50.0, ed->getText().getDoubleValue());
                ed->setText(juce::String(m, 2), false); setDistance(i, m);
                int s2 = 3; for (int k = 0; k < 3; ++k) if (std::abs(m - v[k]) < 0.005) s2 = k;
                for (int k = 0; k < 4; ++k) chips[k]->setToggleState(k == s2, juce::dontSendNotification);
            };
            ed->onReturnKey = commit; ed->onFocusLost = commit;
            f.row(nm, ctl);
        }
        f.slider("Module latency (Trigger)", 0.0, 20.0, 0.1, s.moduleLatencyMs, " ms", [this](double v) { ctx.core.editSession([v](SessionSettings& x) { x.moduleLatencyMs = v; }); });
        f.note("E-drum modules add their own trigger-to-MIDI delay, typically 2 to 5 ms.");
    }

    void setDistance(int idx, double m)
    {
        ctx.core.editSession([&](SessionSettings& s) { if (idx < (int) s.channels.size()) s.channels[(size_t) idx].distanceM = m; s.autoRoleMap = false; });
        if (ctx.standalone) {
            std::vector<InputPref*> en;
            for (auto& p : ctx.user.inputPrefs) if (p.enabled) en.push_back(&p);
            std::sort(en.begin(), en.end(), [](InputPref* a, InputPref* b) { return a->deviceChannel < b->deviceChannel; });
            if (idx < (int) en.size()) { en[(size_t) idx]->distanceM = m; ctx.saveUser(); }
        }
    }

    void timerCallback() override
    {
        pollLoopback(); pollTap(); pollHardware();
    }

    void layout()
    {
        auto r = owner.getLocalBounds().reduced(8, 4);
        strip.setBounds(r.removeFromTop(34));
        r.removeFromTop(4);
        auto bar = r.removeFromBottom(40);
        doneBtn.setBounds(bar.removeFromRight(110).reduced(2, 3));
        backBtn.setBounds(bar.removeFromLeft(110).reduced(2, 3));
        pageView.setBounds(r);
    }
};

CalibrationWizard::CalibrationWizard(UiContext& c) : PanelBase(c), impl_(std::make_unique<Impl>(*this, c)) {}
CalibrationWizard::~CalibrationWizard() = default;
void CalibrationWizard::resized() { impl_->layout(); }

} // namespace pacemaker::ui
