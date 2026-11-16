# F-30: Foundation: HostClock, virtual time and real-time safety harness

Status: draft 1.0 · Priority: P0 · Estimate: 6 pd · Owner: engine/foundation
Depends on: none · Enables: F-01, F-03, F-04, F-05, F-29
Related: ES-02 section 1, QUALITY-STRATEGY sections 4 and 5

## 1. Goal and user value
Make all timing code testable without wall-clock sleeps and make real-time
safety a mechanical check. Users never see it; it removes the main source of
flaky tests and lets macOS, Windows and Linux backends be developed against one
interface.

## 2. Scope and non-goals
In: `HostClock`, `VirtualClock`, `Sleeper`, refactor of `ClockThread`,
`OscRunner`, `Host`, `ClockMap` users to take a clock; audio-thread
annotation and allocation/lock detector; test helpers. Out: Link clock
implementation (F-04), platform MIDI (F-05).

## 3. Requirements
- R-1 `HostClock::nowUs()` is monotonic, thread-safe, microsecond resolution.
- R-2 `SteadyHostClock` wraps `std::chrono::steady_clock`; document that on macOS it
  must equal the clock used by CoreMIDI timestamps (F-05) and by Link (F-04)
  or be converted by a `ClockBridge`.
- R-3 `VirtualClock` only advances through `advance(us)`/`set(us)`; sleeping on it
  blocks until another thread advances it past the deadline, or returns immediately
  in single-thread "stepped" mode.
- R-4 `ClockThread`, `OscRunner` and `Host` accept a `HostClock&` and `Sleeper&`; defaults
  preserve current behaviour.
- R-5 With `VirtualClock`, a 10 s MIDI clock test completes in under 50 ms of wall time and
  produces exactly the generator's tick list.
- R-6 `PM_AUDIO_THREAD` marks functions that run on the audio thread; `ScopedAudioThread`
  makes the test allocator hook fail on `new`, `delete`, `malloc`, mutex lock, file or
  socket calls inside its scope (Linux and macOS; Windows by hooking `HeapAlloc` is a
  later task).
- R-7 `Engine::process`, `ClockMap::addBlock`, `renderSync` and (later) the plugin
  `processBlock` run clean under `ScopedAudioThread` in unit tests.
- R-8 A CI grep check rejects banned APIs in files listed in `tools/dev/audio_thread_files.txt`.

## 4. Design
```cpp
struct HostClock { virtual ~HostClock(); virtual int64_t nowUs() const = 0; };
struct Sleeper   { virtual void sleepUntil(const HostClock&, int64_t us) = 0; virtual void spinUntil(const HostClock&, int64_t us) = 0; };
class VirtualClock : public HostClock, public Sleeper { void advance(int64_t us); /* wakes sleepers */ };
```
Threads take `std::function<int64_t()>` today; replace by `HostClock&`. The host
bundle `Services { HostClock& clock; Sleeper& sleeper; }` is passed down. Allocation hook:
a thread-local flag checked by replacement `operator new/delete` compiled only into
`PacemakerTests` (`PM_TEST_ALLOC_HOOK`); `pthread_mutex_lock` interposition via
linker `--wrap` on Linux.

## 5. Test plan
F30_R5 virtual 10 s tick test; F30_R3 sleeper wake order; F30_R6 detector catches a
deliberate `std::vector::push_back` and a `std::mutex` in a fixture; F30_R7 engine, clock map,
sync renderer clean over 60 s of audio; F30_R8 grep script has a failing fixture.

## 6. Acceptance
All existing O2 to O5 tests run on `VirtualClock` except one tagged `realtime` test per thread
class; no change in production behaviour (manual run of `pacemaker_server` unchanged).

## 7. Development plan
| Task | pd | Notes |
|---|---|---|
| Interfaces, `SteadyHostClock`, `VirtualClock`, `Sleeper` | 1 | header only |
| Refactor `ClockThread`, `OscRunner`, `Host` to `Services` | 2 | keep adaptive spin in the Sleeper |
| Convert tests to virtual time | 1 | |
| Alloc/lock hook, `PM_AUDIO_THREAD`, banned-API grep | 1.5 | |
| Docs and ADR | 0.5 | |
Risks: hidden uses of the system clock (grep for `steady_clock`, `system_clock`) — add a lint.
