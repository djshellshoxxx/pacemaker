# F-05: Cross-platform MIDI clock (macOS, Windows, Linux)

Status: draft 1.0 · Priority: P0 · Estimate: 20 pd · Owner: I/O
Depends on: F-30 · Related: ES-02 section 3, research 02, research 05 section 1

## 1. Goal and user value
Hardware sequencers, drum machines and modular gear follow Pacemaker through MIDI clock. Today the generator and a Linux raw-device sink exist and pass on Linux.
This feature adds first-class backends per OS with accurate scheduling, virtual ports, hot-plug, and a way to measure real jitter. It is the output that hardware users judge us on.

## 2. Scope and non-goals
In: `MidiOutBackend` abstraction (list ports, open, send with timestamp, virtual port), CoreMIDI, ALSA sequencer, WinMM and Windows MIDI Services backends, JACK MIDI (optional),
real-time thread setup, power-throttling opt-outs, port UI and persistence, jitter probe tool and CI, hardware rig, plugin MIDI-buffer output (shared with F-03). Out: MIDI clock input (slaving to external clock), MIDI over network (RTP-MIDI), BLE MIDI.

## 3. Requirements
- R-1 `MidiOutBackend::list()` returns ports (id, display name, kind: hardware, virtual, software) and updates on hot-plug within 2 s.
- R-2 macOS: CoreMIDI virtual source "Pacemaker Clock" always available; hardware destinations via `MIDISend`/event lists with host-time timestamps up to 40 ms ahead; USB/DIN devices reached through the driver schedule.
- R-3 Linux: ALSA sequencer client "Pacemaker" with one output port; events scheduled on a real-time queue (`snd_seq_ev_schedule_real`) with queue time resynchronised to the host clock every second; `aconnect`-style subscriptions respected; rawmidi fallback for `/dev/snd/midiC*D*`.
- R-4 Windows: baseline WinMM `midiOutShortMsg` from a thread with MMCSS "Pro Audio" class, `timeBeginPeriod(1)`, QPC spin to the due time; Windows MIDI Services backend selected at runtime when the service is present, using its virtual endpoint and timestamped send (**VERIFY** SDK calls); loopMIDI guidance when no virtual port is available.
- R-5 JACK MIDI (optional build flag): events written sample-accurately from the JACK process callback.
- R-6 `ClockThread` uses the backend's scheduling capability: `scheduled` backends receive messages up to 30 ms early with timestamps; `immediate` backends are driven by sleep-then-spin to the due time (adaptive slack, existing).
- R-7 Real-time setup: macOS `thread_policy_set` time-constraint policy and an activity assertion (no App Nap, no timer coalescing); Linux `SCHED_FIFO` via rtkit or limits.conf with a UI hint when denied; Windows MMCSS plus disabling process power throttling.
- R-8 Jitter targets measured at the software boundary (loopback into a probe on a virtual or hardware loop): SD of tick intervals <= 0.3 ms on macOS and Linux, <= 1.0 ms on Windows baseline, <= 0.5 ms on Windows MIDI Services; p99 error versus schedule <= 1.0 ms; max <= 3 ms over 10 minutes at 120 BPM with the machine at 50 percent CPU load.
- R-9 Wire-level expectation documented: USB MIDI 1.0 devices quantise to about 1 ms; DIN direct is serial at 31250 baud (one byte is 0.32 ms); acceptance on hardware is p99 <= 2 ms for USB and <= 0.7 ms for DIN.
- R-10 Port loss: output shows "port not found", keeps the name, retries every 5 s; engine keeps running (ES-04 section 8).
- R-11 Start (0xFA) on first Locked, Stop (0xFC) on disable or after Hold timeout, SPP plus Continue only on relock when enabled; all aligned to the beat (existing generator behaviour) and verified per backend.
- R-12 CPU: spin path averages under 2 percent of one core at 120 BPM; configurable "spin budget" caps it at 4 ms per tick.
- R-13 Optional device latency offset per port (ms), stored per port name; hardware calibration helper (ES-03 section 6) writes it.
- R-14 `pacemaker_midi_probe` tool: opens an input, timestamps every 0xF8 with the OS receive time, and prints mean, SD, p50, p99, max and a histogram; exit code non-zero on threshold breach.

## 4. Design
```cpp
struct MidiPortInfo { std::string id, name; enum Kind { Hardware, Virtual, Software } kind; };
class MidiOutBackend {
 public:
  virtual std::vector<MidiPortInfo> list() = 0;
  virtual bool open(const std::string& id, std::string* err) = 0;     // or createVirtual(name)
  virtual bool scheduled() const = 0;                                 // supports future timestamps
  virtual bool send(const uint8_t* b, int n, int64_t hostUs) = 0;     // hostUs in HostClock time
  virtual void close() = 0;
};
```
The `HostClock` to backend clock conversion is inside each backend (CoreMIDI mach time, ALSA queue time, QPC). `ClockThread` is unchanged apart from asking `scheduled()`. Backends live in `outputs/src/platform/<os>/` with the same unit-test interface (`FakeBackend` in tests).
Probe loops: macOS in-process virtual source plus virtual destination; Linux `snd-seq-dummy` or an ALSA seq loop; Windows loopMIDI or WMS loopback on the hardware rig; the hardware rig uses an interface's out-to-in cable and a logic analyser on the DIN line (Saleae-class or sigrok-compatible clone) for wire timing.

## 5. Test plan
| ID | Method | Pass |
|---|---|---|
| F05_R6 | Virtual clock plus `FakeBackend` (scheduled and immediate) | exact tick times, 10 s in under 50 ms |
| F05_O3 | Linux CI: ALSA seq loop and probe, 60 s | SD <= 0.5 ms (CI tolerance), reports uploaded |
| F05_O3m | macOS CI: CoreMIDI virtual source to destination, 60 s | SD <= 0.5 ms |
| F05_O3w | Windows nightly on hardware rig with WinMM and WMS | thresholds R-8 |
| F05_R10 | Unplug/replug (fake and manual) | warning then recovery within 5 s |
| F05_R11 | Start/Stop/SPP per backend with FakeBackend and real loop | order and times correct |
| F05_R12 | CPU profile 10 minutes | under budget |
| F05_FUZZ | Port name parsing and settings | no crash |
Manual: Elektron Digitakt, Korg Volca, Roland TR-8S over USB and DIN; Pi Pisound DIN.

## 6. Acceptance criteria
Three real devices stay locked to Pacemaker for a 30-minute set with tempo changes, with no audible flams; jitter reports for each OS attached to the release; documentation of the USB 1 ms floor.

## 7. Development plan
| # | Task | pd | Notes |
|---|---|---|---|
| M-1 | `MidiOutBackend` interface, `FakeBackend`, refactor `ClockThread` | 2 | |
| M-2 | ALSA sequencer backend, rtkit hints, tests on Linux CI | 3 | also Pi |
| M-3 | CoreMIDI backend, virtual source, real-time policy, activity assertion | 4 | |
| M-4 | WinMM backend with MMCSS and power throttling | 3 | |
| M-5 | Windows MIDI Services backend (runtime detect) | 3 | spike first (0.5) |
| M-6 | Probe tool and CI integration, hardware rig set-up | 2.5 | buy interface, logic analyser |
| M-7 | Port UI, persistence, hot-plug, errors | 1.5 | |
| M-8 | JACK MIDI (optional) | 1 | |
Risks: CoreMIDI scheduling behaviour differs by driver (measure, fall back to sleep-then-spin); Windows hosted CI has no MIDI devices (rig plus nightly); USB quantisation (documented, recommend DIN or audio pulse).
