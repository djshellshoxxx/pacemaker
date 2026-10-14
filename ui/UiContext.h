// Everything the shared UI needs from its host (plugin editor or standalone window).
#pragma once
#include "AudioBridge.h"
#include "PortManager.h"
#include "UserSettingsStore.h"
#include "pacemaker/Core.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <functional>

namespace pacemaker::ui {

struct UiContext {
    Core& core;
    MidiPortManager& ports;
    UserSettingsStore& store;
    UserSettings& user;                         // global settings; call saveUser() after edits
    bool standalone = false;
    std::function<juce::StringArray()> inputChannelNames;       // flat input channel labels for the role tiles
    AudioBridge* bridge = nullptr;                              // standalone only
    juce::AudioDeviceManager* deviceManager = nullptr;          // standalone only
    std::function<void()> showDevicePanel;                      // standalone only
    std::function<void(bool)> setAlwaysOnTop;                   // standalone only
    std::function<void()> requestResize;                        // stage mode toggles the layout
    std::function<juce::String()> diagnostics;                  // text for "Copy diagnostics"

    void saveUser() const { store.save(user); }
    juce::String calibrationKey() const
    {
        if (bridge == nullptr) return {};
        return juce::String(CalibrationTable::key(bridge->deviceName().toStdString(), bridge->sampleRate(), bridge->bufferSize()));
    }
    bool hasCalibration() const
    {
        if (standalone) { CalibrationEntry e; return user.calibration.get(calibrationKey().toStdString(), e); }
        return core.session().tapAlongValid;
    }
};

} // namespace pacemaker::ui
