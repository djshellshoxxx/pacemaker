# F-28: Audio-pulse clock plugin and latency calibrator (spin-offs)

Status: draft 1.0 · Priority: Spin · Estimate: 10 pd · Depends on: F-03, F-30, calibration maths (existing) · Related: ES-02 section 5, ES-03, DIFFERENTIATION section 4

## 1. Two small products from existing code
**A. Audio-pulse clock plugin.** Renders a precise pulse/click clock on an audio output from the host transport or Link tempo, as a software alternative to hardware clock boxes (E-RM Multiclock, Innerclock class), for sync-ing analogue and modular gear sample-accurately.
**B. Latency calibrator.** A utility that measures round-trip latency of an interface (loopback) and writes DAW recording offsets or prints the values, using the ES-03 maths.
## 2. Requirements (A)
- R-1 Plugin (VST3, AU, CLAP) with a stereo or multichannel output: pulse at 1, 2, 4, 24, 48 ppqn, beat click, start/stop gate, reset pulse, polarity, pulse width, level (including +6 dBFS), division and swing; sample-accurate from host `ppq` position or Link.
- R-2 Phase locked to the host transport (loop, jump handling: re-sync, no stray pulses) and tempo changes (ramps rendered with correct spacing).
- R-3 Clock offset in samples and ms, with a calibration helper using a loopback input.
- R-4 CPU negligible; no allocations; state saved with the project.
## 3. Requirements (B)
- R-5 Standalone app and CLI: choose output/input pair, run the loopback (8 clicks, ES-03 section 4), show round trip, reported latency, hidden latency, pass/fail; save to a JSON profile and print DAW instructions (Live: Recording/Monitoring delay, Reaper: Preferences recording offset, Logic: I/O buffer recording delay, Cubase: Record shift); optional acoustic mode with a speaker and microphone.
- R-6 Accuracy +-1 sample at 48 kHz (L1) with noise robustness (L2); multichannel interface support; sample rates 44.1 to 192 kHz.
## 4. Tests
F28_R2 pulse positions versus ppq with loops and tempo ramps (offline render, exact sample positions); F28_R1 levels and formats; F28_R5 loopback with `FileBackend` simulated device (L1, L2); installer smoke; plugin validation (pluginval).
## 5. Plan
| Task | pd |
|---|---|
| Pulse renderer generalisation (ppqn, transport, loops) | 3 |
| Plugin shell and parameters | 2 |
| Calibrator app/CLI on `AudioBackend` | 3 |
| Tests, docs, packaging | 2 |
Risks: small market (keep scope minimal, reuse code), host transport quirks (loop and jump handling tests).
