# Changelog

All notable changes to Pacemaker are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[Semantic Versioning](https://semver.org/). The Box image is versioned with the product.

Entries from 0.1.0 describe what is in the repository. 1.0.0 is the **planned** launch contents (PROJECT-PLAN phases 2
to 4); it stays under "Unreleased" until the release checklist (RS-05 section 4) is complete, and items not yet merged
are marked as planned.

## [Unreleased]

### Planned for 1.0.0 (phases 2 to 4, from docs/PROJECT-PLAN.md)

- Plugin builds (VST3, CLAP, AU, LV2) and the standalone app on JUCE 8 (RS-02, RS-03, ES-04).
- Calibration wizards: loopback and tap-along; hardware clock offset helper (ES-03).
- Main screen, stage mode, MIDI learn, song map panel, drift report UI (RS-01, RS-04).
- Licensing wired into the product: key file, 3 machines, 14-day trial (docs/licensing/LICENSE-DESIGN.md).
- Website, KVR listing, demo video, stage test log (docs/stage-test-checklist.md).
- Signed and notarised release builds via `release.yml`; pluginval/clap-validator/lv2lint in CI.

### Added (integrations and docs, not yet released)

- REAPER: `Pacemaker.ReaperOSC` pattern config, `Pacemaker_TempoFollow.lua` ReaScript with a pure Lua OSC decoder,
  smoothing, deadband and confidence gate; Lua unit tests (`integrations/reaper/`).
- Bitwig: `Pacemaker.control.js` controller script (OSC to transport tempo).
- How-tos and samples for QLab, Resolume/VDMX, TouchDesigner (OSC In DAT callbacks) and MagicQ;
  `integrations/common/osc_tool.py` OSC monitor/simulator.
- Host guides for Live, Logic, MainStage, Reaper, Bitwig, Cubase/Nuendo, Studio One, Pro Tools, Reason, QLab,
  Resolume/VDMX/TouchDesigner (`docs/hosts/`), `docs/INSTALL.md`, `docs/stage-test-checklist.md`.
- Licence design and reference implementation: Ed25519 key file verification, machine id hashing, tiers, 14-day trial
  with clock-rollback detection, tests (`integrations/licensing/`, vendored `orlp/ed25519`, zlib licence).
- Static launch page, KVR listing text, demo video plan (`website/`) and trademark search worksheet
  (`docs/trademark-search.md`).

## [0.1.0] - repository state at this commit

### Added

- Research notes (`docs/research/01` to `04`), engineering specs ES-01 to ES-04, regular specs RS-01 to RS-06,
  differentiation analysis and project plan.
- `pacemaker_engine`: tracking engine core in plain C++17 (ES-01): STFT front end with log compression, band spectral
  flux, whitening and online peak picking; tempo induction by real-time PLP tempogram with prior and octave flags;
  phase/period tracker with Stage and Rehearsal profiles, Chase and Hold; bar tracker with patterns and overrides;
  supervisor state machine, count-in detector, confidence fusion and rate limit.
- `pacemaker_services` (ES-02, ES-03, RS-04): ClockMap, Link output behind `PACEMAKER_WITH_LINK`, MIDI clock
  scheduling, OSC output with Generic, Resolume and MagicQ profiles, bundle timetags and inbound OSC control, audio
  Sync bus renderer, loopback calibration maths, session/user settings with JSON round trips and song map import
  (CSV, AbleSet-style columns), drift log, report and MIDI tempo-map export.
- `pacemaker_eval` offline evaluation CLI and Python scorer/corpus scripts; `PacemakerTests` unit tests including a
  synthetic drum machine corpus generator (tempo random walk, phase noise, fills, count-ins, silence, ramps).
- CMake options: `PACEMAKER_WITH_LINK`, `PACEMAKER_BUILD_TESTS`, `PACEMAKER_BUILD_EVAL`, `PACEMAKER_BUILD_PLUGIN`,
  `PACEMAKER_BUILD_CLAP`, `PACEMAKER_BUILD_HEADLESS`; JUCE support helpers (`juce_support/`) and `cmake/PacemakerJuce.cmake`.
- GitHub Actions CI for Linux build, unit tests and engine regression on the synthetic set.

### Known gaps at 0.1.0

- Plugin and standalone UI are not built into the default configuration; no installers; no licensing in product.
- Several engine thresholds (T6, T8, T9) and the GMD pipeline are pending (see PROJECT-PLAN phase 1).
