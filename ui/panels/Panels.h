// Overlay panels shown over the main screen. MainComponent draws the frame, title and close button.
#pragma once
#include "UiContext.h"
#include "PmLookAndFeel.h"

namespace pacemaker::ui {

class PanelBase : public juce::Component {
public:
    explicit PanelBase(UiContext& c) : ctx(c) { setOpaque(true); }
    // Opaque panel surface so the main screen behind it never shows through.
    void paint(juce::Graphics& g) override
    {
        g.fillAll(colours::panel);
        g.setColour(colours::line); g.drawRect(getLocalBounds(), 1);
    }
    virtual juce::String title() const = 0;
    std::function<void()> onClose;          // call to dismiss the panel
protected:
    UiContext& ctx;
};

class SettingsPanel : public PanelBase {
public:
    enum Tab { Inputs = 0, Outputs, Triggers, Tools, About };
    SettingsPanel(UiContext& c, int initialTab = Inputs);
    ~SettingsPanel() override;
    juce::String title() const override { return "Settings"; }
    void resized() override;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

class SongMapPanel : public PanelBase {
public:
    explicit SongMapPanel(UiContext& c);
    ~SongMapPanel() override;
    juce::String title() const override { return "Song map"; }
    void resized() override;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

class CalibrationWizard : public PanelBase {
public:
    explicit CalibrationWizard(UiContext& c);
    ~CalibrationWizard() override;
    juce::String title() const override { return "Calibration"; }
    void resized() override;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

class ReportPanel : public PanelBase {
public:
    explicit ReportPanel(UiContext& c);
    ~ReportPanel() override;
    juce::String title() const override { return "Session report"; }
    void resized() override;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

} // namespace pacemaker::ui
