# Research 01: Real-time beat tracking

Status: research notes, October 2026. Primary input to
`docs/specs/ES-01-tracking-engine.md`. Many primary sources (arXiv, ISMIR
archives, AudioLabs, NIME, madmom docs) were blocked by the research proxy, so
figures come from readable GitHub source, search summaries of the papers and
prior knowledge. Items that could not be confirmed first-hand are flagged
"(unverified)".

---

## 1. Onset detection functions (ODFs)

| ODF | What it measures | Drum suitability | Notes |
|---|---|---|---|
| Spectral flux (SF), half-wave rectified | Positive change of magnitude spectrum between frames | Good general purpose; robust on kick plus snare mixes | Basis of SuperFlux, madmom, PLP, Krzyzaniak lib |
| SuperFlux (Böck and Widmer, DAFx-13) | SF on a max-filtered (3 bins) log-mel spectrogram; suppresses vibrato false positives | Marginal benefit on drums (no vibrato); still a good default | Essentia defaults: frame 2048, hop 256 at 44.1 kHz; up to 60 percent fewer false positives on vibrato material |
| Complex spectral difference (CSD) | Deviation of predicted vs actual complex STFT bin | Good for percussive and soft onsets; used by BTrack | More sensitive to noise and phase jitter than SF |
| High-frequency content (HFC, Masri) | Frequency-weighted energy sum | Excellent for snare and hi-hat, poor for kick | Cheapest to compute |
| Band-wise / percussive (median-filter HPSS) | Stark, Robertson, Davies 2014: percussive component via real-time median filtering; low-band vs high-band correlate with kick vs snare | Best for a drum-only mic feed; gives kick/snare separation for free | Can also drive downbeat pattern logic |
| Time-domain envelope | Amplitude envelope derivative | Fast (sub-10 ms) but more false positives; hybrid time plus spectral improves onset-time accuracy by about 3 ms average, up to 12 ms (DAFx 2014) | Useful for a low-latency trigger path |

Typical real-time parameters: 44.1 or 48 kHz, window 1024 to 2048, hop 256 to
512 (5.8 to 11.6 ms per frame; 86 to 100 fps). madmom uses 100 fps; BTrack
uses hop 512 and frame 1024 (11.6 ms at 44.1 kHz); real-time PLP uses hop 512
and window 1024 at 48 kHz (93.75 Hz). Inherent ODF latency is about window/2
plus one hop plus peak-picking delay (online peak picking needs at least one
frame of look-back, typically 1 to 3). aubio-style figures: hop 256 / win 512
gives about 13 ms delay; hop 1024 / win 2048 about 27 ms.

