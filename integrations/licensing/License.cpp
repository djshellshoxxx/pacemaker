#include "License.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

extern "C" {
#include "third_party/ed25519/ed25519.h"
#include "third_party/ed25519/sha512.h"
}

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <direct.h>
#elif defined(__APPLE__)
  #include <CoreFoundation/CoreFoundation.h>
  #include <IOKit/IOKitLib.h>
  #include <sys/stat.h>
  #include <unistd.h>
#else
  #include <sys/stat.h>
  #include <unistd.h>
#endif

namespace pacemaker { namespace licensing {

// ------------------------------------------------------------------------------------ helpers
static std::string toHex(const uint8_t* d, size_t n) {
    static const char* h = "0123456789abcdef";
    std::string s; s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) { s += h[d[i] >> 4]; s += h[d[i] & 15]; }
    return s;
}
static std::string sha512Raw(const std::string& in) {
    unsigned char out[64];
    sha512(reinterpret_cast<const unsigned char*>(in.data()), in.size(), out);
    return std::string(reinterpret_cast<char*>(out), 64);
}
static std::string hmacSha512(const std::string& key, const std::string& msg) {
    const size_t B = 128;
    std::string k = key.size() > B ? sha512Raw(key) : key;
    k.resize(B, '\0');
    std::string ipad(B, 0), opad(B, 0);
    for (size_t i = 0; i < B; ++i) { ipad[i] = char(k[i] ^ 0x36); opad[i] = char(k[i] ^ 0x5c); }
    return sha512Raw(opad + sha512Raw(ipad + msg));
}
static bool constTimeEq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char r = 0;
    for (size_t i = 0; i < a.size(); ++i) r |= (unsigned char) (a[i] ^ b[i]);
    return r == 0;
}
static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char) s[a])) ++a;
    while (b > a && std::isspace((unsigned char) s[b - 1])) --b;
    return s.substr(a, b - a);
}

// ------------------------------------------------------------------------------------ base64url
static const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
std::string base64urlEncode(const uint8_t* d, size_t n) {
    std::string o;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = d[i] << 16;
        if (i + 1 < n) v |= d[i + 1] << 8;
        if (i + 2 < n) v |= d[i + 2];
        o += kB64[(v >> 18) & 63]; o += kB64[(v >> 12) & 63];
        if (i + 1 < n) o += kB64[(v >> 6) & 63];
        if (i + 2 < n) o += kB64[v & 63];
    }
    return o;
}
bool base64urlDecode(const std::string& s, std::vector<uint8_t>& out) {
    out.clear();
    if (s.size() % 4 == 1) return false;
    uint32_t acc = 0; int bits = 0;
    for (char c : s) {
        const char* p = std::strchr(kB64, c);
        if (!p || c == '\0') return false;
        acc = (acc << 6) | uint32_t(p - kB64); bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back(uint8_t((acc >> bits) & 0xFF)); }
    }
    return true;
}

// ------------------------------------------------------------------------------------ features
Features featuresFor(Tier t) {
    Features f;
    if (t == Tier::Lite) { f.linkOutput = true; f.maxInputRoles = 1; }
    else if (t == Tier::Full) {
        f.linkOutput = f.midiClock = f.osc = f.syncBus = f.hostTransport = f.songMap = f.driftReport = true;
        f.maxInputRoles = 99;
    }
    return f;
}

// ------------------------------------------------------------------------------------ key file
static const char* kMagic = "PACEMAKER-LICENSE-1";

