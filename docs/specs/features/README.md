# Feature specs index

Each feature spec follows one template (`_TEMPLATE.md`): goal, scope,
numbered requirements (R-n, each testable), design, test plan (IDs), acceptance
criteria and a development plan with tasks, estimates (person-days, pd, one
experienced C++ audio developer), dependencies and risks. Engineering
rules shared by all features are in `docs/dev/QUALITY-STRATEGY.md`; the
schedule is in `docs/dev/DEVELOPMENT-PLAN.md`.

Priority: **P0** blocks v1.0, **P1** v1.0 if time allows, **P2** v1.1,
**P3** v1.2 and later, **Spin** separate product.

| ID | Feature | Pri | Est (pd) | Depends on |
|---|---|---|---|---|
| F-01 | Real audio input and device layer | P0 | 18 | F-30 |
| F-02 | Real-drummer validation corpus and scoring | P0 | 25 | |
| F-03 | Plugin (VST3/AU/CLAP/LV2) and standalone apps | P0 | 45 | F-01, F-30 |
| F-04 | Ableton Link output | P0 | 14 | F-30, licence |
| F-05 | Cross-platform MIDI clock (macOS, Windows, Linux) | P0 | 20 | F-30 |
| F-06 | E-drum and trigger MIDI input | P0 | 10 | F-01 |
| F-07 | Bar tracker Learn mode and odd meters | P0 | 14 | F-02 |
| F-08 | Music analysis engine (key, chords, pitch, harmonics, melody, percussion) | P2 | 75 | F-01 |
| F-09 | Music analysis UI, outputs and export | P2 | 20 | F-08 |
| F-10 | Count-in launcher | P1 | 6 | F-05 |
| F-11 | Drummer visual metronome page | P1 | 5 | |
| F-12 | MIDI learn and footswitch defaults | P1 | 8 | F-06 |
| F-13 | Guided first run | P1 | 6 | F-01 |
| F-14 | Diagnostics and health | P1 | 7 | F-01 |
| F-15 | Setlist import (AbleSet and generic) | P2 | 4 | |
| F-16 | Per-song profile and humanise limits | P2 | 5 | |
| F-17 | Tempo map export and DAW send | P1 | 8 | |
| F-18 | Dual-instance redundancy | P3 | 15 | F-03, F-04 |
| F-19 | Pacemaker Box (Raspberry Pi appliance) | P3 | 30 | F-01, F-05 |
| F-20 | Windows MIDI clock utility | Spin | 12 | F-05 |
| F-21 | Section change suggestions | P3 | 15 | F-08 |
| F-22 | Lighting and video OSC presets | P2 | 8 | |
| F-23 | Learned onset detector | P3 | 30 | F-02 |
| F-24 | Tempo map from a drum stem (offline app) | Spin | 8 | F-17 |
| F-25 | Theatre score following (research only) | P3 | 5 | |
| F-26 | Licensing and distribution implementation | P0 | 12 | |
| F-27 | Host guides, install docs, bundled scripts | P1 | 10 | F-03 |
| F-28 | Audio-pulse clock plugin and latency calibrator | Spin | 10 | F-03 |
| F-29 | Quality infrastructure | P0 | 20 | |
| F-30 | Foundation: HostClock, virtual time, real-time safety harness | P0 | 6 | |

Totals: v1.0 scope (all P0 and P1) 234 pd; P2 additions 112 pd; P3 and Spin 125 pd; everything 471 pd.
