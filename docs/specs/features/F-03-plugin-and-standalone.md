# F-03: Plugin (VST3, AU, CLAP, LV2) and standalone apps

Status: draft 1.0 · Priority: P0 · Estimate: 45 pd · Owner: apps
Depends on: F-01, F-30 · Related: ES-04, RS-01, RS-02, RS-04, F-26 (installers), F-27 (host guides)

## 1. Goal and user value
Most users live in a DAW. A plugin on the drum bus (or a sidechain) must make Pacemaker usable in Ableton Live, Logic, Reaper,
Bitwig, Cubase and MainStage, and the standalone app serves rigs without a DAW. Both share one engine, one set of outputs and one visual design with the web UI.

## 2. Scope and non-goals
In: JUCE 8 project, targets (`pacemaker_plugin` with VST3, AU, LV2, Standalone; CLAP via clap-juce-extensions), processor with buses and role map,
parameters and state, MIDI input and output behaviour, native editor (all RS-01 section 4 elements), outputs service shared per process, standalone shell with
device panel (JUCE backend of `AudioBackend`), validators, host quirks table. Out: AAX, mobile, installers (F-26), music analysis UI (F-09), MIDI learn (F-12).

## 3. Requirements
- R-1 Formats: VST3 and AU (macOS), VST3 and CLAP and LV2 (Windows, Linux where applicable), Standalone on all three OSes; macOS universal (arm64 and x86_64).
- R-2 Buses per ES-04 section 4; `isBusesLayoutSupported` accepts every documented layout; Main input mono or stereo, optional Trigger, Kick, Snare inputs, optional Sync stereo output.
- R-3 Role map editable per bus channel; defaults: Main to Any, Trigger to Kick, Kick to Kick, Snare to Snare; stored in state.
- R-4 Parameters (APVTS, automatable): follow, profile, tempo min/max, reference BPM, meter num/den, pattern, sensitivity, per-role gain, monitor on/off. IDs are frozen once released.
- R-5 State: parameters plus `PM_SETTINGS` subtree (RS-04): role map, output enables and offsets, OSC destination, MIDI port, Link quantum, song map, tap-along calibration; version integer with explicit migrations; unknown keys ignored, future versions load what they can; preset load changes parameters only.
- R-6 `processBlock`: no allocation, lock or logging; engine runs on role buffers; `ClockMap` fed with host time when `AudioPlayHead` provides it, else regression fallback; Sync bus rendered sample-accurately; passes input through when Monitor is on, else silence.
- R-7 MIDI input: note-ons feed the engine through the note-to-role table (F-06); MIDI output buffer carries 0xF8/0xFA/0xFB/0xFC for AU, CLAP and LV2 (not VST3).
- R-8 Offline bounce (`isNonRealtime`): outputs other than Sync are muted; engine still runs for render-time analysis (setting).
- R-9 Sample rate and block size changes (44.1 to 192 kHz, 16 to 4096) are handled in `prepareToPlay` without leaks; `reset()` on transport jumps is optional and off by default.
- R-10 Editor: same information architecture as the web UI (header, trace, inputs, outputs, transport, footer, stage mode, calibration, songs, report), resizable from 720x480, HiDPI, keyboard accessible, JUCE accessibility handlers for state word and controls, 60 fps trace without audio-thread work.
- R-11 Outputs service: one process-wide `OutputsService` (ref-counted) hosts `ClockThread`, `OscRunner`, `LinkService`; multiple plugin instances share it and arbitrate the publisher (ES-02 section 2).
- R-12 Standalone: device selection and channel map via a JUCE `AudioDeviceManager` backend implementing `AudioBackend` (F-01), MIDI inputs for e-drums, always-on-top option, stage mode, remembers devices.
- R-13 Validation: pluginval strictness 5 (10 target) passes for VST3 and AU; clap-validator, lv2lint and `auval` pass.
- R-14 Latency: plugin reports 0 samples latency and no tail; no added latency to pass-through.
- R-15 Crash safety: an exception or fault in the outputs service never reaches the audio thread; the plugin degrades to "outputs off" with a UI warning.
- R-16 CPU: three roles at 48 kHz, 64-sample blocks: under 10 percent of one core on the CI x86 runner; editor open adds under 3 percent.
- R-17 Design tokens (colours, spacing, type scale) live in `design/tokens.json` and generate both the CSS variables of the web UI and a C++ header, so the two UIs cannot drift.

