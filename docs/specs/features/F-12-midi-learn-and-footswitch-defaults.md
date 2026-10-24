# F-12: MIDI learn and footswitch defaults

Status: draft 1.0 · Priority: P1 · Estimate: 8 pd · Depends on: F-06 MIDI input backends · Related: RS-04 section 5, RS-01 section 8

## 1. Goal and user value
Operate Pacemaker hands-free: map any footswitch, pad or controller to Follow, Tap, Downbeat now, Nudge, Half, Double, Relock, Arm count-in, song select. Defaults make a cheap footswitch work without setup.

## 2. Requirements
- R-1 Every action control in the UI exposes "Learn MIDI" (right-click or long press): the next incoming CC, note, or program change binds to it; a visible "Listening..." state times out after 10 s.
- R-2 Binding model: `{device|any, channel|any, type: note|cc|pc, number, mode: momentary|toggle|trigger|value, threshold}`; toggle uses on-edge; value mode maps CC range to a parameter (reference BPM, sensitivity).
- R-3 Defaults (shipped, disabled until a MIDI device is present, shown in a suggestion banner): CC 64 to Follow toggle, CC 65 to Tap (documented footswitch suggestion), program change n to song n (RS-04).
- R-4 Bindings are session settings (RS-04), listed in a Bindings dialog with test, delete, export/import (JSON); duplicate bindings warn; conflicts with note-to-role mappings (F-06) are flagged because notes can be both hits and commands: command notes need a separate channel or device filter.
- R-5 OSC bindings: `/pacemaker/action <name>` and `/pacemaker/song <i>` are always available (RS-01 section 6) and listed in the dialog.
- R-6 Debounce 30 ms for footswitches, long-press (600 ms) alternative action optional.
- R-7 Feedback: activity shown in the header (last command), LED output to controllers with LED support is a later item.
- R-8 Parser shared with F-06; commands execute on the message thread, engine actions through the existing atomics; no allocation on the audio thread.
- R-9 Keyboard shortcuts configurable in the same dialog (web and plugin).
## 3. Design
`BindingTable` (fixed capacity 128), `MidiCommandRouter::onMessage(const MidiMsg&)` returns an `Action`; UI learn is a state machine (`Idle -> Listening(action) -> Bound`). Persisted in settings `bindings`.
## 4. Tests
F12_R1 learn flow with scripted messages; F12_R2 each binding mode; F12_R3 defaults; F12_R4 conflict detection; F12_R6 debounce and long press; F12_R5 OSC actions; fuzz of binding import; UI screenshot of learn state.
## 5. Acceptance
A user with a generic USB footswitch maps Follow, Tap and Downbeat in under a minute and uses them during a rehearsal without touching the screen.
## 6. Plan
| Task | pd |
|---|---|
| Binding model, router, persistence | 2.5 |
| Learn state machine and UI (web, plugin hook) | 2.5 |
| Defaults, banner, conflicts, OSC actions | 1.5 |
| Tests and docs | 1.5 |
Risks: plugin hosts consuming MIDI (document, standalone recommended); stuck toggles (state shown, reset on connect).
