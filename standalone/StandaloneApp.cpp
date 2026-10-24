// Pacemaker standalone: custom shell with its own device panel, MIDI inputs, Link/MIDI/OSC/Sync outputs.
#include "DevicePanel.h"
#include "AutoStart.h"
#include "MainComponent.h"
#include "DrumMachine.h"
#ifdef PACEMAKER_HAVE_WEB
 #include "pacemaker/WebUi.h"
#endif
#include <juce_gui_extra/juce_gui_extra.h>

using namespace pacemaker;

namespace {

// Synthetic drummer used by --demo (screenshots, UI work without hardware): audio is fed to the Core in real time.
class DemoFeed : private juce::Timer {
public:
    DemoFeed(Core& c, double sr) : core(c), sampleRate(sr)
    {
        drums::Machine dm(sr, 7);
        dm.bpm = 118; dm.walkSigma = 0.15; dm.jitterMs = 4.0;
        dm.countIn(4); dm.bars(40, false);
        audio = std::move(dm.audio);
        start = juce::Time::getMillisecondCounterHiRes();
        startTimerHz(200);
    }
    ~DemoFeed() override { stopTimer(); }
private:
    void timerCallback() override
    {
        const double elapsed = (juce::Time::getMillisecondCounterHiRes() - start) * 0.001;
        const int64_t target = (int64_t) (elapsed * sampleRate);
        while (pos + 256 <= target && pos + 256 <= (int64_t) audio.size()) {
            const float* ch[1] = { audio.data() + pos };
            CoreHostInfo h; h.hostTimeUs = -1;
            core.process(ch, 1, 256, h, nullptr, 0, nullptr, nullptr, nullptr, nullptr, 0);
            pos += 256;
        }
        if (pos + 256 > (int64_t) audio.size()) { pos = 0; start = juce::Time::getMillisecondCounterHiRes(); }
    }
    Core& core; double sampleRate; std::vector<float> audio; int64_t pos = 0; double start = 0;
};

class StandaloneApp : public juce::JUCEApplication, private juce::Timer {
public:
    const juce::String getApplicationName() override { return "Pacemaker"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& cmd) override
    {
        const auto args = juce::StringArray::fromTokens(cmd, true);
        demo = args.contains("--demo");
        const int si = args.indexOf("--screenshot");
        if (si >= 0 && si + 1 < args.size()) shotPath = args[si + 1].unquoted();
        const int wi = args.indexOf("--size");
        int w = 0, h = 0;
        if (wi >= 0 && wi + 1 < args.size()) { w = args[wi + 1].upToFirstOccurrenceOf("x", false, false).getIntValue(); h = args[wi + 1].fromFirstOccurrenceOf("x", false, false).getIntValue(); }
        const bool stage = args.contains("--stage");
        const int panelIdx = args.indexOf("--panel");
        const juce::String panelName = panelIdx >= 0 && panelIdx + 1 < args.size() ? args[panelIdx + 1] : juce::String();

        user = store.load();
        if (stage) user.stageMode = true;
        logger.reset(juce::FileLogger::createDateStampedLogger(store.folder().getChildFile("logs").getFullPathName(), "pacemaker-", ".log", "Pacemaker standalone"));
        juce::Logger::setCurrentLogger(logger.get());

        // Restore the session state of the last run.
        const auto stateFile = store.folder().getChildFile("standalone-state.json");
        if (stateFile.existsAsFile() && !demo) {
            PluginState st; std::string warn;
            if (PluginState::deserialise(stateFile.loadFileAsString().toStdString(), st, &warn)) core.setState(st);
        }
        core.setMidiSink(std::shared_ptr<MidiSink>(&ports, [](MidiSink*) {}));
        core.setPluginMidiOutput(false);

        bridge = std::make_unique<AudioBridge>(core);
        bridge->setLatencyLookup([this](const juce::String& dev, double sr, int buf, int& hi, int& ho) {
            CalibrationEntry e;
            if (user.calibration.get(CalibrationTable::key(dev.toStdString(), sr, buf), e)) { hi = e.hiddenInputSamples; ho = e.hiddenOutputSamples; return true; }
            hi = ho = 0; return false;
        });
        ctx.reset(new ui::UiContext { core, ports, store, user, false, {}, nullptr, nullptr, {}, {}, {}, {} });
        ctx->standalone = true;
        ctx->bridge = bridge.get();
        ctx->deviceManager = &deviceManager;
        ctx->inputChannelNames = [this] {
            juce::StringArray names;
            if (auto* d = deviceManager.getCurrentAudioDevice()) {
                const auto all = d->getInputChannelNames();
                for (int i = 0; i < all.size(); ++i) {
                    for (const auto& p : user.inputPrefs) if (p.deviceChannel == i && p.enabled) { names.add(all[i]); break; }
                }
            }
            if (names.isEmpty()) names.add("Input 1");
            return names;
        };
        ctx->diagnostics = [this] {
            juce::String s = "Pacemaker " + getApplicationVersion() + " (standalone)\n" + juce::SystemStats::getOperatingSystemName() + "\n";
            if (auto* d = deviceManager.getCurrentAudioDevice())
                s << d->getTypeName() << " / " << d->getName() << " " << d->getCurrentSampleRate() << " Hz, buffer " << d->getCurrentBufferSizeSamples()
                  << ", latency in/out " << d->getInputLatencyInSamples() << "/" << d->getOutputLatencyInSamples() << "\n";
            s << core.statusSummary() << "\nMIDI: " << ports.statusText() << "\n";
            return s;
        };
        ctx->setAlwaysOnTop = [this](bool on) { if (window) window->setAlwaysOnTop(on); };
        ctx->showDevicePanel = [this] { if (window) window->content().showPanel(std::make_unique<ui::DevicePanel>(*ctx)); };

        if (!demo) {
            juce::String err;
            std::unique_ptr<juce::XmlElement> xml;
            if (user.audioDeviceXml.size()) xml = juce::XmlDocument::parse(juce::String(user.audioDeviceXml));
            err = deviceManager.initialise(64, 2, xml.get(), true);
            if (err.isNotEmpty()) juce::Logger::writeToLog("Audio init: " + err);
            deviceManager.addAudioCallback(bridge.get());
            ui::applyOutputRouting(*ctx);
            ui::applyInputPrefs(*ctx);
            ui::openConfiguredMidiInputs(*ctx);
        } else {
            core.prepare(48000.0, 2048, 1);
            core.editSession([](SessionSettings& s) { s.channels = { { 0, InputRole::Any, 0.0, false } }; s.sync.enabled = true; s.midi.enabled = true; s.osc.enabled = true;
                                                      s.link.enabled = false; s.songs.songs = { { "Opener", 118, 4, 4, BarPattern::Rock44, 4, 0 }, { "Slow burner", 72, 4, 4, BarPattern::HalfTime44, 4, 1 } }; });
            demoFeed = std::make_unique<DemoFeed>(core, 48000.0);
        }

        window = std::make_unique<Window>(*this, *ctx, w, h);
        if (stage) window->content().setStageMode(true);
        if (panelName == "settings") window->content().showSettings(0);
        else if (panelName == "devices" && ctx->showDevicePanel) ctx->showDevicePanel();
        else if (panelName == "songs") window->content().showSongs();
        else if (panelName == "calibration") window->content().showCalibration();
        else if (panelName == "report") window->content().showReport();
        if (user.alwaysOnTop) window->setAlwaysOnTop(true);
        startTimer(1000);
        if (shotPath.isNotEmpty()) juce::Timer::callAfterDelay(demo ? 14000 : 2500, [this] { takeScreenshot(); });
    }

