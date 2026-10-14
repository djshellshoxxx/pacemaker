# F-08: Music analysis engine (key, chords, pitch, harmonics, melody, percussion)

Status: draft 1.0 · Priority: P2 (v1.1) · Estimate: 75 pd · Owner: insight
Depends on: F-01 (audio), tracker beat grid (existing) · Feeds: F-09 (UI, outputs, export), F-21
Related: research 05 section 2, ES-01 (front end, supervisor), ES-04 (threads)

## 1. Goal and user value
Pacemaker already listens to the band. This feature turns that listening into musical information shown live and exported: the **current key** (best effort,
with confidence), the **current chord** and progression, the **pitch** of the dominant note (tuner view), the **spectrum and harmonics** of the sound, the
**melody** as notes, and **percussion information** (what is being hit, how hard, how the groove feels, how steady the tempo is). Uses: a drummer sees the song key and chord
chart without a tablet; a lighting operator colours by key and chord via OSC; a producer exports the chords, melody and tempo map of a take; a drum tech tunes the kit.
Everything is labelled best effort and shows its confidence; where the engine is not sure it shows a dash, never a confident wrong answer.

## 2. Scope and non-goals
In: analysis thread and API; spectrum and peak tracking; monophonic pitch (YIN/pYIN); chroma with percussion suppression and tuning estimation; key estimation with
profile sets and temporal smoothing; chord recognition (beat-synchronous, key-aware, fixed-lag smoothing); harmonic analysis; melody extraction and note segmentation;
percussion analysis (hit classification, dynamics, groove, swing, microtiming, tempo stability, fills); loudness and energy; accuracy methodology; optional ML
(Basic Pitch, CREPE) behind an interface. Out: lyrics, source separation to stems, instrument recognition, full polyphonic transcription accuracy claims, score following (F-25), UI (F-09).

## 3. Inputs: what can be known from what
| Source | Gives | Cannot give |
|---|---|---|
| Kick, snare, hat, tom, overhead close mics | percussion info, drum pitches (tuning), dynamics, groove | key and chords (kit has little harmony) |
| Overhead or room mics, drum bus | some pitch from toms and cymbal spectrum; room tone | reliable harmony (bleed only) |
| **Harmony input** (new role `Harmonic`: line from keys, guitar, bass, vocal mic, or a stereo mix) | pitch, chroma, key, chords, melody, harmonics | |
| Full mix (stereo bus in a plugin) | key and chords with percussion suppression; melody (experimental) | clean per-instrument detail |
So the engine adds an input role `Harmonic` (and `Bass` as an optional low-register role) to the role map (ES-04). When only drum roles exist, the harmony cards show "needs a Harmonic input" and
percussion cards remain active.

## 4. Requirements
Update rates and latencies are targets measured at the output of the engine.

**Infrastructure**
- R-1 `MusicEngine` runs on its own thread (or is pulled synchronously in tests); the audio thread only writes mono mixes of the selected roles into a lock-free ring (`pushAudio`, no allocation).
- R-2 Results are published as a POD `MusicSnapshot` through a seqlock plus an event queue for notes and chord changes; consumers never block the engine.
- R-3 Every estimate carries `confidence` in 0..1 calibrated so that, on the test corpus, reported 0.8 means at least 80 percent correct (reliability plot in the report).
- R-4 CPU budget: all features on, one harmony input and 3 percussion roles: under 8 percent of one core on a 2020 laptop, under 20 percent on Raspberry Pi 5; each feature can be switched off individually and has its own budget.
- R-5 Deterministic and block-size invariant for a given input and settings (offline replays equal live results).

**Spectrum and frequency (S)**
- R-6 Log-frequency magnitude spectrum 20 Hz to 20 kHz (FFT 8192 at 48 kHz, 4x overlap), 20 updates per second, smoothed; dominant peaks list (top 8, parabolic interpolation, frequency error under 0.5 Hz below 2 kHz), note name and cents for each peak.

**Pitch (P)**
- R-7 Monophonic pitch via YIN with parabolic interpolation, absolute threshold 0.12 default, range 40 Hz to 2 kHz, frame 2048 at 48 kHz (hop 256), voicing probability; reports Hz, MIDI note, cents, clarity at 50 updates per second; octave errors under 3 percent on clean tonal sources; pYIN-style HMM smoothing as option.
- R-8 Tuning reference: global tuning offset estimate (A4 = 440 Hz +- 50 cents) from long-term peak histogram; shown and applied to note naming; user can lock to 440.

