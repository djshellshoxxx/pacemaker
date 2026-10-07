# RS-04: State, presets and settings (regular spec)

Status: draft 0.1.

## 1. Three kinds of state

| Kind | Lives in | Examples | Changed by |
|---|---|---|---|
| Parameters | APVTS, saved in session, automatable | follow, profile, gains, tempo range, reference BPM, meter, pattern, sensitivity | user, host automation, presets |
| Session settings | plugin state subtree `PM_SETTINGS`, saved in session, not automatable | role map, output enables and offsets, OSC destination, MIDI port name, Link quantum, song map, tap-along calibration | user |
| User settings | global `ApplicationProperties` | loopback calibration table, virtual port preference, theme, last devices, window size | user, calibration wizard |

Presets change parameters only. Loading a preset never changes session or
user settings (same rule as Vivisect 0.9.3).

## 2. Presets

Factory presets: Rock 4/4 stage, Rock 4/4 rehearsal, Four-on-the-floor,
Half-time, 6/8 ballad, 3/4 waltz, Jazz ride (Any role, high sensitivity,
slow gains), E-drums (Trigger role only), Full mix (Any, reference BPM
required). User presets stored as JSON in the user preset folder.

## 3. Song map file

JSON array of `{name, bpm, meterNum, meterDen, pattern, countInBeats,
programChange}`. Also importable from CSV with those columns. Selecting a
song applies bpm, meter, pattern and count-in length as parameters.

## 4. Versioning and migration

`stateVersion` integer in the root. Loader ignores unknown keys, defaults
missing ones, and applies explicit migration steps between versions.
Loading a newer version than the build knows logs a warning and loads
what it can.

## 5. MIDI mappings

Right-click any control to learn a CC or note, as in Vivisect. Mappings
are session settings. Default mappings: none, except a documented
suggestion for footswitch CC 64 to Follow and CC 65 to Tap.

## 6. Drift log

Written as CSV per session: `timestampUs, bpm, confidence, state,
bar, beat, event`. Location configurable; default user documents. Never
written in offline bounce unless enabled.
