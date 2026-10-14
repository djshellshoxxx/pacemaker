# F-15: Setlist import (AbleSet and generic)

Status: draft 1.0 · Priority: P2 · Estimate: 4 pd · Depends on: song map (existing) · Related: RS-01 section 6, RS-04 section 3

## 1. Goal and user value
Bands already keep setlists in other tools. Importing them fills the song map (name, reference BPM, meter) without retyping and avoids octave errors.
## 2. Requirements
- R-1 Importers behind one interface `SetlistImporter { canParse(bytes, name); parse(bytes) -> vector<Song> }` with warnings per row.
- R-2 Supported: Pacemaker CSV and JSON (existing), generic CSV with auto-detected columns (title/name, bpm/tempo, time signature as `4/4`, key optional), AbleSet export (format to be confirmed from AbleSet documentation or user-provided sample; **VERIFY** before implementing), Ableton Live set text export via a documented Max/script path (optional), plain text lines "Title - 120".
- R-3 Preview table with per-row warnings (missing BPM, BPM outside 30 to 300, odd meters, duplicate titles); user chooses replace or append.
- R-4 Meter parsing: `4/4`, `6/8`, `7/8`, `5/4`; unknown becomes 4/4 with a warning; grouping suggestions for odd meters (F-07).
- R-5 Size and safety: 1 MB limit, 500 songs, UTF-8 and UTF-16 with BOM, malicious input never crashes (fuzz target), no network access.
- R-6 Drag-and-drop in the UI and `POST /api/songs/import` with `format` hint.
## 3. Tests
F15_R1..R5 golden files for each format; fuzz; encoding tests; preview warnings; UI import flow screenshot.
## 4. Plan
| Task | pd |
|---|---|
| Interface, generic CSV/text, encodings, preview | 1.5 |
| AbleSet importer (after sample obtained) | 1 |
| UI drag-and-drop, API | 0.5 |
| Fuzz and tests | 1 |
Risk: format unknown or changes (importer is isolated, generic CSV fallback; ask AbleSet author for a sample).
