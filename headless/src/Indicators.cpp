#include "Indicators.h"
#include "Log.h"
#include "Oled.h"
#include <algorithm>
#include <chrono>
#include <cstring>

#if defined(PM_HEADLESS_GPIO) && PM_HEADLESS_GPIO
#include <fcntl.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace pmhl {

using namespace pacemaker;

bool gpioCompiledIn()
{
#if defined(PM_HEADLESS_GPIO) && PM_HEADLESS_GPIO
    return true;
#else
    return false;
#endif
}
bool oledCompiledIn()
{
#if defined(PM_HEADLESS_I2C) && PM_HEADLESS_I2C
    return true;
#else
    return false;
#endif
}

namespace {
struct FakeLeds : LedBackend {
    void set(int, bool) override {}
};

#if defined(PM_HEADLESS_GPIO) && PM_HEADLESS_GPIO
// Drives lines through the kernel GPIO character device (uAPI v2): the same interface libgpiod wraps, no library needed.
struct GpioLeds : LedBackend {
    int fd[2] { -1, -1 };
    ~GpioLeds() override { for (int f : fd) if (f >= 0) ::close(f); }
    bool request(int chipFd, int idx, int line)
    {
        if (line < 0) return true;
        gpio_v2_line_request req;
        std::memset(&req, 0, sizeof req);
        req.offsets[0] = (uint32_t) line;
        req.num_lines = 1;
        std::strncpy(req.consumer, "pacemaker", sizeof req.consumer - 1);
        req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
        if (::ioctl(chipFd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) return false;
        fd[idx] = req.fd;
        return true;
    }
    void set(int led, bool on) override
    {
        if (led < 0 || led > 1 || fd[led] < 0) return;
        gpio_v2_line_values v;
        std::memset(&v, 0, sizeof v);
        v.mask = 1; v.bits = on ? 1 : 0;
        (void) ::ioctl(fd[led], GPIO_V2_LINE_SET_VALUES_IOCTL, &v);
    }
};
#endif
} // namespace

std::unique_ptr<LedBackend> openLeds(const HeadlessConfig::Gpio& cfg, std::string& err)
{
    if (cfg.chip == "fake") return std::make_unique<FakeLeds>();
#if defined(PM_HEADLESS_GPIO) && PM_HEADLESS_GPIO
    const int chipFd = ::open(cfg.chip.c_str(), O_RDWR | O_CLOEXEC);
    if (chipFd < 0) { err = "cannot open " + cfg.chip; return nullptr; }
    auto g = std::make_unique<GpioLeds>();
    const bool ok = g->request(chipFd, 0, cfg.beatLine) && g->request(chipFd, 1, cfg.downbeatLine);
    ::close(chipFd);
    if (!ok) { err = "cannot request the GPIO lines (wrong line numbers, or in use?)"; return nullptr; }
    return g;
#else
    err = "built without GPIO support (PACEMAKER_HEADLESS_GPIO=OFF or no <linux/gpio.h>)";
    return nullptr;
#endif
}

void Indicators::start(const HeadlessConfig& cfg)
{
    stop_ = false;
    if (cfg.gpio.enabled) {
        std::string err;
        leds_ = openLeds(cfg.gpio, err);
        if (leds_) { ledThread_ = std::thread([this, ms = cfg.gpio.pulseMs] { ledLoop(ms); }); PM_LOGI("status LEDs on %s (beat line %d, downbeat line %d)", cfg.gpio.chip.c_str(), cfg.gpio.beatLine, cfg.gpio.downbeatLine); }
        else PM_LOGW("status LEDs disabled: %s", err.c_str());
    }
    if (cfg.oled.enabled) {
        if (!oledCompiledIn()) PM_LOGW("OLED disabled: built without I2C support");
        else oledThread_ = std::thread([this, o = cfg.oled] { oledLoop(o); });
    }
}

void Indicators::stop()
{
    stop_ = true;
    if (ledThread_.joinable()) ledThread_.join();
    if (oledThread_.joinable()) oledThread_.join();
    if (leds_) { leds_->set(0, false); leds_->set(1, false); leds_.reset(); }
}

void Indicators::ledLoop(int pulseMs)
{
    int64_t lastId = INT64_MIN, offAt[2] { 0, 0 };
    auto service = [&](int64_t now) {
        for (int i = 0; i < 2; ++i) if (offAt[i] && now >= offAt[i]) { leds_->set(i, false); offAt[i] = 0; }
    };
    while (!stop_.load()) {
        int64_t now = steadyNowUs();
        service(now);
        const Timeline tl = core_.timeline();
        int64_t sleepUs = 5000;
        if (tl.map.valid && periodSamples(tl.snap) > 0 && tl.snap.state != (uint8_t) EngineState::Idle) {
            const GridBeat g = gridBeat(tl, firstBeatAtOrAfter(tl, now));
            const int64_t id = (int64_t) g.barNumber * 64 + g.beatInBar;
            const int64_t until = g.hostUs - now;
            if (id != lastId && until <= 3000) {
                if (until > 0) std::this_thread::sleep_for(std::chrono::microseconds(until));
                now = steadyNowUs();
                lastId = id;
                leds_->set(0, true); offAt[0] = now + pulseMs * 1000LL; ++beatPulses_;
                if (g.beatInBar == 1) { leds_->set(1, true); offAt[1] = now + pulseMs * 1000LL; ++downbeatPulses_; }
                continue;
            }
            sleepUs = std::clamp<int64_t>(until - 3000, 500, 5000);
            if (id == lastId) sleepUs = 2000;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(sleepUs));
    }
}

void Indicators::oledLoop(HeadlessConfig::Oled cfg)
{
    Ssd1306 disp;
    uint8_t fb[kOledBytes], prev[kOledBytes];
    std::memset(prev, 0xFF, sizeof prev);
    int64_t retryAt = 0; bool warned = false;
    while (!stop_.load()) {
        const int64_t now = steadyNowUs();
        if (!disp.isOpen() && now >= retryAt) {
            std::string err;
            if (disp.open(cfg.device, cfg.address, cfg.rotate180, &err)) { PM_LOGI("OLED on %s address 0x%02X", cfg.device.c_str(), cfg.address); warned = false; std::memset(prev, 0xFF, sizeof prev); }
            else { if (!warned) PM_LOGW("OLED not available: %s (retrying every 5 s)", err.c_str()); warned = true; retryAt = now + 5000000; }
        }
        if (disp.isOpen()) {
            const Timeline tl = core_.timeline();
            OledInfo in;
            in.valid = tl.map.valid && periodSamples(tl.snap) > 0 && tl.snap.state != (uint8_t) EngineState::Idle;
            in.bpm = tl.snap.bpm; in.state = stateName(tl.snap.state); in.bar = tl.snap.barNumber; in.beat = tl.snap.beatInBar;
            in.confidence = tl.snap.confidence; in.follow = core_.follow(); in.deviceLost = core_.deviceLost();
            renderOled(in, fb);
            if (std::memcmp(fb, prev, sizeof fb) != 0) {
                if (disp.flush(fb)) std::memcpy(prev, fb, sizeof fb);
                else { PM_LOGW("OLED write failed, reopening"); disp.close(); retryAt = now + 5000000; }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (disp.isOpen()) { std::memset(fb, 0, sizeof fb); disp.flush(fb); }
}

} // namespace pmhl
