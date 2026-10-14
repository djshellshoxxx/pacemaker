# F-25: Theatre score following (research spike only)

Status: draft 1.0 · Priority: P3 (not planned for build) · Estimate: 5 pd (spike) · Related: DIFFERENTIATION rank 13

## 1. Goal
Decide whether following a live pit orchestra against a known score (to drive QLab cues or backing stems) is a viable extension, and what partner integration would be needed. The full feature is very large and is explicitly not planned.
## 2. Questions to answer
1. What do musical directors need: cue-level alignment (bar number) or sample-level tempo following? (interview 5 pit MDs and 2 QLab operators).
2. Which existing tools cover it (QLab markers, Ableton, commercial score followers) and where is the gap?
3. Can the existing tracker plus a score tempo/meter map and chord/key hints (F-08) provide bar-level position with a "where are we" confidence, using DTW-style alignment of chroma to a MIDI/MusicXML score?
4. Input requirements (pit mixes, bleed) and failure behaviour.
## 3. Spike plan
| Task | pd |
|---|---|
| Interviews and competitor review | 2 |
| Prototype: offline DTW alignment of chroma (F-08) to a MIDI score on 3 recordings, report accuracy | 2 |
| Write-up and go/no-go (`docs/research/06-score-following.md`) | 1 |
## 4. Exit criteria
Go only if bar-level alignment is above 95 percent on rehearsal recordings and a partner (QLab or a theatre) commits to a pilot; otherwise close.
