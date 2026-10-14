# F-13: Guided first run

Status: draft 1.0 · Priority: P1 · Estimate: 6 pd · Depends on: F-01 · Related: RS-01 section 5, F-14

## 1. Goal and user value
A new user gets from install to a locked tempo in under five minutes: choose the audio device, assign channels, set levels, run calibration, load a preset, and verify with a test hit.

## 2. Requirements
- R-1 Wizard starts on first run (or from Help): steps Welcome, Audio device, Channels and roles, Levels (with a "play your drums now" live meter and clip warnings), Latency (loopback if possible; skip with a note), Style (preset choice with short descriptions), Verify (live lock status with a checklist: signal present, onsets detected, tempo lock, bar position), Done.
- R-2 Each step has Skip and Back; progress persists if closed; wizard can be re-run.
- R-3 Smart defaults: detects interface names (kick/snare labels) and proposes a map; detects an e-drum MIDI device and proposes the e-drum path (F-06).
- R-4 Level step suggests trim: target peak -12 dBFS on loudest hits (measured over 10 s); warns for clipping or too quiet (< -40 dBFS).
- R-5 Verify step shows what is wrong in plain words when lock fails (links to diagnostics F-14).
- R-6 Plugin variant: skips device steps, shows bus/role mapping and tap-along calibration.
- R-7 All copy plain language, no jargon without a tooltip, keyboard accessible, localisable strings in a resource file.
## 3. Design
A step array with `canEnter/canLeave` predicates fed by live status; a `FirstRunState` in user settings; reuses the channel strips, calibration dialog and preset selector.
## 4. Tests
F13_R1 scripted Playwright run on the simulated source (all steps, skip paths); F13_R4 gain advice unit tests; F13_R3 device-name heuristics with a table of 20 names; F13_R5 failure messages for synthetic faults (silent input, clipped input, no rhythm); usability test with 5 new users (target: 4 of 5 reach lock unaided in 5 minutes).
## 5. Plan
| Task | pd |
|---|---|
| Wizard framework and state | 1.5 |
| Steps: device, channels, levels, latency, style | 2.5 |
| Verify and failure messaging | 1 |
| Copy, usability test, fixes | 1 |
Risks: wizard fatigue (skip everywhere, short copy); device quirks (manual path always visible).
