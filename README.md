# PACEMAKER

**The band sets the tempo. The machine keeps up.**

Pacemaker listens to a live drummer (kick and snare mics, overheads, e-drum
MIDI or triggers) and continuously predicts the band's tempo, beat phase
and bar position. It publishes that clock as **Ableton Link**, **MIDI
clock**, **OSC** and an **audio sync pulse**, so backing tracks, hardware
sequencers, lights and video follow the musicians instead of the musicians
following a click.

It ships as a VST3 / CLAP / AU / LV2 plugin, a standalone app, and later a
headless Raspberry Pi appliance. JUCE 8, C++17, CMake, same toolchain as
[Vivisect](https://github.com/djshellshoxxx/faultline).

Status: engine core (ES-01), clock outputs and calibration maths (ES-02, ES-03) are plain C++17 libraries with tests, plus an offline evaluation CLI and `pacemaker_server`, a runnable host with a modern web UI and a built-in simulated drummer. No plugin or live audio input yet (Phase 3).

Try it, no audio hardware needed:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/app/server/pacemaker_server        # then open http://127.0.0.1:8080/
```

The page shows state, tempo, bar and beat, confidence, a live trace with predicted beats, inputs, outputs (MIDI clock, OSC), transport buttons with keyboard shortcuts (press `?`), song map, drift report and a stage mode. Use `--bind 0.0.0.0` to open it from a phone on the same network.

Build: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build`

## Documents

- `docs/research/` — deep research notes with sources
  (`00-index.md` summarises them)
- `docs/specs/ES-*.md` — detailed engineering specs for the hard parts:
  tracking engine, clock outputs, latency calibration, architecture
- `docs/specs/RS-*.md` — regular specs: product and UX, plugin formats and
  hosts, standalone and appliance, state and presets, testing and release,
  licensing and distribution
- `docs/specs/RS-07-web-ui-and-control-api.md` — the web UI and control API of `pacemaker_server`
- `docs/DIFFERENTIATION.md` — what makes Pacemaker different and the
  ranked list of additional features
- `docs/PROJECT-PLAN.md` — phases, checklist and risks

## Why it is different

Ableton's built-in Tempo Follower is Live-only, tempo-only, audio-only, and
switches Link off while it runs. BeatSeeker is a 2015 Max for Live device
that Ableton lists as not compatible with Apple Silicon. Nothing exists for
Logic, Cubase, Bitwig, Reaper, QLab or hardware rigs. Pacemaker adds bar
and downbeat awareness, multi-input fusion, confidence-gated hold and
chase behaviour, simultaneous outputs, and a one-button latency
calibration. See `docs/DIFFERENTIATION.md`.
