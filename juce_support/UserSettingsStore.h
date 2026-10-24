// Global user settings on disk (RS-04 section 1): a JSON file in the per-user application data folder.
#pragma once
#include "pacemaker/Settings.h"
#include <juce_core/juce_core.h>

namespace pacemaker {

class UserSettingsStore {
public:
    // appName selects the folder, e.g. "Pacemaker".
    explicit UserSettingsStore(const juce::String& appName = "Pacemaker");
    UserSettings load() const;
    bool save(const UserSettings& s) const;
    juce::File folder() const { return dir_; }
    juce::File presetsFolder() const { return dir_.getChildFile("Presets"); }
    juce::File logFile() const { return dir_.getChildFile("pacemaker.log"); }
private:
    juce::File dir_;
};

} // namespace pacemaker
