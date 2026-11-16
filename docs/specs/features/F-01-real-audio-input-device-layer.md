# F-01: Real audio input and device layer

Status: draft 1.0 · Priority: P0 · Estimate: 18 pd · Owner: I/O
Depends on: F-30 · Enables: F-03 (standalone), F-06, F-13, F-14, F-19
Related: ES-04 section 5, RS-03, ES-03

## 1. Goal and user value
The engine currently only hears a simulated drummer. This feature feeds it
live audio from a real interface (kick, snare, overheads) or a recorded file,
with correct timing, so Pacemaker can be used at rehearsal and so the real corpus
(F-02) can be replayed. This is the largest single gap to a usable product.

## 2. Scope and non-goals
In: `AudioBackend` abstraction; miniaudio backend (ALSA, PulseAudio, JACK,
CoreAudio, WASAPI) for the standalone host and the Box; file backend (WAV replay); channel to
role mapping; input trim, DC removal, clip detection, per-channel meters; device
selection and persistence; xrun and device-loss handling; latency reporting into `ClockMap`;
web UI device panel. Out: ASIO (RtAudio or JUCE backend, task A-9, optional), output
audio (Sync bus device, covered by F-03/F-05), JUCE plugin audio (F-03).

## 3. Requirements
- R-1 Enumerate devices with name, API, channel counts, supported sample rates and buffer sizes, and
  reported input/output latency.
- R-2 Open an input (and optional output) at 44.1, 48, 88.2 or 96 kHz, buffer 64 to 1024 frames;
  default 48 kHz and 128 frames.
- R-3 Per-channel role map (`Kick`, `Snare`, `HiHat`, `Overhead`, `Any`, `Trigger`, `Off`) for up to
  8 input channels, of which at most `kMaxRoles` (4) feed the engine; extra channels are metered only.
  Channels mapped to the same role are summed with 1/sqrt(n) gain.
- R-4 Every callback supplies `hostTimeUs` of the first input sample; if the backend lacks it, `ClockMap`
  uses the callback entry time and its regression filter (ES-02 section 1).
- R-5 Audio callback allocates nothing and takes no locks (verified by F-30 harness); samples reach
  the engine within one callback.
- R-6 Input trim (-24 to +24 dB), 20 Hz DC blocker, per-channel peak and RMS meter, clip flag latched
  for 2 s, all applied before the engine.
- R-7 Xrun/overload counter exposed in status; more than 3 xruns per minute raises a UI warning with advice.
- R-8 Device unplugged: engine goes Idle, outputs enter Hold at the last tempo (ES-04 section 8), UI shows a red
  banner, device list refreshes every 2 s, reconnect resumes automatically.
- R-9 `FileBackend` plays 16/24/32-bit PCM and 32-bit float WAV (mono to 8 channels) at real time or `--speed N`,
  loops optionally, and provides exact sample positions so offline replays are bit-reproducible.
- R-10 Selected device, channel map, rate, buffer and trims persist in settings (`audio` block, state version 2
  with migration from version 1).
- R-11 Input latency from the backend is applied to `ClockMap::setInputCompUs` together with loopback calibration (F-calib).
- R-12 CLI: `pacemaker_server --audio-in "name" --channels 1:kick,2:snare,3:overhead --rate 48000 --buffer 128`, `--wav file`, `--list-devices`.
- R-13 CPU: engine plus device layer at 48 kHz, 64-frame blocks, 3 roles: under 10 percent of one core on the CI runner (T8), under 25 percent on Raspberry Pi 4.

