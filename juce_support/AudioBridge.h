// Connects an AudioDeviceManager (standalone, headless) to Core: gathers channels, MIDI input, Sync bus and
// calibration click output, latency compensation and device-loss handling (ES-04 sections 5 and 8).
#pragma once
#include "pacemaker/Core.h"
#include "pacemaker/SpscQueue.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <functional>

namespace pacemaker {

class AudioBridge : public juce::AudioIODeviceCallback, public juce::MidiInputCallback {
public:
    explicit AudioBridge(Core& c) : core_(c) {}

    struct Routing {
        int syncLeft = -1, syncRight = -1;    // output channel indices for the Sync bus (-1 = off)
        int calibrationOut = -1;              // output channel that carries the loopback clicks
    };
    void setRouting(const Routing& r) { syncL_ = r.syncLeft; syncR_ = r.syncRight; calOut_ = r.calibrationOut; }
    Routing routing() const { return { syncL_.load(), syncR_.load(), calOut_.load() }; }

    // Looks up the stored hidden latency for the device key (user settings, ES-03 section 8).
    using LatencyLookup = std::function<bool(const juce::String& device, double sampleRate, int bufferSize, int& hiddenIn, int& hiddenOut)>;
    void setLatencyLookup(LatencyLookup f) { lookup_ = std::move(f); }

    juce::String deviceName() const { return deviceName_; }
    int bufferSize() const { return bufferSize_.load(); }
    double sampleRate() const { return sampleRate_.load(); }
    int reportedInputLatency() const { return reportedIn_.load(); }
    int reportedOutputLatency() const { return reportedOut_.load(); }
    bool running() const { return running_.load(); }
    int callbackCount() const { return callbacks_.load(); }

    void audioDeviceAboutToStart(juce::AudioIODevice* d) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String&) override { core_.notifyDeviceLost(); }
    void audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut, int numSamples,
                                          const juce::AudioIODeviceCallbackContext& ctx) override;
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;

private:
    struct Raw { double t; uint8_t s, d1, d2; };
    Core& core_;
    SpscQueue<Raw, 1024> midiQ_;
    std::vector<CoreMidiIn> midiScratch_;
    std::vector<const float*> inPtrs_;
    LatencyLookup lookup_;
    juce::String deviceName_;
    std::atomic<int> syncL_ { -1 }, syncR_ { -1 }, calOut_ { -1 }, bufferSize_ { 0 }, reportedIn_ { 0 }, reportedOut_ { 0 }, callbacks_ { 0 };
    std::atomic<double> sampleRate_ { 48000.0 };
    std::atomic<bool> running_ { false };
    std::vector<float> zeros_, scratchL_, scratchR_, scratchCal_;
};

} // namespace pacemaker
