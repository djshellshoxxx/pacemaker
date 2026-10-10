# F-07: Bar tracker Learn mode and odd meters

Status: draft 1.0 · Priority: P0 · Estimate: 14 pd · Owner: engine
Depends on: F-02 data for tuning (synthetic first) · Related: ES-01 section 7, RS-01, F-16

## 1. Goal and user value
Downbeat ("one") is the feature that lets lights and clips launch on the right beat. The shipped patterns cover common 4/4, 3/4 and 6/8 grooves, but real songs
use their own patterns, syncopation, and odd meters (5/4, 7/8, 9/8, 12/8). Learn mode lets the band teach Pacemaker one groove in two bars; grouped odd-meter tables make 5/4 and 7/8 work out of the box.

## 2. Scope and non-goals
In: finer-grained evidence (subdivisions per beat), pattern representation, rotation-correlation bar tracker, Learn mode (record, validate, save), grouped meter tables, pulse mode for compound meters, meter changes at song bar numbers, pattern editor and library, metrics.
Out: automatic meter detection without user input (a later research item), mixed-meter songs beyond scripted changes, learned neural downbeat model (F-23).

## 3. Requirements
- R-1 `BeatEvidence` carries, per closed beat, onset strength by band tag at 4 sub-slots (16th grid) or 3 sub-slots for compound meters, plus a strong-hit flag; the old coarse call remains for shipped tables.
- R-2 `PatternTemplate { beats, subdivisions, kick[beats*sub], snare[beats*sub], hat[beats*sub], strong[beats] }`; all shipped patterns are expressed in this form.
- R-3 Bar position score per hypothesis `h` (rotation) is the normalised correlation of exponentially forgotten evidence with the template rotated by `h`; accept when the margin over the runner-up exceeds `m` for 2 bars (existing rule), margin scaled to template distinctiveness.
- R-4 Learn mode: user presses Learn (or the engine auto-arms after a count-in); the next `N` bars (default 2, range 1 to 8) are recorded; the user marks bar 1 with Downbeat now, by the count-in, or by a tap; the template is the mean of the evidence per grid position, normalised. Requirements: at least 8 onsets per bar and a tracker in Locked state; otherwise Learn aborts with a reason.
- R-5 Distinctiveness: the template's self-correlation across rotations is computed; if the best non-trivial rotation exceeds 0.9 the UI says "pattern repeats every beat/half bar: downbeat ambiguous" and recommends Downbeat now or a longer Learn window.
- R-6 Learned patterns are named, stored in the pattern library (user settings), assignable per song (song map field `patternId`), exportable as JSON, and editable in a step grid (toggle kick, snare, hat per slot).
- R-7 Odd meters: grouped table generator `makeGroupedTable(groups)` produces kick weight on group starts and snare/accents on secondary group starts; shipped groupings: 5/4 (3+2) and (2+3), 7/8 (2+2+3), (3+2+2), (2+3+2), 7/4 (4+3), 9/8 (3+3+3) and (2+2+2+3), 12/8 (compound 4), 6/4, 3/2; user can enter any grouping.
- R-8 Pulse mode: `Subdivision` (tracker pulse is the eighth for /8 meters, existing) or `Group` (pulse is the dotted-quarter or group unit, bar positions are group numbers); selected per song; tempo range limits follow the pulse (maximum 300 BPM for eighth pulse).
- R-9 Meter map: song map entries may carry `meterMap: [{bar, num, den, grouping}]`; the bar tracker switches template at the given bar numbers (bar counter continues) and also on OSC `/pacemaker/meter n d`.
- R-10 Learn and pattern edits never change the tempo tracker; applying a new template reuses current beat phase (no Relock).
- R-11 Real-time: evidence update and scoring cost under 5 microseconds per beat; no allocation.
- R-12 UI: Learn button with progress ring and bars counter, pattern view (grid), meter/grouping selector, ambiguity warning, "Downbeat now" stays one tap away; web and plugin versions.

## 4. Design
Evidence is collected in `Tracker` when closing a beat: onsets within +-0.5 beat of the predicted grid are assigned to sub-slot `round(phase * sub)`; strengths per band tag accumulate with velocity weighting. `BarTracker::closeBeat(const BeatEvidence&)` updates `E[b][slot][class]` with forgetting 0.9 per beat and computes `score[h] = <E, rotate(T,h)> / (|E||T|)`. The shipped coarse tables are lifted into templates by placing weight at slot 0 of the beat. Learn state machine: `Idle -> Arming -> Recording(bars) -> Validating -> Done/Aborted`. Distinctiveness uses rotation autocorrelation of the template.
Data format:
```json
{ "id": "learned-001", "name": "Verse groove", "beats": 4, "subdivisions": 4,
  "kick": [1,0,0,0, 0,0,0,0, 0.8,0,0,0, 0,0,0.4,0], "snare": [...], "hat": [...] }
```

## 5. Test plan
| ID | Method | Pass |
|---|---|---|
| F07_R3 | Rotation scoring unit tests with known templates | correct rotation; margin as computed |
| F07_M1 | Synthetic 5/4 (3+2 and 2+3), 7/8 (three groupings), 9/8, 12/8, 6/8 with 10 ms jitter, 4 BPM drift | beat F >= 0.97; bar-position accuracy >= 0.85 after 4 bars |
| F07_L1 | 50 random synthetic 4/4 patterns (random kick/snare placement) learned over 2 bars then played for 16 bars | downbeat found in >= 90 percent |
| F07_L2 | Symmetric pattern (kick every beat) | ambiguity flagged; Downbeat now works |
| F07_L3 | Learn aborts on silence or unlocked tracker | reason shown |
| F07_R9 | Meter map 4/4 to 7/8 at bar 9 | position correct from bar 10; no Relock |
| F07_R11 | CPU micro-benchmark | within budget |
| F07_C | Corpus T13 slice and added odd-meter excerpts (F-02 R-2) | T13 met |
| F07_ST | Serialisation round trip, fuzz of pattern JSON | no crash |

## 6. Acceptance criteria
With a real drummer playing a syncopated groove (kick on 1, and of 2, 4), Learn finds bar 1 within 2 bars of learning and holds it through a fill; 7/8 (2+2+3) song locks with correct "one" within 4 bars; T13 passes.

## 7. Development plan
| # | Task | pd | Notes |
|---|---|---|---|
| B-1 | `BeatEvidence`, sub-slot accumulation in `Tracker` | 2 | keep T1 to T7 passing |
| B-2 | Template type, lifting of shipped tables, rotation scoring | 2.5 | |
| B-3 | Grouped table generator and shipped odd meters | 1.5 | |
| B-4 | Pulse modes and tempo range handling | 1.5 | |
| B-5 | Learn state machine, validation, distinctiveness | 2 | |
| B-6 | Meter map and OSC meter command | 1 | |
| B-7 | Library, editor, UI (web) and settings | 2.5 | plugin UI in F-03 |
| B-8 | Tests, synthetic odd-meter generator, corpus slices | 1 | |
Risks: odd-meter beat tracking itself (not just bar position) may fail on tempogram priors (Group pulse mode, per-meter priors); templates over-fit one groove (distinctiveness check, forgetting); real drummers deviate from learned pattern (forgetting, partial-match scoring, hold last position).
