# F-29: Quality infrastructure

Status: draft 1.0 · Priority: P0 · Estimate: 20 pd · Owner: foundation
Depends on: F-30 (partly) · Implements: `docs/dev/QUALITY-STRATEGY.md`

## 1. Goal and user value
Build the machinery that keeps bugs out and development fast: CI tiers, sanitizers, static analysis, fuzzing, traceability, accuracy ratchet, benchmarks, hardware rig, templates and scripts. Users benefit through fewer regressions and faster releases.
## 2. Requirements
- R-1 CI workflows: `pr-fast` (T1), `pr-full` (T2, macOS and Windows), `nightly` (T3), `weekly-release` (T4) as in QUALITY-STRATEGY section 3, with ccache, dependency caches, job summaries and artifacts (eval reports, jitter reports, coverage, sanitizer logs).
- R-2 Sanitizer builds (ASan+UBSan per PR, TSan nightly) with suppression files reviewed monthly.
- R-3 `.clang-format`, `.clang-tidy`, cppcheck config; `tools/dev/check.sh` runs T0 locally; pre-commit hook installer.
- R-4 Fuzz harnesses for Json, OSC, HTTP parser, Settings merge, CSV/setlist, SMF writer round trip, WAV reader, MIDI parser; corpora in `Tests/fuzz/`; PR job replays corpora; nightly fuzzes 10 minutes per target; crashers are saved as regression files automatically.
- R-5 Traceability tool `tools/dev/trace.py` (R-n to test IDs; fails on uncovered P0/P1 requirements; produces the coverage table).
- R-6 Accuracy ratchet: `eval.json` schema, baseline file, comparison tool with noise bands, PR comment bot (GitHub Actions script) posting the metric table; baseline update workflow requires review.
- R-7 Benchmarks: `pacemaker_bench` and CI comparison to stored baselines on a pinned runner class; trend chart in the nightly artifacts.
- R-8 Coverage reports with floors per module (engine 85, outputs 85, host 70) and a diff-coverage check on PRs (new lines at least 80 percent covered).
- R-9 Real-time harness integration (F-30) in unit tests and nightly TSan run of the full threaded host test.
- R-10 Hardware-in-the-loop rig on a self-hosted runner: Pi plus USB MIDI interface with loopback, audio interface with loopback cable, logic analyser; scripts run F-05 jitter, F-01 latency and Sync-bus alignment nightly; results archived.
- R-11 Developer experience: dev container, `tools/dev/setup.sh`, feature scaffolding script, PR and issue templates, CODEOWNERS, ADR template, changelog tooling (conventional commits optional), branch protection rules documented and applied (required checks T1 and T2).
- R-12 Flaky test tracking: CI annotates failures that pass on re-run (not allowed to auto-retry tests; only infra steps), weekly report of flake rate; policy in QUALITY-STRATEGY section 5.
- R-13 Security hygiene: dependency pin check, secret scanning, licence ledger gate (F-26), SBOM artifact.
- R-14 Mutation testing job for selected modules (monthly), report only.
## 3. Test plan (of the infrastructure)
Each tool has a self-test: planted failures prove the gates fail (a deliberate allocation in an audio-thread function, a planted data race, a requirement without a test, a metric regression, a GPL dependency, an uncovered diff). Gate self-tests run weekly.
## 4. Acceptance
A PR with a deliberate regression in each category is blocked by the corresponding gate with an understandable message; T1 median duration under 6 minutes; nightly report published; hardware rig produces first jitter report.
## 5. Plan
| # | Task | pd |
|---|---|---|
| Q-1 | CI workflows, caches, artifacts, branch protection | 4 |
| Q-2 | Sanitizers, clang-tidy/format, pre-commit, check script | 2.5 |
| Q-3 | Fuzz harnesses and corpora, nightly fuzz job | 3 |
| Q-4 | Traceability tool and PR template | 2 |
| Q-5 | Ratchet tooling and PR comment bot | 3 |
| Q-6 | Bench harness and baselines | 1.5 |
| Q-7 | Coverage and diff coverage | 1 |
| Q-8 | Hardware rig set-up and scripts | 2.5 |
| Q-9 | Dev container, scaffolding, templates, docs | 1.5 |
Order: Q-1, Q-2 (week 1 to 2) then Q-4, Q-3, Q-5 in parallel with feature work; Q-8 when F-05 starts.
Risks: CI cost and time (ccache, tiers, nightly for heavy jobs); self-hosted runner reliability (alerts, manual fallback); gate fatigue (clear messages, noise bands).
