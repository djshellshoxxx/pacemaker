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

Status: specification stage. No code yet.

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
