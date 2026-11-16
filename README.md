# PACEMAKER

**The band sets the tempo. The machine keeps up.**

Pacemaker listens to a live drummer (kick and snare mics, overheads, e-drum
MIDI or triggers) and continuously predicts the band's tempo, beat phase
and bar position. It publishes that clock as **Ableton Link**, **MIDI
clock**, **OSC** and an **audio sync pulse**, so backing tracks, hardware
sequencers, lights and video follow the musicians instead of the musicians
following a click.

The intended deliverables are VST3 / CLAP / AU / LV2 plugins, a standalone application and, later, a headless Raspberry Pi appliance. **These formats are planned, not currently shipped.** The current root CMake configuration builds the C++ engine, offline evaluation CLI and tests. JUCE 8, C++17, CMake, same toolchain as
[Vivisect](https://github.com/djshellshoxxx/faultline).

Status: engine core (ES-01) implemented as a plain C++17 library with tests and an offline evaluation CLI; no plugin yet.

Build: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build`

## Documents

- `docs/research/` — deep research notes with sources
  (`00-index.md` summarises them)
- `docs/specs/ES-*.md` — detailed engineering specs for the hard parts:
  tracking engine, clock outputs, latency calibration, architecture
- `docs/specs/RS-*.md` — regular specs: product and UX, plugin formats and
  hosts, standalone and appliance, state and presets, testing and release,
  licensing and distribution
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

## Verified build and test status

On Linux, the root CMake configuration (`-G Ninja` or the default generator) configures, builds and passes its single CTest test (`ctest --test-dir build`). The plugin, appliance and CLI-to-DAW paths are not part of this build.

## Wiki

Wiki source pages are in [docs/wiki/](docs/wiki/Home.md): developer guide, architecture summary and codebase map. The specifications in `docs/specs/` remain the authority.
