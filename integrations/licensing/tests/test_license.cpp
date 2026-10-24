// Build and run (from integrations/licensing/):  see tests/run_tests.sh
#include "../License.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

extern "C" {
#include "../third_party/ed25519/ed25519.h"
}

using namespace pacemaker::licensing;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static const unsigned char kSeed[32] = { 'P','A','C','E','M','A','K','E','R','-','T','E','S','T','-','K','E','Y','-','N','O','T','-','S','E','C','R','E','T','!','!','!' };
static unsigned char gPub[32], gPriv[64];

static std::string readFile(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }

// Makes a licence signed by the C implementation (independent of the Python tool).
static std::string makeLicense(const std::string& body) {
    unsigned char sig[64];
    ed25519_sign(sig, (const unsigned char*) body.data(), body.size(), gPub, gPriv);
    return body + "signature=" + base64urlEncode(sig, 64) + "\n";
}
static std::string baseBody(const std::string& tier = "full", int machines = 3, int major = 1, const std::string& bind = "") {
    std::string b = "PACEMAKER-LICENSE-1\nproduct=pacemaker\ntier=" + tier + "\nlicensee=A B\nemail=a@b.invalid\norder=PM-1\nissued=2026-01-01\nmachines=" +
                    std::to_string(machines) + "\nmajor=" + std::to_string(major) + "\n";
    if (!bind.empty()) b += "bind=" + bind + "\n";
    return b;
}

struct MemStore : TrialStore {
    std::map<int, std::string> d;
    bool read(int s, std::string& o) override { auto it = d.find(s); if (it == d.end()) return false; o = it->second; return true; }
    bool write(int s, const std::string& x) override { d[s] = x; return true; }
};

static void testBase64() {
    for (size_t n = 0; n < 20; ++n) {
        std::vector<uint8_t> v(n); for (size_t i = 0; i < n; ++i) v[i] = uint8_t(i * 37 + 11);
        std::vector<uint8_t> back;
        CHECK(base64urlDecode(base64urlEncode(v.data(), n), back) && back == v);
    }
    std::vector<uint8_t> o; CHECK(!base64urlDecode("ab$d", o)); CHECK(!base64urlDecode("a", o));
}

static void testLicense() {
    const std::string mh = machineHash("test-machine-1");
    LicenseInfo li = verifyLicense(makeLicense(baseBody()), gPub, 1, mh);
    CHECK(li.valid && li.tier == Tier::Full && li.machines == 3 && li.major == 1 && li.licensee == "A B");
    CHECK(featuresFor(li.tier).midiClock && featuresFor(li.tier).maxInputRoles > 1);

    li = verifyLicense(makeLicense(baseBody("lite", 1)), gPub, 1, mh);
    CHECK(li.valid && li.tier == Tier::Lite);
    Features f = featuresFor(Tier::Lite);
    CHECK(f.linkOutput && !f.midiClock && !f.osc && !f.syncBus && !f.songMap && !f.driftReport && f.maxInputRoles == 1);

    // CRLF line endings from an email client still verify
    std::string crlf; for (char c : makeLicense(baseBody())) { if (c == '\n') crlf += '\r'; crlf += c; }
    CHECK(verifyLicense(crlf, gPub, 1, mh).valid);

    // Tampering: change the tier, the licensee, the machine count
    std::string good = makeLicense(baseBody("lite", 1));
    std::string t = good; t.replace(t.find("tier=lite"), 9, "tier=full");
    CHECK(!verifyLicense(t, gPub, 1, mh).valid);
    t = good; t.replace(t.find("machines=1"), 10, "machines=3");
    CHECK(!verifyLicense(t, gPub, 1, mh).valid);
    t = good; t.replace(t.find("A B"), 3, "X Y");
    CHECK(!verifyLicense(t, gPub, 1, mh).valid);
    // appended line after signature, truncated, garbage, empty, wrong magic
    CHECK(!verifyLicense(good + "tier=full\n", gPub, 1, mh).valid);
    CHECK(!verifyLicense(good.substr(0, good.size() - 10), gPub, 1, mh).valid);
    CHECK(!verifyLicense("hello", gPub, 1, mh).valid);
    CHECK(!verifyLicense("", gPub, 1, mh).valid);
    CHECK(!verifyLicense(std::string(5000, 'x'), gPub, 1, mh).valid);
    // wrong key
    unsigned char otherPub[32], otherPriv[64], otherSeed[32]; std::memset(otherSeed, 7, 32);
    ed25519_create_keypair(otherPub, otherPriv, otherSeed);
    CHECK(!verifyLicense(good, otherPub, 1, mh).valid);
    // all-zero (unconfigured) public key never validates
    unsigned char zero[32] = {};
    CHECK(!verifyLicense(good, zero, 1, mh).valid);

    // product, tier, machines, major
    CHECK(!verifyLicense(makeLicense("PACEMAKER-LICENSE-1\nproduct=other\ntier=full\nmachines=3\nmajor=1\n"), gPub, 1, mh).valid);
    CHECK(!verifyLicense(makeLicense(baseBody("gold")), gPub, 1, mh).valid);
    CHECK(!verifyLicense(makeLicense(baseBody("full", 4)), gPub, 1, mh).valid);
    CHECK(!verifyLicense(makeLicense(baseBody("full", 0)), gPub, 1, mh).valid);
    li = verifyLicense(makeLicense(baseBody("full", 3, 1)), gPub, 2, mh);          // v2 app, v1 key
    CHECK(!li.valid && li.error.find("upgrade") != std::string::npos);
    CHECK(verifyLicense(makeLicense(baseBody("full", 3, 2)), gPub, 1, mh).valid);   // v1 app, v2 key: fine
    CHECK(!verifyLicense(makeLicense(baseBody() + "tier=lite\n"), gPub, 1, mh).valid);   // duplicate key

    // machine binding
    const std::string other = machineHash("test-machine-2"), third = machineHash("test-machine-3");
    li = verifyLicense(makeLicense(baseBody("full", 2, 1, mh + "," + other)), gPub, 1, mh);
    CHECK(li.valid && li.bound.size() == 2);
    CHECK(verifyLicense(makeLicense(baseBody("full", 2, 1, mh + "," + other)), gPub, 1, other).valid);
    CHECK(!verifyLicense(makeLicense(baseBody("full", 2, 1, mh + "," + other)), gPub, 1, third).valid);
    CHECK(verifyLicense(makeLicense(baseBody("full", 1, 1, formatMachineHash(mh))), gPub, 1, mh).valid);   // dashed, as shown to users
    CHECK(!verifyLicense(makeLicense(baseBody("full", 1, 1, mh + "," + other)), gPub, 1, mh).valid);       // 2 bound > 1 machine
}

