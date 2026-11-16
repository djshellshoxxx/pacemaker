# Quality strategy: fewer bugs, faster delivery

Status: draft 1.0, normative for all contributors. Implemented by F-29
(quality infrastructure) and F-30 (foundation). The aim is a short feedback
loop: most defects should be caught within two minutes on a laptop, the
rest within one CI run, and the remainder before a release by the nightly
and beta process.

## 1. Principles

1. Specs are the contract; requirements map to tests (traceability).
2. Determinism beats retries: no test depends on wall-clock timing unless
   it is explicitly a real-time test.
3. Real-time code is checked mechanically, not by eye.
4. Accuracy is a number tracked per commit with a ratchet, never a feeling.
5. Anything that fails twice gets a regression test.
6. Make the right thing the easy thing: scripts, templates, pre-commit hooks.

## 2. Spec-driven traceability

- Each requirement `R-n` in a feature spec appears in tests as a tag:
  `TEST_CASE(F04_R3_ForceOnlyOnRelock)`; `tools/dev/trace.py` parses
  `docs/specs/**` and `Tests/**`, and CI fails when a P0 or P1 requirement
  has no test or a test tag points to a missing requirement.
- The PR template asks: which requirement IDs does this change satisfy,
  and which spec lines changed.
- A coverage-of-requirements table is published as a CI artifact.

## 3. Test pyramid and CI tiers

| Tier | Runs | Budget | Contents |
|---|---|---|---|
| **T0 pre-commit** | on commit | < 10 s | clang-format, clang-tidy on changed files, spec lint, trailing whitespace |
| **T1 PR fast** | every push | < 6 min | build Debug+ASan/UBSan and Release on Linux; unit tests (labels `unit`); JSON/OSC/HTTP fuzz corpus replay; virtual-time output tests; engine synthetic suite subset (T1 to T7); UI smoke |
| **T2 PR full** | PR ready for review | < 20 min | macOS arm64 and Windows MSVC build and unit tests; real-time tests (`realtime` label) with tolerance; corpus subset scoring with ratchet; plugin load test with pluginval (once F-03 exists) |
| **T3 nightly** | 02:00 | < 2 h | full corpus scoring and dashboard; TSan job; 10 minute fuzz per target; mutation testing on a rotating module; long soak (6 h simulated, leak check); CPU benchmarks versus baseline; hardware-in-the-loop on the self-hosted rig |
| **T4 weekly and release** | weekly, tags | < 6 h | host matrix smoke (Reaper headless render, Live and Logic by script where possible), installer tests on clean VMs, upgrade and state-migration tests, signing and notarisation dry run |

Test labels: `unit`, `engine`, `output`, `realtime`, `corpus`, `ui`, `soak`.
Every test is deterministic; seeds are printed on failure and replayable
with `--seed`.

## 4. Static and dynamic analysis

- Compiler: `-Wall -Wextra -Wconversion -Wshadow -Wpedantic`, `-Werror` in CI.
- clang-tidy profile: `bugprone-*`, `performance-*`, `cppcoreguidelines-*`
  (selected), `concurrency-*`, `modernize-use-*`; suppressions need a reason
  comment. cppcheck as a second opinion nightly.
- Sanitizers: ASan+UBSan in every PR; TSan nightly (engine, clock thread, OSC
  thread, HTTP, music analysis thread running together); MSan optional.
- **Real-time safety:** (1) audio-thread entry points are annotated
  (`PM_AUDIO_THREAD`); (2) a test-only allocation hook (override of
  `operator new`, `malloc` interposition) fails any allocation or lock
  inside functions executed under `ScopedAudioThread`; (3) clang's
  RealtimeSanitizer with `[[clang::nonblocking]]` where the installed clang
  supports it (**VERIFY** availability on the CI image); (4) a banned-API
  grep in CI for `std::mutex`, `new`, `std::vector::push_back`, `printf`
  in files tagged as audio-thread code.
- **Fuzzing:** libFuzzer targets for `Json::parse`, `oscDecode`, HTTP request
  parser, `Settings::merge`, CSV song import, SMF writer round trip, MIDI
  input parser (F-06), setlist importers (F-15). Corpora checked in, replayed
  in T1, fuzzed nightly. Crashers become regression files.
- **Property tests:** invariants such as "ClockMap round trip", "tick spacing
  monotonic", "OSC timetags monotonic", "block-size invariance" run on random
  inputs (own tiny generator, no dependency).
- **Mutation testing** (mull or dextool) on `Tracker`, `BarTracker`,
  `MidiClockGenerator`, `LinkPolicy` quarterly; surviving mutants become tests.
- **Coverage:** gcov/lcov in the nightly job; floors: engine 85 percent line,
  outputs 85, host 70; floors only go up.

## 5. Deterministic time (F-30)

All timing code takes a `HostClock&`. Production uses the steady clock (or
the Link clock when Link is on); tests use `VirtualClock` which advances only
when the test says so. The generator classes are already pure; threads
(`ClockThread`, `OscRunner`) accept a clock and a `Sleeper`. Result: the
24-ticks-per-beat, relock, offset and SPP tests run in microseconds with exact
assertions, and a small number of `realtime` tests measure the real thread
against tolerances and run in their own CI job so noisy neighbours cannot
fail the unit tier.

