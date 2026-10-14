// OS MIDI output for the clock (ES-02 section 3, ES-04 section 8): port selection, virtual port, 5 s retry.
#pragma once
#include "pacemaker/MidiClock.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <mutex>

namespace pacemaker {

class MidiPortManager : public MidiSink {
public:
    MidiPortManager() = default;
    ~MidiPortManager() override;

    // Name of the wanted output. Empty = none. With virtualPort the port is created (macOS/Linux) under that name
    // (default "Pacemaker Clock"). If the port is missing the name is kept and refresh() retries.
    void setPort(const juce::String& name, bool virtualPort);
    juce::String portName() const { std::lock_guard<std::mutex> l(m_); return wanted_; }
    // Call about every 5 s from any non-audio thread; cheap when the port is already open.
    void refresh();
    static juce::StringArray availablePorts();
    static bool virtualPortsSupported();
    juce::String statusText() const;

    // MidiSink
    bool isOpen() const override { return open_.load(); }
    std::string name() const override { return statusText().toStdString(); }
    bool send(const uint8_t* data, int len) override;

private:
    void tryOpen();
    mutable std::mutex m_;
    juce::String wanted_;
    bool virtual_ = false;
    std::unique_ptr<juce::MidiOutput> out_;
    std::atomic<bool> open_ { false };
    juce::String lastError_;
};

} // namespace pacemaker
