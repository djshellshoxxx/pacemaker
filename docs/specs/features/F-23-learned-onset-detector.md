# F-23: Learned onset detector (activation stage)

Status: draft 1.0 · Priority: P3 · Estimate: 30 pd · Depends on: F-02 corpus, extension point ES-01 section 9 · Related: research 01, research 05 section 2

## 1. Goal and user value
If the signal-processing front end proves weak on syncopated, ghost-note-heavy or bleed-heavy real recordings, a small neural network trained on our corpus can produce a better beat/downbeat activation. This is the planned fallback to hit the accuracy gates, built only if F-02 shows the need.
## 2. Decision gate
Start only if after the F-02 tuning round T10 or T13 are missed by more than 5 points, or the bleed-augmented T14 is below target. Otherwise stay on DSP.
## 3. Requirements
- R-1 Model: causal (no look-ahead beyond 50 ms) small CRNN or TCN, under 200k parameters, input log-mel (40 bands) plus spectral flux per role, output frame-wise onset, beat and downbeat activations at about 100 fps; fixed 48 kHz and 44.1 kHz variants.
- R-2 Training data: our corpus (training drummers only), synthetic renders from the drum machine and licensed datasets after the ledger check, with augmentation (bleed, gain, polarity, resample, MP3); leave-drummer-out validation; training code in Python (PyTorch) under `training/`, deterministic seeds, config files, experiment log.
- R-3 Runtime: C++ inference without a heavy framework (hand-written GRU/conv kernels or ONNX Runtime embedded), no allocation, under 3 percent of a core for three roles at 48 kHz on the CI runner and under 10 percent on Pi 4; weights stored as a versioned binary with a checksum; fixed-point or float16 optional.
- R-4 Integration: output replaces or augments the novelty function in the tracker (weighted fusion with the DSP flux); toggle in settings "Neural onsets (beta)"; the tracker, supervisor and outputs unchanged.
- R-5 Safety: model failure or NaN falls back to DSP within one hop; confidence logging for diagnostics.
- R-6 Evaluation: improvement of at least 3 points F-measure and 5 points downbeat accuracy on the locked test set without regression on synthetic tests T1 to T7; reliability of confidence; latency not worse than 20 ms; model card with data, limits, licence.
- R-7 Licence: all training data redistributable or owned; the model weights are our property; no copyleft components in the runtime.
## 4. Test plan
F23_R3 inference equality versus the PyTorch reference (max abs error 1e-4); F23_R5 NaN injection; F23_R6 corpus A/B with the ratchet; F23_R3b CPU benchmark; block-size invariance; fuzzed weights file loader.
## 5. Plan
| Task | pd |
|---|---|
| Data pipeline, augmentation, splits | 5 |
| Model design and training experiments | 10 |
| C++ inference engine and weights format | 6 |
| Integration, fusion and settings | 3 |
| Evaluation, model card, CI gates | 4 |
| Beta test and fixes | 2 |
Risks: not enough data (synthetic plus augmentation, semi-supervised), overfit to kit types (leave-drummer-out), CPU (small model, quantisation), maintenance (versioned weights, reproducible training).