**Chroma, key, chords (K, C)**
- R-9 Chroma from a constant-Q or peak-picked spectrum (36 bins per octave, 55 Hz to 7 kHz), harmonic weighting (HPCP style), tuning-corrected, A-weighted, log-compressed; percussion suppressed by a streaming harmonic-percussive median filter (time 17 frames, frequency 17 bins, lookahead 8 frames) so drums in a mix do not smear chroma.
- R-10 Key: 24 major/minor hypotheses by correlating a decaying chroma accumulator (default half-life 12 s, minimum 6 s of harmonic energy) with profile sets: Krumhansl-Kessler (default for pop/rock), Temperley and an EDM-oriented set; profile set selectable and auto-chosen from a style hint; mode candidates (Dorian, Mixolydian, Lydian, Phrygian) reported when the diatonic pitch-class set fits better; relative and parallel alternatives shown; temporal smoothing by Viterbi over 24 states with self-transition 0.995 (fixed lag 8 s); key updates at 1 Hz; modulation flagged when a different key wins for 10 s.
- R-11 Key output: tonic, mode, confidence (margin between best and second normalised correlation, calibrated), alternates, scale pitch classes, Camelot/Open Key labels, tuning offset; "Key locked by user" override supplies the chord prior.
- R-12 Chords: beat-synchronous chroma (mean over the interval between tracked beats; fallback to fixed 250 ms hop when no beat grid), template matching over roots x qualities (major, minor, 7, maj7, min7, dim, aug, sus2, sus4, 5, 6, add9; extended set optional) with harmonic-aware templates; slash-chord bass from a low-register chroma (40 to 250 Hz); fixed-lag Viterbi (lag 1 beat) with key-aware transition prior (diatonic chords favoured, self-transition high); output chord symbol, root, quality, bass, Roman numeral relative to current key, confidence, start beat and bar; "N.C." when energy or confidence is low.
- R-13 Complexity setting: Triads (default), Sevenths, Extended; lower complexity merges qualities (e.g., maj7 to maj).

**Harmonics and timbre (H)**
- R-14 For the dominant pitch: relative amplitude of partials H1..H8 in dB, inharmonicity coefficient (B from partial deviations), harmonic-to-noise ratio, spectral centroid, rolloff (85 percent), flatness; updated at 10 Hz when voiced.
- R-15 Drum tuning helper: on a hit of a selected drum (tom, snare, kick), estimate the fundamental from the decaying spectrum 20 to 120 ms after the onset (peak refinement, quadratic interpolation), report Hz, nearest note and cents, decay time (T60 estimate) and ring; keeps a list of lug measurements (up to 12) and shows spread in cents. Accuracy +-1 Hz on isolated hits of a tonal drum at close-mic distance.

**Melody (M)**
- R-16 Melody extraction for monophonic or lead-dominated input: per-frame f0 track (pYIN) or, for mixes, salience by harmonic summation with continuity tracking, then note segmentation (minimum 60 ms, pitch change over 0.7 semitone or amplitude re-attack starts a new note, hysteresis, vibrato smoothing); outputs note events (onset sample, duration, MIDI pitch, velocity from amplitude), latency under 150 ms for monophonic input; mixes are labelled experimental.
- R-17 Notes are quantised against the tracked beat grid for display and export (nearest 16th, optional triplets) while keeping raw times.
- R-18 Optional ML back ends (`PitchBackend` interface): Basic Pitch (Apache-2.0) for polyphonic note events, CREPE (MIT) for f0; each loaded from a model file, run on the analysis thread through ONNX Runtime (MIT) or a small native port; disabled unless the S-1 spike proves latency and CPU.

**Percussion information (D)**
- R-19 Hit classification from the existing per-role onsets and band tags plus extra features at the onset (spectral centroid, low/mid/high energy ratios, decay measure over 80 ms, zero-crossing rate): classes kick, snare, closed hat, open hat, tom (with rank by f0), crash/ride (long broadband decay), rim/side-stick, other; confidence per hit; with a dedicated close mic per piece the class comes from the role and features only decide variants (open/closed hat, rim shot).
- R-20 Per piece and per bar statistics: hit counts, velocity (dB) distribution and range, accent pattern, hits per second (density), fill detection (density at least 2x the 8-bar baseline or a snare/tom roll), ghost notes (hits under 0.3 of the local mean within 60 ms before a main hit), flams (two hits under 40 ms apart).
- R-21 Groove grid: 16-step (or 12-step compound) occupancy and velocity map of the last 4 bars per piece, aggregated into a named feel (straight 8ths, straight 16ths, shuffle, half-time, four-on-the-floor, backbeat) by template distance with confidence.
- R-22 Swing: ratio of the off-beat 8th (or 16th) delay to the beat interval (50 percent straight, 66 percent triplet) averaged per piece over 8 bars with confidence.
- R-23 Microtiming: mean and SD of each piece's offset from the tracked grid in ms (positive is behind the beat), updated per bar; "pushing" or "laying back" labels beyond +-5 ms with at least 16 hits.
- R-24 Tempo stability: SD of inter-beat intervals in ms and in BPM, slope of tempo in BPM per minute (rushing or dragging), cumulative drift against the reference BPM when set; updated per 8 beats.
- R-25 Dynamics: short-term loudness (EBU R128 short-term 3 s and momentary 400 ms) per input and for the mix, peak and crest factor, dynamic range of the last 60 s, energy curve (1 Hz) used by F-21.

