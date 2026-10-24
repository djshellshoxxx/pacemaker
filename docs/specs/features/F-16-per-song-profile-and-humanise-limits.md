# F-16: Per-song profile and humanise limits

Status: draft 1.0 · Priority: P2 · Estimate: 5 pd · Depends on: song map, F-07 (patterns) · Related: ES-01 section 6, RS-04

## 1. Goal and user value
Different songs need different behaviour: a click-tight dance track should reject tempo wander, a ballad should follow breathing tempo freely. Per-song limits let the operator trade tracking agility against stability, per song.
## 2. Requirements
- R-1 Song fields (all optional): `profile` (stage, rehearsal, custom), `maxTempoDeviationPct` (clamp around reference BPM, default off), `maxSlewBpmPerSec`, `holdOnLowConfidence` (bool), `patternId`, `meterMap`, `countInBeats`, `sensitivity`, `tempoRange`.
- R-2 Applying a song changes only tracker parameters at the next beat boundary, never restarts the tracker, and never changes output settings (RS-04).
- R-3 Deviation clamp: published tempo is limited to reference +- X percent; the internal estimate continues; when the estimate leaves the band for more than 8 beats the UI shows "Tempo outside song range" and offers "Release limit".
- R-4 Slew limit applies to published tempo (all outputs), complementing the phase slew in the profile.
- R-5 "Tight", "Natural", "Free" quick presets set the three limits; the editor shows what each does in words and with a small simulated example curve.
- R-6 Settings migration: songs without the new fields behave as before.
- R-7 Export/import of song settings with the song map JSON/CSV (new columns optional).
## 3. Design
`SongTrackerSettings` converted to an `EngineSettings` patch delivered through the realtime settings object (ES-04 section 3); clamp and slew implemented in the publish stage of the tracker (single place), unit-tested in isolation.
## 4. Tests
F16_R3 clamp and release; F16_R4 slew; F16_R2 patch at beat boundary without Relock; corpus A/B: Tight mode reduces published tempo SD by at least 50 percent on a steady click-like excerpt while Free keeps the T2 ramp result; migration test.
## 5. Plan
| Task | pd |
|---|---|
| Fields, migration, import/export | 1 |
| Clamp and slew in publish stage, patch application | 2 |
| UI editor and quick presets | 1.5 |
| Tests and corpus A/B | 0.5 |
Risks: clamp hiding real tempo changes (visible state, release button, default off).
