# Pacemaker project plan and checklist

Phases are sequential; each ends with something usable. Tick boxes are the
working checklist; keep this file current. Scheduling, milestones M3 to v1.2,
the risk register and estimates are in `docs/dev/DEVELOPMENT-PLAN.md`; every
remaining item below has a spec in `docs/specs/features/` (index there).

## Phase 0: Foundations (1 to 2 weeks)

- [x] Research notes written (`docs/research/01` to `04`)
- [x] Engineering specs ES-01 to ES-04, regular specs RS-01 to RS-06
- [x] CMake skeleton: `pacemaker_engine`, `PacemakerTests`, `pacemaker_eval` (plain C++17, no JUCE yet; JUCE FetchContent, clap-juce-extensions and the plugin targets come with Phase 3)
- [x] CI: Linux build and unit tests on every PR (`.github/workflows/ci.yml`)
- [ ] Request Ableton Link proprietary licence (link-devs@ableton.com); sign Steinberg VST3 agreement
- [ ] Trademark search for "Pacemaker" (RS-06)
- [x] Test corpus generator: synthetic drum machine with tempo random walk, phase noise, fills, count-ins, silence, ramps (`Tests/DrumMachine.h`)

## Phase 1: Engine and evaluation (3 to 5 weeks)

- [x] Front end: STFT, log compression, band spectral flux, whitening, online peak picking (ES-01 section 4)
- [x] Tempo induction: real-time PLP tempogram port with prior and octave flags (section 5)
- [x] Phase/period tracker with Stage and Rehearsal profiles, Chase and Hold (section 6)
- [ ] Bar tracker with shipped patterns and Learn mode (section 7): patterns and overrides done, Learn mode and 5/4, 7/8 tables pending
- [x] Supervisor state machine, count-in detector, confidence fusion, rate limit (section 8)
- [ ] `pacemaker_eval` CLI and `mir_eval` scorer; thresholds T1 to T9 enforced in CI: CLI done, T1 to T5 and T7 in CI on synthetic data (T1 sd bound loosened to 22 ms below 100 BPM, T2 allows one bar of settling), T6, T8, T9 and the `mir_eval` scorer pending
- [ ] GMD and E-GMD render pipeline; first own recordings annotated
- [ ] Block-size invariance and CPU tests passing: block-size invariance (T7) passing, CPU test (T8) pending

## Phase 2: Outputs and calibration (3 to 4 weeks)

- [x] ClockMap with host timestamps and regression fallback (ES-02 section 1); plugin host-timestamp plumbing comes with Phase 3
- [ ] Link output behind `PACEMAKER_WITH_LINK`, publisher arbitration, two-process test O1 and O2: policy and O2 (mock session) done, `ableton::Link` adapter and O1 pending the licence
- [ ] MIDI clock ClockThread with CoreMIDI timestamps, Linux ALSA, Windows spin path; jitter test O3: generator, adaptive spin thread, raw device sink and O3 done on Linux; CoreMIDI timestamps and Windows path pending
- [x] OSC output with profiles and timetags; test O5
- [x] Sync bus pulse and click; test O4 (renderer only, audio device comes with the plugin and standalone)
- [ ] Loopback calibration wizard and storage (ES-03 section 4); tests L1 to L4: maths, storage and L1 to L3 done, wizard runs against a simulated interface in the web UI, real device capture and L4 pending
- [ ] Tap-along calibration for plugin builds (ES-03 section 5): maths and test done, host beat grid plumbing pending
- [x] Hardware clock offset helper (ES-03 section 6): maths and test; UI page pending

## Phase 3: Plugin, standalone and UX (4 to 6 weeks)

- [ ] Processor with buses and role map (ES-04 section 4); MIDI note to role table
- [ ] APVTS parameters, session settings, user settings, migrations (RS-04): settings object with versioned merge, presets and persistence done in `Settings`; APVTS binding pending
- [ ] Main screen in the house style (RS-01 section 4), stage mode, MIDI learn: web version with stage mode and keyboard control done (RS-07), JUCE version and MIDI learn pending
- [ ] Standalone shell with per-channel device panel (ES-04 section 5)
- [x] Song map panel and CSV import (RS-01 section 6); AbleSet import pending format confirmation
- [x] Drift log and report, tempo map export (RS-01 section 7)
- [ ] pluginval, clap-validator, lv2lint in CI; thread sanitizer job
- [ ] Host documentation for Live, Logic, Reaper, Bitwig, Cubase, Studio One (RS-02 section 7)
- [ ] Bundled Reaper script and Bitwig controller script
- [ ] Release workflow with macOS universal build, signing and notarisation on tags

## Phase 4: Beta and launch (3 to 4 weeks)

- [ ] Closed beta with at least 5 bands: 2 Ableton, 1 Logic/MainStage, 1 hardware-only, 1 lighting operator
- [ ] Manual stage test checklist run and logged (RS-05 section 1.5)
- [ ] Licensing: key file, trial mode (RS-06 section 2)
- [ ] Website, KVR listing, demo video with a real drummer and lights
- [ ] v1.0 release

## Phase 5: Pacemaker Box (4 to 6 weeks, after v1.0)

- [ ] Headless target, JSON config, cpp-httplib web UI with WebSocket status (RS-03 section 2): `pacemaker_server` has the JSON config and the web UI (own HTTP server, SSE instead of WebSocket); real audio input and the systemd packaging pending
- [ ] Raspberry Pi OS image with read-only root, systemd service, avahi
- [ ] Pisound DIN MIDI, GPIO LEDs, optional OLED
- [ ] Field test with a hardware-only act
- [ ] Image sold via Gumroad

## Later

- [ ] Dual-instance redundancy
- [ ] Section change suggestions
- [ ] Learned activation stage (in-house model)
- [ ] AAX

## Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Ableton declines or delays the Link licence | no Link in commercial builds | build flag; MIDI clock and OSC still ship; GPL edition option |
| Downbeat accuracy disappoints on syncopated material | bar output unreliable | patterns, Learn mode, Downbeat-now override, song map; v2 learned stage |
| Windows MIDI clock jitter | hardware users on Windows unhappy | spin-wait path first; Windows MIDI Services backend when JUCE supports it; recommend the Sync bus plus audio-clock box |
| Host sidechain limits | only two roles in Live and Logic | documented baseline; standalone for full multi-mic |
| Trademark conflict on "Pacemaker" | rename | search early; fallback names recorded |
