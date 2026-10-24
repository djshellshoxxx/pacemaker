# F-17: Tempo map export and DAW send

Status: draft 1.0 · Priority: P1 · Estimate: 8 pd · Depends on: drift log (existing) · Related: RS-01 section 7, F-24

## 1. Goal and user value
Producers recording a live band need the real tempo map in the DAW so they can edit to the grid. No live tool produces it. Export and, where possible, send it directly.
## 2. Requirements
- R-1 Exports: MIDI tempo track (SMF format 0 and 1, 480 or 960 ppq, existing writer improved), Reaper tempo envelope (`.RPP` snippet or script), Logic tempo (via MIDI import of tempo track, documented), Cubase and Studio One via SMF import, Ableton Live (warp markers cannot be written reliably; provide SMF and a documented manual flow; **VERIFY** current Live import options), CSV of beat times and bpm, and a Cue/marker file with bar numbers and song names.
- R-2 Tempo map quality: beat-synchronous points, smoothing by a cubic fit with a user tolerance (0.05 to 1 BPM), keeping bar lines on tracked downbeats; tempo change events limited to a maximum count; first beat anchored at time zero or at a given offset (session start, count-in).
- R-3 Bar line mapping: exported maps include time signature events and bar markers so the DAW's bar 1 matches the band's bar 1; pickup measures handled.
- R-4 Latency alignment: export uses input-compensated beat times (ES-03) so the map aligns with the recorded audio when the recording starts at the same moment; offset field for other cases.
- R-5 Range selection: whole session, a song, or a time range; per-song files in one zip.
- R-6 Send: Reaper via the bundled ReaScript receiving OSC (F-27) can build the tempo map live or after the take; other DAWs via files.
- R-7 Validation: exported SMF parsed back gives the same beat times within 1 ms for all tempo changes retained; the reconstructed grid deviates from tracked beats by at most the tolerance.
- R-8 UI: Export dialog with preview curve, tolerance slider, format list, and "open folder".
## 3. Design
`TempoMapBuilder(beats[], tolerance) -> vector<TempoEvent{tick, usPerQuarter}>` (Ramer-Douglas-Peucker on the cumulative-time curve), `SmfWriter`, `ReaperWriter`, `CsvWriter`. All pure functions with golden tests.
## 4. Tests
F17_R2 builder properties (monotonic, error bound, event count); F17_R7 round trip with an independent SMF parser (`mido`) and a Python script; goldens for each format; F17_R3 pickup and meter change; UI screenshot of preview.
## 5. Plan
| Task | pd |
|---|---|
| Tempo map builder and SMF writer upgrade | 2.5 |
| Reaper, CSV, marker writers | 1.5 |
| Latency alignment, ranges, per-song zips | 1 |
| Export dialog and API | 1.5 |
| DAW import checks (Reaper, Logic, Cubase, Studio One), docs | 1.5 |
Risks: DAW import quirks (checklist, per-DAW notes); ppq rounding (960 ppq default, tolerance).
