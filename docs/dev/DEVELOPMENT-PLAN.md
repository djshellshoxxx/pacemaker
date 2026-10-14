# Development plan

Status: draft 1.0. Supersedes the phase list in `docs/PROJECT-PLAN.md` for
scheduling; the checklist there stays the source of truth for what is
done. Estimates are person-days (pd) for one experienced C++ audio
developer; calendar time assumes two developers working in parallel tracks
and 70 percent focus time (the rest is review, support, meetings).

## 1. Principles

1. **Measure before you build.** The engine has only met synthetic
   drumming. Real-drummer data (F-02) comes first and gates every
   accuracy claim and every tuning change.
2. **Walking skeleton first.** Get real audio into the existing engine and
   web UI (F-01) before any plugin work. A runnable standalone with a web
   UI is a product in itself and de-risks the plugin.
3. **Abstractions that make the hard parts testable.** `HostClock`,
   `MidiSink`, `AudioDevice`, `LinkSession` (F-30) so that timing logic runs
   in virtual time on any CI machine and platform backends are thin.
4. **Small vertical slices behind flags.** Every feature lands as a
   sequence of PRs that each keep `main` releasable; incomplete work sits
   behind a build or runtime flag.
5. **Quality gates, not heroics.** Rules in `QUALITY-STRATEGY.md` are enforced
   by CI, not by memory.
6. **Long-lead items start on day 1:** Link licence request, trademark
   search, drummer recruiting and studio booking, JUCE licence decision,
   Apple Developer and Windows signing accounts.

## 2. Milestones

| Milestone | Outcome (demo-able) | Features | Gate to pass |
|---|---|---|---|
| **M3 Hear real drummers** | Standalone host takes a real interface or e-drum and locks onto a live kit; web UI shows it; scorecard on real data | F-30, F-01, F-06, F-02 (first 3 drummers), F-29 (CI core) | T10 to T14 on real corpus meet thresholds or have a tuning plan; CPU budget T8 |
| **M4 Plugin alpha** | VST3 and CLAP in Reaper, Live and Logic follow a drummer and output MIDI clock, OSC, Link | F-03, F-04, F-05, F-07, F-14 | pluginval 5, clap-validator, auval pass; O1, O3 pass on three OSes |
| **M5 Closed beta** | Five bands use it for real rehearsals; stage mode; setlists | F-10, F-11, F-12, F-13, F-17, F-26, F-27 | Manual stage checklist (RS-05 1.5) logged; crash-free sessions >= 99 percent; beta bug burn-down |
| **v1.0** | Public release (plugin and standalone, three OSes) | all P0 and P1 | Release checklist; licence ledger clean; trademark cleared |
| **v1.1 Insight** | Key, chord, pitch, percussion info; setlist import; lighting presets | F-08 phases 1 to 5, F-09, F-15, F-16, F-22 | Accuracy targets M-T on own corpus; analysis CPU budget |
| **v1.2 Box and pro** | Raspberry Pi appliance, redundancy, Windows clock utility, offline tool | F-19, F-18, F-20, F-24, F-28 | Field test with a hardware-only act |
| **Later** | Section suggestions, learned onsets, ML melody, score following research | F-21, F-23, F-08 phase 6, F-25 | Per-feature spikes |

## 3. Sequence and critical path

```
Week  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Dev A  [F-30][F-01 audio ......][F-06][F-07 odd meters ..][F-03 JUCE plugin ..............]
Dev B  [F-29 CI core ..][F-02 corpus build + scorer .......][F-05 MIDI platforms ....][F-04 Link]
Parallel (non-dev): Link licence, trademark, drummer sessions (weeks 2-8), JUCE licence, signing accounts
Weeks 25-34: F-14, F-10, F-11, F-12, F-13, F-17, F-26, F-27, beta, fixes.   v1.0 at about week 36.
```

Critical path: F-30 -> F-01 -> F-02 tuning loop -> F-03 -> beta fixes. F-04
waits on the Ableton licence but its policy is already written and tested
(mock session), so a late licence costs one week, not a quarter.
Contingency: 20 percent buffer on every milestone; the tuning loop after
F-02 is the biggest unknown and is time-boxed to 4 weeks, after which the
Learned onset detector (F-23) moves forward.

## 4. Work breakdown by track

