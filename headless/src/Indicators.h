// Physical status indicators of the Pacemaker Box: beat / downbeat LEDs on GPIO and an SSD1306 OLED.
// Both are optional and compiled out cleanly when the platform lacks them (PM_HEADLESS_GPIO / PM_HEADLESS_I2C).
#pragma once
#include "Config.h"
#include "pacemaker/Core.h"
#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace pmhl {

class LedBackend {
public:
    virtual ~LedBackend() = default;
    virtual void set(int led, bool on) = 0;        // led 0 = beat, 1 = downbeat
};

// chip "fake" returns a backend that only counts pulses (used for demos and tests). nullptr + err when unavailable.
std::unique_ptr<LedBackend> openLeds(const HeadlessConfig::Gpio& cfg, std::string& err);
bool gpioCompiledIn();
bool oledCompiledIn();

class Indicators {
public:
    explicit Indicators(pacemaker::Core& c) : core_(c) {}
    ~Indicators() { stop(); }
    // Starts whichever of LEDs / OLED are enabled in the config; logs and carries on when one is unavailable.
    void start(const HeadlessConfig& cfg);
    void stop();
    long beatPulses() const { return beatPulses_.load(); }
    long downbeatPulses() const { return downbeatPulses_.load(); }
    bool ledsActive() const { return leds_ != nullptr; }
    bool oledActive() const { return oledThread_.joinable(); }

private:
    void ledLoop(int pulseMs);
    void oledLoop(HeadlessConfig::Oled cfg);
    pacemaker::Core& core_;
    std::unique_ptr<LedBackend> leds_;
    std::thread ledThread_, oledThread_;
    std::atomic<bool> stop_ { false };
    std::atomic<long> beatPulses_ { 0 }, downbeatPulses_ { 0 };
};

} // namespace pmhl