**Other musical information (O)**
- R-26 Time feel and meter hint: beats per bar from the bar tracker plus a suggestion when a different meter fits the accent pattern.
- R-27 Tonal versus percussive ratio (from HPSS energies), spectral flatness, brightness; stereo correlation and width for stereo inputs.
- R-28 Tempo-derived helper values (not an estimate): delay times (1/4, 1/8, dotted, triplet) in ms and LFO rates in Hz for the current BPM.

## 5. Design
```cpp
namespace pacemaker::music {
struct MusicConfig { double sampleRate; uint32_t features; /* bit set */ int harmonicChannel; std::string keyProfile = "krumhansl";
                     int chordComplexity = 1; double tuningHz = 0 /*auto*/; int maxLatencyMs = 250; };
struct KeyEstimate   { int8_t tonic; bool minor; uint8_t mode; float confidence; int8_t altTonic; bool altMinor; float tuningCents; uint32_t scaleMask; };
struct ChordEstimate { int8_t root, quality, bass; float confidence; int32_t startBar, startBeat; };
struct PitchEstimate { float hz, cents, clarity; int16_t midi; bool voiced; };
struct HarmonicInfo  { float partialsDb[8]; float inharmonicity, hnr, centroid, rolloff, flatness; };
struct PercussionStats { float density[kPieces]; float swing; float microMs[kPieces]; float microSd[kPieces]; uint8_t feel; float feelConf; float tempoSdMs; float tempoSlopeBpmMin; uint32_t gridMask[kPieces][..]; uint8_t fill; };
struct MusicSnapshot { uint32_t seq; KeyEstimate key; ChordEstimate chord; PitchEstimate pitch; HarmonicInfo harm; PercussionStats perc; float loudnessM, loudnessS; ... };
struct NoteEvent { int64_t onsetSample; int32_t durSamples; int16_t midi; float velocity; };
class MusicEngine { void prepare(const MusicConfig&); void pushAudio(const float* mono, int n, int64_t sample);   // audio thread
                    void setBeatGrid(const BeatMapSnapshot&); void pushHit(const OnsetEvent&, const HitFeatures&);
                    void run(int maxFrames);  /* analysis thread or tests */ MusicSnapshot snapshot() const;
                    bool popNote(NoteEvent&); bool popChord(ChordEstimate&); const SpectrumFrame& spectrum() const; };
}
```
Pipeline on the analysis thread (all preallocated): ring buffer -> STFT 8192/2048 -> spectrum frame (S) -> peak picking -> HPSS -> CQT-like chroma + tuning -> key accumulator (K) -> beat-synchronous chroma -> chord Viterbi (C); parallel YIN on the time domain (P, H); melody tracker (M); percussion analyser consumes hit events from the tracker plus short windows of per-role audio kept in a small ring (D). Chord and key use the tracker's `beatOriginSample/nextBeatSample` to align windows; if the tracker is Idle they fall back to a fixed hop.
Profiles are constant tables compiled in; the Krumhansl-Kessler major profile is `6.35 2.23 3.48 2.33 4.38 4.09 2.52 5.19 2.39 3.66 2.29 2.88` and minor `6.33 2.68 3.52 5.38 2.60 3.53 2.54 4.75 3.98 2.69 3.34 3.17`; Temperley and EDM-oriented values are transcribed from their primary papers and checked by unit tests against published rankings (**VERIFY** each table against the source before shipping).
Own implementation of every algorithm, no GPL or non-commercial dependency (research 05); FFT reuse of `FFT.h`.

