# ES-01: Tracking Engine (detailed engineering spec)

Status: draft 0.1, normative for implementation and tests.
Research inputs: `docs/research/01-real-time-beat-tracking.md`.

The tracking engine is the part that turns audio (and optionally MIDI
trigger events) from a live drummer into a continuously predicted beat
timeline: tempo, beat phase, bar position and a confidence value. It is a
plain C++17 static library (`pacemaker_engine`) with **no JUCE GUI
dependency**, so the same code runs in the plugin, the standalone, the
headless Raspberry Pi build and the offline evaluation CLI.

---

## 1. Goals and non-goals

**Goals**

- G1. Follow a human drummer's tempo drift and push/pull without audible
  hunting. Target: published beat error (predicted vs annotated) mean under
  10 ms, standard deviation under 15 ms on the drum-stem test set
  (RS-05 section 3).
- G2. Lock within 2 bars of a count-in and within 4 bars of free playing at
  50 to 220 BPM.
- G3. Hold through fills, breakdowns and silence without jumping tempo.
- G4. Report bar position (beat 1 to N of the meter) with a learnable pattern
  and explicit user override.
- G5. Expose a single confidence value in [0, 1] that downstream outputs use
  to gate publishing.
- G6. CPU: at most 10 percent of one core at 48 kHz on x86 and 30 percent on
  a Raspberry Pi 4 for three input roles.
- G7. No GPL or non-commercial code in the engine. All algorithms are
  implemented from the papers or from MIT/BSD sources.

**Non-goals**

- Not a general-purpose music beat tracker. The input is assumed to be
  percussive (drum mics, drum bus, e-drum module, triggers). Full-mix input
  is supported as "best effort".
- No neural network in v1. A learned activation stage is a v2 option
  feeding the same fusion layer (section 9).
- No score following or structure detection in the engine.

## 2. Interfaces

### 2.1 Input

```cpp
struct EngineConfig {
    double sampleRate;          // 44100 .. 192000
    int    maxBlockSize;        // host block size upper bound
    int    numRoles;            // 1..4 audio roles (see InputRole)
    Meter  meter;               // beatsPerBar (1..16), beatUnit (4 or 8)
    TempoRange tempoRange;      // min/max BPM, default 50..220
    double referenceBpm;        // 0 = none; else prior centre
    FollowProfile profile;      // Stage / Rehearsal / Custom gains
};

enum class InputRole : uint8_t { Any, Kick, Snare, HiHat, Overhead, Trigger };

// Called on the audio thread once per block. Channels are mapped to roles
// by the caller (plugin bus mapping or standalone routing matrix).
void process(const float* const* roleChannels, int numSamples,
             int64_t blockStartSample, int64_t blockHostTimeUs);

// Optional discrete events (e-drum MIDI, piezo trigger, tap/footswitch),
// timestamped in the same sample clock.
void pushEvent(DiscreteOnset ev);   // {sample, role, velocity}
```

`blockHostTimeUs` is the host-time (Link clock domain) of the first sample
in the block, after input-latency compensation. See ES-03 for how it is
derived. All engine timestamps are in **samples** internally; conversion to
host time is a linear map maintained by the caller.

### 2.2 Output

```cpp
struct BeatMapSnapshot {
    double   bpm;                 // current period estimate as BPM
    int64_t  beatOriginSample;    // sample of the most recent predicted beat
    int64_t  nextBeatSample;      // prediction of the next beat
    int      beatInBar;           // 1..beatsPerBar at beatOriginSample
    int      barNumber;           // monotonic since lock (or count-in)
    float    confidence;          // 0..1
    uint8_t  state;               // Idle, CountIn, Locking, Locked, Hold, Chase
    uint32_t sequence;            // increments on every update
};
```

Published once per block via a seqlock (ES-04). Consumers read the latest
snapshot and extrapolate: `beatAt(sample) = (sample - beatOriginSample) /
periodSamples + beatIndex`.