    void shutdown() override
    {
        stopTimer();
        saveState();
        window.reset();
        demoFeed.reset();
#ifdef PACEMAKER_HAVE_WEB
        web.reset();
#endif
        if (!demo) { deviceManager.removeAudioCallback(bridge.get()); }
        if (auto xml = deviceManager.createStateXml()) user.audioDeviceXml = xml->toString().toStdString();
        store.save(user);
        deviceManager.closeAudioDevice();
        juce::Logger::setCurrentLogger(nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    class Window : public juce::DocumentWindow {
    public:
        Window(StandaloneApp& a, ui::UiContext& c, int w, int h)
            : juce::DocumentWindow("Pacemaker", ui::colours::bg, juce::DocumentWindow::allButtons), app(a), main_(c)
        {
            setUsingNativeTitleBar(true);
            setContentNonOwned(&main_, false);
            setResizable(true, true);
            setResizeLimits(760, 520, 4000, 3000);
            const int ww = w > 0 ? w : juce::jmax(760, c.user.windowW), hh = h > 0 ? h : juce::jmax(520, c.user.windowH);
            centreWithSize(ww, hh);
            setVisible(true);
        }
        ~Window() override { clearContentComponent(); }
        void closeButtonPressed() override { app.user.windowW = getWidth(); app.user.windowH = getHeight(); juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
        ui::MainComponent& content() { return main_; }
        StandaloneApp& app;
    private:
        ui::MainComponent main_;
    };

    void timerCallback() override
    {
        ports.refresh();                                   // retry a missing MIDI port every second here, port manager is cheap
        const auto s = core.session();
        ports.setPort(juce::String(s.midiPort), s.midiVirtualPort);
        if (++saveTicks >= 5) { saveTicks = 0; saveState(); }
#ifdef PACEMAKER_HAVE_WEB
        if (user.remotePageEnabled && !web) {
            web = std::make_unique<WebUi>(core);
            if (!web->start(user.remotePagePort)) { juce::Logger::writeToLog("Web page could not start on the configured port"); web.reset(); user.remotePageEnabled = false; }
        } else if (!user.remotePageEnabled && web) { web->stop(); web.reset(); }
#endif
    }

    void saveState()
    {
        if (demo) return;
        store.folder().getChildFile("standalone-state.json").replaceWithText(juce::String(core.state().serialise()));
    }

    void takeScreenshot()
    {
        if (!window) { quit(); return; }
        auto img = window->content().createComponentSnapshot(window->content().getLocalBounds(), true, 1.0f);
        juce::File f(shotPath);
        f.deleteFile();
        juce::FileOutputStream os(f);
        if (os.openedOk()) { juce::PNGImageFormat png; png.writeImageToStream(img, os); }
        quit();
    }

    Core core;
    MidiPortManager ports;
    UserSettingsStore store { "Pacemaker" };
    UserSettings user;
    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<AudioBridge> bridge;
    std::unique_ptr<ui::UiContext> ctx;
    std::unique_ptr<juce::FileLogger> logger;
    std::unique_ptr<DemoFeed> demoFeed;
#ifdef PACEMAKER_HAVE_WEB
    std::unique_ptr<WebUi> web;
#endif
    std::unique_ptr<Window> window;
    bool demo = false;
    juce::String shotPath;
    int saveTicks = 0;
};

} // namespace

START_JUCE_APPLICATION(StandaloneApp)
