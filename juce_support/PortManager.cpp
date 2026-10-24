#include "PortManager.h"

namespace pacemaker {

MidiPortManager::~MidiPortManager()
{
    std::lock_guard<std::mutex> l(m_);
    out_.reset();
}

bool MidiPortManager::virtualPortsSupported()
{
#if JUCE_MAC || JUCE_LINUX || JUCE_BSD
    return true;
#else
    return false;      // Windows: loopMIDI or Windows MIDI Services
#endif
}

juce::StringArray MidiPortManager::availablePorts()
{
    juce::StringArray names;
    for (const auto& d : juce::MidiOutput::getAvailableDevices()) names.add(d.name);
    return names;
}

void MidiPortManager::setPort(const juce::String& name, bool virtualPort)
{
    {
        std::lock_guard<std::mutex> l(m_);
        if (name == wanted_ && virtualPort == virtual_ && out_) return;
        wanted_ = name; virtual_ = virtualPort;
        out_.reset(); open_ = false; lastError_.clear();
    }
    tryOpen();
}

void MidiPortManager::refresh()
{
    {
        std::lock_guard<std::mutex> l(m_);
        if (wanted_.isEmpty() || out_) return;
    }
    tryOpen();
}

void MidiPortManager::tryOpen()
{
    std::lock_guard<std::mutex> l(m_);
    if (wanted_.isEmpty()) return;
    std::unique_ptr<juce::MidiOutput> o;
    if (virtual_ && virtualPortsSupported()) {
        o = juce::MidiOutput::createNewDevice(wanted_.isEmpty() ? juce::String("Pacemaker Clock") : wanted_);
    } else {
        for (const auto& d : juce::MidiOutput::getAvailableDevices())
            if (d.name == wanted_) { o = juce::MidiOutput::openDevice(d.identifier); break; }
    }
    if (o) { out_ = std::move(o); open_ = true; lastError_.clear(); }
    else lastError_ = "port not found";
}

juce::String MidiPortManager::statusText() const
{
    std::lock_guard<std::mutex> l(m_);
    if (wanted_.isEmpty()) return "no port selected";
    return open_.load() ? wanted_ : wanted_ + " (" + (lastError_.isEmpty() ? juce::String("closed") : lastError_) + ")";
}

bool MidiPortManager::send(const uint8_t* data, int len)
{
    std::lock_guard<std::mutex> l(m_);
    if (!out_) return false;
    out_->sendMessageNow(juce::MidiMessage(data, len));
    return true;
}

} // namespace pacemaker
