# F-27: Host guides, install docs and bundled scripts

Status: draft 1.0 · Priority: P1 · Estimate: 10 pd · Depends on: F-03, F-04, F-05 · Related: RS-02 section 7, ES-02 section 6

## 1. Goal and user value
Most support questions are "how do I set this up in my DAW?". Short, tested guides per host plus scripts that close the gaps (Reaper and Bitwig tempo follow) make setup predictable and cut support load.
## 2. Requirements
- R-1 One guide per host (Live, Logic Pro and MainStage, Reaper, Bitwig, Cubase, Studio One, FL Studio, Pro Tools, QLab, standalone) with: install location, adding the plugin on a drum bus or sidechain, routing for Main plus Trigger inputs, which outputs work there (Link, MIDI clock, OSC, host transport), known limits, calibration procedure, troubleshooting, and screenshots with version numbers.
- R-2 A host support matrix (table in docs and in the app's Help) with status per feature per host, generated from a data file reviewed each release (**VERIFY** each cell by testing; do not claim Link for hosts without it, see F-04 erratum).
- R-3 Bundled scripts: Reaper `Pacemaker_TempoFollow.lua` (OSC to `SetCurrentBPM`, optional tempo map building, uses ReaImGui only if present), Bitwig controller script (OSC to `transport.tempo()`), installation instructions and uninstall steps; scripts versioned with the app and covered by a smoke test.
- R-4 INSTALL.md and in-app "Getting started" page (matching F-13 wizard), quick-start PDF one page, FAQ with 20 questions from beta.
- R-5 Each guide has a "tested with" line (host version, OS, date); CI checks links and that every referenced screenshot exists; guides are updated by the owner of any change that affects them (PR template).
- R-6 Video walkthroughs (3 to 5 minutes) for Live, Reaper and standalone with hardware.
- R-7 Troubleshooting flowcharts: no signal, no lock, wrong bar, MIDI clock jitter, Link not found, OSC not received.
## 3. Tests
F27_R3 Reaper script run headless against a mock OSC sender and a REAPER test project (tempo changes observed); Bitwig script syntax check and manual test; docs link checker; matrix data validated against a schema; manual host smoke per release (RS-05 1.5).
## 4. Plan
| Task | pd |
|---|---|
| Host test sessions and matrix data | 3 |
| Guides and screenshots (10 hosts) | 3 |
| Reaper and Bitwig scripts with tests | 2 |
| Getting-started page, FAQ, flowcharts | 1.5 |
| Videos, link checker | 0.5 |
Risks: host UI changes (dated guides, matrix), no access to all hosts (borrow, beta testers, mark "untested" honestly).