bool splitLicense(const std::string& text, std::string& msg, std::vector<uint8_t>& sig, std::string& error) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) {
        if (c == '\r') continue;
        if (c == '\n') { lines.push_back(cur); cur.clear(); } else cur += c;
    }
    if (!cur.empty()) lines.push_back(cur);
    while (!lines.empty() && trim(lines.back()).empty()) lines.pop_back();
    if (lines.empty() || trim(lines[0]) != kMagic) { error = "not a Pacemaker licence file"; return false; }
    if (lines.size() < 3) { error = "licence file too short"; return false; }
    const std::string& last = lines.back();
    if (last.compare(0, 10, "signature=") != 0) { error = "signature line missing"; return false; }
    if (!base64urlDecode(trim(last.substr(10)), sig) || sig.size() != 64) { error = "bad signature encoding"; return false; }
    msg.clear();
    for (size_t i = 0; i + 1 < lines.size(); ++i) msg += trim(lines[i]) + "\n";
    return true;
}

LicenseInfo verifyLicense(const std::string& text, const uint8_t pub[32], int appMajor, const std::string& mHash) {
    LicenseInfo li;
    std::string msg, err; std::vector<uint8_t> sig;
    if (!splitLicense(text, msg, sig, err)) { li.error = err; return li; }
    if (!ed25519_verify(sig.data(), reinterpret_cast<const unsigned char*>(msg.data()), msg.size(), pub)) {
        li.error = "signature does not match (file edited or wrong key)"; return li;
    }
    std::map<std::string, std::string> kv;
    std::istringstream is(msg); std::string line; std::getline(is, line);   // magic
    while (std::getline(is, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0) { li.error = "malformed line"; return li; }
        std::string k = line.substr(0, eq);
        if (kv.count(k)) { li.error = "duplicate key " + k; return li; }
        kv[k] = line.substr(eq + 1);
    }
    auto get = [&](const char* k) { auto it = kv.find(k); return it == kv.end() ? std::string() : it->second; };
    if (get("product") != "pacemaker") { li.error = "licence is for another product"; return li; }
    const std::string tier = get("tier");
    if (tier == "lite") li.tier = Tier::Lite; else if (tier == "full") li.tier = Tier::Full;
    else { li.error = "unknown tier"; return li; }
    li.licensee = get("licensee"); li.email = get("email"); li.order = get("order"); li.issued = get("issued");
    li.machines = std::atoi(get("machines").c_str());
    li.major = std::atoi(get("major").c_str());
    if (li.machines < 1 || li.machines > 3) { li.error = "machines must be 1 to 3"; li.tier = Tier::None; return li; }
    if (li.major < 1) { li.error = "bad major version"; li.tier = Tier::None; return li; }
    if (!get("bind").empty()) {
        std::istringstream bs(get("bind")); std::string h;
        while (std::getline(bs, h, ',')) li.bound.push_back(normalizeMachineHash(h));
        if ((int) li.bound.size() > li.machines) { li.error = "more bound machines than the licence allows"; li.tier = Tier::None; return li; }
    }
    if (appMajor > li.major) { li.error = "licence covers version " + std::to_string(li.major) + ", upgrade needed"; li.tier = Tier::None; return li; }
    if (!li.bound.empty() && std::find(li.bound.begin(), li.bound.end(), normalizeMachineHash(mHash)) == li.bound.end()) {
        li.error = "licence is bound to other machines"; li.tier = Tier::None; return li;
    }
    li.valid = true;
    return li;
}

// ------------------------------------------------------------------------------------ machine identity
static std::string readFirstLine(const char* path) {
    std::ifstream f(path);
    std::string s; std::getline(f, s);
    return trim(s);
}

std::string machineId() {
#if defined(_WIN32)
    char buf[128]; DWORD sz = sizeof(buf);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", "MachineGuid",
                     RRF_RT_REG_SZ | 0x00010000 /* RRF_SUBKEY_WOW6464KEY */, nullptr, buf, &sz) == ERROR_SUCCESS)
        return std::string(buf);
    return std::string();
#elif defined(__APPLE__)
    std::string result;
    io_registry_entry_t e = IORegistryEntryFromPath(kIOMasterPortDefault, "IOService:/");
    if (e) {
        CFTypeRef p = IORegistryEntryCreateCFProperty(e, CFSTR("IOPlatformUUID"), kCFAllocatorDefault, 0);
        if (p) {
            char buf[128];
            if (CFGetTypeID(p) == CFStringGetTypeID() &&
                CFStringGetCString((CFStringRef) p, buf, sizeof buf, kCFStringEncodingUTF8)) result = buf;
            CFRelease(p);
        }
        IOObjectRelease(e);
    }
    return result;