A separate SPSC event queue carries discrete `OnsetEvent{sample, role,
strength, bandTag}` and `BeatEvent{sample, beatInBar, confidence}` for the
UI, the OSC pulse and the drift log.

## 3. Pipeline overview

```
audio roles ──> [3.1 Front end]  per role: STFT, log-compressed magnitude,
                                 band spectral flux, adaptive whitening,
                                 online peak picking ──> OnsetEvent stream
discrete events ───────────────────────────────────────┘
                     │
                     v
          [3.2 Tempo induction]  PLP-style Fourier tempogram on the fused
                                 novelty curve; top-3 tempo hypotheses with
                                 octave relations and a stability score
                     │
                     v
          [3.3 Phase/period tracker]  event-driven two-process loop
                                 (Kalman form): fast phase, slow period,
                                 expectancy-window gating, chase/hold
                     │
                     v
          [3.4 Bar tracker]      bar-position histogram from role-tagged
                                 onsets; hysteresis; learn mode; overrides
                     │
                     v
          [3.5 Supervisor]       state machine: Idle, CountIn, Locking,
                                 Locked, Hold, Chase; confidence fusion;
                                 rate limits on published tempo
                     │
                     v
                 BeatMapSnapshot
```

## 4. Front end (per role)

| Parameter | Value | Rationale |
|---|---|---|
| Analysis rate | input sample rate, no resampling | avoids resampler latency |
| Window | 1024 samples at 44.1/48 kHz (2048 at 88.2/96, 4096 at 176.4/192) | about 21 ms |
| Hop | 256 at 44.1/48 kHz (scaled with rate) | 5.3 ms frame rate, about 188 fps |
| Window function | Hann | standard |
| Magnitude compression | `log(1 + gamma * |X|)`, gamma = 1000 | PLP paper default |
| Bands | low: 20 to 150 Hz; mid: 150 Hz to 1 kHz; high: 1 to 16 kHz; full | kick / body / snare-hat separation |
| Novelty | half-wave rectified spectral flux per band, local-mean subtracted (M = 10 frames) | Böck 2012 online SF |
| Whitening | adaptive per-bin peak hold with 0.997 per-frame decay (Stowell and Plumbley) | level independence |
| Peak picking | online: frame n is a peak if `nov[n] >= max(nov[n-w1..n])`, `nov[n] >= mean(nov[n-w2..n]) + delta`, and `n - lastPeak >= minGap`; w1 = 3, w2 = 10, delta adaptive = k * running std (k = 1.0), minGap = 30 ms | Böck 2012, Krzyzaniak |
| Onset time refinement | parabolic interpolation of the novelty peak, plus optional time-domain envelope onset in the hop around the peak for Kick/Snare/Trigger roles | sub-frame precision, DAFx14 hybrid |
| Band tag | the band with the largest contribution to the peak frame: KickLike, SnareLike, Other | bar tracking evidence |

Latency budget of the front end: window/2 (10.7 ms) + 1 hop (5.3 ms) + w1
frames (16 ms) = about 32 ms worst case. This is known and constant, so it
is subtracted from every onset timestamp (`detectorDelaySamples`). The
onset position itself is refined to sub-hop accuracy, so the effective
timing error of a detected onset is the refinement error, not the latency.

Discrete events from `pushEvent` bypass the front end and enter the onset
stream directly with `strength = velocity / 127` and the role's band tag
(Kick role → KickLike, Snare role → SnareLike, Trigger → Other).

Role fusion: onsets from all roles are merged into one time-ordered stream.
When two roles report onsets within 15 ms, they are merged into one with
summed strength and both tags. Role weights (default: Kick 1.0, Snare 1.0,
Trigger 1.0, HiHat 0.5, Overhead 0.7, Any 0.7) scale the strength used by
the tracker.

## 5. Tempo induction

