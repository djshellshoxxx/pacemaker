// Pacemaker licence verification, tiers and trial (RS-06 sections 2 and 3, docs/licensing/LICENSE-DESIGN.md).
// Plain C++17, no JUCE. Not yet wired into CMake. Ed25519 comes from third_party/ed25519 (zlib licence).
//
// Rules this module encodes:
//  * the DETECTOR is never gated: Entitlements::detector is always true, in every mode;
//  * only OUTPUTS are gated (Entitlements::outputsNow), by licence tier and by the trial output gate.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pacemaker { namespace licensing {

constexpr int kTrialDays = 14;
constexpr int kTrialCycleSeconds = 300;   // trial output gate: 280 s on, 20 s silent, repeating
constexpr int kTrialSilentSeconds = 20;
constexpr int64_t kRollbackToleranceSeconds = 300;

enum class Tier { None, Lite, Full };

// What a tier allows (RS-06 section 3).
struct Features {
    bool linkOutput = false;
    bool midiClock = false;
    bool osc = false;
    bool syncBus = false;
    bool hostTransport = false;
    bool songMap = false;
    bool driftReport = false;
    int maxInputRoles = 0;          // Lite: 1 role; Full: all
};
Features featuresFor(Tier t);

// ---- key file -------------------------------------------------------------------------------
struct LicenseInfo {
    bool valid = false;
    std::string error;              // non-empty when !valid
    Tier tier = Tier::None;
    std::string licensee, email, order, issued;
    int machines = 0;               // entitlement (1..3 for retail)
    int major = 0;                  // product major version the key covers
    std::vector<std::string> bound; // machine hashes; empty = unbound (honour system up to `machines`)
};

// Verifies `fileText` against the 32 byte Ed25519 public key.
// `appMajor` is the running product's major version; `machineHash` is machineHash(machineId()).
// valid=false with a specific error for: bad format, bad signature, wrong product, wrong major, not bound to
// this machine, inconsistent machine count.
LicenseInfo verifyLicense(const std::string& fileText, const uint8_t publicKey[32],
                          int appMajor, const std::string& machineHash);

// The signed message for a key file body (all lines before `signature=`, LF-terminated). Exposed for tools/tests.
bool splitLicense(const std::string& fileText, std::string& signedMessage, std::vector<uint8_t>& signature,
                  std::string& error);

std::string base64urlEncode(const uint8_t* d, size_t n);
bool base64urlDecode(const std::string& s, std::vector<uint8_t>& out);

// ---- machine identity -----------------------------------------------------------------------
// Raw platform id: Windows HKLM\SOFTWARE\Microsoft\Cryptography\MachineGuid, macOS IOPlatformUUID,
// Linux /etc/machine-id (fallback /var/lib/dbus/machine-id). Empty string when unavailable.
std::string machineId();
// 32 hex chars: first 16 bytes of SHA-512("pacemaker-machine-v1\0" + id), shown to users as 4 groups of 8.
std::string machineHash(const std::string& rawId);
std::string formatMachineHash(const std::string& hash32hex);   // XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX
std::string normalizeMachineHash(const std::string& s);        // strips dashes/spaces, lower-cases

// ---- trial ----------------------------------------------------------------------------------
// Two redundant records are kept (see defaultTrialStorePaths). Each record is HMAC-SHA-512 protected with a key
// derived from the machine hash, so editing or copying it to another machine invalidates it.
class TrialStore {
public:
    virtual ~TrialStore() = default;
    virtual bool read(int slot, std::string& out) = 0;     // false = missing
    virtual bool write(int slot, const std::string& data) = 0;
    virtual int slots() const { return 2; }
};
std::unique_ptr<TrialStore> makeFileTrialStore(const std::vector<std::string>& paths);
std::vector<std::string> defaultTrialStorePaths();          // platform specific, two locations

enum class TrialState { Active, Expired, ClockRollback, Tampered };
struct TrialStatus {
    TrialState state = TrialState::Active;
    int daysLeft = kTrialDays;      // 0 when not Active
    int64_t firstRunUnix = 0;
};
// Call at start-up and then about once a minute. Creates the trial on first use, updates last-seen time.
// Any state other than Active means outputs are off (detector still runs).
TrialStatus evaluateTrial(TrialStore& store, int64_t nowUnix, const std::string& machineHash);

// ---- the combined decision --------------------------------------------------------------------
enum class Mode { Licensed, Trial, TrialExpired, Unlicensed };
struct Entitlements {
    Mode mode = Mode::Unlicensed;
    Tier tier = Tier::None;
    Features features;              // what the user may enable
    bool detector = true;           // ALWAYS true
    bool outputsNow = false;        // false during trial silence, after expiry, or when unlicensed
    int trialDaysLeft = 0;
};
// `runtimeSeconds` = seconds since process start from a monotonic clock (the silence cycle is not wall-clock based).
Entitlements decide(const LicenseInfo* license, const TrialStatus& trial, double runtimeSeconds);
bool trialOutputGateOpen(double runtimeSeconds);

}}   // namespace
