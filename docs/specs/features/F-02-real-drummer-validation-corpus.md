# F-02: Real-drummer validation corpus and scoring

Status: draft 1.0 · Priority: P0 · Estimate: 25 pd (plus studio calendar time) · Owner: engine
Depends on: F-01 task A-2 (file replay) for replays; recording can start now
Related: ES-01 section 12, RS-05 section 2, research 05 section 1 (dataset licences)

## 1. Goal and user value
Every claim ("locks in two bars", "downbeat 85 percent") is currently measured on a synthetic drum
machine. This feature builds the evidence base: recordings of real drummers with trustworthy annotations,
a scorer using standard metrics, thresholds enforced in CI, and a tuning workflow that does not overfit.
Users benefit through a tracker tuned to real playing, honest accuracy figures, and fewer stage failures.

## 2. Scope and non-goals
In: recording protocol, annotation tool and protocol, manifest and storage, scorer (C++ plus Python
`mir_eval`), metrics and thresholds T10 to T16, ratchet baselines, augmentation, tuning workflow, licence ledger,
participant consent. Out: model training (F-23), music-analysis corpora (F-08 section 9).

## 3. Requirements
- R-1 Corpus v1: at least 6 drummers, 3 kits (acoustic, e-kit with audio, electronic with MIDI), 40 minutes
  per drummer, 36 distinct excerpts minimum of 45 s each; v2 grows to 10 drummers.
- R-2 Style coverage per drummer: straight rock 4/4, pop with 16th hats, half-time, four-on-the-floor,
  funk with ghost notes, jazz swing ride (Any role), shuffle 12/8, ballad 6/8, 3/4 waltz, 5/4 and 7/8 example,
  metal double-kick, fills (1, 2, 4 bars), tempo ramp (up and down 10 percent), breakdown with silence, count-in
  by sticks (4 clicks), count-in by hi-hat, endings with ritardando.
- R-3 Capture: simultaneous multitrack at 48 kHz, 24-bit: kick in, kick out (optional), snare top, snare bottom
  (optional), hi-hat, toms (optional), 2 overheads, 1 room; plus e-kit MIDI where applicable. Mic placement and
  interface recorded in the manifest.
- R-4 Annotation per excerpt: beat times with beat-in-bar numbers (`.beats` text: `seconds beatInBar`), tempo
  segments, meter, section markers, and "no-play" regions (silence, fills) so metrics can be sliced. Two annotators
  independently; disagreements over 30 ms resolved by the lead; inter-annotator agreement F-measure reported (target >= 0.97).
- R-5 Annotation uses a web tool (`pacemaker_annotate`, reuses the UI trace) with tap-along, snapping to detected onsets, and
  playback with click overlay for verification. Ground truth is the drummer's stick/pedal hits where the beat is audible, with
  perceptual beat positions for sparse passages.
- R-6 Splits: leave-one-drummer-out cross validation for tuning; a locked **test set** (at least 2 drummers) used only for
  release gates. Test excerpts are never used for parameter search.
- R-7 Scorer `pacemaker_eval --corpus manifest.json --out eval.json` runs the engine at block sizes 64 and 256 on each excerpt using the
  role map in the manifest and reports per excerpt and aggregate metrics (section 4). A Python script (`tools/eval/score.py`) cross-checks
  beat metrics with `mir_eval` (BSD-3).
- R-8 CI: PR tier runs corpus-lite (12 clips, 30 s each, stored in the CI cache from object storage); nightly runs the full corpus; both compare
  to `Tests/baselines/eval-baseline.json` with noise bands (QUALITY-STRATEGY 6).
- R-9 Augmentation suite for robustness (nightly): add stage bleed (guitar and bass recordings or synthesised at -6, -12, -18 dB relative), crowd
  noise, mic-polarity flip, +-3 dB channel gain changes, 44.1 kHz resample, MP3 128 kbps round trip, tail of reverb.
- R-10 Hosting: audio is not committed. `tools/data/fetch_corpus.sh` downloads by manifest with SHA-256 verification from private object storage;
  only manifests, annotations and the scorer are in git. Synthetic corpus stays in-repo.
- R-11 Licence ledger `docs/data/LICENCE-LEDGER.md`: each external dataset (E-GMD, MDB Drums, ENST-Drums, IDMT-SMT-Drums, ADTOF, STAR) with licence
  text, commercial-use verdict and how it is used (evaluation only, never redistributed). Release gate: no entry marked "unknown".
