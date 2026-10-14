# F-06: E-drum and trigger MIDI input

Status: draft 1.0 · Priority: P0 · Estimate: 10 pd · Owner: I/O
Depends on: F-01 (host structure), F-30 · Related: ES-04 section 4, ES-03 section 3, F-12

## 1. Goal and user value
Electronic kits and trigger pads send exact hit times as MIDI. They are the most reliable input and the most requested missing feature in Live's follower.
The engine already accepts `DiscreteOnset`; this feature supplies the front end: MIDI device input, note-to-role mapping, hit filtering, timestamp conversion and a UI to set it up.

## 2. Scope and non-goals
In: MIDI input backends (CoreMIDI, ALSA seq, WinMM/WMS; plugin host MIDI buffer in F-03), parser, mapping presets, pad learn, filters, timestamp mapping, module latency, UI, OSC hit input.
Out: sending to modules, MIDI clock input, drum-module editing, controller mappings for actions (F-12).

## 3. Requirements
- R-1 Parser accepts running status, interleaved real-time bytes (0xF8 to 0xFF), SysEx (skipped), and malformed input without crashing; only note-on with velocity above zero creates a hit.
- R-2 Mapping table: note number (optionally channel) to role (`Kick`, `Snare`, `HiHat`, `Tom`, `Cymbal`, `Trigger`, `Ignore`). Default General MIDI map: kick 35 and 36, snare 38 and 40, side stick 37 as Snare with 0.5 weight, closed hat 42, hat pedal 44 (Ignore by default), open hat 46, toms 41 43 45 47 48 50, crashes 49 and 57, rides 51 and 59 and 53 (bell), others Ignore. Presets for common modules are data files (`presets/midi/*.json`) and must each cite the manufacturer's MIDI implementation chart (**VERIFY** per module before release).
- R-3 Pad learn: "Learn" arms a role; the next incoming note is assigned to it; learned maps are saved per device name.
- R-4 Filters: minimum velocity (default 8), retrigger window per note (default 12 ms) against crosstalk and double triggers, hat pedal and cymbal choke messages ignored, channel filter, velocity curve (linear, soft, hard) producing strength in 0..1.
- R-5 Timestamping: each hit carries the OS receive timestamp (CoreMIDI timestamp, ALSA event time, or sample offset in a plugin host buffer) converted to engine samples with `ClockMap::sampleForHostTime`; module latency (default 3 ms, 0 to 20) and cable jitter allowance are subtracted. Hits older than the current engine time are clamped to now.
- R-6 Multiple devices and multiple instances of the same device are supported (up to 4 inputs); each has its own map and latency.
- R-7 Fusion: MIDI hits and audio onsets merge inside the engine's 15 ms window; role tags from the map drive the bar tracker (Kick to KickLike, Snare to SnareLike, others Other).
- R-8 "MIDI only" mode disables audio detectors to save CPU and avoid bleed.
- R-9 OSC and UDP triggers: `/pacemaker/hit i role f velocity` and a plain `/hit` message are accepted on a configurable port (off by default, localhost bound).
- R-10 UI: Inputs card shows MIDI devices with activity LED and last note and velocity, a pad map editor (table, learn, test), module preset dropdown, latency field and velocity curve selector.
- R-11 Latency chain is shown in the calibration page: module latency, transport (USB 1 ms, DIN about 1 ms per 3-byte message), engine detector delay (zero for discrete hits).
- R-12 No allocation on the audio thread: the MIDI thread pushes hits to a lock-free queue (`SpscQueue<DiscreteOnset>`); in plugins hits are read from the host buffer in `processBlock`.

## 4. Design
```cpp
struct MidiHit { int64_t hostUs; uint8_t note, channel, velocity; };
class MidiInBackend { virtual std::vector<MidiPortInfo> list(); virtual bool open(id, cb); ... };   // same shape as F-05
class HitMapper { DiscreteOnset* map(const MidiHit&, const ClockMap&, DiscreteOnset& out); /* filters, curve, latency */ };
```
`HitMapper` is pure and unit-testable. State (`lastHitUs[128]`) is preallocated. Mapping presets are loaded at start and on user action only. The engine call is `Engine::pushEvent(DiscreteOnset)` (already present).

## 5. Test plan
| ID | Method | Pass |
|---|---|---|
| F06_R1 | Parser unit and fuzz (random bytes, truncation, running status, SysEx) | no crash; expected hits only |
| F06_R2 | Default and preset maps vs table of notes | exact roles |
| F06_R4 | Retrigger, min velocity, curves | exact outputs |
| F06_R5 | Timestamp conversion with synthetic `ClockMap`; module latency | sample error <= 1 |
| F06_E2E1 | Synthetic e-drum stream at 120 BPM with +-1 ms jitter, with and without fills | beat F >= 0.99, phase SD <= 6 ms (T15) |
| F06_E2E2 | Recorded e-kit MIDI from the F-02 corpus | T15 thresholds |
| F06_R12 | `ScopedAudioThread` on the plugin path | clean |
Manual: Roland TD-17, Alesis Nitro, Yamaha DTX, a trigger pad (Roland SPD) over USB and DIN.

## 6. Acceptance criteria
With a real e-kit the engine locks within 4 beats, bar 1 is found from kick/snare pattern, and the phase error SD against the module's own click is under 10 ms; setup (device select, preset, test) takes under 2 minutes.

## 7. Development plan
| # | Task | pd | Notes |
|---|---|---|---|
| E-1 | Parser, `HitMapper`, default map, curves, filters (+fuzz) | 2.5 | |
| E-2 | Backends for ALSA, CoreMIDI, WinMM sharing F-05 enumeration | 2.5 | |
| E-3 | Module presets (3 to 5 modules) and pad learn | 1.5 | verify charts |
| E-4 | Host integration (server) and plugin buffer path | 1.5 | |
| E-5 | UI card and settings, OSC hit input | 1.5 | |
| E-6 | Tests and corpus replays | 1 | |
Risks: module mapping differences (learn mode, presets as data); crosstalk between pads (retrigger and velocity filters); USB timestamp quantisation (documented, latency field).
