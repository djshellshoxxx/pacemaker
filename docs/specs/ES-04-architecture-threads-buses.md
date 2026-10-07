# ES-04: Architecture, Threads and Buses (detailed engineering spec)

Status: draft 0.1, normative.
Research inputs: `docs/research/04-plugin-architecture-and-latency.md`.

---

## 1. Targets

One CMake project, JUCE 8 via FetchContent (same pattern as Vivisect),
C++17.

| Target | Type | Depends on | Notes |
|---|---|---|---|
| `pacemaker_engine` | static lib | juce_core, juce_dsp (FFT) or pffft | ES-01. No GUI, no audio devices. |
| `pacemaker_outputs` | static lib | juce_core, juce_events, juce_audio_devices (MIDI), juce_osc, Link (optional) | ES-02. |
| `pacemaker_plugin` | `juce_add_plugin` | engine, outputs, juce_gui_* | FORMATS VST3 AU LV2 Standalone; CLAP via clap-juce-extensions |
| `pacemaker_headless` | console app | engine, outputs, juce_audio_devices | Linux and Raspberry Pi; web UI |
| `pacemaker_eval` | console app | engine | offline evaluation CLI |
| `PacemakerTests` | console app | all libs | regression harness (RS-05) |

Options: `PACEMAKER_WITH_LINK` (ON), `PACEMAKER_BUILD_CLAP` (ON),
`PACEMAKER_BUILD_HEADLESS` (OFF on Windows), `PACEMAKER_SYSTEM_JUCE`,
`JUCE_TAG`.

## 2. Threads

| Thread | Owner | Does | Must not |
|---|---|---|---|
| Audio | host or `AudioDeviceManager` | `ClockMap` update, engine `process()`, Link audio-session commit, Sync bus render, snapshot publish, onset/beat event push | allocate, lock, log, touch MIDI or sockets |
| Link network | `ableton::Link` | peer discovery, timeline gossip | |
| Clock | `ClockThread` (realtime priority) | MIDI clock tick scheduling and send | read anything but the seqlock and atomics |
| OSC | `OscThread` | drains beat events, sends OSC | |
| Message | JUCE | UI, settings, calibration state machine, device and port selection, Link enable, publisher arbitration | block |
| Calibration worker | `ThreadPool` job | cross-correlation of captured clicks | |

## 3. Data flow and synchronisation primitives

- `BeatMapSnapshot` (ES-01 section 2.2): written by the audio thread,
  read by all. Implemented as a seqlock (`crill::seqlock_object<T>` or an
  in-house 2-counter seqlock; T is trivially copyable, 64 bytes).
- Event queues: `OnsetEvent` and `BeatEvent` through a single-producer
  single-consumer FIFO (`farbot::fifo` or `choc::fifo`) of capacity 1024,
  drained by the OSC thread which fans out to the UI through a second FIFO.
  Overflow drops the oldest and increments a counter shown in diagnostics.
- Settings to audio: `farbot::RealtimeObject<EngineSettings,
  nonRealtimeMutatable>`; the audio thread picks up a new settings object
  at block start.
- Output status to UI: `std::atomic` fields in `OutputStatus`.
- Calibration capture: the audio thread writes the input into a
  pre-allocated 2 s ring; the worker reads after the capture flag flips.

## 4. Buses (plugin)

```
BusesProperties()
  .withInput ("Main",      stereo, true)    // drum bus / overheads / full mix
  .withInput ("Trigger",   mono,   false)   // aux: kick or snare mic or trigger
  .withInput ("Kick",      mono,   false)   // Reaper, Bitwig, AudioPluginHost only
  .withInput ("Snare",     mono,   false)
  .withOutput("Main",      stereo, true)    // pass-through or silence
  .withOutput("Sync",      stereo, false)   // pulse + click (ES-02 section 5)
```

`isBusesLayoutSupported` accepts: Main mono or stereo; any subset of the
optional inputs, each mono or stereo; Sync stereo only. The processor maps
bus channels to `InputRole`s through a `RoleMap` in state; defaults: Main
to Any (or Overhead when the user says so), Trigger to Kick, Kick to Kick,
Snare to Snare. Hosts that expose one sidechain (Live, Logic, Cubase, Pro
Tools) get Main plus Trigger. The UI shows which roles are live.

MIDI input bus: accepted in all formats; note-ons on configurable notes
(default GM kick 36, snare 38/40, hi-hat 42/44/46, plus "any note") become
`DiscreteOnset`s with role from a note-to-role table. A configurable note
or CC acts as Tap, Downbeat-now, Follow toggle, Nudge.

## 5. Standalone audio and MIDI

Replaces JUCE's `StandaloneFilterWindow` with a custom shell:

- Device panel listing every input channel individually (not stereo
  pairs), with a role dropdown and a distance field per channel; output
  device and the Sync bus channel pair.
- `AudioDeviceManager` configured with the exact input channel set; the
  callback assembles role buffers before calling the engine.
- MIDI inputs: any number, for e-drum modules and footswitches.
- The standalone owns `LinkService`, `ClockThread` and `OscThread`
  directly.

## 6. Headless build

`pacemaker_headless` links no `juce_gui_basics`. Configuration comes from
a JSON file and an embedded HTTP/WebSocket server (cpp-httplib, MIT)
serving a single static page with live status. Runs as a systemd service
on Raspberry Pi OS (RS-03).

## 7. State

Plugin state (saved with the session): parameters (ES-01 section 11),
`RoleMap`, output enables and offsets, tap-along calibration, OSC profile
and destination, MIDI port name, Link quantum.

User settings (global, `ApplicationProperties`): loopback calibration
table, virtual MIDI port preference, UI theme, last devices.

Unknown or stale keys in saved state are ignored safely. State version is
an integer; migrations are explicit functions.

## 8. Error handling

- Missing MIDI port at load: output shows "port not found", keeps the name
  and retries every 5 s.
- Link licence or build flag off: Link section hidden.
- Audio device loss in standalone: engine goes Idle, outputs enter Hold at
  the last tempo, UI shows a red banner. MIDI clock keeps running at the
  frozen tempo so hardware does not stop mid-song.
- Any output exception is caught in its own thread, logged and shown; the
  audio thread never sees it.

## 9. Test requirements

- A1. pluginval strictness 5 passes for VST3 and AU; clap-validator passes;
  lv2lint passes.
- A2. Thread sanitizer build of `PacemakerTests` runs the engine, clock
  thread and OSC thread concurrently for 60 s without reports.
- A3. Bus layout matrix: every combination accepted by
  `isBusesLayoutSupported` processes 10 s without assertion.
- A4. State round trip: save and load restores every field; loading a
  state with unknown keys and a future version does not crash.
