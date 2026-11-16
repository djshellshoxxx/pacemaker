# F-26: Licensing and distribution implementation

Status: draft 1.0 · Priority: P0 · Estimate: 12 pd · Depends on: none (installers need F-03) · Related: RS-06

## 1. Goal and user value
Sell and ship safely: a fair, simple licence system, a trial that works offline, installers that macOS and Windows trust, and a legal ledger of every dependency and dataset.
## 2. Requirements
- R-1 Licence file: signed (Ed25519) JSON `{product, version-range, name/email hash, issued, features, expiry?}`; the public key is embedded; verification offline; no always-on activation; machine-count rules per RS-06.
- R-2 Trial: 14 days or N sessions (decide in RS-06), full features, nag screen after expiry that mutes outputs after 60 seconds of tracking but never stops audio pass-through; trial state stored tamper-evidently in user data.
- R-3 Behaviour on invalid/missing licence never affects the audio thread beyond muting outputs; clear status in the UI with a "Buy" and "Enter licence" flow; offline entry by dropping the file.
- R-4 Store integration: a payment provider with a merchant of record (e.g., Paddle, FastSpring, Gumroad for the Box image; **VERIFY** fees and tax handling) delivering keys by email; refund and re-issue process documented.
- R-5 Installers: macOS notarised `.pkg` or `.dmg` with plugins in the right folders (AU component, VST3, CLAP, standalone app), Windows signed installer (Inno Setup or WiX) with component selection, Linux `.tar.gz` plus AppImage and `.deb` for the standalone and LV2/VST3 bundles; uninstallers; silent mode.
- R-6 Signing: Apple Developer ID and notarisation, Windows code signing (EV or cloud signing); secrets in CI vault; release workflow on `v*` tags (RS-05 section 3).
- R-7 Auto-update: opt-in checker (version JSON over HTTPS, signed manifest); no automatic installs; changelog shown.
- R-8 Licence ledger and SBOM: every third-party component with licence, version, hash, usage; generated per release; GPL/AGPL/non-commercial entries blocked by CI; datasets and models included (F-02 R-11, F-23 R-7); Link and JUCE licences documented.
- R-9 Legal pack: EULA, privacy policy, open-source notices page in the app, trademark search result and fallback name list.
- R-10 Privacy: no telemetry by default; opt-in usage stats and crash reports described in plain words.
## 3. Design
Small `licensing` library (verification only, no secrets), a command-line tool to issue keys (offline signer on the maintainer's machine), tests with fixtures. Installer scripts in `installers/`. Release workflow produces artifacts, SBOM and ledger.
## 4. Tests
F26_R1 signature verify/tamper/expiry/version range; F26_R2 trial state transitions and clock rollback detection; F26_R3 muting does not touch audio (F-30 harness); F26_R5/6 install/uninstall on clean VMs (macOS arm64/x86_64, Windows 10/11, Ubuntu); F26_R8 ledger gate fails on a planted GPL dependency.
## 5. Plan
| Task | pd |
|---|---|
| Licence library, key tool, UI flow | 3 |
| Trial logic and tests | 1.5 |
| Installers (3 OSes), signing set-up | 4 |
| Ledger/SBOM tooling and CI gate | 1.5 |
| Store, legal pack, privacy docs | 2 |
Risks: signing lead times (apply in week 1), tax/merchant-of-record choices, licence cracking (acceptable; keep honest users happy).