Böck, Krebs and Schedl (ISMIR 2012, "Evaluating the online capabilities of
onset detection methods") showed most published ODFs rely on global
normalisation and must be modified for online use; they proposed an online SF
variant with an adaptive peak picker that matches offline performance and is
level-independent.

Recommendation for a drum feed: a two-band (low at or below 150 Hz for kick,
at or above 1 kHz for snare and hat) log-compressed spectral flux with
local-mean subtraction and adaptive threshold (mean plus k times std, as in the
Krzyzaniak library), plus an optional per-input time-domain detector when a
separate kick or snare mic is available. Adaptive whitening (Stowell and
Plumbley 2007) is a cheap, proven real-time normalisation for level changes.

## 2. Real-time tempo and beat-phase trackers

### BTrack (Stark, Davies, Plumbley; DAFx-09)

- Algorithm (from BTrack.cpp): CSD-HWR ODF, hop 512 / frame 1024; 512-frame
  ODF buffer (about 6 s); balanced autocorrelation (FFT, zero-padded to 1024,
  divided by lag); 4-element comb filterbank over periods 2 to 127 frames;
  Rayleigh weighting with parameter 43 (about 120 bpm prior); tempo
  observation vector 80 to 160 bpm at 2 bpm resolution (41 states) with a
  Gaussian transition matrix (sigma about 5.1 states); cumulative score
  recurrence with alpha 0.9 (90 percent past, 10 percent new ODF); beat
  prediction made halfway through the beat period using a log-Gaussian window
  over [-2, -0.5] periods of future cumulative score; `fixTempo()` can lock
  tempo.
- Latency: a beat is decided about half a period before it occurs, so it
  predicts; ODF latency 12 to 20 ms. Needs 3 to 6 s of onset history to
  settle. CPU: trivial (one 1024 FFT per hop plus a 1024 ACF every hop).
- Licence: GPL-3 (QMUL). C++, Python and Vamp wrappers; FFTW or bundled
  KissFFT.
- Weaknesses: restricted 80 to 160 bpm range leads to half and double time
  errors outside it; ACF needs regular energy at the beat period, so sustained
  syncopation or fills lower correlation at the true period; alpha 0.9 makes
  it sluggish on deliberate tempo changes; no downbeat.

### Davies and Plumbley context-dependent tracker (2004 causal; TASLP 2007), aubio, QM Vamp

- Two-state model: "general" (Rayleigh-weighted comb filterbank on ODF ACF
  over about 6 s windows, stepped about 1.5 s) then "context-dependent"
  (Gaussian prior around the locked tempo). aubio's `beattracking.c`
  implements this (CSD or specflux ODF, default hop 256 / win 512). GPL-3
  (aubio), GPL-2+ (qm-dsp). BeatNet's benchmark puts aubio at 57.1 beat F1 on
  GTZAN, the lowest of the online systems.

### Ellis dynamic programming (2007)

- Onset strength envelope (mel bands, about 100 fps), global tempo via
  windowed ACF with a log-Gaussian prior at 120 bpm, then DP with transition
  cost alpha times (log(delta/tau)) squared ("tightness", librosa default 100).
  Inherently offline (backtracking); causal variants exist (the
  cumulative-score recurrence in BTrack and OBTAIN is essentially an online
  form). librosa is ISC. Useful as an offline reference for tests, not for the
  live path.

### B-Keeper and BeatSeeker (Robertson and Plumbley; NIME 2007, NIME 2008, CMJ 2013)

- Event-based, not ODF-based: takes onsets from the kick (and snare) signals,
  interprets each onset relative to bar position with an internal weighting,
  places Gaussian windows around expected beat locations to score an onset's
  relevance; a tempo process (best inter-onset interval estimate) runs in
  parallel with a phase-synchronisation process. Parameters (window widths,
  adaptation rates) are user-tunable; designed to respond to drift but stay
  stable through syncopation and fills. Evaluated with a stochastic drum
  machine (noise added to tempo and phase) by synchronisation error; the
  Turing-test study found listeners rated it closer to a human tapper than to
  a metronome. Java plus Max/MSP; later released commercially as Ableton's
  BeatSeeker. Code not open source (unverified). Requires a drum signal, kick
  preferably.
- Strengths for Pacemaker: exactly the drummer-following problem; explicitly
  models hold vs chase. Weakness: assumes the pattern loosely follows 4/4
  kick/snare expectations; numeric sync-error results could not be retrieved.

### Böck / madmom RNN plus DBN

- Offline: BLSTM activations (3 frame sizes, 100 fps) then DBN (tempo 55 to
  215 bpm, Viterbi). Online: `RNNBeatProcessor(online=True)` uses
  unidirectional LSTM on a single 2048 frame with 12 mel bands;
  `DBNBeatTrackingProcessor(online=True)` runs the forward algorithm frame by
  frame. Latency about one frame (10 ms) plus windowing, but the DBN forward
  pass can lag true tempo changes by several beats. BeatNet reports "Böck FF"
  at 74.2 beat F1 on GTZAN.
- CPU: a small LSTM per 10 ms frame, fine on desktop; porting means
  re-implementing LSTM inference in C++.
- Licence trap: madmom source is BSD-2, but the pre-trained models are CC
  BY-NC-SA 4.0. Not usable in a commercial plugin without a licence or
  retraining.

### DLB (Heydari and Duan, ICASSP 2021), BeatNet (ISMIR 2021), BeatNet+ (TISMIR 2024)

- DLB: unidirectional RNN activation plus enhanced Monte-Carlo localisation
  (particle filter) using only the current frame; 73.8 beat F1 (GTZAN online).
- BeatNet: 22.05 kHz, log-filtered spectrogram, 20 ms hop / 64 ms window (50
  fps); causal CRNN; cascade of two particle filters: 1,500 particles for beat
  (55 to 215 bpm, 300 tempo values) and 250 for downbeat; observations under
  0.4 clamped to weight 0.03; downbeat emitted when activation over 0.7 and
  the particle mode sits in a first-beat state. Streaming mode adds a
  documented 84 ms buffering delay. Results (online): GTZAN beat 75.4 /
  downbeat 46.5; Ballroom 77.4 / 47.5; Rock Corpus 73.1 / 45.0; vs IBT 69.0,
  aubio 57.1. Offline BeatNet plus DBN 80.6 / 54.1. Licence: CC BY 4.0 (code),
  Python/PyTorch, depends on madmom.
- BeatNet+: adds percussion-invariant auxiliary training; better on
  non-percussive audio; licence not confirmed.

### BEAST (streaming Transformer, ICASSP 2024)

- Contextual block processing plus relative positional encoding; 7.5 M
  parameters; BEAST-1: beat F1 80.0, downbeat 46.8 at 46 ms latency, real-time
  factor 0.41 on GPU. No public code found (unverified). Too heavy for a CPU
  plugin today without distillation.

### Real-time PLP (Meier, Chiu, Müller; DAFx 2024 and TISMIR 7(1) 2024)

- From `realtimeplp.py` (MIT, Python): 48 kHz, window 1024, hop 512 (93.75
  fps); spectral-flux novelty on a log-compressed (gamma 1000) spectrogram,
  local-average subtraction (M 10 frames), HWR; per-frame Fourier tempogram
  over 60 to 180 bpm at 1 bpm (121 bins) with a 6 s Hann kernel; pick the max
  tempo bin, synthesise a windowed sinusoid at that tempo and phase and
  overlap-add into a PLP buffer; beats are the peaks of the PLP curve. "Zero
  latency" because the kernel is centred so the current frame sits at buffer
  position N/2 plus lookahead; `lookahead` (frames) lets you read beats ahead
  of time. Outputs beat **stability** (peak amplitude / max peak amplitude),
  beat context, continuous LFO-style control signals and Hilbert-envelope
  confidence.
- CPU: one 1024 FFT per hop plus a 121-bin times 562-frame complex dot
  product per hop (about 70k MACs), trivial.
- Weakness: a 6 s kernel means tempo changes integrate over about 6 s
  (sluggish on intentional ramps; shorter kernels are faster but less stable);
  single-best-tempo selection can flip between octaves; no downbeat; range 60
  to 180 clamps.

### IBT (Oliveira, Gouyon, Martins, Reis; ISMIR 2010; Marsyas)

- Multi-agent (BeatRoot-style) causal tracker: spectral-flux ODF, induction
  window about 5 s, agents hypothesise period and phase and are scored with
  inner and outer tolerance windows; agents are created and killed as evidence
  changes; a later version adds state recovery. C++ in Marsyas, GPL. BeatNet
  benchmark: 69.0 beat F1 on GTZAN "with 5 s latency". Handles tempo change
  reasonably through agent competition; tends to octave confusion.

### Kalman and particle filter trackers (Cemgil et al. 2001)

- Tempo as hidden state (period plus phase, optionally acceleration)
  estimated from onset times by switching Kalman filtering or sequential Monte
  Carlo; naturally online, provides covariance as confidence, and is the
  mathematical basis of B-Keeper-like correction and BeatNet's particle
  filters. A 2-state Kalman filter (phase, period) with onset measurements
  gated by a Gaussian window around the predicted beat is cheap (microseconds)
  and ideal as Pacemaker's fusion layer.

### Other C++ real-time trackers

- OBTAIN (Mottaghi et al. 2017): CSD-like ODF plus cumulative beat strength
  plus tempo via ACF; C++ port `introlab/MusicBeatDetector` (GPL-3, FFTW).
- Krzyzaniak `Beat-and-Tempo-Tracking` (ANSI C, MIT): spectral-flux onset
  (10 Hz LPF, threshold 1 std over running mean), Percival and
  Tzanetakis-style tempo (ACF candidates scored by pulse-train
  cross-correlation, decaying Gaussian histogram), Stark-style CBSS with
  4-click cross-correlation and Gaussian forward projection; defaults 50 to
  200 bpm; requires 2 count-in beats.
- `beat_this_cpp` (MIT, ONNX Runtime, about 97 MB transformer): offline only.
- GBD (MIT, KissFFT) and BeatDetektor (MIT): simple energy-based BPM
  detectors; not phase-accurate.

## 3. Downbeat and bar tracking in real time

- Classic rule: in rock and pop, kick on 1 and 3, snare on 2 and 4 (Goto
  1995/2001 BTS: multiple agents plus matching detected bass and snare
  patterns against eight pre-registered drum patterns; 42 of 44 songs correct,
  about 1 s decision delay, unverified). Krebs, Böck and Widmer rhythmic
  pattern DBNs (ISMIR 2013) showed explicitly modelling style patterns
  "drastically reduces octave errors and substantially improves downbeat
  tracking".
- Learned online systems: BeatNet online downbeat F1 about 45 to 47; BEAST
  46.8 at 46 ms; offline state of the art about 54 to 75 depending on dataset.
  Realistic expectation for generic music is roughly half of bars correctly
  labelled within ±70 ms. For a drum-only feed with explicit kick/snare
  pattern matching in 4/4 expect much better (Goto level), with failures during
  fills and on half-time or snare-on-3 grooves.
- Practical real-time recipe: maintain a bar-position histogram (4 or 8
  states) scored by low-band onsets (kick) on 1 and 3 and high-band onsets
  (snare) on 2 and 4; accumulate with exponential forgetting; accept a
  downbeat phase only when its score beats the runner-up by a margin for at
  least 2 bars; allow user and MIDI nudge and a "learn pattern from count-in or
  first 2 bars" mode. Offer a half-time toggle because snare-on-3 patterns
  look like double tempo to the heuristic.

## 4. Fills, silence, count-ins, rubato, tempo changes, confidence, hold vs chase

- Fills and syncopation: ACF and comb trackers see a drop in beat-period
  correlation; B-Keeper handles it by weighting onsets with Gaussian
  expectancy windows (onsets far from expected beats contribute little) and
  keeping tempo adaptation slow while phase adjustment is small; Stark's alpha
  0.9 and PLP's 6 s kernel give similar inertia. The Krzyzaniak library
  requires 2 count-in beats before locking. The "SMC blind spot" analysis
  (2026, arXiv 2605.12287) catalogues failure modes of modern trackers: octave
  errors, continuity errors, total failure and "confident but wrong"
  activations; a 55 bpm floor forced double tempo on 21 percent of SMC tracks,
  so tempo range must be user-settable and multi-hypothesis.
- Silence and breakdowns: freeze tempo (hold), keep phase extrapolating from
  the last confident beat (free-running clock), decay confidence; re-sync on
  the first onsets that fall within the expectancy window.
- Count-in: detect 2 to 4 evenly spaced isolated onsets (IOI coefficient of
  variation under about 5 percent) before any dense playing; set tempo to the
  median IOI, phase to last click plus IOI, bar to the next downbeat. Hi-hat
  count-ins are high-band only, so use HFC or the high band for this.
- Rubato and intentional changes: use two time constants, phase correction
  fast (alpha about 0.2 to 0.5 of measured asynchrony per beat, per Repp's
  sensorimotor synchronisation literature), period correction slow (beta
  about 0.05 to 0.2) and gated by consecutive consistent evidence (3 to 4
  beats all early or late in the same direction raises beta temporarily,
  "chase" mode). This two-process error-correction model is what both humans
  (Repp 2005; Repp and Su 2013) and B-Keeper use.
- Confidence: PLP stability (peak / max-peak ratio) and Hilbert envelope;
  Kalman innovation variance; particle spread; ratio of onsets landing inside
  expectancy windows over the last N beats; tempogram peak-to-second-peak
  ratio. Expose a single 0 to 1 confidence; below a threshold hold tempo and
  stop publishing tempo changes to Link and MIDI clock (phase still
  extrapolates).

## 5. Phase prediction, lookahead and drummer timing statistics

- All causal trackers publish beats before they occur by extrapolating:
  BTrack predicts at mid-period; PLP uses `lookahead` frames; B-Keeper
  schedules the sequencer's next beat from current period plus phase. For
  Pacemaker: next_beat = last_beat + period_estimate; publish Link
  `setTempo(bpm, atTime)` and MIDI clock ticks scheduled from the Kalman
  state; apply corrections as small per-beat phase slews (at most a few ms per
  tick) rather than jumps, except on a confident re-lock.
- Human timing numbers: trained musicians keep asynchrony SD about 2 percent
  of the inter-beat interval (about 10 ms at 120 bpm; about 16 ms at 60 bpm);
  untrained at least twice that (Repp 2005; Repp and Su 2013). Professional
  drummers against a click: mean sync error about ±2 to 10 ms depending on
  limb and tempo (hi-hat about 2 ms, snare and kick about 10 ms early at 60 to
  120 bpm; hi-hat about 10 ms late at 200 bpm) (Fujii et al., via search
  summary). Negative mean asynchrony of about 10 to 30 ms is normal. Hennig et
  al. (PLoS ONE 2011): deviations are long-range (1/f-like) correlated, so
  drift is a random walk with memory, not white noise, and a random-walk
  period model in the Kalman filter is the right prior. Without a click, tempo
  drifts by several bpm over a song (Toto "Rosanna": 82 to 87 bpm, over 1 s
  accumulated drift in 50 s; Carter and von Appen 2025 found tempo CV varies
  strongly by drummer). Design budget: expect per-beat IOI jitter sigma about
  8 to 15 ms and slow drift about 0.5 to 2 bpm per 30 s in live rock; a fast
  phase loop plus slow period loop covers both.

## 6. Evaluation

- Metrics (mir_eval defaults): F-measure with ±70 ms window; Cemgil (Gaussian
  sigma 40 ms); Goto; P-score (0.2 times median IBI); continuity CMLc, CMLt,
  AMLc, AMLt (phase and period tolerance 17.5 percent of IBI; AML also accepts
  2x, half, off-beat); information gain (41 bins); beats before 5 s are
  trimmed. For a live follower add: time-to-lock (s), mean and SD of
  predicted-vs-annotated beat error for predictions issued at least one hop
  ahead, recovery time after a fill or silence, max phase slew per beat, and
  percent of tempo-change events followed within N bars.
- Datasets: Ballroom (685 excerpts, beats plus downbeats), GTZAN (1000, beats
  plus downbeats), Beatles (180), RWC Popular (100), SMC (217, hard, beats
  only), Hainsworth. Drum-specific: ENST-Drums (3 drummers, wet mixes 64
  tracks about 1 h, onset labels by instrument), MDB-Drums (23 MedleyDB
  tracks, drum stems plus per-instrument onsets), Groove MIDI Dataset (13.6 h,
  10 drummers, MIDI with microtiming recorded to a click, tempo known) and
  E-GMD (444 h audio from 43 kits). MUSDB18 gives 150 drum stems but no beat
  annotations. For Pacemaker's drummer-following target, record or synthesise
  drum-only test material with ground-truth tempo curves: (a) GMD/E-GMD
  rendered through drum samples (known click grid, real microtiming), (b) a
  stochastic drum machine with injected tempo random walk and phase noise
  (Robertson's protocol), (c) real live multitrack kick and snare stems with
  hand-tapped annotations including fills, breakdowns, count-ins and
  deliberate ramps.
- Offline harness: feed stems through the DSP core in a CLI at real-time hop
  granularity, log published tempo and phase events, score with a C++ port of
  mir_eval beat metrics (adamstark/Beat-Tracking-Evaluation-Toolbox has C++
  and Python implementations; licence to be checked; mir_eval itself is MIT).

## 7. Licensing map of open-source code

- GPL or AGPL, cannot be linked into a closed-source commercial plugin:
  BTrack (GPL-3), aubio (GPL-3+), qm-dsp / QM Vamp plugins (GPL-2+),
  Marsyas/IBT (GPL), Essentia (AGPL-3, commercial licence from UPF),
  introlab/MusicBeatDetector (GPL-3), FFTW (GPL-2+).
- Non-commercial: madmom pre-trained models (CC BY-NC-SA 4.0) even though
  madmom source is BSD-2; BeatNet depends on madmom feature code (BSD) but its
  own code is CC BY 4.0.
- Permissive: groupmm/real_time_plp (MIT, Python, port to C++),
  michaelkrzyzaniak/Beat-and-Tempo-Tracking (MIT, ANSI C, directly usable),
  CPJKU beat_this and mosynthkey/beat_this_cpp (MIT, offline reference),
  librosa (ISC, offline reference), mir_eval (MIT), GBD and BeatDetektor (MIT,
  low quality), KissFFT (BSD-3), pffft (BSD-like), `juce::dsp::FFT` (JUCE
  licence). Ableton Link SDK is GPL-2 with a commercial exception granted on
  request (verify current terms before shipping).
- Papers are not code: re-implementing BTrack's or Davies and Plumbley's
  published algorithm from the paper in clean-room fashion is not a GPL issue;
  copying BTrack.cpp is.

## Recommended algorithm stack for Pacemaker

1. **Front end** (per channel, 48 kHz, hop 256 = 5.3 ms, window 1024):
   log-compressed magnitude spectrum, two-band and full-band half-wave
   rectified spectral flux with local-mean subtraction and adaptive whitening;
   online adaptive peak picking (Böck 2012 style) to emit discrete onsets with
   band tags (kick-like, snare-like). Optional per-input time-domain envelope
   detector for sub-10 ms trigger-grade onsets when the user patches direct
   kick and snare mics. All permissive (written from the papers, structure
   borrowed from Krzyzaniak's MIT code).
2. **Tempo induction**: a real-time PLP-style Fourier tempogram (port of the
   MIT Python code) over a user-settable range (default 50 to 220 bpm, 1 bpm
   bins) with a 4 to 6 s kernel, keeping the top 3 tempo peaks and their
   octave relations as hypotheses; PLP stability gives tempo confidence.
   Combine with a comb-filter ACF check (BTrack-style, re-implemented) to
   resolve the beat level, with a style prior (default 120 bpm Rayleigh) that
   the user can bias.
3. **Phase and period tracking and prediction**: an event-driven Kalman or
   B-Keeper-style loop; state (phase, period, optional period slope);
   measurements are onsets inside a Gaussian expectancy window around the
   predicted beat (sub-beat positions weighted lower); fast phase correction
   (alpha about 0.3), slow period correction (beta about 0.1) that temporarily
   grows when 3 or more consecutive consistent errors indicate an intentional
   tempo change ("chase"), and freezes when confidence collapses ("hold").
   This directly supplies the next-beat time needed to pre-schedule Link
   `setTempo` / `forceBeatAtTime` and MIDI clock ticks with lookahead.
4. **Bar tracking**: user-selectable meter, bar-position histogram scored by
   kick-on-1/3 and snare-on-2/4 evidence with hysteresis and a
   learn-from-count-in mode; expose half-time, double-time and "set downbeat
   now" controls, because learned online downbeat trackers only reach about
   45 to 50 percent F1 and none are commercially usable in C++ today.
5. **Supervisory logic**: count-in detector (2 to 4 isochronous isolated
   onsets), silence or breakdown hold with free-running extrapolation,
   confidence output (0 to 1) built from PLP stability plus Kalman innovation
   plus onset hit rate, and rate-limited tempo publishing (at most 0.5 bpm per
   beat unless a confident re-lock).
6. **Optional future tier**: a small causal CRNN activation (BeatNet-like,
   trained in-house on GMD/E-GMD plus own recordings to avoid madmom's NC
   models) exported to ONNX or RTNeural, feeding the same Kalman fusion layer,
   only if the DSP stack proves insufficient on syncopated material.

Reasoning: every proven live drummer-following system (B-Keeper/BeatSeeker,
Goto's BTS, Krzyzaniak's robots) is event-based with explicit tempo and phase
correction and musical priors, which gives controllable hold/chase behaviour,
microsecond CPU cost, zero licence risk and sub-frame phase precision from
onset times. PLP adds a robust, MIT-licensed, zero-latency tempo-induction
layer with a built-in confidence measure. The neural online trackers currently
win on heterogeneous music but not on drum-only input, are heavier, and all
have licence or code-availability problems for a commercial C++ plugin.

## Sources

- BTrack: https://github.com/adamstark/BTrack ; https://code.soundsoftware.ac.uk/projects/btrack
- Stark, Davies, Plumbley, DAFx-09: https://dafx.de/paper-archive/details/XiEJjIc0a2Hb_KTJon0SaA
- Stark, Robertson, Davies, real-time percussive beat tracking (2014): https://repositorio.inesctec.pt/items/b1ef3d8a-8975-497a-bce9-b63572a21006
- Davies and Plumbley, causal tempo tracking (ISMIR 2004): https://archives.ismir.net/ismir2004/paper/000226.pdf ; context-dependent beat tracking (TASLP 2007): https://openresearch.surrey.ac.uk/esploro/outputs/journalArticle/Context-dependent-beat-tracking-of-musical-audio/99511418902346
- aubio: https://github.com/aubio/aubio ; qm-dsp: https://github.com/c4dm/qm-dsp
- Robertson and Plumbley, B-Keeper (NIME 2007): https://nime.org/proc/nime2007_robertson/ ; Turing test (NIME 2008): https://nime.org/proceedings/2008/nime2008_319.pdf ; CMJ 2013: https://direct.mit.edu/comj/article/37/2/46/94421/Synchronizing-Sequencing-Software-to-a-Live
- Ableton BeatSeeker: https://www.ableton.com/en/packs/beatseeker/
- madmom: https://github.com/CPJKU/madmom/blob/main/madmom/features/beats.py ; https://github.com/CPJKU/madmom/blob/main/LICENSE ; Böck et al. ISMIR 2014: https://www.cp.jku.at/research/papers/Boeck_etal_ISMIR.2014.pdf
- Böck, Krebs, Schedl, online onset detection (ISMIR 2012): https://archives.ismir.net/ismir2012/paper/000049.pdf
- SuperFlux (Essentia): https://essentia.upf.edu/reference/std_SuperFluxExtractor.html ; Essentia licence: https://essentia.upf.edu/licensing_information.html
- Bello et al. onset tutorial (2005): https://www.researchgate.net/publication/3334132_A_Tutorial_on_Onset_Detection_in_Music_Signals
- Onset time estimation hybrid (DAFx14): https://www.dafx14.fau.de/papers/dafx14_bertrand_scherrer_onset_time_estimation_for.pdf
- Ellis 2007 DP beat tracking: https://www.ee.columbia.edu/~dpwe/LabROSA/projects/beattrack/
- DLB (ICASSP 2021): https://arxiv.org/abs/2011.02619
- BeatNet (ISMIR 2021): https://archives.ismir.net/ismir2021/paper/000033.pdf ; https://github.com/mjhydri/BeatNet
- BeatNet+ (TISMIR 2024): https://transactions.ismir.net/articles/10.5334/tismir.198
- BEAST (ICASSP 2024): https://arxiv.org/abs/2312.17156
- Real-time PLP: https://audiolabs-erlangen.de/resources/MIR/2024-TISMIR-RealTimePLP ; https://github.com/groupmm/real_time_plp
- IBT (ISMIR 2010): https://archives.ismir.net/ismir2010/paper/000050.pdf
- OBTAIN: https://arxiv.org/abs/1704.02216 ; https://github.com/introlab/MusicBeatDetector
- Cemgil et al. (NIPS 2001): https://proceedings.neurips.cc/paper/2001/hash/5ec829debe54b19a5f78d9a65b900a39-Abstract.html
- Goto real-time BTS: https://staff.aist.go.jp/m.goto/PAPER/ICMC95goto.pdf ; https://staff.aist.go.jp/m.goto/PAPER/JNMR2001goto.pdf
- Krebs, Böck, Widmer (ISMIR 2013): https://archives.ismir.net/ismir2013/paper/000218.pdf
- SMC blind spot analysis (2026): https://arxiv.org/abs/2605.12287
- Krzyzaniak Beat-and-Tempo-Tracking (MIT): https://github.com/michaelkrzyzaniak/Beat-and-Tempo-Tracking ; beat_this_cpp: https://github.com/mosynthkey/beat_this_cpp ; Beat This!: https://arxiv.org/abs/2407.21658
- mir_eval beat metrics: https://github.com/mir-evaluation/mir_eval/blob/main/mir_eval/beat.py ; Beat-Tracking-Evaluation-Toolbox: https://github.com/adamstark/Beat-Tracking-Evaluation-Toolbox
- Datasets: ENST-Drums https://researchportal.ip-paris.fr/en/publications/enst-drums-an-extensive-audio-visual-database-for-drum-signals-pr/ ; MDB-Drums https://www.open-access.bcu.ac.uk/6179/1/Southall2017a.pdf ; Groove MIDI https://magenta.tensorflow.org/datasets/groove ; E-GMD https://magenta.tensorflow.org/oaf-drums
- Human timing: Repp 2005 http://users.df.uba.ar/anita/f1_labo/clase1/repp%20psycho%20bull%20rev%202006%20synchro%20tapping%20review.pdf ; Repp and Su 2013 https://link.springer.com/article/10.3758/s13423-012-0371-2 ; drummer sync error https://www.researchgate.net/publication/259730940_Synchronization_Error_of_Drum_Kit_Playing_with_a_Metronome_at_Different_Tempi_by_Professional_Drummers ; Hennig et al. 2011 https://www.psych.uni-goettingen.de/de/cognition/publications/hennigetal2011 ; Rosanna timing https://arxiv.org/abs/2411.06892 ; Carter and von Appen 2025 https://revuemusicaleoicrm.org/vol12-n2/microtiming-tempo-variability-rock-drummers/