static void testFixtures() {
    // Licences produced by tools/keygen.py (Python signer) must verify with the C verifier.
    const std::string mh = machineHash("anything");
    LicenseInfo li = verifyLicense(readFile("tests/fixtures/test_full.license"), gPub, 1, mh);
    CHECK(li.valid && li.tier == Tier::Full && li.order == "PM-TEST-0001");
    li = verifyLicense(readFile("tests/fixtures/test_lite.license"), gPub, 1, mh);
    CHECK(li.valid && li.tier == Tier::Lite && li.machines == 1);
    CHECK(!verifyLicense(readFile("tests/fixtures/test_bound.license"), gPub, 1, mh).valid);          // bound to someone else
    CHECK(verifyLicense(readFile("tests/fixtures/test_bound.license"), gPub, 1, "00112233445566778899AABBCCDDEEFF").valid);
    CHECK(verifyLicense(readFile("tests/fixtures/test_bound.license"), gPub, 1, "00112233-44556677-8899aabb-ccddeeff").valid);
    // the public key the Python tool prints for the TEST seed
    const char* want = "771b6f1bf7a0f9e44e5728fb376cb02b3ff117189295d24960df246c491c9c21";
    std::string got; char b[3]; for (int i = 0; i < 32; ++i) { std::snprintf(b, 3, "%02x", gPub[i]); got += b; }
    CHECK(got == want);
}

static void testMachine() {
    const std::string id = machineId();
#if defined(__linux__)
    CHECK(!id.empty());       // /etc/machine-id or dbus id is expected on a normal Linux install
#endif
    const std::string h = machineHash(id.empty() ? "x" : id);
    CHECK(h.size() == 32 && machineHash(id.empty() ? "x" : id) == h);
    CHECK(machineHash("a") != machineHash("b"));
    CHECK(machineHash("").empty());
    CHECK(normalizeMachineHash(formatMachineHash(h)) == h);
    std::printf("  machineId present: %s, hash %s\n", id.empty() ? "no" : "yes", formatMachineHash(h).c_str());
}