## 4. Design
```
PacemakerProcessor : juce::AudioProcessor
  - Engine engine; ClockMap map; RoleMap roles; RealtimeObject<EngineSettings> settings;
  - AudioProcessorValueTreeState apvts;  SettingsTree session;
  - SpscQueue<EngineEvent> events -> UI timer (30 Hz) -> EditorModel (message thread)
PacemakerEditor : juce::AudioProcessorEditor
  - Header, TraceView (Path cache, 8 s ring), InputsStrip, OutputsStrip, TransportBar, Footer, dialogs
OutputsService (singleton): ClockThread, OscRunner, LinkService, device lists; control methods on message thread only.
```
Block flow: gather bus channels into role pointers (no copies when layout allows), apply per-role gain, engine.process, `map.addBlock`, publish snapshot, push events,
`SyncRenderer`, MIDI out for clock-capable formats. Editor model reads snapshot through the seqlock and events through the SPSC queue; all painting uses the model, never the processor internals.
CMake: JUCE via FetchContent pinned by tag; `PACEMAKER_SYSTEM_JUCE`, `PACEMAKER_BUILD_CLAP`; `juce_add_plugin` with `COPY_PLUGIN_AFTER_BUILD` off in CI. Host quirk table lives in RS-02 and is extended as found.

## 5. Test plan
| ID | Method | Pass |
|---|---|---|
| F03_R2 | Bus-layout matrix through `AudioPluginHost`-less harness | all accepted layouts process 10 s |
| F03_R5 | State round trip and migrations, fuzzed state blobs | no crash, fields equal |
| F03_R6 | `ScopedAudioThread` around `processBlock` | clean |
| F03_R13 | pluginval, clap-validator, lv2lint, auval in CI (macOS runner) | pass |
| F03_R10 | Editor screenshot tests at 3 sizes and stage mode (JUCE `createComponentSnapshot`) compared to goldens with tolerance | no diff beyond tolerance |
| F03_R11 | Two instances: one publisher, no double MIDI clock | verified with callback sink |
| F03_R16 | Benchmark | budget |
| F03_HOST | Host smoke (weekly): Reaper headless render with plugin, scripted load in Live/Logic/Bitwig/Cubase on a test Mac/PC | loads, passes audio, shows UI, saves and restores project |
Manual checklist per host in F-27.

## 6. Acceptance criteria
In Live, Reaper and Logic (AU): load on a drum bus, follow a recorded drummer, output MIDI clock to a hardware box and OSC to a lighting program, save and reopen the session with identical state; standalone runs the same on three OSes.

## 7. Development plan
| # | Task | pd | Notes |
|---|---|---|---|
| P-1 | JUCE and CMake setup, CLAP integration, CI builds on 3 OSes | 4 | licence decision first |
| P-2 | Processor, buses, role map, APVTS, state and migrations | 6 | |
| P-3 | Engine integration, ClockMap, Sync bus, MIDI out | 4 | |
| P-4 | OutputsService, arbitration, error isolation | 4 | uses F-04/F-05 modules |
| P-5 | Design tokens pipeline | 2 | |
| P-6 | Editor components (header, trace, strips, transport, footer) | 10 | reuse web layout |
| P-7 | Dialogs: settings, calibration (tap-along and loopback), songs, report | 6 | |
| P-8 | Standalone shell, JUCE `AudioBackend`, MIDI inputs | 4 | |
| P-9 | Validators, host matrix, fixes | 5 | |
Milestones: alpha after P-1..P-4 (headless behaviour in Reaper); beta after P-6..P-9.
Risks: host-specific sidechain limits (documented baseline: Main plus Trigger); AU sidechain in Logic quirks; JUCE/CLAP licence or API changes (isolated wrappers, ADR); editor performance (path caching, no per-frame allocation).