A real-time PLP-style Fourier tempogram, implemented from the TISMIR 2024
paper and the MIT reference code (`groupmm/real_time_plp`), on the fused
full-band novelty curve downsampled to 100 fps.

| Parameter | Value |
|---|---|
| Tempo axis | `tempoRange` at 1 BPM resolution (default 50 to 220, 171 bins) |
| Kernel | Hann, length `kernelSeconds` (default 5 s; Rehearsal profile 3 s) |
| Update | every novelty frame (10 ms) |
| Output | magnitude per tempo bin; top-3 peaks after 3-bin smoothing; stability = `peak1 / maxPeakSeen` with slow decay; `octaveRatio = peak at 2x or 0.5x / peak1` |

A tempo prior is applied multiplicatively before peak picking:

- If `referenceBpm > 0`: Gaussian in log-tempo with sigma = 12 percent,
  centred on the reference. This is the "song map" feature from
  DIFFERENTIATION.md and mirrors Live's reference-tempo behaviour.
- Else: a wide Rayleigh prior centred at 120 BPM (BTrack convention).

Octave handling: the induction never changes the tracker's period by a
factor of 2 on its own. When the best peak is within 4 percent of 2x or
0.5x the tracker's current period for more than 4 s, the supervisor raises
an `OctaveAmbiguity` flag that the UI shows and a user action (Half /
Double button, MIDI mappable) resolves. The "Auto octave" setting (off by
default on Stage, on for Rehearsal) lets the supervisor switch when
ambiguity persists for 8 bars and the bar tracker's evidence agrees.

## 6. Phase and period tracker

An event-driven two-process loop in Kalman form, after B-Keeper, Repp's
two-process model and Cemgil's tempo tracking.

State vector per beat k: `x_k = [phi_k, tau_k]` where `phi_k` is the
predicted time (samples) of beat k and `tau_k` is the period (samples).

Prediction: `phi_{k+1} = phi_k + tau_k`, `tau_{k+1} = tau_k`, with process
noise `Q = diag(q_phi, q_tau)`. `q_tau` models a random walk in period
(Hennig et al.: drift is correlated, not white), default `q_tau` equivalent
to 0.3 BPM per beat at 120 BPM.

Measurement: each onset inside the expectancy window around `phi_{k+1}` is
a measurement of `phi_{k+1}` with measurement noise
`R = (sigma_meas)^2`, `sigma_meas` = 12 ms scaled by `1 / strength`. The
expectancy window is a Gaussian with sigma `0.15 * tau` truncated at
`0.3 * tau`; onsets outside it are ignored by this loop (they still feed
tempo induction and the bar tracker). Onsets near the half-beat
(`phi + tau/2`, window sigma `0.1 * tau`) are accepted as sub-beat
measurements with weight 0.3, which keeps 8th-note hi-hat patterns useful
without letting them pull phase.

Gains are expressed as the Repp two-process constants rather than raw
Kalman gains so that profiles are musically meaningful:

| Profile | alpha (phase) | beta (period) | chase beta | hold threshold |
|---|---|---|---|---|
| Stage | 0.25 | 0.06 | 0.20 | confidence < 0.35 |
| Rehearsal | 0.40 | 0.12 | 0.35 | confidence < 0.25 |
| Custom | user | user | user | user |

Update rule per accepted measurement with asynchrony `e = onset - phi`:

```
phi  += alpha * e
tau  += beta  * e          (beta is raised to chaseBeta in Chase state)
```

Phase corrections are applied to the published timeline as a **slew**, not
a jump: the consumer-facing `beatOriginSample` moves by at most
`maxSlewMs` per beat (default 6 ms, Rehearsal 12 ms), except on a
`Relock` event (section 8), where it jumps.

Chase detection: if the last 3 asynchronies share a sign and each exceeds
`0.5 * sigma_meas`, enter Chase for the next 4 beats (beta raised). This
tracks a deliberate ritardando or push without making the loop twitchy in
steady state.

