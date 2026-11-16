// Standalone device panel (ES-04 section 5): audio device, every input channel with role and distance, the Sync
// output pair, MIDI inputs, window and start-up options.
#pragma once
#include "panels/Panels.h"

namespace pacemaker::ui {

// Re-applies user.inputPrefs to the device channel mask, the session channel list and the bridge routing.
void applyInputPrefs(UiContext& ctx);
void applyOutputRouting(UiContext& ctx);
void openConfiguredMidiInputs(UiContext& ctx);

class DevicePanel : public PanelBase {
public:
    explicit DevicePanel(UiContext& c);
    ~DevicePanel() override;
    juce::String title() const override { return "Devices"; }
    void resized() override;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

} // namespace pacemaker::ui
