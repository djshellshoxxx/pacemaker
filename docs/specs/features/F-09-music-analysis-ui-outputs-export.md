# F-09: Music analysis UI, outputs and export

Status: draft 1.0 · Priority: P2 (v1.1) · Estimate: 20 pd · Owner: apps/insight
Depends on: F-08 phases (cards ship as each phase lands) · Related: RS-01, RS-07, F-22, F-17

## 1. Goal and user value
Show the music information from F-08 clearly on stage and in rehearsal, let other gear use it (lighting colours by key and chord, DAW sessions), and export a session sheet
(chord chart, key, tempo map, melody MIDI). The panel is called **Insight**.

## 2. Scope and non-goals
In: Insight panel (web and plugin), card designs, settings, overrides, OSC and API outputs, export (CSV, JSON, SMF, chart as HTML/PDF-ready), accessibility, stage layout.
Out: engine algorithms (F-08), MIDI output of chords to synths (possible later).

## 3. Requirements
- R-1 Insight is a tab or drawer; in stage mode a compact strip shows Key and Chord only (large type, readable at 2 m).
- R-2 **Key card**: tonic and mode large ("A minor"), confidence ring, relative key in smaller type, Camelot code, tuning offset ("+12 cents"), scale notes strip, "Best effort" tooltip explaining confidence; dash with the message "Listening for harmony" when below threshold; **Lock key** control and manual key picker.
- R-3 **Chord card**: current chord symbol, Roman numeral, confidence, mini keyboard/fretboard highlighting chord tones, timeline of the last 8 bars aligned with beat ticks (chord blocks, uncertain ones hatched), optional "next bar" is not predicted.
- R-4 **Pitch/tuner card**: note name, cents needle and numeric Hz, voicing indicator, stability trace; tuning reference setting.
- R-5 **Spectrum card**: log-frequency spectrum with note labels, peak markers and harmonic ladder of the dominant pitch; freeze button; H1 to H8 bars; timbre readouts (centroid, flatness).
- R-6 **Melody card**: piano-roll of the last 8 bars quantised to the beat grid, raw timing toggle, "Capture to MIDI" button (records the last N bars).
- R-7 **Percussion card**: drum grid (pieces by 16 steps, velocity as cell brightness, last 4 bars), per-piece counts and dynamics, swing %, microtiming bars ("snare +6 ms, kick -2 ms"), feel label, fill indicator, tempo stability (SD ms) and drift arrow (rushing, dragging, steady).
- R-8 **Drum tuner mode**: choose a drum, hit it, see Hz, note, cents, decay; lug list with spread; clear and export.
- R-9 **Energy and loudness card**: short-term loudness meters per input and mix, 60 s energy sparkline.
- R-10 **Tempo toolbox**: delay and LFO values for the current BPM, copy buttons.
- R-11 Every card has an enable switch and a help line; disabled cards cost no CPU (the engine feature is off).
- R-12 Outputs: OSC `/pacemaker/key s`, `/pacemaker/key/hue f` (circle-of-fifths hue 0..1), `/pacemaker/chord s`, `/pacemaker/chord/root i`, `/pacemaker/chord/quality s`, `/pacemaker/chord/numeral s`, `/pacemaker/pitch f hz i midi f cents`, `/pacemaker/feel s`, `/pacemaker/swing f`, `/pacemaker/energy f`, on change plus 1 s heartbeat; per-address enable; HTTP `/api/music` JSON.
- R-13 Export: session sheet (HTML) with title, key, tempo, chord chart per bar, section markers, drift summary; `chords.csv` (bar, beat, chord, confidence); `melody.mid` (notes quantised, tempo map from drift log); `percussion.json` (stats per song); all from `/api/export/*`.
- R-14 Song map link: key, chord chart and melody capture can be stored with a song entry as notes for next rehearsal; "Use as reference" sets the reference key for the chord prior.
- R-15 Accessibility and design: same tokens as the rest of the UI, colour is never the only carrier (labels and patterns), `aria-live` politeness for key and chord changes limited to 1 announcement per 5 s, reduced-motion respected.
- R-16 Performance: UI redraw under 4 ms per frame, updates driven by snapshot deltas under 20 kB/s.

## 4. Design
Server API: `music` block appended to status (deltas for fast-changing items, full snapshot on request); notes and chords as id-ordered lists as with beats. Chord timeline and piano roll are canvas components sharing the beat-grid math of the trace. Plugin editor gets the same cards as JUCE components fed by `MusicSnapshot`. OSC additions plug into `OscGenerator` with their own rate limiter. Export endpoints render from the in-memory history (bounded, 50 000 chord rows, 200 000 note rows).

## 5. Test plan
F09_R2/R3 screenshot goldens with canned snapshots (known, uncertain, none); F09_R12 OSC golden stream; F09_R13 export golden files (CSV, SMF parsed back, HTML), SMF validated by a parser and by `mido` in a CI script; F09_R15 axe-style accessibility checks via Playwright; F09_R16 render benchmark; F09_E2E replay of a corpus M file through the server and checking cards over time with Playwright; F09_R8 drum tuner flow on synthetic hits.

## 6. Acceptance criteria
In the demo, Insight shows key, chord, melody and percussion cards updating in sync with the trace; a lighting patch reacts to `/pacemaker/key/hue`; exporting the session produces a chart and MIDI file that open correctly in a DAW.

## 7. Development plan
| # | Task | pd |
|---|---|---|
| U-1 | Status/API extension, history store, export endpoints | 3 |
| U-2 | Key, chord, tuner cards (web) | 4 |
| U-3 | Spectrum and harmonic cards, timbre, tempo toolbox | 3 |
| U-4 | Percussion card and drum tuner mode | 4 |
| U-5 | Melody card and capture/export | 2 |
| U-6 | OSC outputs, settings, per-card enables | 1.5 |
| U-7 | Plugin editor port of cards | 2.5 (shared with F-03 UI work) |
Cards land with their F-08 phase (spectrum/tuner/percussion with phases 1 and 2, key with 3, chord with 4, melody with 5). Risks: visual clutter on small screens (drawer, tabs, stage strip); users trusting wrong output (confidence UI, dash state, "best effort" wording).
