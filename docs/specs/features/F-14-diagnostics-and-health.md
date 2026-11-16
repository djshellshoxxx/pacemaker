# F-14: Diagnostics and health

Status: draft 1.0 · Priority: P1 · Estimate: 7 pd · Depends on: F-01 · Related: QUALITY-STRATEGY 10, ES-04 section 8

## 1. Goal and user value
When something is wrong on stage the app says what and how to fix it in plain words; when users report a problem, one click produces everything needed to reproduce it.

## 2. Requirements
- R-1 Health checks (continuous, cheap): input level too low or clipping per channel; silent channel while others play; xruns above threshold; CPU above 70 percent; audio device change; MIDI port missing; Link peers zero or fighting; OSC send errors; clock mismatch warnings; calibration missing; low confidence for more than 20 s while playing; octave ambiguity; Hold longer than the song's expected silence; disk full for logs.
- R-2 Each check has id, severity (info, warning, error), human message, suggested action (button when actionable), and a doc anchor; shown in the warnings area and a Health dialog with history.
- R-3 Stage mode shows only errors, as a single bar, to avoid distraction.
- R-4 "Copy diagnostics" produces a bundle (zip or JSON) with version, OS, devices, settings (secrets removed), health history, last 10 minutes of drift log and events, counters (xruns, dropped events, CPU), and optionally a 10-minute onset/novelty recording for offline replay (`pacemaker_eval --replay`).
- R-5 Privacy: no audio content, no setlist titles unless the user ticks "include song names"; bundle preview before saving; everything local, user attaches it themselves.
- R-6 Log file with rotation (5 files x 2 MB) in the user data folder; log levels; no logging on the audio thread (events enqueue to a logger thread).
- R-7 Crash reporting opt-in (minidump) with a clear dialog on next start.
## 3. Design
`HealthMonitor` samples counters atomics at 2 Hz on a low-priority thread, evaluates rules (hysteresis to avoid flapping), publishes `HealthItem[]`. Bundle writer is a pure function over collected state. Replay support uses the FileBackend format with a small custom feature file.
## 4. Tests
F14_R1 each rule with scripted counters (trigger and clear, hysteresis); F14_R4/5 bundle contents and redaction golden; F14_R6 rotation; F14_UI stage-mode bar screenshot; F14_REPLAY a bundle replay reproduces the recorded tempo curve within 0.1 BPM.
## 5. Plan
| Task | pd |
|---|---|
| Rules engine and checks | 2.5 |
| UI (warnings, dialog, stage bar) | 1.5 |
| Bundle and redaction, replay format | 1.5 |
| Logging, crash opt-in | 1 |
| Tests and docs | 0.5 |
Risks: noisy warnings (hysteresis, severity tuning from beta); privacy concerns (preview, opt-in, documentation).