## 6. Accuracy methodology (corpus M, built in parallel with F-02)
- Synthetic suite (deterministic, in repo): additive-synthesis chord progressions in all 24 keys with harmonics and decay, strums, bass lines, with noise and with synthetic drums added at several SNRs; monophonic melodies with vibrato and glides; drum patterns with known swing, microtiming and velocities.
- Own recordings (guitar, bass, keys, vocal, full band) with ground truth from charts and annotated chords/keys; at least 60 clips of 30 to 90 s; licence-clean.
- Public datasets evaluated only after the licence ledger verdict (F-02 R-11); offline reference comparison with Essentia on a developer machine is allowed but nothing from it ships.
- Metrics: key weighted score (`mir_eval.key`), chord root/majmin/seventh accuracy (`mir_eval.chord`), pitch RPA/RCA/voicing (`mir_eval.melody`), note F1 (`mir_eval.transcription`), percussion class F1 on onsets, swing and microtiming error in ms/percent, reliability diagrams for confidences.
- Targets (initial, ratchet applies): key weighted score >= 0.70 after 30 s on mixed band material and >= 0.85 on clean single-instrument tonal material after 20 s; chord root accuracy >= 0.75 and majmin >= 0.65 on clean guitar/keys with beat sync; pitch RPA >= 0.95 on clean monophonic, octave errors <= 3 percent, within 10 cents; melody note F1 >= 0.6 monophonic vocal; percussion class F1 >= 0.90 close-miked kit with role input, swing error <= 3 percent points, microtiming error <= 3 ms; confidence calibration error (ECE) <= 0.1.

## 7. Test plan
| ID | Method | Pass |
|---|---|---|
| M1 | Key on synthetic progressions, 24 keys, noise 0 to 20 dB, drums at -6 dB | weighted >= 0.9 clean, >= 0.8 with drums; transposition invariance |
| M2 | Tuning offset +-35 cents | correct key; offset estimate within 5 cents |
| M3 | HPSS effect on key and chord accuracy with drums | improves versus no HPSS |
| M4 | Chord templates, qualities, slash chords, N.C. | root >= 0.95 on synthetic |
| M5 | Beat-sync versus fixed hop on progressions with chord changes on beats | beat-sync >= +5 points |
| M6 | Fixed-lag Viterbi latency | decision within 1 beat after change |
| M7 | Pitch: sines, saws, vibrato, noise, octave stress | RPA, octave error limits, <= 1 cent bias |
| M8 | Harmonics: known partial amplitudes and inharmonic strings | within 1.5 dB and 5 percent B |
| M9 | Drum tuning on synthesised tonal drums | +-1 Hz |
| M10 | Melody segmentation on synthetic note sequences | F1 >= 0.9 |
| M11 | Percussion class, density, fills, ghosts, flams | F1 >= 0.9 |
| M12 | Swing and microtiming recovery | within targets |
| M13 | Tempo stability and slope | within 5 percent |
| M14 | Block-size invariance and offline equals live | identical |
| M15 | `ScopedAudioThread` on `pushAudio` | clean |
| M16 | CPU benchmark per feature | within budgets |
| M17 | Reliability diagram generation and ECE | <= 0.1 on corpus M |
| M18 | Fuzz: NaN, silence, DC, full-scale, 8 kHz to 192 kHz rates | no crash, "unknown" outputs |

## 8. Acceptance criteria
On the demo band recording the UI shows the right key within 30 s and the chord chart within one beat of changes for at least 75 percent of the song, flags low confidence instead of guessing when
the drummer plays alone, the drum tuner reads a tom within 1 Hz, and CPU stays inside the budget with all features on.

## 9. Development plan
| Phase | Content | pd | Exit |
|---|---|---|---|
| S-1 | Spike: Basic Pitch and CREPE via ONNX Runtime: latency, CPU, accuracy on corpus M; decision ADR | 4 | written finding |
| 1 | Infrastructure (thread, ring, snapshot, config), spectrum, YIN pitch, harmonics, tuning helper, loudness | 14 | M7 to M9, M14 to M16 |
| 2 | Percussion analysis (classification, stats, grid, swing, microtiming, stability, fills) | 14 | M11 to M13 |
| 3 | Chroma, HPSS, tuning, key (profiles, smoothing, modes) | 14 | M1 to M3 |
| 4 | Beat-synchronous chords, Viterbi, Roman numerals, bass | 14 | M4 to M6 |
| 5 | Melody and note segmentation, grid quantisation | 10 | M10 |
| 6 | Calibration of confidences, corpus M runs, tuning, report | 5 | M17, targets |
| 7 | Optional ML back ends if S-1 positive | (10, not in the 75) | |
Total phases 1 to 6 plus spike: 75 pd. Parallelisable: phases 1 and 2 are independent of 3 to 5.
Risks: chord and key accuracy on mixes with drums (HPSS, harmony input recommendation, honest confidence); CPU on Raspberry Pi (per-feature switches, decimation, lower FFT size); ML latency (optional only); expectations (labels "best effort", dashes when unsure, user key lock); licence of reference tables (transcribe from papers, verify).