static void testTrial() {
    const std::string mh = machineHash("trial-machine");
    const int64_t T0 = 1800000000, DAY = 86400;
    MemStore s;
    TrialStatus t = evaluateTrial(s, T0, mh);
    CHECK(t.state == TrialState::Active && t.daysLeft == 14 && s.d.size() == 2);
    t = evaluateTrial(s, T0 + 13 * DAY, mh);
    CHECK(t.state == TrialState::Active && t.daysLeft == 1);
    t = evaluateTrial(s, T0 + 14 * DAY - 1, mh);
    CHECK(t.state == TrialState::Active);
    t = evaluateTrial(s, T0 + 14 * DAY, mh);
    CHECK(t.state == TrialState::Expired && t.daysLeft == 0);

    // clock rollback after use
    MemStore r; evaluateTrial(r, T0, mh); evaluateTrial(r, T0 + 5 * DAY, mh);
    t = evaluateTrial(r, T0 + 1 * DAY, mh);
    CHECK(t.state == TrialState::ClockRollback);
    // small backwards step within tolerance is accepted
    CHECK(evaluateTrial(r, T0 + 5 * DAY - 120, mh).state == TrialState::Active);
    // rolling back, then forward again: still on the same record, expiry counted from first run
    CHECK(evaluateTrial(r, T0 + 20 * DAY, mh).state == TrialState::Expired);

    // expired trial cannot be revived by rolling the clock back to day 1
    MemStore e; evaluateTrial(e, T0, mh); evaluateTrial(e, T0 + 20 * DAY, mh);
    CHECK(evaluateTrial(e, T0 + 2 * DAY, mh).state != TrialState::Active);

    // tampering with a record (change first-run date) is detected
    MemStore m; evaluateTrial(m, T0, mh);
    std::string rec = m.d[0]; rec.replace(rec.find("first=") + 6, 10, "1900000000");
    m.d[0] = rec;
    CHECK(evaluateTrial(m, T0 + 3 * DAY, mh).state == TrialState::Tampered);
    // garbage record
    MemStore g; evaluateTrial(g, T0, mh); g.d[1] = "junk";
    CHECK(evaluateTrial(g, T0 + DAY, mh).state == TrialState::Tampered);

    // deleting ONE copy is not a reset; the other copy is used and the first is restored on next save
    MemStore d; evaluateTrial(d, T0, mh); evaluateTrial(d, T0 + 5 * DAY, mh);
    d.d.erase(0);
    t = evaluateTrial(d, T0 + 6 * DAY, mh);
    CHECK(t.state == TrialState::Active && t.daysLeft == 8);
    // (documented limit) deleting both starts a new trial
    MemStore z; evaluateTrial(z, T0, mh); z.d.clear();
    CHECK(evaluateTrial(z, T0 + 10 * DAY, mh).daysLeft == 14);

    // a record copied from another machine is rejected
    MemStore c1; evaluateTrial(c1, T0, machineHash("machine-A"));
    CHECK(evaluateTrial(c1, T0, machineHash("machine-B")).state == TrialState::Tampered);

    // file store round trip
    const std::string base = "/tmp/pm_trial_test_" + std::to_string((long) T0);
    std::remove((base + "/a.dat").c_str()); std::remove((base + "/b.dat").c_str());
    auto fs = makeFileTrialStore({ base + "/a.dat", base + "/b.dat" });
    CHECK(evaluateTrial(*fs, T0, mh).state == TrialState::Active);
    auto fs2 = makeFileTrialStore({ base + "/a.dat", base + "/b.dat" });
    CHECK(evaluateTrial(*fs2, T0 + 14 * DAY, mh).state == TrialState::Expired);
    std::remove((base + "/a.dat").c_str()); std::remove((base + "/b.dat").c_str());
}

static void testDecision() {
    TrialStatus active; active.state = TrialState::Active; active.daysLeft = 9;
    TrialStatus expired; expired.state = TrialState::Expired; expired.daysLeft = 0;
    LicenseInfo full; full.valid = true; full.tier = Tier::Full;
    LicenseInfo lite; lite.valid = true; lite.tier = Tier::Lite;

    Entitlements e = decide(&full, expired, 1e6);
    CHECK(e.mode == Mode::Licensed && e.outputsNow && e.detector);
    e = decide(&lite, active, 299.9);        // licence beats trial, no periodic silence
    CHECK(e.mode == Mode::Licensed && e.outputsNow && !e.features.midiClock && e.features.linkOutput);

    // trial: outputs silent for the last 20 s of every 300 s, detector never
    int open = 0, closed = 0;
    for (int s = 0; s < 3000; ++s) {
        e = decide(nullptr, active, s);
        CHECK(e.detector);
        CHECK(e.mode == Mode::Trial && e.features.midiClock);
        (e.outputsNow ? open : closed)++;
    }
    CHECK(open == 2800 && closed == 200);
    CHECK(decide(nullptr, active, 279.9).outputsNow && !decide(nullptr, active, 280.0).outputsNow &&
          !decide(nullptr, active, 299.9).outputsNow && decide(nullptr, active, 300.0).outputsNow);
    CHECK(decide(nullptr, active, 100).trialDaysLeft == 9);

    // expired / tampered / rollback: outputs off, detector on
    for (TrialState st : { TrialState::Expired, TrialState::Tampered, TrialState::ClockRollback }) {
        TrialStatus ts; ts.state = st;
        e = decide(nullptr, ts, 10);
        CHECK(e.mode == Mode::TrialExpired && !e.outputsNow && e.detector && !e.features.linkOutput);
    }
    // an invalid licence does not grant anything
    LicenseInfo bad; bad.valid = false;
    e = decide(&bad, expired, 10);
    CHECK(!e.outputsNow && e.detector);
}

int main() {
    ed25519_create_keypair(gPub, gPriv, kSeed);
    testBase64(); testLicense(); testFixtures(); testMachine(); testTrial(); testDecision();
    std::printf("%d checks, %d failures\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
