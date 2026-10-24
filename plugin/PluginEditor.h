#pragma once
#include "PluginProcessor.h"
#include "MainComponent.h"

namespace pacemaker {

class PacemakerEditor : public juce::AudioProcessorEditor {
public:
    explicit PacemakerEditor(PacemakerProcessor& p);
    ~PacemakerEditor() override;
    void resized() override;
private:
    PacemakerProcessor& proc_;
    ui::UiContext ctx_;
    ui::MainComponent main_;
    juce::ComponentBoundsConstrainer constrainer_;
};

} // namespace pacemaker
