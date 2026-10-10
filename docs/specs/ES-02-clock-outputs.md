# ES-02: Clock Outputs (detailed engineering spec)

Status: draft 0.1, normative.
Research inputs: `docs/research/02-clock-output-mechanisms.md`,
`docs/research/04-plugin-architecture-and-latency.md`.

All outputs consume the `BeatMapSnapshot` published by the engine (ES-01)
and never feed back into it. Each output is an independent module with its
own enable, offset and status, so a failure or licence problem in one never
disables another.

---

## 1. Common clock model

The engine publishes beats in **sample time**. Outputs need **host time**
(microseconds on the Link clock, `ableton::Link::clock().micros()`). The
`ClockMap` object maintains the linear map between them:

- Preferred source: host-provided timestamp of the first sample in each
  block (`AudioPlayHead::PositionInfo::getHostTimeNs()` in plugins,
  `AudioIODeviceCallbackContext::hostTimeNs` in the standalone).
- Fallback: `ableton::link::HostTimeFilter` (512-point regression of block
  start sample vs `clock().micros()` sampled at the top of each callback).
- `ClockMap::hostTimeForSample(s) = origin + (s - originSample) * 1e6 / fs + outputLatencyUs`

The `outputLatencyUs` term is the audio device's reported output latency
(standalone) or zero in plugins (the host's own output latency is applied
by the host to its own audio; Pacemaker's outputs leave by network or MIDI,
not through the host's audio path). A per-output user offset is added on
top (section 7).

The published beat timeline is: beat index `b` occurs at
`hostTimeForSample(beatOriginSample + (b - beatIndexAtOrigin) * periodSamples)`.

Every output thread reads the snapshot through the seqlock, recomputes
its schedule from `sequence` changes, and applies `relock` once per
sequence that carries it.

## 2. Ableton Link output

**Instance policy.** One `ableton::Link` per process, owned by a refcounted
`LinkService` singleton. All Pacemaker instances in a host share it. Only
the instance whose `follow` is on and whose confidence is highest acts as
the tempo publisher (arbitration by `sequence` and confidence, re-evaluated
every 500 ms on the message thread). Others are read-only peers.

**Configuration**: enable, quantum (default `beatsPerBar`, 4), start/stop
sync (default off), publish tempo (default on), publish phase (default on),
confidence gate (default 0.35), commit interval (default 50 ms), tempo
deadband (default 0.05 BPM), "Stage" vs "Rehearsal" preset selects commit
interval 100/30 ms and deadband 0.1/0.02.

**Audio-thread commit** (in `processBlock`, realtime-safe path):

```
if (!enabled || !isPublisher) return;
state = link.captureAudioSessionState();
now   = clockMap.hostTimeForSample(blockStart);
if (snapshot.relock && snapshot.sequence != lastRelockSeq) {
    beat = snapshot.barNumber * quantum + (snapshot.beatInBar - 1);
    state.forceBeatAtTime(beat, hostTimeOf(snapshot.beatOriginSample), quantum);
    lastRelockSeq = snapshot.sequence;
} else if (timeSinceLastCommit >= commitInterval
           && |snapshot.bpm - state.tempo()| > deadband
           && snapshot.confidence >= gate) {
    state.setTempo(snapshot.bpm, now);
}
// Phase drift correction without forceBeatAtTime: compare Link's
// beatAtTime(nextBeatHostTime) to the integer beat we predict; if the
// fractional error exceeds 0.02 beats for 4 consecutive beats, nudge by
// adjusting tempo by +-0.2 BPM for one beat (slew), never by forcing.
link.commitAudioSessionState(state);
```

Rationale: `setTempo` is socially safe in a Link session, `forceBeatAtTime`
is not, so phase is kept by construction (slewing via tiny tempo
adjustments) and forced only on explicit Relock events.

**Fighting peers.** If another peer changes tempo away from ours by more
than 2 BPM twice within 10 s, show a "Another app is setting tempo"
warning in the UI (Live's own Tempo Follower, a DJ app). Do not escalate
commit rate.

**Licence.** Link is GPLv2+ or proprietary. The Link module is a separate
CMake target `pacemaker_link` behind `PACEMAKER_WITH_LINK` (default ON).
Commercial builds require the Ableton licence (see RS-06). Builds with the
flag OFF must compile and run with Link controls hidden.

## 3. MIDI clock output

**Transport.** Direct OS MIDI in every build (plugin and standalone)
because VST3 cannot carry 0xF8 and hosts do not forward clock reliably
(research 02 section 2). Port selection in the UI: any existing output
port, or a virtual port named "Pacemaker Clock" (created on macOS and
Linux via `MidiOutput::createNewDevice`; on Windows the UI explains
loopMIDI and offers to open its download page).

Additionally, CLAP, AU and LV2 builds place 0xF8/0xFA/0xFB/0xFC into the
plugin's MIDI output buffer sample-accurately, for hosts that route plugin
MIDI to hardware. VST3 skips this.

**Scheduler thread.** `ClockThread : juce::Thread` at
`Thread::Priority::highest` with `setCurrentThreadPriority` realtime where
permitted. Loop:

```
snapshot = read seqlock (spin on sequence mismatch)
compute tick schedule: tick n at hostTimeOfBeat(b) + n/24 * period for the
   next 40 ms window
for each tick due within [now, now + 20 ms]:
   if (macOS) queue with CoreMIDI timestamp = tickTime (future stamps)
   else wait: sleep_until(tickTime - 300 us), then spin until tickTime,
        sendMessageNow(0xF8)
```

On Windows the JUCE `sendBlockOfMessages` path is not used (documented
latency bug). When the Windows MIDI Services backend becomes available in
JUCE, it is preferred. Jitter target: 0.3 ms on macOS and Linux, 1 ms on
Windows, measured by the loopback test (RS-05).

**Transport messages.** `Start` (0xFA) is sent when the engine leaves Idle
or CountIn into Locked for the first time, timed at the first beat. `Stop`
(0xFC) is sent when the user disables the MIDI output or the engine goes
Idle after Hold timeout. `Song Position Pointer` plus `Continue` (0xFB) are
sent **only** on a Relock event, and only when `sppOnRelock` is enabled
(default off; on causes an audible gap on many boxes). Tempo is conveyed
solely by tick spacing. In Hold state ticks continue at the frozen tempo.

**Offset.** `midiClockOffsetMs` in [-300, +300], default 0; the UI's
"Hardware calibration" page helps the user set it with a metronome
loopback (ES-03 section 5).

## 4. OSC output

`juce::OSCSender` on a dedicated `OscThread` with a `WaitableEvent`
signalled by the engine's beat events and a 10 ms timer for continuous
values. Destination: host, port, and a profile.

Messages (all addresses prefixed by a configurable root, default
`/pacemaker`):

| Address | Args | When |
|---|---|---|
| `/tempo` | f bpm | on change (deadband 0.01) and every 1 s |
| `/beat` | i bar, i beat, i beatsPerBar | on every beat, bundle timetag = predicted beat host time (NTP) |
| `/downbeat` | i bar | on beat 1 only |
| `/phase` | f barPhase (0..1) | every 10 ms |
| `/confidence` | f | every 100 ms |
| `/state` | s name | on change |
| `/relock` | i bar, i beat | on relock |

Profiles map these onto receiver conventions: **Resolume**
(`/composition/tempocontroller/tempo f`, `/composition/tempocontroller/resync`
on downbeat), **MagicQ/Chamsys** (tap on each beat to a configured
address), **Generic**. OSC bundles carry NTP timetags derived from the Link
clock so NTP-aware receivers can schedule.

## 5. Audio pulse output

An optional stereo output bus ("Sync") renders: left = 24 ppqn pulse
(1 ms, +6 dBFS peak rectangular, polarity selectable), right = beat click
(2 ms sine burst at 1 kHz, downbeat at 2 kHz). Sample-accurate from the
snapshot. This drives E-RM multiclock and Innerclock-class boxes for
±1-sample hardware clock and serves the acoustic calibration (ES-03). In
plugin builds the bus is optional (`BusesProperties().withOutput("Sync",
stereo, false)`); the main output passes input through unchanged when the
"Monitor" setting is on, else silence.

## 6. Host transport (opportunistic)

- **CLAP**: if the host provides `clap.transport-control` (draft),
  `request_tempo(bpm)` is called on the main thread at the same cadence as
  Link commits. Exposed through a custom clap-juce-extensions hook; if the
  extension is unavailable, nothing happens.
- **Reaper**: a bundled ReaScript (`Scripts/Pacemaker_TempoFollow.lua`)
  listens for `/pacemaker/tempo` on localhost OSC and calls
  `SetCurrentBPM`. Documented in INSTALL.
- **Bitwig**: a bundled controller script subscribes to the same OSC and
  sets `transport.tempo()`. Documented.
- **Live, Reason** (and Link-enabled apps such as Resolume): Link. **VERIFY** before publishing: Logic Pro and Pro Tools are not known to support Link natively; use MIDI clock or the Reaper/Bitwig scripts for them (see F-04 and F-27).
- **Cubase, Studio One**: MIDI clock to a virtual port.
- JUCE `AudioPlayHead::transportPlay()` is used for Start on hosts that
  report `canControlTransport()`; never for tempo.

## 7. Per-output offset and lookahead

Each output has `offsetMs` (applied to its schedule) and reads the global
`lookaheadMs` (default 30 ms): the scheduler must know a beat's time at
least `lookaheadMs` before it occurs. The engine predicts a full period
ahead, so this is always satisfied except immediately after Relock, where
the first beat may be published late; outputs then start on the following
beat.

## 8. Status reporting

Each output publishes `OutputStatus{enabled, connected, lastErrorText,
measuredJitterUs}` to the UI via atomics. Link shows peer count, MIDI
shows port name and tick jitter, OSC shows send errors.

## 9. Test requirements

- O1. Link: in a two-process test (Pacemaker standalone plus the Link
  example app), after Relock the example's `phaseAtTime` at Pacemaker's
  predicted beat is within 0.01 beats; after a 120 to 126 BPM ramp the
  example's tempo matches within 0.1 BPM within 2 s.
- O2. Link: no `forceBeatAtTime` is issued without a Relock (counted via a
  test hook).
- O3. MIDI clock: loopback through a virtual port to a receiving thread:
  inter-tick interval standard deviation <= 0.3 ms on macOS and Linux CI;
  <= 1.5 ms on Windows CI.
- O4. MIDI clock: tick 0 of each beat is within 1 ms of the audio pulse on
  the Sync bus (measured in an offline test that renders both).
- O5. OSC: `/beat` bundle timetags are monotonic and within 1 ms of the
  Sync bus clicks.
- O6. Build with `PACEMAKER_WITH_LINK=OFF` compiles, runs and passes all
  non-Link tests.

---

## 10. Implementation status (`pacemaker_outputs`, plain C++17, no JUCE)

| Spec section | Code | Notes |
|---|---|---|
| 1 ClockMap | `ClockMap.h` | 512 point regression of block start versus host time, seqlock readers, `inputCompUs` (input plus acoustic latency, equivalent to correcting every onset sample) and `outputLatencyUs`. Host-provided timestamps are used by feeding them to `addBlock`. `BeatGrid` converts a snapshot to beat times, bar and phase. |
| 2 Link | `LinkPolicy.h` | The policy (gate, deadband, commit interval, relock-only `forceBeatAtTime`, drift slew, fighting-peer warning) runs against the `LinkSession` interface and is tested with a mock (O2). The `ableton::Link` adapter is added with the licence; until then the server shows Link as unavailable. |
| 3 MIDI clock | `MidiClock.h` | `MidiClockGenerator` is deterministic (Start on the first beat, 24 ticks per beat from the snapshot grid, Stop on Idle, SPP plus Continue on relock when enabled, per-output offset). `ClockThread` sleeps with adaptive slack then spins, and reports jitter. `MidiSink` implementations: raw device or FIFO (`/dev/snd/midiC*D*`) and callback. CoreMIDI future timestamps, Windows spin path with loopMIDI and the 0xF8 plugin MIDI buffer come with the plugin. |
| 4 OSC | `Osc.h` | Encoder and decoder, NTP timetags, generator with Generic, Resolume (tempo normalised over 20 to 500 BPM) and MagicQ (tap address configurable) profiles, UDP sender and runner thread. Receiver conventions must be checked against each product's documentation. |
| 5 Sync bus | `SyncRenderer.h` | Block-size independent renderer (O4). Needs an audio device to be audible; the server build has none. |
| 7, 8 Offsets, status | `OutputStatus`, `MidiClockConfig::offsetMs`, `OscConfig::offsetMs` | |

Tests: O2 to O5 in `Tests/OutputTests.cpp`. O1 (two-process Link) and O6
(build without Link) wait for the Link adapter.
