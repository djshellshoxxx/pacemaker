#include "Oled.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

#if defined(PM_HEADLESS_I2C) && PM_HEADLESS_I2C
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#define PM_HAVE_I2C 1
#endif

namespace pmhl {

namespace {
struct Glyph { char c; uint8_t col[5]; };
// Classic 5x7 font, column-major, bit 0 = top row.
const Glyph kFont[] = {
    { ' ', { 0x00, 0x00, 0x00, 0x00, 0x00 } }, { '%', { 0x23, 0x13, 0x08, 0x64, 0x62 } }, { '+', { 0x08, 0x08, 0x3E, 0x08, 0x08 } },
    { '-', { 0x08, 0x08, 0x08, 0x08, 0x08 } }, { '.', { 0x00, 0x60, 0x60, 0x00, 0x00 } }, { '/', { 0x20, 0x10, 0x08, 0x04, 0x02 } },
    { '0', { 0x3E, 0x51, 0x49, 0x45, 0x3E } }, { '1', { 0x00, 0x42, 0x7F, 0x40, 0x00 } }, { '2', { 0x42, 0x61, 0x51, 0x49, 0x46 } },
    { '3', { 0x21, 0x41, 0x45, 0x4B, 0x31 } }, { '4', { 0x18, 0x14, 0x12, 0x7F, 0x10 } }, { '5', { 0x27, 0x45, 0x45, 0x45, 0x39 } },
    { '6', { 0x3C, 0x4A, 0x49, 0x49, 0x30 } }, { '7', { 0x01, 0x71, 0x09, 0x05, 0x03 } }, { '8', { 0x36, 0x49, 0x49, 0x49, 0x36 } },
    { '9', { 0x06, 0x49, 0x49, 0x29, 0x1E } }, { ':', { 0x00, 0x36, 0x36, 0x00, 0x00 } },
    { 'A', { 0x7E, 0x11, 0x11, 0x11, 0x7E } }, { 'B', { 0x7F, 0x49, 0x49, 0x49, 0x36 } }, { 'C', { 0x3E, 0x41, 0x41, 0x41, 0x22 } },
    { 'D', { 0x7F, 0x41, 0x41, 0x22, 0x1C } }, { 'E', { 0x7F, 0x49, 0x49, 0x49, 0x41 } }, { 'F', { 0x7F, 0x09, 0x09, 0x09, 0x01 } },
    { 'G', { 0x3E, 0x41, 0x49, 0x49, 0x7A } }, { 'H', { 0x7F, 0x08, 0x08, 0x08, 0x7F } }, { 'I', { 0x00, 0x41, 0x7F, 0x41, 0x00 } },
    { 'J', { 0x20, 0x40, 0x41, 0x3F, 0x01 } }, { 'K', { 0x7F, 0x08, 0x14, 0x22, 0x41 } }, { 'L', { 0x7F, 0x40, 0x40, 0x40, 0x40 } },
    { 'M', { 0x7F, 0x02, 0x0C, 0x02, 0x7F } }, { 'N', { 0x7F, 0x04, 0x08, 0x10, 0x7F } }, { 'O', { 0x3E, 0x41, 0x41, 0x41, 0x3E } },
    { 'P', { 0x7F, 0x09, 0x09, 0x09, 0x06 } }, { 'Q', { 0x3E, 0x41, 0x51, 0x21, 0x5E } }, { 'R', { 0x7F, 0x09, 0x19, 0x29, 0x46 } },
    { 'S', { 0x46, 0x49, 0x49, 0x49, 0x31 } }, { 'T', { 0x01, 0x01, 0x7F, 0x01, 0x01 } }, { 'U', { 0x3F, 0x40, 0x40, 0x40, 0x3F } },
    { 'V', { 0x1F, 0x20, 0x40, 0x20, 0x1F } }, { 'W', { 0x3F, 0x40, 0x38, 0x40, 0x3F } }, { 'X', { 0x63, 0x14, 0x08, 0x14, 0x63 } },
    { 'Y', { 0x07, 0x08, 0x70, 0x08, 0x07 } }, { 'Z', { 0x61, 0x51, 0x49, 0x45, 0x43 } },
};

const Glyph* findGlyph(char c)
{
    if (c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
    for (const auto& g : kFont) if (g.c == c) return &g;
    return &kFont[0];
}

inline void setPx(uint8_t* fb, int x, int y, bool on)
{
    if (x < 0 || y < 0 || x >= kOledW || y >= kOledH) return;
    uint8_t& b = fb[(y >> 3) * kOledW + x];
    if (on) b |= (uint8_t) (1u << (y & 7)); else b &= (uint8_t) ~(1u << (y & 7));
}
inline bool getPx(const uint8_t* fb, int x, int y) { return (fb[(y >> 3) * kOledW + x] >> (y & 7)) & 1; }

void fillRect(uint8_t* fb, int x, int y, int w, int h, bool on)
{
    for (int j = 0; j < h; ++j) for (int i = 0; i < w; ++i) setPx(fb, x + i, y + j, on);
}
void frameRect(uint8_t* fb, int x, int y, int w, int h)
{
    for (int i = 0; i < w; ++i) { setPx(fb, x + i, y, true); setPx(fb, x + i, y + h - 1, true); }
    for (int j = 0; j < h; ++j) { setPx(fb, x, y + j, true); setPx(fb, x + w - 1, y + j, true); }
}
} // namespace

int oledTextWidth(int scale, const std::string& s) { return s.empty() ? 0 : (int) s.size() * 6 * scale - scale; }

void oledText(uint8_t* fb, int x, int y, int scale, const std::string& s)
{
    for (char ch : s) {
        const Glyph* g = findGlyph(ch);
        for (int cx = 0; cx < 5; ++cx)
            for (int cy = 0; cy < 7; ++cy)
                if ((g->col[cx] >> cy) & 1) fillRect(fb, x + cx * scale, y + cy * scale, scale, scale, true);
        x += 6 * scale;
    }
}

void renderOled(const OledInfo& in, uint8_t* fb)
{
    std::memset(fb, 0, kOledBytes);
    char buf[32];
    if (in.valid) std::snprintf(buf, sizeof buf, "%.1f", in.bpm); else std::snprintf(buf, sizeof buf, "---");
    std::string t = buf;
    int scale = oledTextWidth(4, t) <= kOledW ? 4 : 3;
    oledText(fb, 0, 0, scale, t);
    oledText(fb, kOledW - oledTextWidth(1, "BPM"), scale == 4 ? 20 : 14, 1, "BPM");

    const std::string st = in.deviceLost ? "NO AUDIO" : in.state;
    oledText(fb, 0, 33, 2, st);
    if (in.beat > 0 && in.state != "IDLE") {
        std::snprintf(buf, sizeof buf, "%d:%d", in.bar, in.beat);
        oledText(fb, kOledW - oledTextWidth(2, buf), 33, 2, buf);
    }
    // confidence bar
    frameRect(fb, 0, 56, kOledW - 24, 8);
    const int w = std::clamp((int) (in.confidence * (kOledW - 28)), 0, kOledW - 28);
    fillRect(fb, 2, 58, w, 4, true);
    std::snprintf(buf, sizeof buf, "%d%%", std::clamp((int) (in.confidence * 100.0f + 0.5f), 0, 100));
    oledText(fb, kOledW - oledTextWidth(1, buf), 57, 1, buf);
    if (!in.follow) oledText(fb, 0, 47, 1, "FOLLOW OFF");
    if (in.beatFlash) for (int y = 0; y < 8; ++y) for (int x = kOledW - 8; x < kOledW; ++x) setPx(fb, x, y, !getPx(fb, x, y));
}

int oledLitPixels(const uint8_t* fb)
{
    int n = 0;
    for (int i = 0; i < kOledBytes; ++i) for (int b = 0; b < 8; ++b) n += (fb[i] >> b) & 1;
    return n;
}

std::string oledAscii(const uint8_t* fb)
{
    std::string s;
    for (int y = 0; y < kOledH; ++y) { for (int x = 0; x < kOledW; ++x) s += getPx(fb, x, y) ? '#' : '.'; s += '\n'; }
    return s;
}

// ------------------------------------------------------------------ device
#if defined(PM_HAVE_I2C)
bool Ssd1306::open(const std::string& dev, int address, bool rotate180, std::string* err)
{
    close();
    fd_ = ::open(dev.c_str(), O_RDWR);
    if (fd_ < 0) { if (err) *err = "cannot open " + dev; return false; }
    if (::ioctl(fd_, I2C_SLAVE, address) < 0) { if (err) *err = "cannot select I2C address"; close(); return false; }
    const uint8_t init[] = { 0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00,
                             (uint8_t) (rotate180 ? 0xA0 : 0xA1), (uint8_t) (rotate180 ? 0xC0 : 0xC8),
                             0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF };
    if (!cmd(init, (int) sizeof init)) { if (err) *err = "display did not acknowledge"; close(); return false; }
    return true;
}
void Ssd1306::close() { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }
bool Ssd1306::cmd(const uint8_t* c, int n)
{
    for (int i = 0; i < n; ++i) {
        const uint8_t pkt[2] = { 0x00, c[i] };
        if (::write(fd_, pkt, 2) != 2) return false;
    }
    return true;
}
bool Ssd1306::flush(const uint8_t* fb)
{
    if (fd_ < 0) return false;
    const uint8_t setAddr[] = { 0x21, 0, 127, 0x22, 0, 7 };
    if (!cmd(setAddr, (int) sizeof setAddr)) return false;
    uint8_t pkt[1 + 16];
    pkt[0] = 0x40;
    for (int i = 0; i < kOledBytes; i += 16) {
        std::memcpy(pkt + 1, fb + i, 16);
        if (::write(fd_, pkt, sizeof pkt) != (ssize_t) sizeof pkt) return false;
    }
    return true;
}
#else
bool Ssd1306::open(const std::string&, int, bool, std::string* err) { if (err) *err = "I2C is only supported on Linux"; return false; }
void Ssd1306::close() {}
bool Ssd1306::cmd(const uint8_t*, int) { return false; }
bool Ssd1306::flush(const uint8_t*) { return false; }
#endif

} // namespace pmhl