Missing beats: if no onset lands in the window for a beat, the loop
predicts forward with no update and decrements a `hitCounter`. After
`holdAfterMissedBeats` (default 2) consecutive misses, the supervisor
enters Hold: tau is frozen, phi keeps extrapolating, confidence decays
with time constant 2 s.

## 7. Bar tracker

Maintains `beatsPerBar` accumulators `S[b]`, one per bar position
hypothesis, updated on every accepted beat with exponential forgetting
`S[b] *= 0.9` per beat. For the hypothesis "this beat is bar position b",
evidence is scored from the band tags of onsets near this beat:

| Meter pattern (default 4/4 rock) | Score contribution |
|---|---|
| KickLike onset on hypothesised beat 1 or 3 | +1.0 |
| SnareLike onset on hypothesised beat 2 or 4 | +1.0 |
| SnareLike on 1 or 3, KickLike on 2 or 4 | -0.5 |
| Strong onset (strength > 0.8) on 1 | +0.5 |

Patterns are tables keyed by meter and style. Shipped patterns: 4/4 rock
(above), 4/4 half-time (snare on 3), 4/4 four-on-the-floor (kick every
beat, weight on snare 2 and 4), 3/4, 6/8 (two compound beats; kick on 1,
snare on 4 of the eighth count), 5/4 (3+2), 7/8 (user chooses grouping).
A "Learn" mode records the band-tag histogram over the first 2 bars after a
count-in (or on user command) and uses that as the pattern.

Decision rule: the bar position is accepted when `max S - secondMax S >
margin` (margin 1.5) for 2 consecutive bars. Until then the bar output is
"unconfirmed" and consumers keep the previous bar phase. User overrides:
"Downbeat now" (MIDI mappable, sets the current beat to 1 with full
score), "Shift +1 / -1".

## 8. Supervisor state machine

```
Idle ──(2..4 isochronous isolated onsets)──> CountIn ──(dense onsets)──> Locked
Idle ──(continuous onsets, induction stable 2 s)──> Locking ──(4 beats hit)──> Locked
Locked ──(2 missed beats)──> Hold ──(onset in window)──> Locked
Locked ──(3 consistent errors)──> Chase ──(4 beats)──> Locked
Hold ──(>= holdTimeout, default 20 s)──> Idle (tempo retained as reference)
any ──(user Relock / Tap)──> Locked
```

**Count-in detection**: 2 to 4 onsets with inter-onset interval coefficient
of variation under 5 percent, each preceded by at least 0.5 s of no strong
onsets, all tagged SnareLike or Other (stick clicks and hi-hat are
high-band). Tempo is set to the median IOI, phase to `lastClick + IOI`,
bar position so the next beat after the count is beat 1. A count-in with
4 clicks sets `barNumber = 1` on the following beat; with 2 clicks it
assumes "1, 2" and places beat 1 two beats later.

**Relock**: a jump of the published timeline. Triggered by user Tap or
Downbeat-now, by CountIn completion, and by the supervisor when the loop
has been in Hold and the first new onsets fit a phase that differs from the
extrapolated one by more than `0.25 * tau`. Relock sets a `relock` flag in
the snapshot so that outputs can perform their own jump (Link
`forceBeatAtTime`, MIDI SPP) once rather than slewing.

**Confidence** fusion, recomputed per beat:

```
c_hit   = hits in last 8 beats / 8
c_tempo = PLP stability (0..1)
c_inno  = exp(-(innovationRms / (0.1 * tau))^2)
confidence = 0.5 * c_hit + 0.3 * c_tempo + 0.2 * c_inno
```

Decays in Hold with time constant 2 s, floors at 0.05.

**Publishing rate limit**: the BPM in the snapshot changes by at most
`maxBpmStepPerBeat` (default 0.5 BPM; in Chase 2 BPM) per beat unless a
Relock occurred. This is what keeps Link peers and MIDI hardware from
hunting.

