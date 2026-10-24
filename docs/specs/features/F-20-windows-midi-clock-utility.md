# F-20: Windows MIDI clock utility (spin-off)

Status: draft 1.0 · Priority: Spin · Estimate: 12 pd · Depends on: F-05 (Windows backends), F-04 (Link) · Related: DIFFERENTIATION section 4

## 1. Goal and user value
A small tray application that takes Ableton Link (or a manual tempo) and outputs tight MIDI clock on Windows, where common tools jitter. A separate, simple product that proves the clock stack and attracts users.
## 2. Requirements
- R-1 Input: Link session (tempo and phase), manual BPM, tap tempo; optional OSC tempo input.
- R-2 Output: selected MIDI ports (up to 4 mirrored) via the F-05 Windows backends with offsets per port; Start/Stop follows Link start/stop.
- R-3 Jitter per R-8 of F-05; live jitter meter and histogram in the UI.
- R-4 Tray app, starts with Windows, remembers settings, single small window, light and dark theme, no admin rights needed.
- R-5 Installer signed (F-26), auto-update opt-in, privacy statement (no telemetry by default).
- R-6 Licensing: Link requires the Ableton licence (F-04); standalone price and trial rules from F-26.
## 3. Design
Reuses `ClockMap`, `MidiClockGenerator`, `ClockThread`, `LinkService`; UI as a minimal native window (JUCE) or WebView2 hosting the web UI. A beat grid is synthesised from Link tempo and phase instead of the tracker.
## 4. Tests
F20_R3 loopback jitter on hardware rig; F20_R2 Start/Stop follow with Link probe; installer and upgrade tests; UI screenshots; soak 24 h.
## 5. Plan
| Task | pd |
|---|---|
| Link-driven grid source and manual/tap source | 3 |
| Tray UI, ports, offsets, meter | 4 |
| Installer, autostart, settings | 2 |
| Tests, jitter reports, docs, product page | 3 |
Risks: Windows MIDI stack changes (backends are replaceable), licence cost versus price.
