# RS-02: Plugin formats and host matrix (regular spec)

Status: draft 0.1. Research: `docs/research/02-clock-output-mechanisms.md`,
`docs/research/04-plugin-architecture-and-latency.md`.

## 1. Formats

| Format | Platforms | Built by | Notes |
|---|---|---|---|
| VST3 | Win, macOS, Linux | JUCE | No MIDI clock through host; OS MIDI path used |
| CLAP | Win, macOS, Linux | clap-juce-extensions | MIDI clock also in event output; `transport-control` draft if host offers |
| AU | macOS | JUCE | MIDI output callback for clock bytes |
| LV2 | Linux | JUCE | Atom MIDI output |
| Standalone | all | JUCE custom shell | primary non-DAW product |
| AAX | later | JUCE, requires Avid signing | Pro Tools users have Link; AAX is phase 4 |

## 2. How each host gets tempo

| Host | Tempo path | Clock to hardware | Sidechain inputs | Notes |
|---|---|---|---|---|
| Ableton Live 11/12 | Link | OS MIDI port | 1 | Disable Live's own Tempo Follower; Live greys Link out while its follower is on |
| Bitwig | Link, or controller script via OSC | OS MIDI, or Bitwig MIDI out | multiple | |
| Logic Pro 10.7.5+ | Link | OS MIDI port (Logic cannot slave to MIDI clock anyway) | 1 | |
| MainStage | Link not supported; MIDI clock in | OS MIDI | 1 | MainStage follows incoming MIDI clock |
| Reaper | ReaScript via OSC, or MIDI clock slave | OS MIDI or Reaper MIDI out | up to 32 | |
| Cubase / Nuendo | none live; MIDI clock not slaved | OS MIDI | 1 | Document as "clock out only" |
| Studio One | MIDI clock slave | OS MIDI | 1 | |
| Pro Tools 2020.9+ | Link | OS MIDI | 1 | |
| Reason | Link | OS MIDI | | |
| QLab | Link or OSC | | | cue triggering on downbeat via OSC |
| Resolume, VDMX, TouchDesigner | Link | | | OSC profile for Resolume tempo |

## 3. Sidechain and bus behaviour

Main input plus one aux bus is the baseline (ES-04 section 4). The UI
reports which roles are connected. In hosts with a single sidechain, the
recommended setup is: Main = drum bus or overheads, Trigger = kick mic.

## 4. Latency reporting

The plugin reports `getLatencySamples() = 0`. It does not delay audio; the
pass-through is unprocessed. The detector delay is internal to the beat
prediction and is not a plugin delay.

## 5. Multiple instances

Allowed. One shared Link instance per process; the publisher is the
instance with Follow on and the highest confidence. Two instances with
Follow on show a warning badge.

## 6. Offline rendering

When the host renders offline (bounce), the plugin still processes and
publishes a snapshot, but all outputs are suppressed. This makes the
"tempo map from a drum stem" export possible in-DAW: the drift log is
written during the bounce.

## 7. Host-specific documentation to ship

- Live: Link setup, disable Tempo Follower, "Audio From" sidechain.
- Logic: Link, Sync bus to an output for hardware via audio-sync box.
- Reaper: install the bundled script, OSC port.
- Bitwig: install the bundled controller script.
- Cubase and Studio One: virtual MIDI port, loopMIDI on Windows.
