# F-10: Count-in launcher

Status: draft 1.0 · Priority: P1 · Estimate: 6 pd · Depends on: F-05 (MIDI out), existing supervisor count-in detector
Related: ES-01 section 8, ES-02, F-16

## 1. Goal and user value
The drummer clicks sticks, the band counts in, and the backing track or sequencer starts at that tempo on bar 1. Today the detector sets bar 1; this feature fires the start.

## 2. Requirements
- R-1 Trigger types (each optional): MIDI note (channel, note, velocity), MIDI program change, MIDI CC, OSC message (address, args), MIDI Start (0xFA) plus clock alignment, Link start (F-04), and host transport request where available.
- R-2 Armed per song or globally; "Arm" button and MIDI/OSC remote arm; disarm after firing unless "Rearm after song".
- R-3 Fire on the downbeat that follows the count-in (the first played beat, not the last click), timed from the beat grid with a lookahead (default 40 ms) and a configurable offset (-500 to +500 ms) for the receiving system's latency.
- R-4 Count-in length detected (2 to 8 clicks) or fixed per song (`countInBeats`); behaviour if the count-in is irregular (CV of intervals over 12 percent): no fire, warning "count-in too uneven", Tap or Fire now remain available.
- R-5 Sets the tempo for the receiving system: before firing, sends tempo (Link commit, OSC `/pacemaker/tempo`, and MIDI clock already running); optional "pre-roll bars" before the trigger.
- R-6 Safety: minimum 2 s between fires, "Fire now" and "Cancel" always available, state shown in the header ("ARMED", "FIRED bar 1").
- R-7 Selecting a song with a count-in arms it; the Count-in launcher status is part of the drift log (`event=f`).
## 3. Design
`CountInLauncher` subscribes to supervisor transitions (`CountIn -> Locked`) and beat events; it schedules triggers with the same `OutputScheduler` used by MIDI/OSC (host-time stamped, lookahead). Config in session settings (`launcher` block) with a list of triggers.
## 4. Tests
F10_R3 timing vs beat grid in virtual time (fire within 1 ms of target); F10_R4 irregular count-in; F10_R5 order of tempo and trigger; F10_R6 double-fire guard; F10_E2E corpus count-in excerpts (T12 slice) fire once at bar 1; F10_UI screenshot of ARMED/FIRED states.
## 5. Acceptance
With a hardware sampler/Ableton Live, count-in by a drummer starts the track on the first beat within 15 ms of the drummer's hit, 20 out of 20 times in rehearsal.
## 6. Plan
| Task | pd |
|---|---|
| Trigger model, scheduler integration, settings | 2 |
| Count-in irregularity rules, safety and state machine | 1.5 |
| UI (arm, status, trigger editor) and API | 1.5 |
| Tests and corpus slice | 1 |
Risks: false triggers from stray hits (arming required, irregularity check); receiver latency (offset, calibration helper reuse).
