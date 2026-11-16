# Licence key design (RS-06 section 2)

Status: design and reference implementation 0.1. Code: `integrations/licensing/` (`License.h/.cpp`, `tools/keygen.py`,
tests). Not yet wired into CMake. The runtime is plain C++17 and has no JUCE dependency, so the plugin, standalone and
headless Box can all link it.

## 1. Requirements (from RS-06 and the project plan)

- Closed source, perpetual licence per user, offline key file, 3 machines, no iLok in v1.
- 14-day trial. During the trial **outputs** are periodically silenced; the **detector is never** silenced, so users can
  judge tracking (ES-02: each output has its own enable and a licence problem in one must not disable another; the
  detector feeds nothing back).
- Tiers Lite (US$39: plugin and standalone, Link output only, one input role) and Full (US$99: all outputs, all roles,
  song map, drift report). The Box image requires a Full licence.
- Must work on a stage laptop with no network, so no activation server and no phone-home.

## 2. Key file format

Plain text, UTF-8, LF or CRLF, one `key=value` per line, extension `.pacemaker-license`:

```
PACEMAKER-LICENSE-1
product=pacemaker
tier=full                  lite | full
licensee=Jane Drummer
email=jane@example.com
order=PM-000123
issued=2026-10-14
machines=3                 1..3, the entitlement
major=1                    product major version covered (perpetual within the major)
bind=<hash>,<hash>         optional, machine hashes, at most `machines` entries
signature=<base64url>      Ed25519 over all preceding lines, each LF-terminated
```

- The signed message is exactly the lines before `signature=` with trailing whitespace trimmed and every line ended by
  `\n`, so email clients that convert line endings do not break it.
- Ed25519 (RFC 8032), 64-byte signature, 32-byte public key compiled into the product. Verification code is the
  vendored `orlp/ed25519` (zlib licence, `third_party/ed25519`); signing is done offline by `tools/keygen.py`.
- Readable on purpose: support can see tier, order number and bound machines without tooling. Editing any byte
  invalidates the signature (tested for tier, machine count, licensee, appended lines, truncation).
- Rejected with a specific message: wrong magic, missing/short signature, bad signature, wrong product, unknown
  tier, `machines` outside 1..3, duplicate keys, more bound machines than `machines`, `major` lower than the running
  app's major ("upgrade needed"), a bound list that does not include this machine.

## 3. The 3-machine rule without a server

Offline enforcement of a machine count is a trade-off. Two modes, both produced by the same tool:

| Mode | How | Enforcement | Use |
|---|---|---|---|
| Unbound (default) | no `bind=` line | honour system: licence text says "up to 3 machines". Nothing prevents copying | retail checkout, zero friction, works for a replacement laptop |
| Bound | `bind=` lists machine hashes | the file only works on those machines (up to `machines`) | on request: the user sends the Machine ID shown in the About box, support issues a bound file, or the merchant webhook does it |

Re-issuing a bound file for a replaced machine is manual and logged by order number. We expect the honest majority and
treat piracy as a cost of doing business; pricing, a good support loop and free minor updates matter more than DRM.

### Machine ID

Raw platform id, then hashed so the user never sends or stores a raw identifier:

| Platform | Source |
|---|---|
| Windows | `HKLM\SOFTWARE\Microsoft\Cryptography\MachineGuid` |
| macOS | `IOPlatformUUID` (IOKit, `IORegistryEntryFromPath("IOService:/")`) |
| Linux | `/etc/machine-id`, fallback `/var/lib/dbus/machine-id` |

`hash = first 16 bytes of SHA-512("pacemaker-machine-v1" 0x00 rawId)`, shown as `XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX`.
Dashes, spaces and case are ignored when comparing. On the Pi Box the id is `/etc/machine-id` regenerated at image
first boot (the image build must not ship a fixed machine-id).

Status of the three sources: Linux is exercised by the tests on a Linux host. The Windows and macOS code is written
against the documented APIs and was **not compiled** in this repo's Linux environment; build it in CI on those
runners before relying on it.

## 4. Trial

