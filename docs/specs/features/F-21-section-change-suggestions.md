# F-21: Section change suggestions

Status: draft 1.0 · Priority: P3 · Estimate: 15 pd · Depends on: F-08 (energy, percussion stats), F-02 data · Related: DIFFERENTIATION rank 11

## 1. Goal and user value
Lighting and visuals benefit from knowing when a song changes section (verse to chorus, build, drop, breakdown). Pacemaker already sees energy, density and pattern; it can suggest section changes as OSC events. Suggestions only: never auto-fire anything critical.
## 2. Requirements
- R-1 Features per bar: loudness, onset density, pattern novelty (distance from previous bars' groove grid), hi-hat/cymbal activity, fill presence, harmonic change rate (if F-08 chords are on), silence.
- R-2 Boundary detector on bar-synchronous features: novelty curve with a checkerboard kernel over 8 bars (self-similarity), peak picking with a minimum section length of 4 bars, plus rule-based triggers (fill followed by pattern change, silence then re-entry, density doubling).
- R-3 Labels: `build`, `drop` (energy jump at least +6 dB after a build), `breakdown` (density or loudness drop at least 8 dB), `change` (generic), `ending` (decreasing energy, ritardando), with confidence.
- R-4 Output: OSC `/pacemaker/section s label f confidence i bar` sent at the predicted bar line (one bar of lead time is not possible for unknown future; the event fires at the first bar of the new section, plus an optional `pre-fill` event when a fill begins, as a hint); UI shows a timeline of sections.
- R-5 Learning from the setlist: if a song has stored section bars from a previous take, boundaries are predicted ("bar 17 is probably a chorus") with a confidence; the learned map is stored per song and editable.
- R-6 Safety: events are suggestions with confidence; mappings that fire actions require the user to choose a minimum confidence (default 0.7) and have a "dead time" (default 4 bars).
- R-7 Evaluation on annotated corpus excerpts (section markers from F-02 R-4): boundary F-measure at +-1 bar >= 0.6 for rehearsed band material; false triggers per song <= 2 at default settings.
## 3. Design
Bar feature extractor consuming F-08 snapshots, novelty detector and rule engine as pure classes with deterministic tests; per-song memory in the song map; outputs through the OSC generator. 
## 4. Tests
F21_R2 novelty on synthetic arrangements (known boundaries); F21_R3 labelling rules; F21_R5 prediction from a previous take; F21_R6 dead time; F21_C corpus metric (R-7); OSC golden.
## 5. Plan
| Task | pd |
|---|---|
| Bar features and novelty detector | 4 |
| Rules and labels | 3 |
| Song memory and prediction | 3 |
| UI timeline, OSC, settings | 3 |
| Corpus annotations and tuning | 2 |
Risks: poor accuracy on dynamic music (suggestion-only, confidence gates), annotation cost.
