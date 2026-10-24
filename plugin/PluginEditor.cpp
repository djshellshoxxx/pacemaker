#include "PluginEditor.h"

namespace pacemaker {

namespace {
ui::UiContext makeCtx(PacemakerProcessor& p)
{
    ui::UiContext c { p.core(), p.ports(), p.store(), p.userSettings() };
    c.standalone = false;
    c.inputChannelNames = [&p] { return p.inputChannelNames(); };
    c.diagnostics = [&p] { return juce::String(p.core().statusSummary()); };
    return c;
}
}

PacemakerEditor::PacemakerEditor(PacemakerProcessor& p)
    : juce::AudioProcessorEditor(p), proc_(p), ctx_(makeCtx(p)), main_(ctx_)
{
    addAndMakeVisible(main_);
    setResizable(true, true);
    constrainer_.setMinimumSize(760, 520);
    setConstrainer(&constrainer_);
    setSize(ui::MainComponent::preferredWidth(), ui::MainComponent::preferredHeight());
    ctx_.requestResize = [this] { resized(); };
}

PacemakerEditor::~PacemakerEditor() { setConstrainer(nullptr); }

void PacemakerEditor::resized() { main_.setBounds(getLocalBounds()); }

} // namespace pacemaker
