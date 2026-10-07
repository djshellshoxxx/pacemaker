# RS-05: Testing, CI and release (regular spec)

Status: draft 0.1. Research: `docs/research/04-plugin-architecture-and-latency.md` section 5, `docs/research/01-real-time-beat-tracking.md` section 6.

## 1. Test layers

1. **Unit tests** (`PacemakerTests`, Catch2 or JUCE UnitTest as in
   Vivisect): front end, induction, tracker, bar tracker, supervisor,
   ClockMap, calibration maths, state round trips.
2. **Engine regression** (`pacemaker_eval` plus a Python scorer using
   `mir_eval`, MIT): runs the engine over the test corpus at several block
   sizes and reports F-measure (±70 ms), CMLt, AMLt, time-to-lock, mean and
   SD of predicted beat error, relock count, octave errors, downbeat
   accuracy. Thresholds in ES-01 section 12.
3. **Output tests**: MIDI clock loopback jitter, Sync bus alignment, OSC
   timetag checks, Link two-process test (ES-02 section 9).
4. **Plugin validation**: pluginval strictness 5 (VST3, AU),
   clap-validator, lv2lint.
5. **Manual stage test checklist** before each release: real kit, two mics,
   Live and Logic, one hardware box on MIDI clock, one lighting app on
   Link.

## 2. Test corpus

| Set | Source | Licence | Use |
|---|---|---|---|
| Synthetic drum machine | generated in-repo | ours | tempo random walk, phase noise, fills, count-ins, silence, ramps |
| GMD and E-GMD renders | Magenta datasets rendered through our own samples | CC BY 4.0 | real human microtiming with known tempo |
| Own recordings | 3 drummers, kick/snare/overhead stems, hand-tapped annotations | ours | the ground truth that matters |
| Ballroom, GTZAN subsets | public | academic | sanity only, full-mix robustness |

Annotation format: one line per beat `timeSeconds beatInBar`. Stored under
`Tests/corpus/` with a manifest; large audio fetched by a script, not
committed.

## 3. CI (GitHub Actions)

- `ci.yml`: Linux build of engine, tests and eval; runs unit tests and the
  engine regression on the synthetic set and a small GMD subset on every
  PR. Fails on threshold regressions.
- `release.yml`: matrix Windows x64, Linux x64, macOS universal (arm64 and
  x86_64). Builds VST3, CLAP, Standalone everywhere, AU on macOS, LV2 on
  Linux. Runs pluginval. Uploads zips as artifacts; publishes a GitHub
  Release on `v*` tags. macOS signing and notarisation only on tags.
- Thread sanitizer job weekly.

## 4. Release checklist

- Version bumped in CMakeLists.
- Changelog entry.
- All CI green including regression thresholds.
- Manual stage test done and logged.
- macOS notarised; Windows signed if a certificate exists.
- INSTALL.md updated for any new host notes.
