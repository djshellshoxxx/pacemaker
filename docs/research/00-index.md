# Research index

Deep research notes gathered in October 2026 before writing the specs.
Each file keeps its own Sources list. Items flagged "uncertain" or
"unverified" in the notes should be re-checked against primary docs before
they are relied on in code.

| File | Topic | Key conclusions |
|---|---|---|
| `01-real-time-beat-tracking.md` | Onset detection, tempo induction, phase tracking, downbeat, fills, human timing, evaluation, licences | Event-based two-process tracker (B-Keeper, Repp) plus a real-time PLP tempogram (MIT) is the right stack. All GPL and non-commercial trackers are excluded. Learned online downbeat trackers reach only about 45 to 50 percent F1, so bar tracking uses explicit patterns and overrides. |
| `02-clock-output-mechanisms.md` | Ableton Link API and licence, MIDI clock from plugins and OS, host transport control per format, OSC conventions, timecode, MIDI 2.0, latency alignment | No plugin format can set host tempo except CLAP's draft extension. Link is the primary path; MIDI clock must be generated on an OS port from our own thread because VST3 cannot carry realtime bytes. `forceBeatAtTime` only on relock. |
| `03-competitors-and-user-needs.md` | Ableton Tempo Follower, BeatSeeker, B-Keeper, hardware, forum pain points, pricing, channels | Live's follower is tempo-only, audio-only, and disables Link while active; BeatSeeker is unmaintained and not Apple Silicon compatible; no hardware derives clock from free playing. Loudest complaints: 6/8 oscillation, quiet overheads, triggers unusable, no bar awareness. Price band $29 to $269. |
| `04-plugin-architecture-and-latency.md` | JUCE real-time patterns, host timestamps, multi-input buses, latency reporting per driver, loopback calibration, MIDI output per format, testing, CI, Raspberry Pi, licences | Main plus one sidechain is the host baseline. CoreAudio and USB interfaces hide 1 to 3 ms; loopback calibration is required. JUCE 8 Starter cap is US$20k. Engine must be GUI-free for the headless and eval targets. |

Open questions carried into the specs:

- Exact terms of Ableton's proprietary Link licence (ask).
- Whether JUCE 8 CoreAudio latency reporting still omits the buffer size.
- CLAP `transport-control` host support in Bitwig and Reaper.
- Windows MIDI Services availability in JUCE.
- BeatNet+ licence, in case a learned stage is pursued later.
