# RS-01: Product overview and UX (regular spec)

Status: draft 0.1.

## 1. One-line description

Pacemaker listens to a live drummer (or any rhythmic input) and makes the
computer, the hardware and the lights follow the band's tempo, beat and
bar, instead of the band following a click.

## 2. Who it is for

| Persona | Setup | What they need |
|---|---|---|
| Cover or worship band with backing tracks | Ableton, Logic/MainStage, Playback or Loop Community rig | Tracks that follow the drummer; count-in to start songs; never a dropped click |
| Hybrid electronic act with a drummer | Hardware sequencers, modular, Elektron boxes | A MIDI clock that follows the drummer without hunting |
| Theatre pit / musical director | QLab, Ableton, click for the band | Backing stems that breathe with the pit |
| Lighting or VJ operator | grandMA, Resolume, TouchDesigner | Beat phase and downbeat from the real band, as Link or OSC |
| Producer tracking a band | Any DAW | A tempo map of the real performance for editing |

## 3. Products in the line

- **Pacemaker plugin**: VST3, CLAP, AU, LV2. Runs inside the DAW on a
  drum bus or sidechain.
- **Pacemaker standalone**: desktop app with its own audio and MIDI I/O.
  The main product for non-DAW rigs.
- **Pacemaker Box** (phase 3): headless Raspberry Pi appliance with DIN
  MIDI out, Link and a phone web page. See RS-03.

## 4. Main screen

Same house style as Vivisect: dark, high contrast, medical. The waveform
is a heart-monitor trace of the fused novelty curve; detected onsets are
spikes; predicted beats are the grid; the pulse icon flashes on the
downbeat. State is a single large word: LISTENING, COUNT-IN, LOCKED, HOLD,
CHASE.

Layout, top to bottom:

1. **Header**: tempo in large digits (one decimal), meter, bar:beat, a
   confidence arc, and the Follow button (big, toggles Hold).
2. **Trace**: 8 s of novelty with onsets, predicted beats and the
   expectancy windows drawn as faint bands.
3. **Inputs strip**: one tile per role (Kick, Snare, Hat, Overhead, Trigger,
   Any): level meter, onset LED, mic-distance chip, mute.
4. **Outputs strip**: Link (peer count, publisher badge), MIDI clock (port,
   jitter), OSC (destination), Sync bus, each with enable and offset.
5. **Transport buttons**: Tap, Downbeat now, Nudge -, Nudge +, Half, Double,
   Relock. Each MIDI-mappable via right-click, as in Vivisect.
6. **Footer**: profile selector (Stage, Rehearsal, Custom), reference BPM
   field, pattern selector, Calibrate button, settings gear.

## 5. Calibration flow

A modal wizard (ES-03): choose input and output, patch, run, see the
numbers, set mic distances, done. Plugin builds show the tap-along
variant. The footer shows a warning badge until a calibration exists for
the current device.

## 6. Song map (setlist)

A side panel with a list of songs: name, reference BPM, meter, pattern,
optional count-in length. Selecting a song loads those values. Songs can
be selected by MIDI program change, OSC `/pacemaker/song i`, or
`/pacemaker/song/name s`. Import from a CSV and from AbleSet's setlist
export (format to confirm). This is the "reference tempo" feature that
avoids octave errors.

## 7. Drift log and report

Every session records tempo per beat, confidence and state changes to a
CSV in the user documents folder. A "Report" button renders a per-song
tempo curve, max deviation, time in Hold and relock count. Export tempo
map as MIDI file (tempo events) for post-production.

## 8. Accessibility and stage use

- All text at least 14 px; status readable from 2 m.
- Full keyboard control; every action MIDI-mappable.
- "Stage mode" hides everything except header, trace and transport
  buttons.
- No modal dialogs while Locked except calibration, which asks for
  confirmation.

## 9. Copy and naming

Product name: Pacemaker. Tagline: "The band sets the tempo. The machine
keeps up." Avoid the word "click" in positive copy. The trademark search
for "Pacemaker" in audio software (the 2008 Tonium device) is a pre-release
task (RS-06).
