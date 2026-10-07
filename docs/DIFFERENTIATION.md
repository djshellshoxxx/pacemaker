# What makes Pacemaker different, and what else it could do

Research basis: `docs/research/03-competitors-and-user-needs.md`.

## 1. The field today

| | Ableton Tempo Follower (Live 11.1+) | BeatSeeker (Max for Live, 2015) | B-Keeper (academic) | Pacemaker |
|---|---|---|---|---|
| Hosts | Live only | Live Suite only | Live via Max | any VST3/CLAP/AU/LV2 host, standalone, Raspberry Pi |
| Apple Silicon | yes | officially not compatible | n/a | yes |
| Inputs | one external audio input | one audio track | kick and snare | kick, snare, hat, overhead, trigger, e-drum MIDI, tap; weighted fusion |
| Outputs | sets Live tempo; Link and Ext sync greyed out while following | sets Live tempo | sets Live tempo | Link, MIDI clock with SPP, OSC, audio pulse, host transport, all at once |
| Beat phase and bar | tempo only | tempo only | phase, no bar | tempo, phase, bar, downbeat, confidence |
| Fills and 6/8 | documented oscillation on 6/8; erratic on full kit | locks to first tempo and drifts | gating windows | expectancy gating, hold and chase states, meter patterns |
| Operator control | one Follow button | mode switch | parameters | Follow, Tap, Downbeat now, Nudge, Half, Double, Relock, confidence meter |
| Count-in | none | none | none | detects stick clicks, sets bar 1 |
| Calibration | latency slider | none | none | loopback and tap-along wizards, per-role mic distance |
| Post-show | none | none | none | drift log, tempo map export |

## 2. The eight differentiators

1. **Bar and downbeat awareness.** Lights and clips launch on "one", not
   just at the right speed.
2. **Multi-input fusion.** Fixes the two loudest Ableton complaints:
   overhead too quiet when only hi-hat is playing, and triggers that do
   not work.
3. **Confidence gating with Hold and Chase.** Fills, breakdowns and 6/8
   do not throw the tempo; the operator can see why.
4. **Outputs while following.** Link, MIDI clock and OSC simultaneously.
   Live itself cannot.
5. **Host-agnostic.** Logic, MainStage, Cubase, Bitwig, Reaper, QLab,
   hardware grooveboxes and modular.
6. **Standalone box.** No laptop on stage. No commercial equivalent.
7. **Current platforms.** Apple Silicon, Windows, Linux.
8. **Lighting and video output.** Beat, bar and downbeat as OSC with
   timetags and receiver profiles.

## 3. Additional features, ranked by value divided by effort

| Rank | Feature | Why | Effort | Target phase |
|---|---|---|---|---|
| 1 | Song map with reference tempo and meter per song | kills octave and half-time errors; mirrors Live's reference tempo | low | 1 |
| 2 | Confidence meter, Lock/Free footswitch, nudge | operator trust; replaces the Nudge-button workaround | low | 1 |
| 3 | E-drum and trigger MIDI input mode | repeatedly requested; Live cannot | low | 1 |
| 4 | Visual metronome page for the drummer (phone browser) | closes the feedback loop; cheap | low | 2 |
| 5 | Count-in launcher: fire a MIDI note or OSC on bar 1 after a count-in | starts the song at the drummer's tempo | medium | 2 |
| 6 | Tempo map export (MIDI tempo track, Reaper, Logic, Cubase) | no live tool does it; producers ask for it | low | 2 |
| 7 | Drift report after the show | rehearsal review; MDs and worship teams | low | 2 |
| 8 | Time-signature patterns and Learn mode | the 6/8 failure thread | medium | 1 |
| 9 | Pacemaker Box on Raspberry Pi | market gap; buyers already pay four figures for stage reliability | high | 3 |
| 10 | Dual-instance redundancy (two machines agree, one publishes) | pro rigs expect redundancy | medium | 3 |
| 11 | Section change suggestions (energy and pattern change) to OSC | nice for lights; risky to auto-fire | high | 4 |
| 12 | Learned activation stage (in-house CRNN) | only if DSP stack is insufficient on syncopated material | high | 4 |
| 13 | Score following for theatre | huge; defer, partner with QLab markers | very high | not planned |

## 4. Spin-off products that reuse the code

- **Tight MIDI clock for Windows**: Link in, scheduled MIDI clock out via
  Windows MIDI Services. ES-02 section 3 as a tray app.
- **Audio-pulse clock plugin**: ES-02 section 5 on its own, from host
  transport, as a software alternative to E-RM multiclock.
- **Latency calibrator**: ES-03 as a standalone utility that writes DAW
  recording offsets.
- **Tempo map from a drum stem**: the engine offline, as a drag-and-drop
  app.
- **Count-in launcher**: supervisor state machine plus one MIDI note.