| Track | Features | Total pd |
|---|---|---|
| Engine and accuracy | F-02, F-07, F-23 | 69 |
| I/O and outputs | F-01, F-04, F-05, F-06, F-10, F-20 | 80 |
| Apps and UX | F-03, F-11, F-12, F-13, F-14, F-17, F-19, F-24, F-28 | 127 |
| Insight (music analysis) | F-08, F-09, F-21 | 110 |
| Product and release | F-15, F-16, F-18, F-22, F-25, F-26, F-27 | 59 |
| Foundation and quality | F-29, F-30 | 26 |

## 5. Ways of working

- **Branching:** trunk-based. Short-lived branches `feat/F-NN-short-name`,
  draft PR on first push, merge when CI green and one review done, squash
  merge. `main` is always releasable; release branches only for hotfixes.
- **Spec first:** a feature starts when its spec is "ready" (section 6).
  Spec changes go through the same PR flow; the spec is updated in the
  same PR as the code that changes behaviour.
- **Weekly cadence:** Monday plan (pick tasks from this file), Friday demo
  of the running app (screenshots in the PR), accuracy dashboard reviewed.
- **Spikes** are time-boxed (2 to 5 days), end in a written finding in
  `docs/research/`, and never merge production code.
- **Decision log:** architecture decisions as short ADRs in `docs/adr/`
  (template in QUALITY-STRATEGY section 9).
- **Risk review** every two weeks using the register in section 8.

## 6. Definitions

**Ready (spec):** goal and non-goals clear; every requirement testable; test
IDs assigned; interfaces drafted; dependencies available; estimate agreed;
risks listed.

**Done (feature):** all requirements have passing tests tagged with their
IDs; docs and `PROJECT-PLAN.md` updated; no new warnings; sanitizers and
static analysis clean; CPU and latency budgets measured and recorded;
accuracy dashboard not regressed; UI reviewed on desktop, phone and stage
mode with screenshots; changelog entry; feature flag removed or default
decided.

## 7. Release plan

- **Versioning:** semantic versions; plugin state has its own integer
  version with migrations (RS-04).
- **Channels:** nightly (internal), beta (opt-in testers, weekly), stable.
- **Hotfix rule:** a regression found in stable gets a test first, then
  the fix, then a patch release within 2 working days.
- **Checklist:** RS-05 section 4 plus licence ledger, signing, notarisation,
  pluginval, installer test on clean VMs for each OS.

## 8. Risk register

| # | Risk | Likelihood | Impact | Mitigation | Owner |
|---|---|---|---|---|---|
| R1 | Accuracy on real kits worse than synthetic | High | High | F-02 first; per-input tuning; Learn mode; F-23 as fallback; honest confidence UI | Engine |
| R2 | Link licence delayed or costly | Medium | Medium | Policy and mock done; GPL edition option; MIDI clock and OSC still ship | Product |
| R3 | MIDI clock jitter on Windows | Medium | High | Spin path baseline, Windows MIDI Services backend, audio pulse recommendation | I/O |
| R4 | Host quirks (sidechain, bus layouts, latency reporting) | High | Medium | Host matrix, standalone fallback, host guides | Apps |
| R5 | JUCE or CLAP licensing or API change | Low | Medium | Isolate behind thin wrappers; evaluate before purchase | Apps |
| R6 | Dataset licences block commercial use | Medium | Medium | Own corpus; licence ledger gate | Engine |
| R7 | Music analysis disappoints (key/chords wrong) | Medium | Medium | Best-effort UI with confidence and "off" default until proven; own-corpus targets | Insight |
| R8 | Scope creep from many P2/P3 ideas | High | High | Milestone gates; P0/P1 only until v1.0 | Product |
| R9 | Flaky timing tests erode trust in CI | High | Medium | Virtual time (F-30); real-time tests isolated and tolerance-checked | Foundation |
| R10 | Single-developer bus factor | Medium | High | Specs, ADRs, weekly demos, pairing on the engine | All |
| R11 | Trademark conflict on "Pacemaker" | Low | High | Search now; fallback names recorded | Product |
| R12 | Apple notarisation or Windows signing delays | Medium | Medium | Set up accounts in week 1; test the pipeline with a dummy app | Release |

## 9. Estimates summary

| Scope | pd |
|---|---|
| v1.0 (P0 + P1) | 234 |
| v1.1 (P2: F-08, F-09, F-15, F-16, F-22) | 112 |
| v1.2 and later (P3 and Spin) | 125 |
| Everything | 471 |
| Buffer 20 percent on top of v1.0 | 47 |

Two developers: v1.0 in about 36 weeks including beta; music analysis
phases can start in parallel after M3 if a third developer or contractor is
available.
