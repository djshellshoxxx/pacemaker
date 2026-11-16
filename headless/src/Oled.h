// SSD1306 128x64 OLED status display over /dev/i2c-N (tempo + state), with a tiny built-in 5x7 font.
// The renderer is pure (framebuffer in, nothing else) so it is testable without hardware.
#pragma once
#include <cstdint>
#include <string>

namespace pmhl {

constexpr int kOledW = 128, kOledH = 64, kOledBytes = kOledW * kOledH / 8;   // page layout: 8 pages of 128 columns

struct OledInfo {
    double bpm = 0;
    bool valid = false;           // tempo is meaningful
    std::string state = "IDLE";   // LOCKED, HOLD, ...
    int bar = 0, beat = 0;
    float confidence = 0;
    bool follow = true;
    bool deviceLost = false;
    bool beatFlash = false;       // inverts a corner block on the beat
};

void renderOled(const OledInfo& info, uint8_t fb[kOledBytes]);
std::string oledAscii(const uint8_t fb[kOledBytes]);        // debugging: 128x64 as text
int oledLitPixels(const uint8_t fb[kOledBytes]);
void oledText(uint8_t fb[kOledBytes], int x, int y, int scale, const std::string& s);   // exposed for tests
int oledTextWidth(int scale, const std::string& s);

class Ssd1306 {
public:
    ~Ssd1306() { close(); }
    bool open(const std::string& dev, int address, bool rotate180, std::string* err = nullptr);
    void close();
    bool isOpen() const { return fd_ >= 0; }
    bool flush(const uint8_t fb[kOledBytes]);
private:
    bool cmd(const uint8_t* c, int n);
    int fd_ = -1;
};

} // namespace pmhl