#else
    std::string id = readFirstLine("/etc/machine-id");
    if (id.empty()) id = readFirstLine("/var/lib/dbus/machine-id");
    return id;
#endif
}

std::string machineHash(const std::string& rawId) {
    if (rawId.empty()) return std::string();
    const std::string d = sha512Raw(std::string("pacemaker-machine-v1\0", 21) + rawId);
    return toHex(reinterpret_cast<const uint8_t*>(d.data()), 16);
}
std::string formatMachineHash(const std::string& h) {
    if (h.size() != 32) return h;
    return h.substr(0, 8) + "-" + h.substr(8, 8) + "-" + h.substr(16, 8) + "-" + h.substr(24, 8);
}
std::string normalizeMachineHash(const std::string& s) {
    std::string o;
    for (char c : s) if (std::isxdigit((unsigned char) c)) o += char(std::tolower((unsigned char) c));
    return o;
}

// ------------------------------------------------------------------------------------ trial
class FileTrialStore : public TrialStore {
public:
    explicit FileTrialStore(std::vector<std::string> p) : paths_(std::move(p)) {}
    int slots() const override { return (int) paths_.size(); }
    bool read(int slot, std::string& out) override {
        std::ifstream f(paths_.at(slot), std::ios::binary);
        if (!f) return false;
        std::ostringstream ss; ss << f.rdbuf(); out = ss.str(); return true;
    }
    bool write(int slot, const std::string& data) override {
        const std::string& p = paths_.at(slot);
        const size_t sep = p.find_last_of("/\\");
        if (sep != std::string::npos) makeDirs(p.substr(0, sep));
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << data; return (bool) f;
    }
private:
    static void makeDirs(const std::string& dir) {
        for (size_t i = 1; i <= dir.size(); ++i)
            if (i == dir.size() || dir[i] == '/' || dir[i] == '\\') {
                const std::string part = dir.substr(0, i);
#if defined(_WIN32)
                _mkdir(part.c_str());
#else
                mkdir(part.c_str(), 0755);
#endif
            }
    }
    std::vector<std::string> paths_;
};
std::unique_ptr<TrialStore> makeFileTrialStore(const std::vector<std::string>& paths) {
    return std::unique_ptr<TrialStore>(new FileTrialStore(paths));
}

std::vector<std::string> defaultTrialStorePaths() {
#if defined(_WIN32)
    const char* pd = std::getenv("PROGRAMDATA"); const char* ad = std::getenv("APPDATA");
    return { std::string(pd ? pd : "C:\\ProgramData") + "\\Pacemaker\\trial.dat",
             std::string(ad ? ad : ".") + "\\Pacemaker\\trial.dat" };
#elif defined(__APPLE__)
    const char* h = std::getenv("HOME");
    return { "/Users/Shared/Pacemaker/trial.dat",
             std::string(h ? h : ".") + "/Library/Application Support/Pacemaker/trial.dat" };
#else
    const char* h = std::getenv("HOME"); const char* x = std::getenv("XDG_DATA_HOME");
    std::string base = x && *x ? std::string(x) : std::string(h ? h : ".") + "/.local/share";
    return { base + "/pacemaker/trial.dat", std::string(h ? h : ".") + "/.pacemaker-trial" };
#endif
}

static std::string trialKey(const std::string& mHash) { return sha512Raw(std::string("pacemaker-trial-v1\0", 19) + mHash); }

