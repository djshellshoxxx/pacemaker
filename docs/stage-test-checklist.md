# Manual stage test checklist (RS-05 section 1.5)

Run before every release (RS-05 section 4). Print it or copy it to `docs/stage-test-logs/<version>-<date>.md` and fill
in. A release needs every **Required** row PASS, or a written waiver with owner and date.

Pass criteria come from the specs: engine thresholds ES-01 section 12 (T1 to T5, T9), output tests ES-02 section 9
(O1 to O5), calibration ES-03 section 8 (L3 and L4: within 2 ms after calibration), jitter targets ES-02 section 3
(0.3 ms macOS/Linux, 1 ms Windows). Where the specs give no number, the criterion below is marked **(proposed)**.

## Run header

| Field | Value |
|---|---|
| Pacemaker version / commit | |
| Date, venue/room | |
| Tester(s), drummer | |
| OS and build (Win/macOS/Linux, arch) | |
| Audio interface, buffer size, sample rate | |
| Calibration done for this device? (Y/N, values) | |
| Network (wired/Wi-Fi, switch) | |

## Section A: real kit, mics

| # | Scenario | Setup | Pass criterion | Req | Result | Measured | Notes |
|---|---|---|---|---|---|---|---|
| A1 | Real kit, steady 4/4 at 90, 120, 160 BPM, 3 min each | kick + snare + overhead mics (standalone) | Locked within 2 bars; tempo within 1 BPM of reference tap; no Relock for 3 min; confidence arc stays above gate (0.35) (ES-01 T1, proposed for room mics) | Y | | | |
| A2 | Same with **two mics only** (kick + overhead) | host sidechain baseline: Main overhead, Trigger kick | Same as A1; Lock time recorded | Y | | | |
| A3 | Fill test: 2 bars of 16th snare rolls inside steady groove | A1 setup | tempo deviation during fill <= 1 BPM; no Relock (ES-01 T3) | Y | | | |
| A4 | Breakdown: stop playing 6 s then re-enter on the beat | A1 setup | state Hold then Locked, no Relock (ES-01 T4) | Y | | | |
| A5 | Count-in: 4 stick clicks at 110 BPM then play | A1 setup | Locked within 1 beat; bar 1 is first played beat (ES-01 T5) | Y | | | |
| A6 | Ramp 120 to 132 BPM over 8 bars (drummer follows metronome in headphones that is **not** the output) | A1 setup | tempo within 1 BPM at the end; no octave error (ES-01 T2) | Y | | | |
| A7 | Half-time / 6-8 song with song map entry | pattern selected | bar tracker confirms within 4 bars; no half/double switch (ES-01 T6) | N | | | |
| A8 | Loud stage: bleed (wedge + guitar amp) with gate on | A1 setup | no false lock on guitar; Hold rather than wandering | Y | | | |

## Section B: hosts

| # | Scenario | Setup | Pass criterion | Req | Result | Measured | Notes |
|---|---|---|---|---|---|---|---|
| B1 | **Ableton Live** (11 or 12) with plugin on drum bus | docs/hosts/live.md, Link on, Tempo Follower off, Audio From = kick | Live tempo within 0.1 BPM of Pacemaker after a 120 to 126 ramp within 2 s (ES-02 O1); warped clips follow; no "Another app" warning | Y | | | |
| B2 | Live: Link phase | Link peer (example app or second laptop) | `phaseAtTime` at predicted beat within 0.01 beats after Relock (ES-02 O1) | Y | | | |
| B3 | **Logic Pro** (10.7.5+) with AU on drum bus | docs/hosts/logic.md, Link on | Logic tempo follows within 0.1 BPM; AU loads in Logic after rescan; Side Chain input works | Y | | | |
| B4 | Plugin validation | pluginval strictness 5 (VST3, AU), clap-validator, lv2lint | zero failures (RS-05 layer 4) | Y | | | |
| B5 | Offline bounce | bounce a drum stem in a DAW | outputs silent, drift log written (RS-02 section 6) | N | | | |

## Section C: hardware box on MIDI clock

| # | Scenario | Setup | Pass criterion | Req | Result | Measured | Notes |
|---|---|---|---|---|---|---|---|
| C1 | **One hardware box** (name: ______) on MIDI clock from Pacemaker | Pacemaker MIDI clock to the box's DIN/USB; box set to external clock | box tempo tracks drummer; ticks received at 24 ppqn | Y | | | |
| C2 | Clock jitter, loopback or scope | ES-02 O3 loopback; or box display | inter-tick SD <= 0.3 ms (macOS/Linux), <= 1.5 ms Windows (ES-02 O3) | Y | | | |
| C3 | Hardware calibration | metronome loopback, set `midiClockOffsetMs` | box clicks land within 2 ms of the audio pulse after calibration (ES-03 L4; ES-02 O4: within 1 ms of the Sync bus) | Y | | | |
| C4 | Start/Stop and SPP | Start on first Lock; SPP only on Relock if enabled | Start at first beat; no gap when `sppOnRelock` off | N | | | |
| C5 | Windows virtual port | loopMIDI port | clock reaches a receiving app | Y (if Windows build) | | | |

## Section D: lighting app on Link (and OSC)

| # | Scenario | Setup | Pass criterion | Req | Result | Measured | Notes |
|---|---|---|---|---|---|---|---|
| D1 | **One lighting/VJ app on Link** (name: ______) | Link on; app joined; peers shown | app tempo within 0.1 BPM of Pacemaker; downbeat lands on "one" within one beat | Y | | | |
| D2 | OSC timetags | listen with `integrations/common/osc_tool.py listen` on the lighting machine | `/beat` bundle timetags monotonic and within 1 ms of the Sync bus click (ES-02 O5) | Y | | | |
| D3 | OSC receiver profile | Resolume or MagicQ profile as applicable | receiver reacts on downbeat (tempo/resync or tap) | N | | | |
| D4 | Network loss | pull Ethernet 10 s, reconnect | outputs recover without restart; tempo holds in Hold | N | | | |

## Section E: operator controls and robustness

| # | Scenario | Pass criterion | Req | Result | Notes |
|---|---|---|---|---|---|
| E1 | Follow button, Hold, Tap, Downbeat now, Nudge, Half, Double, Relock | each works from UI and MIDI mapping (CC 64 Follow, CC 65 Tap) | Y | | |
| E2 | Song select by MIDI program change and by OSC `/pacemaker/song` | song loads bpm/meter/pattern | Y | | |
| E3 | Two instances with Follow on | warning badge shown (RS-02 section 5) | N | | |
| E4 | 2 hour soak | no dropouts, CPU <= 30 percent of one core on Pi / <= 10 percent engine on laptop (ES-01 T8) | Y | | |
| E5 | Trial/licence (when wired) | trial silences outputs 20 s in each 5 min and never the detector; licensed key removes it (docs/licensing/LICENSE-DESIGN.md) | Y | | |
| E6 | Drift log and tempo-map export | CSV written; MIDI tempo map imports into a DAW | N | | |

## Sign-off

| Field | Value |
|---|---|
| Result | PASS / PASS with waivers / FAIL |
| Waivers (row, reason, owner, date) | |
| Release approved by | |