- State is kept in two records in two locations (Windows: `%PROGRAMDATA%\Pacemaker\trial.dat` and
  `%APPDATA%\Pacemaker\trial.dat`; macOS: `/Users/Shared/Pacemaker/trial.dat` and
  `~/Library/Application Support/Pacemaker/trial.dat`; Linux: `$XDG_DATA_HOME/pacemaker/trial.dat` and
  `~/.pacemaker-trial`).
- Record: `PMT1 first=<unix>;last=<unix>;mac=<hex>` where `mac` = HMAC-SHA-512 (first 32 bytes) over the body with a key
  derived from the machine hash. Editing it or copying it to another machine makes it invalid.
- Evaluation (`evaluateTrial`): earliest `first` and latest `last` over the valid records; any invalid record gives
  `Tampered`; `now + 5 min < last` or `< first` gives `ClockRollback`; `now - first >= 14 days` gives `Expired`; else
  `Active` with days left. Progress (`last`) is persisted at most once a minute.
- Deleting one record does not reset the trial (the other wins and is rewritten). Deleting both does; that and
  reinstalling the OS are accepted limits for a 14-day trial.
- Any non-Active state behaves as expired: outputs off, detector on, UI explains why and links to the purchase page.

### Output gate

While a trial is Active, outputs are on for 280 s then silent for 20 s, repeating from process start on a monotonic
clock (`trialOutputGateOpen`). Wall-clock changes cannot affect the cycle. After expiry outputs stay off. Licensed
builds have no gate. Because ES-02 outputs are independent modules, the gate is applied per output at the
point each output is enabled; `Entitlements::detector` is a constant `true` and a test asserts it for every mode.

## 5. Tiers

`featuresFor(tier)`: Lite = Link output, one input role. Full = Link, MIDI clock, OSC, Sync bus, host transport, song
map, drift report, all roles. A Full-tier trial uses the Full feature set so evaluators see everything. Upgrades
(Lite to Full at 50 percent of the difference) are a newly issued key file; the old one is simply replaced.

## 6. Key management

- Private key: 32-byte seed, hex, created by `keygen.py genkey`, kept **outside the repository** (the tool refuses to
  write or read a key path inside a git work tree; `.gitignore` also excludes `*.key`). Store in the password manager
  plus an offline backup. Losing it means shipping a new public key in an update; compromise means the same.
- Public key: compiled into the product (`keygen.py pubkey-c` prints the C array). The repo contains only the TEST
  public key, used by unit tests; the product build must refuse to enable licensing with an all-zero or TEST key
  (add a CMake check when wiring).
- Issuing: merchant-of-record webhook (Paddle or Lemon Squeezy, RS-06 section 4) calls the signing tool on a private
  host, emails the file. Manual issue: `keygen.py sign ...`.
- Revocation: none offline. A leaked key is handled by shipping the next minor release with a denylist of order
  numbers (`major` is not used for revocation).

## 7. Integration plan (for the parent)

1. Add `integrations/licensing/License.cpp` and `third_party/ed25519/{fe,ge,sc,sha512,verify}.c` to a `pacemaker_license`
   target; do not compile `sign.c` or `keypair.c` into products. C sources need the C language enabled.
2. On start-up: read the key file from the user data dir (`licence` path in UserSettings) or the drag-and-drop location,
   `verifyLicense`, else `evaluateTrial`, then `decide(...)` once a second on the message thread; pass `Entitlements`
   to each output's enable logic by atomic flags. Never call into licensing from the audio thread.
3. UI: About box shows tier, licensee, order, machines and the Machine ID with a Copy button; trial banner shows days
   left and the silence countdown.
4. Tests: `integrations/licensing/tests/run_tests.sh` (plain gcc/g++ plus Python 3).

## 8. Threat model (short)

We defend against: casual editing of the key file or trial record, clock rollback, copying a trial record between
machines, accidental sharing. We do not defend against: patched binaries, cracks, deleting both trial records, VMs
with reset ids. This is intentional for a product of this price; time spent on harder DRM is better spent on support.