static std::string makeRecord(int64_t first, int64_t last, const std::string& mHash) {
    const std::string body = "first=" + std::to_string(first) + ";last=" + std::to_string(last);
    const std::string mac = hmacSha512(trialKey(mHash), body);
    return "PMT1 " + body + ";mac=" + toHex(reinterpret_cast<const uint8_t*>(mac.data()), 32) + "\n";
}
// 1 = ok, 0 = unparsable/bad mac
static int parseRecord(const std::string& rec, const std::string& mHash, int64_t& first, int64_t& last) {
    if (rec.compare(0, 5, "PMT1 ") != 0) return 0;
    const size_t m = rec.find(";mac=");
    if (m == std::string::npos) return 0;
    const std::string body = rec.substr(5, m - 5);
    const std::string mac = trim(rec.substr(m + 5));
    const std::string want = hmacSha512(trialKey(mHash), body);
    if (!constTimeEq(mac, toHex(reinterpret_cast<const uint8_t*>(want.data()), 32))) return 0;
    long long f = 0, l = 0;
    if (std::sscanf(body.c_str(), "first=%lld;last=%lld", &f, &l) != 2) return 0;
    first = f; last = l; return 1;
}

TrialStatus evaluateTrial(TrialStore& store, int64_t now, const std::string& mHash) {
    TrialStatus ts;
    bool any = false, bad = false;
    int64_t first = INT64_MAX, last = 0;
    for (int s = 0; s < store.slots(); ++s) {
        std::string rec;
        if (!store.read(s, rec)) continue;
        int64_t f, l;
        if (!parseRecord(rec, mHash, f, l)) { bad = true; continue; }
        any = true; first = std::min(first, f); last = std::max(last, l);
    }
    if (bad) { ts.state = TrialState::Tampered; ts.daysLeft = 0; if (any) ts.firstRunUnix = first; return ts; }
    if (!any) {                                   // first run (or both records deleted: documented limit)
        first = last = now;
        const std::string rec = makeRecord(first, last, mHash);
        for (int s = 0; s < store.slots(); ++s) store.write(s, rec);
        ts.firstRunUnix = first; ts.daysLeft = kTrialDays; return ts;
    }
    ts.firstRunUnix = first;
    if (now + kRollbackToleranceSeconds < last || now + kRollbackToleranceSeconds < first) {
        ts.state = TrialState::ClockRollback; ts.daysLeft = 0; return ts;     // do not update records
    }
    if (now - last >= 60) {                       // persist progress at most once a minute
        const std::string rec = makeRecord(first, std::max(last, now), mHash);
        for (int s = 0; s < store.slots(); ++s) store.write(s, rec);
    }
    const int64_t elapsed = now - first;
    const int64_t total = int64_t(kTrialDays) * 86400;
    if (elapsed >= total) { ts.state = TrialState::Expired; ts.daysLeft = 0; return ts; }
    ts.daysLeft = (int) ((total - elapsed + 86399) / 86400);
    return ts;
}

// ------------------------------------------------------------------------------------ decision
bool trialOutputGateOpen(double runtimeSeconds) {
    if (runtimeSeconds < 0) runtimeSeconds = 0;
    const double pos = runtimeSeconds - kTrialCycleSeconds * (long long) (runtimeSeconds / kTrialCycleSeconds);
    return pos < (kTrialCycleSeconds - kTrialSilentSeconds);
}

Entitlements decide(const LicenseInfo* lic, const TrialStatus& trial, double runtimeSeconds) {
    Entitlements e;
    e.detector = true;
    if (lic && lic->valid) {
        e.mode = Mode::Licensed; e.tier = lic->tier;
        e.features = featuresFor(lic->tier); e.outputsNow = true; return e;
    }
    if (trial.state == TrialState::Active) {
        e.mode = Mode::Trial; e.tier = Tier::Full; e.features = featuresFor(Tier::Full);
        e.outputsNow = trialOutputGateOpen(runtimeSeconds); e.trialDaysLeft = trial.daysLeft; return e;
    }
    e.mode = Mode::TrialExpired; e.tier = Tier::None; e.features = Features(); e.outputsNow = false;
    return e;
}

}}   // namespace