Flaky test policy: a test that fails without a code change is a P1 bug. It
is fixed within 2 working days or moved to the `realtime` tier with an
explicit tolerance and a ticket. Quarantining without a ticket is not
allowed; disabling tests to get green is never allowed.

## 6. Accuracy and performance ratchets

- `pacemaker_eval` writes `eval.json` (per file and aggregate metrics). CI
  compares against `Tests/baselines/eval-baseline.json`. A PR fails if any
  headline metric (beat F-measure, downbeat accuracy, phase error SD,
  time-to-lock, relock count) worsens by more than its noise band; a PR that
  improves a metric updates the baseline in the same commit (the "ratchet").
- Metrics are posted as a PR comment table (before, after, delta).
- CPU: `pacemaker_bench` runs the engine and outputs at 48 kHz, 64-sample
  blocks, reports percent of real time and worst block time; budgets in each
  spec; fail at 120 percent of the stored baseline on the CI machine class.
- Memory: peak and steady RSS recorded; the 6 h soak must not grow by more
  than 1 percent.
- Latency and jitter: the hardware rig logs MIDI clock jitter, Sync bus
  alignment and end-to-end latency nightly.

## 7. Code standards

- C++17; no exceptions or RTTI across the audio thread; no allocation, lock,
  syscall or logging in audio-thread code; fixed-capacity containers.
- Thread ownership documented at the top of each class (who may call what).
- Every public function with a numeric unit says it (`Samples`, `Us`, `Bpm`
  in names or strong types for new code).
- One responsibility per file, headers include only what they use, no
  global mutable state, singletons only for OS-level services (Link).
- Errors: outputs never throw into the audio thread; failures become
  `OutputStatus` and a UI warning; config errors are reported and defaults
  used.
- Naming and format by `.clang-format` (checked in).
- Comments explain why and cite the spec section; no commented-out code.

## 8. Review and collaboration

- PR template: summary, spec IDs, test evidence (paste of command and
  result), screenshots for UI, risk, rollback.
- Review checklist: real-time safety, thread ownership, units, error
  paths, test determinism, spec updated, public API compatibility,
  accessibility (UI), security (input parsing, network exposure).
- Size limit: aim for under 400 changed lines; larger changes are split or
  carry a design note.
- Security: network listeners bind to localhost by default, input size
  limits everywhere, no shell execution, dependency licences recorded
  (F-26), `third_party/` pinned by hash, SBOM generated per release.

## 9. ADRs and documentation

`docs/adr/NNNN-title.md`: context, decision, consequences, alternatives, date,
status. Required for: new third-party dependency, threading model change,
file/state format change, public API change. Docs rules: README stays
runnable, every spec has a status line, every output or protocol has an
example message in docs, changelog entry per user-visible change.

## 10. Observability and support

- Diagnostics bundle (F-14): version, OS, devices, settings, last 10 minutes
  of drift log and events, CPU and xrun counters, anonymised.
- Crash reporting (opt-in): crashpad or breakpad minidumps with symbol
  upload in the release pipeline.
- Telemetry (opt-in, minimal): app version, OS, host name, feature usage
  counts, never audio or setlist content; documented in the privacy page.
- Beta feedback: one-click "send diagnostics" and a session recording of the
  novelty and onset stream (small) so tracking problems can be replayed
  offline with `pacemaker_eval --replay`.

## 11. Speed of development

- Build: Ninja, ccache locally and in CI, precompiled headers for tests,
  `-O1` Debug for sanitizer builds, separate fast test target
  (`PacemakerUnit`) that links only what it needs.
- Dev container and `tools/dev/setup.sh` give a ready environment in one
  command; `tools/dev/check.sh` runs T0 and T1 locally.
- Simulator and `--speed N` let end-to-end flows run 10 times faster than
  real time; web UI development uses `--ui-dir` for live reload.
- Golden-file helpers (`REQUIRE_GOLDEN`) for OSC streams, MIDI event lists,
  SMF output and JSON status; update with `--update-goldens` after review.
- Templates: `tools/dev/new-feature.sh F-NN name` creates the branch, test
  file with requirement tags, flag, and checklist.
- Parallel work by track with interfaces frozen early (F-30).

## 12. Bug handling

| Severity | Definition | Response |
|---|---|---|
| S0 | Crash, audio glitch, wrong clock on stage, data loss | Same day: test, fix, hotfix release |
| S1 | Feature unusable in common setups | Fix in current week |
| S2 | Workaround exists | Next milestone |
| S3 | Cosmetic | Backlog |

Every S0/S1 fix lands with a regression test and a short note in the
changelog. Post-mortem (blameless, half a page) for each S0.

## 13. Metrics reviewed weekly

Lead time per PR, CI duration and flake rate, coverage, requirement
coverage, accuracy dashboard, CPU baseline drift, open S0/S1, beta crash-free
session rate, mean time to fix for S1.