## 9. Extension point: learned activation (v2)

The fusion layer consumes `OnsetEvent`s. A future causal CRNN can emit
beat and downbeat activations as synthetic onsets with `bandTag = Beat` and
`strength = activation`. Nothing else changes. The model must be trained
in-house on permissively licensed data (GMD, E-GMD, own recordings) and run
through RTNeural or ONNX Runtime; it is off by default and never shipped
with CC BY-NC-SA weights.

## 10. Numerical and real-time constraints

- No heap allocation after `prepare()`. All buffers sized from
  `EngineConfig`.
- FFT via `juce::dsp::FFT` or pffft (BSD). Size fixed at prepare.
- All state is float or double; no NaN propagation: every division has a
  guarded denominator; the engine asserts finiteness in debug builds.
- Deterministic: the same input blocks in the same order produce the same
  output regardless of block size (tests enforce this across 32 to 2048
  sample blocks).
- Thread model: `process()` and `pushEvent()` on the audio thread only;
  configuration changes arrive through a `RealtimeObject` swap at block
  boundaries.

## 11. Parameters exposed to the host (automatable)

| ID | Name | Range | Default | Notes |
|---|---|---|---|---|
| `follow` | Follow | off/on | on | off = Hold with tempo frozen; the Lock footswitch |
| `profile` | Profile | Stage/Rehearsal/Custom | Stage | gain presets |
| `alpha` | Phase gain | 0.05..0.8 | per profile | Custom only |
| `beta` | Tempo gain | 0.01..0.5 | per profile | Custom only |
| `tempoMin`, `tempoMax` | Tempo range | 30..300 | 50 / 220 | |
| `referenceBpm` | Reference BPM | 0..300 | 0 | 0 = none |
| `meterNum`, `meterDen` | Meter | 1..16 / 4,8 | 4 / 4 | |
| `pattern` | Bar pattern | list | Rock 4/4 | |
| `autoOctave` | Auto octave | off/on | off | |
| `sensitivity` | Onset sensitivity | 0..1 | 0.5 | scales delta k |
| `nudge` | Nudge | -1..1 | 0 | momentary ±; shifts phase by 10 ms per press |
| `tap`, `downbeatNow`, `relock`, `halfTime`, `doubleTime` | momentary | | | MIDI mappable |

Settings that are not parameters (session state, not automatable): role
routing, mic distances, calibration offsets, output enables.

## 12. Test requirements (see RS-05 for harness)

- T1. Synthetic drum machine with tempo random walk (sigma 0.3 BPM per
  beat) and phase jitter (sigma 10 ms) at 60, 90, 120, 160, 200 BPM: beat
  F-measure at ±70 ms >= 0.98 after lock; mean predicted error <= 8 ms.
- T2. Ramp test: 120 to 132 BPM over 8 bars: tracker within 1 BPM by the
  end of the ramp; no octave error.
- T3. Fill test: 2 bars of 16th-note snare rolls inside steady playing:
  tempo deviation during the fill <= 1 BPM; no Relock.
- T4. Breakdown: 6 s silence then re-entry on the extrapolated beat:
  state goes Hold then Locked with no Relock if re-entry error < 0.25 tau.
- T5. Count-in: 4 stick clicks at 110 BPM then playing: Locked within 1
  beat; bar 1 on the first played beat.
- T6. Half-time trap: snare on 3 pattern: with pattern "half-time" selected
  bar tracker confirms within 4 bars; with "rock" it flags OctaveAmbiguity
  and does not switch when autoOctave is off.
- T7. Block-size invariance: identical output for 32, 64, 128, 256, 512,
  1024, 2048 sample blocks.
- T8. CPU: three roles at 48 kHz, 64-sample blocks: engine time <= 10
  percent of real time on the CI x86 runner.
- T9. GMD-rendered set (RS-05): F-measure >= 0.95, downbeat accuracy >=
  0.85 on 4/4 rock subset.