- R-12 Participants sign a release (recording used for development, evaluation, and optionally as demo audio); personal data minimised.

## 4. Metrics and thresholds
Per excerpt (after lock unless stated): beat F-measure at +-70 ms; CMLc, CMLt, AMLc, AMLt; information gain (mir_eval); signed phase error mean
and SD in ms (published beat versus annotated hit); time to lock (seconds from first played beat to Locked); relock count; seconds in Hold; octave
errors (tempo ratio 0.5 or 2 sustained 2 s); downbeat F-measure and bar-position accuracy; tempo error (BPM) RMS; fill robustness (tempo deviation
during fills); count-in success (bar 1 on first played beat within 1 beat).

Initial thresholds (to be tightened by the ratchet once the first full run exists; first run only records a baseline):
| ID | Slice | Threshold |
|---|---|---|
| T10 | Rock/pop 4/4, close-mic kit, test set | F >= 0.95, phase error SD <= 18 ms, lock <= 6 s |
| T11 | Fills and breakdowns | no Relock caused by a fill in >= 95 percent of excerpts; Hold then Locked on re-entry |
| T12 | Count-ins | bar 1 correct in >= 90 percent |
| T13 | Downbeat (4/4) | downbeat accuracy >= 0.85 within 8 bars |
| T14 | Augmented set (bleed -12 dB) | F >= 0.90 |
| T15 | E-kit MIDI | F >= 0.98, phase SD <= 10 ms |
| T16 | Octave errors | <= 3 percent of excerpts |

## 5. Design
Manifest (JSON): `{ "id", "drummer", "kit", "channels":[{"index","role","file"}], "sampleRate", "meter", "style", "annotation", "slices":[...], "split":"dev|test" }`.
`pacemaker_eval` loads WAV through `FileBackend` code (F-01 A-2) for identical input handling, instantiates the engine with the manifest's roles,
and writes `eval.json` (schema versioned). Tuning workflow: `tools/eval/sweep.py` runs a grid or random search over profile parameters
(`alpha`, `beta`, `chaseBeta`, `maxSlewMs`, `kernelSeconds`, sensitivity) on dev drummers, reports leave-one-drummer-out scores, and
produces a patch to `FollowProfile`; final confirmation on the test set once per release candidate. Annotation tool: static page plus the server
(`/annotate`) storing `.beats` files; keyboard tap, undo, zoom, onset snapping.

## 6. Test plan
F02_R7 scorer unit tests with hand-made annotations (known F, known phase error); F02_R7b agreement of C++ metrics and `mir_eval` within 1e-6
on 20 synthetic cases; F02_R8 baseline compare logic (improvement updates baseline, regression fails); F02_R10 fetch script verifies hashes and fails on
tamper; F02_R9 augmentation determinism (seeded). The corpus gates themselves are T10 to T16.

## 7. Acceptance criteria
First full-corpus run published as `docs/data/EVAL-REPORT-001.md` with per-slice tables and the worst 10 excerpts analysed; thresholds either met or each miss has an
issue with a root-cause note; the ratchet is armed in CI; licence ledger has no unknowns.

## 8. Development plan
| # | Task | pd | Notes |
|---|---|---|---|
| C-1 | Recruit drummers, consent form, studio booking, interface and mic checklist | 2 | start week 1 (long lead) |
| C-2 | Recording sessions (3 sessions x 2 drummers) and ingest/checksums | 4 | calendar 4 weeks |
| C-3 | Annotation tool (`pacemaker_annotate`) | 3 | reuse web trace |
| C-4 | Annotate and cross-check (40 min x 6 drummers x 2 annotators) | 5 | tool speeds up; automatic onset snap |
| C-5 | Scorer in C++, JSON schema, `score.py` with `mir_eval` | 4 | |
| C-6 | Baseline, ratchet, CI jobs (corpus-lite, nightly), PR comment table | 2 | with F-29 |
| C-7 | Augmentation suite | 2 | |
| C-8 | Tuning sweep tooling and first tuning round | 3 | time-box 4 weeks overall |
Risks: recording quality varies (checklist and test recording first); annotation subjectivity (two annotators, agreement metric);
overfitting (locked test set, drummer-level splits); storage cost (compressed FLAC, cold storage for raw); legal (consent, ledger).
Mitigation for schedule: begin with one drummer and the existing synthetic corpus; add drummers weekly.
