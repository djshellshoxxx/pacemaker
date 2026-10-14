// Session report: drift log figures, tempo curve and exports (RS-01 section 7).
#include "Panels.h"
#include "PanelWidgets.h"

namespace pacemaker::ui {

namespace {

juce::String fmtDuration(double sec)
{
    const int s = (int) std::round(sec);
    return juce::String::formatted("%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
}

juce::File logDirectory(const SessionSettings& s)
{
    const std::string d = s.driftLogDir.empty() ? DriftLogger::defaultDirectory() : s.driftLogDir;
    return juce::File(juce::String(d));
}

} // namespace

struct ReportPanel::Impl : juce::Timer {
    // The tempo curve, drawn with juce::Path.
    struct Curve : juce::Component {
        DriftReport rep;
        void paint(juce::Graphics& g) override
        {
            using namespace colours;
            auto r = getLocalBounds().toFloat();
            g.setColour(panel); g.fillRoundedRectangle(r, 6.0f);
            g.setColour(line); g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);
            if (rep.curve.size() < 2) {
                g.setColour(dim); g.setFont(sans(16.0f));
                g.drawText("No tempo data yet. Play with the drift log running and the curve appears here.", getLocalBounds(), juce::Justification::centred);
                return;
            }
            auto plot = r.reduced(8.0f).withTrimmedLeft(46.0f).withTrimmedBottom(22.0f);
            double lo = 1e9, hi = -1e9, t1 = 0;
            for (auto& p : rep.curve) { lo = std::min(lo, p.second); hi = std::max(hi, p.second); t1 = std::max(t1, p.first); }
            const double t0 = rep.curve.front().first;
            if (hi - lo < 4.0) { const double m = 0.5 * (hi + lo); lo = m - 2.0; hi = m + 2.0; }
            const double pad = (hi - lo) * 0.1; lo -= pad; hi += pad;
            const double span = std::max(1e-3, t1 - t0);
            auto X = [&](double t) { return plot.getX() + (float) ((t - t0) / span) * plot.getWidth(); };
            auto Y = [&](double b) { return plot.getBottom() - (float) ((b - lo) / (hi - lo)) * plot.getHeight(); };
            g.setFont(sans(14.0f));
            for (int i = 0; i <= 4; ++i) {
                const double b = lo + (hi - lo) * i / 4.0;
                g.setColour(grid); g.drawHorizontalLine((int) Y(b), plot.getX(), plot.getRight());
                g.setColour(dim); g.drawText(juce::String(b, 0), (int) r.getX() + 4, (int) Y(b) - 9, 40, 18, juce::Justification::centredRight);
            }
            for (int i = 0; i <= 4; ++i) {
                const double t = t0 + span * i / 4.0;
                g.setColour(dim);
                g.drawText(fmtDuration(t - t0), (int) X(t) - 36, (int) plot.getBottom() + 3, 72, 18, juce::Justification::centred);
            }
            // median reference
            g.setColour(cyan.withAlpha(0.5f));
            const float ym = Y(rep.medianBpm);
            g.drawHorizontalLine((int) ym, plot.getX(), plot.getRight());
            juce::Path path;
            bool first = true;
            for (auto& p : rep.curve) {
                const float x = X(p.first), y = juce::jlimit(plot.getY(), plot.getBottom(), Y(p.second));
                if (first) { path.startNewSubPath(x, y); first = false; } else path.lineTo(x, y);
            }
            g.setColour(green);
            g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved));
        }
    };

    struct Figure : juce::Component {
        juce::String name, value;
        void paint(juce::Graphics& g) override
        {
            g.setColour(colours::panel); g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);
            g.setColour(colours::dim); g.setFont(sans(14.0f));
            g.drawText(name, 10, 4, getWidth() - 20, 18, juce::Justification::centredLeft);
            g.setColour(colours::text); g.setFont(mono(22.0f, true));
            g.drawText(value, 10, 22, getWidth() - 20, 30, juce::Justification::centredLeft);
        }
    };

    UiContext& ctx;
    ReportPanel& owner;
    Curve curve;
    juce::OwnedArray<Figure> figures;
    juce::TextButton openHtml { "Open HTML report" }, exportMidi { "Export tempo map (.mid)" }, openFolder { "Open log folder" }, logBtn { "Start drift log" };
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
    bool logging = false;
    size_t lastRows = (size_t) -1;

    Impl(ReportPanel& o, UiContext& c) : ctx(c), owner(o)
    {
        for (int i = 0; i < 8; ++i) { auto* f = figures.add(new Figure()); owner.addAndMakeVisible(f); }
        owner.addAndMakeVisible(curve);
        for (auto* b : { &openHtml, &exportMidi, &openFolder, &logBtn }) owner.addAndMakeVisible(b);
        status.setFont(sans(14.0f)); status.setColour(juce::Label::textColourId, colours::dim);
        owner.addAndMakeVisible(status);

        // The core does not expose "is the log open": treat a recently written log file as running.
        const juce::File lf(juce::String(ctx.core.driftLogPath()));
        logging = !ctx.core.driftLogPath().empty() && lf.existsAsFile() && (juce::Time::getCurrentTime() - lf.getLastModificationTime()).inSeconds() < 6.0;

        openHtml.onClick = [this] { writeHtml(); };
        exportMidi.onClick = [this] { exportTempoMap(); };
        openFolder.onClick = [this] {
            const auto d = logDirectory(ctx.core.session());
            d.createDirectory();
            if (!d.startAsProcess()) say("Could not open " + d.getFullPathName(), true);
        };
        logBtn.onClick = [this] {
            if (logging) { ctx.core.stopDriftLog(); logging = false; say("Drift log stopped."); }
            else if (ctx.core.startDriftLog()) { logging = true; say("Logging to " + juce::String(ctx.core.driftLogPath())); }
            else say("Could not start the drift log. Check the log folder in Settings, Tools.", true);
            refreshButtons();
        };
        refresh();
        startTimerHz(1);
    }
    ~Impl() override { stopTimer(); }

    void timerCallback() override { refresh(); }

    void say(const juce::String& t, bool bad = false)
    {
        status.setText(t, juce::dontSendNotification);
        status.setColour(juce::Label::textColourId, bad ? colours::amber : colours::dim);
    }
    void refreshButtons()
    {
        logBtn.setButtonText(logging ? "Stop drift log" : "Start drift log");
        logBtn.setToggleState(logging, juce::dontSendNotification);
    }

    void refresh()
    {
        const auto r = ctx.core.driftReport();
        const bool has = r.beats > 0 || r.durationSec > 0;
        auto set = [&](int i, const char* n, const juce::String& v) { figures[i]->name = n; figures[i]->value = has ? v : juce::String("-"); figures[i]->repaint(); };
        set(0, "Duration", fmtDuration(r.durationSec));
        set(1, "Mean tempo", juce::String(r.meanBpm, 1) + " BPM");
        set(2, "Median tempo", juce::String(r.medianBpm, 1) + " BPM");
        set(3, "Range", juce::String(r.minBpm, 1) + " - " + juce::String(r.maxBpm, 1));
        set(4, "Max deviation", juce::String(r.maxDeviationBpm, 2) + " BPM");
        set(5, "Time in Hold", juce::String(r.timeInHoldSec, 1) + " s");
        set(6, "Relocks", juce::String(r.relockCount));
        set(7, "Beats logged", juce::String(r.beats));
        curve.rep = r; curve.repaint();
        openHtml.setEnabled(has); exportMidi.setEnabled(has);
        refreshButtons();
    }

    void writeHtml()
    {
        const auto r = ctx.core.driftReport();
        const auto dir = logDirectory(ctx.core.session());
        if (!dir.createDirectory().wasOk() && !dir.isDirectory()) { say("Cannot create " + dir.getFullPathName(), true); return; }
        const auto f = dir.getChildFile("pacemaker-report-" + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + ".html");
        if (!f.replaceWithText(juce::String(renderDriftReportHtml(r, "Pacemaker session report")))) { say("Could not write " + f.getFullPathName(), true); return; }
        say("Report written to " + f.getFullPathName());
        if (!f.startAsProcess()) say("Report saved to " + f.getFullPathName() + " (could not open a browser).", true);
    }

    void exportTempoMap()
    {
        chooser = std::make_unique<juce::FileChooser>("Export tempo map", logDirectory(ctx.core.session()).getChildFile("tempo-map.mid"), "*.mid");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [sp = juce::Component::SafePointer<ReportPanel>(&owner)](const juce::FileChooser& fc) {
                if (!sp) return;
                auto& im = *sp->impl_;
                auto f = fc.getResult();
                if (f == juce::File()) return;
                if (f.getFileExtension().isEmpty()) f = f.withFileExtension("mid");
                const bool ok = im.ctx.core.exportTempoMap(f.getFullPathName().toStdString());
                im.say(ok ? "Tempo map saved to " + f.getFullPathName() : "Could not write " + f.getFullPathName(), !ok);
            });
    }

    void layout()
    {
        auto r = owner.getLocalBounds().reduced(8, 4);
        auto bar = r.removeFromBottom(40);
        status.setBounds(r.removeFromBottom(24));
        const int ws[] = { 190, 230, 170, 170 };
        juce::Component* bs[] = { &openHtml, &exportMidi, &openFolder, &logBtn };
        for (int i = 0; i < 4; ++i) bs[i]->setBounds(bar.removeFromLeft(ws[i]).reduced(2, 3));
        // figures: 4 columns x 2 rows
        auto top = r.removeFromTop(120);
        const int cw = top.getWidth() / 4;
        for (int i = 0; i < 8; ++i) {
            const int col = i % 4, row = i / 4;
            figures[i]->setBounds(top.getX() + col * cw + 3, top.getY() + row * 60 + 2, cw - 6, 54);
        }
        r.removeFromTop(6);
        curve.setBounds(r.reduced(2, 0));
    }
};

ReportPanel::ReportPanel(UiContext& c) : PanelBase(c), impl_(std::make_unique<Impl>(*this, c)) {}
ReportPanel::~ReportPanel() = default;
void ReportPanel::resized() { impl_->layout(); }

} // namespace pacemaker::ui
