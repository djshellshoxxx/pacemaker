// The main screen (RS-01 section 4), shared by the plugin editor and the standalone window.
#pragma once
#include "UiContext.h"
#include "PmLookAndFeel.h"
#include "panels/Panels.h"
#include <deque>

namespace pacemaker::ui {

struct OnsetMark { int64_t hostUs; float strength; InputRole role; };

// Everything the child components draw, refreshed once per timer tick.
struct UiModel {
    Timeline tl;
    int64_t nowUs = 0;
    Params params;
    SessionSettings session;
    float levels[kMaxChannels] {};
    std::deque<OnsetMark> onsets;           // last ~8 s
    int64_t downbeatFlashUntilUs = 0;
    int64_t onsetLedUntilUs[8] {};           // per InputRole index
    bool deviceLost = false, standby = false, linkFighting = false;
    int linkFollowers = 0;
    juce::StringArray channelNames;
};

class MainComponent : public juce::Component, private juce::Timer {
public:
    explicit MainComponent(UiContext& ctx);
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    void mouseDown(const juce::MouseEvent&) override;

    void setStageMode(bool on);
    bool stageMode() const { return stage_; }
    void showSettings(int tab = 0);
    void showSongs();
    void showCalibration();
    void showReport();
    void closePanel();
    void showPanel(std::unique_ptr<PanelBase> p);
    bool panelOpen() const { return panel_ != nullptr; }

    // For tests / screenshots.
    const UiModel& model() const { return model_; }
    static int preferredWidth() { return 1040; }
    static int preferredHeight() { return 740; }

private:
    struct Header; struct Trace; struct Inputs; struct Outputs; struct Transport; struct Footer; struct Backdrop;
    void timerCallback() override;
    UiContext& ctx_;
    UiModel model_;
    bool stage_ = false;
    int sessionRefreshTicks_ = 0;
    std::unique_ptr<Header> header_;
    std::unique_ptr<Trace> trace_;
    std::unique_ptr<Inputs> inputs_;
    std::unique_ptr<Outputs> outputs_;
    std::unique_ptr<Transport> transport_;
    std::unique_ptr<Footer> footer_;
    std::unique_ptr<PanelBase> panel_;
    std::unique_ptr<Backdrop> backdrop_;
    juce::TextButton closeBtn_ { juce::CharPointer_UTF8("\xc3\x97") };
    juce::Rectangle<int> panelFrame_;
    PmLookAndFeel laf_;
    juce::TooltipWindow tooltip_ { this, 400 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace pacemaker::ui
