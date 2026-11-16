# F-24: Tempo map from a drum stem (offline app)

Status: draft 1.0 · Priority: Spin · Estimate: 8 pd · Depends on: F-17, engine, FileBackend · Related: DIFFERENTIATION section 4

## 1. Goal and user value
Drag in a recorded drum track (or full mix) and get a tempo map, bar lines and optionally key and chords, ready for the DAW. Useful to producers and a funnel for the main product.
## 2. Requirements
- R-1 Input: WAV/FLAC/AIFF (via a vendored permissive decoder such as dr_libs, or JUCE's readers in the app), mono or multichannel; channel to role mapping with sensible defaults; sample rates 44.1 to 192 kHz.
- R-2 Processing: runs the engine faster than real time (at least 20x), two passes: forward pass, then a backward pass to refine the first bars and to repair relocks (offline smoothing); outputs beat times, bar numbers, tempo curve, confidence, sections of low confidence.
- R-3 Review UI: waveform with beat grid overlay, drag to correct beats/downbeats, set reference BPM and meter, re-run on a selected range, undo/redo.
- R-4 Export through F-17 (SMF tempo map, Reaper, CSV, markers); optional key/chord chart (F-08) when enabled.
- R-5 CLI: `pacemaker_tempomap in.wav --out map.mid --meter 4/4 --ref 120`; batch mode over a folder; JSON report.
- R-6 Accuracy: on the corpus the offline map has F-measure at least 0.98 and tempo error under 0.5 BPM except flagged regions.
- R-7 Packaging: small standalone app on three OSes and the CLI; no network; licence per F-26.
## 3. Design
Reuses the engine through `FileBackend`-style offline driver; the backward pass runs the tracker on time-reversed audio and fuses forward/backward beats (agreement check). Review UI is the web UI served locally in a window (WebView) or the browser.
## 4. Tests
F24_R2 forward/backward fusion on synthetic and corpus excerpts (improvement of first-bar accuracy); F24_R5 CLI goldens; F24_R3 UI e2e (Playwright) editing and export; speed benchmark.
## 5. Plan
| Task | pd |
|---|---|
| Offline driver, two-pass fusion | 3 |
| CLI and decoding | 1.5 |
| Review UI | 2.5 |
| Packaging and tests | 1 |
Risks: backward pass assumptions (tempo continuity only), editing complexity (limited to drag and reference BPM).