## 4. Design
```cpp
struct AudioDeviceInfo { std::string id, name, api; int inCh, outCh; std::vector<int> rates, buffers; double inLatencyFrames, outLatencyFrames; };
struct AudioConfig { std::string inId, outId; int rate, buffer; std::vector<RoleAssign> map; std::vector<float> trimDb; };
class AudioBackend {
 public:
  virtual std::vector<AudioDeviceInfo> list() = 0;
  virtual bool open(const AudioConfig&, AudioCallback, std::string* err) = 0;
  virtual void close() = 0;
  virtual uint64_t xruns() const = 0;
};
using AudioCallback = void(*)(void* user, const float* const* in, int nIn, float* const* out, int nOut, int frames, int64_t hostUs);
```
Host refactor: `Host::audioLoop` becomes `AudioSource` implementations: `SimSource` (current, wall-clock paced),
`DeviceSource` (callback driven), `FileSource`. `Host` owns a `RoleMixer` (map, trim, DC blocker, meters) and
one `Engine`. For device sources the engine runs in the device callback; the status thread reads meters through
atomics. Sample rates other than 48 kHz use the engine's native scaling (hop 256 times multiplier, ES-01 section 4).
Device list refresh and open run on a control thread, never the callback.

Settings v2 adds `audio: { device, rate, buffer, channels:[{index, role, trimDb}] }`; v1 files load with defaults.

Web UI: Inputs card becomes a channel strip per input channel: role dropdown, trim, peak meter with clip LED, mute;
"Audio device" dialog with device, rate, buffer and a latency read-out; red banner on device loss.

## 5. Test plan
| ID | Method | Pass |
|---|---|---|
| F01_R3 | RoleMixer unit: two kick channels, off channel, over 4 roles | sums and gains exact |
| F01_R6 | DC blocker, trim, clip latch on synthetic signals | within 0.1 dB, flag timing exact |
| F01_R5 | `ScopedAudioThread` around the full callback with `FileBackend` | no alloc or lock |
| F01_R8 | Fake backend that stops delivering; then resumes | Idle then Hold then relock; no crash |
| F01_R9 | WAV parser unit and fuzz (headers, truncated, huge chunk sizes, wrong format tags) | no crash, error text |
| F01_R9b | Replay a rendered drum file through `FileBackend` twice | identical events byte for byte |
| F01_R10 | Settings migration v1 to v2 and unknown keys | defaults filled, round trip |
| F01_R13 | Benchmark job | within budget |
| F01_E2E | Linux loopback (snd-aloop) CI job: play a click file into a loopback device, capture via `DeviceSource` | first onset time within 1 block of truth |
Manual: macOS (built-in, USB interface), Windows (WASAPI shared and exclusive), Linux (ALSA hw, PipeWire), Raspberry Pi with USB interface.

## 6. Acceptance criteria
A rehearsal-room demo: two mics into a USB interface; the web UI shows both meters, the engine locks to a live
drummer within two bars, the plugin-free standalone runs 2 h without xruns at 128 frames on a laptop; unplug and
replug behaves as R-8.

## 7. Development plan
| # | Task | pd | Notes |
|---|---|---|---|
| A-1 | `AudioBackend` interface, `RoleMixer`, DC/trim/meters | 2 | |
| A-2 | `FileBackend` and WAV reader with fuzz target | 2 | enables F-02 early |
| A-3 | Refactor `Host` to `AudioSource`; keep sim | 2 | |
| A-4 | miniaudio backend (vendored, pinned hash; MIT-0/public domain) | 3 | device list, open, xruns |
| A-5 | Timestamps to `ClockMap`, latency compensation | 1.5 | |
| A-6 | Settings v2, CLI, device-loss state machine | 2 | |
| A-7 | Web UI channel strips and device dialog | 3 | screenshots in PR |
| A-8 | Tests, snd-aloop CI job, benchmark | 2 | |
| A-9 | (optional) RtAudio/ASIO backend spike for Windows pros | 0.5 spike | decision ADR; JUCE backend arrives with F-03 |
Order: A-1, A-2, A-3 (then F-02 can start replays), A-4, A-5, A-6, A-7, A-8.
Risks: backend timestamp quality varies (mitigation: regression filter, loopback calibration); PipeWire/Pulse extra
latency (documented, recommend ALSA hw or JACK); Windows shared mode latency (recommend ASIO via F-03 JUCE build).
Rollout: behind `--audio-in`; sim remains default until beta.
