# Research 02: Clock output mechanisms

Status: research notes, October 2026. Inputs to `docs/specs/`. Facts from
sites the research proxy blocked (ableton.com, help.ableton.com, kvraudio.com,
forum.juce.com, forums.steinberg.net, reaper.fm, cdm.link, resolume.com,
elektronauts.com) come from search excerpts and should be spot-checked before
being quoted as normative. Items marked **[uncertain]** have a single source.

Scope: how a JUCE C++17 plugin (VST3, CLAP, AU, LV2, standalone) that tracks a
live drummer can publish tempo, beat phase and bar position to the outside
world.

---

## 1. Ableton Link SDK

**Licence.** `LICENSE.md` in github.com/Ableton/link is GPL v2 or later, with
an explicit note that anyone wanting to incorporate Link into a proprietary
application should contact `link-devs@ableton.com`. The README states Link is
"dual licensed under GPLv2+ and a proprietary license". Shipping a
closed-source commercial plugin that includes Link (it is header-only, so
there is no dynamic-linking escape hatch) requires the proprietary licence.
Community reports (KVR LiveLink thread) say it is granted case by case under
NDA **[uncertain]**. If Pacemaker is itself GPL, GPLv2+ Link is usable freely.

Design consequence: make Link an optional, isolatable output module (separate
translation unit, feature flag) so the product can ship without it if
licensing stalls.

**C++ API (Link.hpp, verified).**

- `ableton::Link(double bpm)`; `enable(bool)` (thread-safe, not RT-safe);
  `isEnabled()`; `enableStartStopSync(bool)`; `numPeers()` (RT-safe);
  callbacks `setTempoCallback`, `setNumPeersCallback`,
  `setStartStopCallback` (not RT-safe); `clock()` returns the platform clock
  (`ticksToMicros`, `micros()`).
- Two session-state paths: `captureAudioSessionState()` /
  `commitAudioSessionState(state)`, realtime-safe and ONLY for the audio
  thread; `captureAppSessionState()` / `commitAppSessionState(state)`,
  thread-safe but not RT-safe. `SessionState` is a value type meant for local
  scope.
- `SessionState::setTempo(double bpm, std::chrono::microseconds atTime)`.
- `beatAtTime(time, quantum)`, `phaseAtTime(time, quantum)` in `[0, quantum)`,
  `timeAtBeat(beat, quantum)`.
- `requestBeatAtTime(beat, time, quantum)`: with no peers it freely re-maps
  beat to time; with peers it maps to the *next* time with matching phase
  (quantised launch).
- `forceBeatAtTime(beat, time, quantum)`: "Rudely re-map the beat/time
  relationship for all peers in a session. DANGER ... very anti-social
  behavior." The header explicitly names *synchronising with an external clock
  source* as a legitimate use, which is exactly Pacemaker's case.
- Transport: `setIsPlaying(bool, time)`, `isPlaying()`, `timeForIsPlaying()`,
  `requestBeatAtStartPlayingTime(beat, quantum)`,
  `setIsPlayingAndRequestBeatAtTime(...)`.

**Phase vs tempo, quantum.** Link shares a timeline (tempo plus beat origin)
and a quantum (4 = one bar). Only phase within the quantum is shared; absolute
song position is not. Tempo changes via `setTempo` are legal from any peer at
any time; the session adopts the latest. Phase changes are what is
"anti-social": `requestBeatAtTime` never breaks peers' phase,
`forceBeatAtTime` does. Strategy for a drummer follower: commit `setTempo`
continuously (rate-limited) and keep beat phase continuous by construction;
use `forceBeatAtTime` only when the tracker detects a genuine phase or bar
re-lock (drummer dropped a beat), and rate-limit it.

**How a peer becomes the de facto tempo source.** There is no master. Each
commit broadcasts a new timeline; the most recent change wins. Pacemaker
becomes the effective source by committing more often than anyone else. A
human turning Live's tempo knob momentarily overrides until the next
Pacemaker commit. When Live's own Tempo Follower is on alongside Link (Push 3
standalone only; desktop Live greys Link out while following), Live pushes
its followed tempo into the session and would fight Pacemaker. Users must
disable Live's Tempo Follower.

**How often tempo can change.** Not documented numerically **[uncertain]**.
The timeline is a continuous linear map, so frequent small `setTempo` calls
are safe in principle; each commit triggers a network broadcast. Guidance from
Link-based followers (SuperCollider, Csound opcodes) is to smooth estimates
before committing. Recommend committing at most every 20 to 100 ms and only
when the change exceeds about 0.05 BPM, to limit multicast chatter and peer UI
jitter.

**Latency compensation.** README: devices must align the moment signals hit
the output; "add the audio system output latency to system time values before
passing them to Link". The CoreAudio example uses
`clock().ticksToMicros(inTimeStamp->mHostTime)` plus
`kAudioDevicePropertyLatency` (output scope). The WASAPI example computes
`bufferBeginAtOutput = mHostTimeFilter.sampleTimeToHostTime(sampleTime) + mOutputLatency`.
`HostTimeFilter` is a 512-point linear regression between sample time and
system time for backends that give no host-time stamps (ASIO, WASAPI, JACK).

**Network and peers.** UDP multicast 224.76.78.75:20808 on the LAN; no VPN or
cross-subnet. No documented hard peer limit **[uncertain]**; the earlier AES
study found reliability up to 22 nodes on a consumer router and 41 on a
high-end mesh. Ableton's FAQ quotes inter-peer latency "below 3 ms" as healthy.
Wired recommended.

**Inside a plugin.** Link spawns its own asio `io_context` thread per `Link`
instance; the audio-thread API is lock-free. Multiple instances in one process
each open the multicast socket and each becomes a peer. Recommend one
process-wide Link instance (refcounted singleton) shared by all Pacemaker
instances in a host.

Products embedding Link: LiveLink (VST3/AU), TouchDesigner Link CHOP, VDMX,
Resolume, Csound opcodes, SuperCollider LinkClock, tidal-link. DAWs with native
Link: Live, Bitwig 2.2+, Reason, Pro Tools 2020.9+, Logic Pro 10.7.5+, Traktor,
Serato, rekordbox 6. Hardware: Akai MPC Live/X/One/Force (Link 3 = tempo plus
start/stop), Circuit Happy Missing Link family (Link to analog or MIDI clock).
Pioneer CDJs use Pro DJ Link, not Ableton Link. Roland and Elektron: no Link
support found.

---

## 2. MIDI clock output

**Protocol.** Timing Clock 0xF8 at 24 ppqn; Start 0xFA (receiver resets to
position 0), Continue 0xFB, Stop 0xFC; Song Position Pointer 0xF2 with a
14-bit LSB-first count of MIDI beats (1 MIDI beat = one 16th = 6 clocks). SPP
must be sent while stopped, followed by Continue. Receivers derive tempo from
inter-clock intervals; there is no tempo value in the stream.

**Jitter requirements.** About 5 ms is felt by sensitive listeners; under 2 ms
is needed for layered drum sounds. E-RM's report says 5-pin MIDI from DAWs can
vary 3 to 12 ms. Target for Pacemaker: clock edges scheduled sample-accurately
with output jitter well under 1 ms.

**From inside a plugin.**

- **VST3 cannot emit MIDI clock.** `ivstevents.h` has only note, data (SysEx
  only), note-expression, chord/scale and `LegacyMIDICCOutEvent`. No carrier
  for system-realtime bytes. JUCE's VST3 wrapper drops 0xF8/0xFA/0xFC. The VST3
  build must send clock via OS MIDI from its own thread.
- **CLAP** `clap_event_midi_t` carries 3 raw bytes with a frame-accurate
  header on a note port with `CLAP_NOTE_DIALECT_MIDI`. The spec does not forbid
  realtime status bytes; host pass-through to an external port is
  host-dependent **[uncertain per host]**.
- **AU/AUv3** `AUMIDIOutputEventBlock(eventSampleTime, cable, length, bytes)`
  carries arbitrary bytes with sample timestamps; Logic forwards plugin MIDI
  out only via MIDI FX or IAC routing **[uncertain]**.
- **LV2** atom output with `midi:MidiEvent` can carry any bytes,
  frame-stamped.
- Host-routed plugin MIDI only reaches hardware if the user routes a MIDI
  track to an output, and it then inherits the host's own scheduling. Reports
  show host-side quantisation to buffer boundaries in some hosts.

**From a standalone or own thread (OS MIDI).**

- macOS CoreMIDI: `MIDIPacketList` timestamps (mach host time) allow
  scheduling ahead; delivery at the stamped time with sub-ms accuracy. JUCE
  `MidiOutput::sendBlockOfMessages` uses this on macOS.
- Windows: WinMM has no output timestamps; JUCE's `sendBlockOfMessages` on
  Windows is a thread-timer emulation with documented bugs (0.5 to 3 s latency
  reported on the JUCE forum). Windows MIDI Services (Windows 11, 2024 to
  2025) adds QPC-based timestamps and scheduled sends with low-microsecond
  jitter, but JUCE does not use it yet **[uncertain as of JUCE 8]**. A native
  Windows MIDI Services backend, or WinRT `MidiOutPort`, is needed for tight
  clock on Windows.
- Linux ALSA sequencer: queue scheduling (`snd_seq_ev_schedule_real`)
  delivers at a specified real time; about 1 ms accuracy unless
  hrtimer-backed.
- Virtual ports: macOS IAC bus or `MIDIDestinationCreate` (JUCE
  `MidiOutput::createNewDevice`); Linux `snd_seq_create_simple_port` (JUCE
  createNewDevice works) or `snd-virmidi`; Windows has no user-space virtual
  port API, so users need loopMIDI or rtpMIDI, or a Windows MIDI Services
  virtual device on Windows 11.
- Audio-sync alternative: E-RM multiclock and Innerclock-class devices take a
  sample-accurate audio pulse track and generate ±1-sample MIDI, DIN and
  analog clock. Pacemaker can render such a pulse (and a 24 ppqn click) into
  an audio output bus. This is the tightest path available from any plugin
  format.

**Hardware behaviour.** Elektron Digitakt/Digitone: display BPM wanders ±0.5
to 1 BPM under DAW clock but internally averages; follows tempo changes;
resets on Start; Live's "MIDI Clock Sync Delay" aligns it. Hardware follows
continuous small drifts smoothly; abrupt jumps cause a few beats of hunting.
SPP is honoured only when stopped then Continue, so a mid-song bar re-lock
requires Stop, SPP, Continue, with an audible gap on many boxes. Design: never
retime via SPP during play; keep phase through tempo; use Start/SPP only at
song boundaries.

---

## 3. Host transport and tempo control from a plugin

- **VST3**: `Vst::ITransportControl` (SDK 3.8.1, host-implemented, optional,
  UI thread only): `requestAction(action, TransportPosition*)` with
  Locate/PlaybackStart/Stop/Record/Cycle actions. **No tempo action.**
- **CLAP**: draft `clap.transport-control/2` host side: `request_start/stop/
  continue/pause/toggle_play/jump/loop_region/toggle_loop/enable_loop/record/
  toggle_record`, **`request_tempo(host, double tempo)`** and
  `request_time_signature`, all main-thread. Draft; host support minimal
  **[uncertain: Bitwig and Reaper unverified]**. Still the only plugin API with
  a tempo request.
- **AU**: `AUHostMusicalContextBlock` / `AUHostTransportStateBlock` (v3) and
  `HostCallbackInfo` (v2) are read-only.
- **LV2**: `time:Position` atoms are host to plugin only.
- **Host-specific back doors.** Reaper: ReaScript / extension API
  `CSurf_OnTempoChange(bpm)`, `SetTempoTimeSigMarker`, `SetCurrentBPM`; a
  Reaper extension or a ReaScript polling OSC from Pacemaker can set tempo
  live. Reaper follows SPP, MTC and LTC but not MIDI clock tempo. Bitwig:
  Controller API `Transport.tempo()` is a settable Parameter, so a tiny
  controller script receiving OSC or MIDI from Pacemaker can drive tempo;
  Bitwig also slaves to MIDI clock and supports Link. Ableton Live: no plugin
  API; options are Link (preferred), external MIDI clock sync with "MIDI Clock
  Sync Delay", or Max for Live `live.object` setting `live_set tempo` (what
  BeatSeeker does). Logic follows Link (10.7.5+) but **cannot** receive MIDI
  clock. Cubase: no MIDI clock slave, no Link; essentially closed. Studio One
  can slave to MIDI clock, no Link. Pro Tools: Link 2020.9+.

Conclusion: Link is the only broadly supported "set host tempo" channel; MIDI
clock is second; CLAP `request_tempo` and per-DAW scripts are opportunistic
extras.

---

## 4. OSC and timecode output

- No universal standard. Receiver conventions: Resolume
  `/composition/tempocontroller/tempo` (float BPM),
  `/composition/tempocontroller/resync`, plus a tap-tempo address.
  TouchDesigner has a native Ableton Link CHOP (tempo, beat, phase, peers), so
  Link beats OSC there. grandMA3 derives BPM from audio and accepts OSC 1.1
  commands. Chamsys MagicQ, Obsidian Onyx and ETC Eos accept OSC or MIDI tap.
  QLab 5 triggers cues via OSC or MIDI and can chase incoming timecode but does
  not run continuous playback to it.
- Recommended Pacemaker OSC vocabulary: `/pacemaker/tempo f`,
  `/pacemaker/beat i i i` (bar, beat, tick), `/pacemaker/phase f` (0 to 1
  within bar), `/pacemaker/pulse` on each beat, `/pacemaker/transport i`, with
  an OSC bundle timetag set to the predicted beat time so NTP-aware receivers
  can schedule. Provide profiles for Resolume and MagicQ-style address maps.
- **LTC/SMPTE**: generation is trivial with libltc (encode into an audio
  output bus), but LTC is wall-clock timecode, not beat time. With a
  drummer-following tempo it would need to run at a variable rate, which
  lighting desks tolerate only within small limits. Feasible as an optional
  "beat-scaled timecode" output. MTC is similar. Beat-based outputs (OSC, MIDI
  clock, Link) are the right primary for lighting.

---

## 5. MIDI 2.0, MIDI-CI and hardware Link

- UMP adds Flex Data "Set Tempo" (10 ns per quarter note units) and a MIDI
  Clip File tempo header; realtime Timing Clock, Start and Stop still exist as
  System messages in UMP. MIDI-CI is capability negotiation, not clock.
  Windows MIDI Services and macOS 14+ CoreMIDI UMP APIs support timestamped
  UMP; JUCE 8 has a MIDI 2.0 preview branch. Practical value today is low (few
  devices honour Flex Set Tempo). Worth emitting Set Tempo alongside clock on
  UMP endpoints later.
- Hardware Link: Akai MPC and Force (native), Circuit Happy Missing Link
  family, Planet-H Link-to-MIDI Bridge (Android, ±300 ms offset, start
  quantisation). Pioneer: rekordbox 6 software has Link; CDJ and DJM hardware
  use Pro DJ Link (bridge via Carabiner and beat-link). Elektron and Roland:
  MIDI clock only.

---

## 6. Latency and beat alignment

Goal: a published beat (Link beat, MIDI clock tick 0, OSC pulse) must
coincide with the drummer's acoustic hit as heard at FOH and in the
backing-track mix.

Chain for an incoming hit at acoustic time T0: mic and ADC plus input buffer
(`AudioIODevice::getInputLatencyInSamples`, or host-reported), Pacemaker
detection delay (onset detector lookahead plus block size), then the output
path. Estimate beat time in the *sample-clock* domain:
`T_beat_samples = detected_sample - inputLatency - detectorDelay`, then
predict the next beat from tempo and publish ahead of time. Convert to each
output's clock domain:

- Link: `hostTime = HostTimeFilter.sampleTimeToHostTime(sample) + outputLatency_us`;
  commit `setTempo` and, on re-lock, `forceBeatAtTime(beat, hostTimeOfBeat, quantum)`.
- MIDI clock (OS path): schedule 0xF8 at `beatTime + userOffset` using
  CoreMIDI, Windows MIDI Services or ALSA timestamps; expose a ±300 ms "clock
  offset" like Live and Planet-H.
- Audio pulse bus: write clicks at the exact predicted sample.

Reported latencies are unreliable (ASIO about exact, CoreAudio needs buffer
size added, DirectSound and Android useless). Provide calibration: (a)
RTL-style loopback, Pacemaker emits a click on an output, user patches it to
an input, measure the round trip to ±1 sample; (b) acoustic calibration, play
a click through FOH, mic it, measure the offset; (c) external-gear
calibration, run a hardware metronome into an input and nudge the MIDI clock
offset until aligned (Live's documented method). Store per-device offsets. Add
a "lookahead vs responsiveness" control: publishing beats requires prediction;
Link peers and hardware need the beat announced at least one audio buffer plus
network or MIDI latency before it happens.

---

## Recommended output architecture for Pacemaker

1. **Single internal clock model** (`BeatTimeline`: tempo, beat origin in
   sample time, quantum/bar length, playing flag), updated by the tracker on
   the audio thread, lock-free published to outputs. All outputs are
   consumers; none feeds back.
2. **Output modules, each optional and independently calibratable (offset in
   ms):**
   - **Ableton Link** (primary "set the DAW tempo" path): one process-wide
     `ableton::Link`; `captureAudioSessionState` / `setTempo` /
     `commitAudioSessionState` in `processBlock` with rate limiting and a
     threshold; `forceBeatAtTime` only on phase re-lock events;
     `HostTimeFilter` plus output latency; start/stop sync optional. Feature
     flag; obtain Ableton's proprietary licence before commercial release, or
     ship GPL.
   - **MIDI clock**: (i) host-event path for CLAP, AU and LV2 (sample-accurate
     0xF8 in the output event list); (ii) OS-direct path for VST3 and
     standalone using native CoreMIDI timestamps, Windows MIDI Services
     (fallback WinMM with a high-priority timer thread, documented as lower
     quality), ALSA queue scheduling; virtual port creation on macOS and
     Linux, loopMIDI guidance on Windows. Start, SPP and Continue only at song
     boundaries; tempo conveyed by tick spacing.
   - **Audio sync bus**: 24 ppqn pulse plus beat click on an extra output bus
     for E-RM multiclock-class hardware and for acoustic calibration.
   - **OSC**: `/pacemaker/*` messages with bundle timetags; profiles for
     Resolume, MagicQ and TouchDesigner.
   - **Host tempo (opportunistic)**: CLAP `clap.transport-control`
     `request_tempo` when the host offers it; a bundled Reaper extension or
     ReaScript and a Bitwig controller script receiving tempo via OSC;
     documentation for Live (Link or external MIDI clock sync; disable Live's
     own Tempo Follower).
   - **Timecode (optional, later)**: libltc-based LTC or MTC at a
     tempo-scaled rate for fixed-tempo shows.
3. **Latency manager**: per-output offsets, RTL-style loopback calibration,
   acoustic calibration, and a global lookahead so predicted beats are
   published before they occur.
4. **Smoothing policy**: tempo updates continuous but rate-limited; phase
   corrections rare and quantised; separate "stage" (conservative) and
   "rehearsal" (responsive) presets, since both Link peers and MIDI-clock
   hardware hunt on abrupt changes.

---

## Sources

- Ableton Link repo, README, LICENSE: https://github.com/Ableton/link ; Link.hpp: https://raw.githubusercontent.com/Ableton/link/master/include/ableton/Link.hpp
- Link protocol port and multicast: https://nowplayingapp.com/help/reference/integrations/ableton-link/ ; SuperCollider LinkClock: https://docs.supercollider.online/Classes/LinkClock.html
- Link proprietary licence discussion (LiveLink): https://www.kvraudio.com/forum/viewtopic.php?p=9250685 ; CDM: https://cdm.link/2016/11/free-jazz-now-ableton-link-sync-works-pure-data/
- Csound link opcodes: https://csound.com/docs/manual/link_beat_force.html ; Carabiner thread: https://club.tidalcycles.org/t/carabiner-and-request-beat-at-time/3075
- JUCE Link tutorial: https://forum.juce.com/t/ableton-link-tutorial-how-to-build-and-some-tips/31242 ; Link FAQ: https://help.ableton.com/hc/en-us/articles/209776125-Link-FAQs
- Live manual: https://www.ableton.com/en/live-manual/12/synchronizing-with-link-tempo-follower-and-midi ; Tempo Following FAQ: https://help.ableton.com/hc/en-us/articles/360019100900 ; MIDI sync: https://help.ableton.com/hc/en-us/articles/209071149
- VST3 ITransportControl: https://raw.githubusercontent.com/steinbergmedia/vst3_pluginterfaces/master/vst/ivsttransportcontrol.h ; VST3 events: https://raw.githubusercontent.com/steinbergmedia/vst3_pluginterfaces/master/vst/ivstevents.h ; Steinberg forum: https://forums.steinberg.net/t/vst3-midi-out-timing-breaks-at-large-asio-buffer/1039971
- CLAP transport-control draft: https://raw.githubusercontent.com/free-audio/clap/main/include/clap/ext/draft/transport-control.h
- AUv3 MIDI output: https://forum.juce.com/t/auv3-clarification-of-midi-event-times/50010 ; https://cp3.io/posts/sample-accurate-midi-timing/
- LV2 time: https://drobilla.net/2012/11/17/lv2-plugin-transport-synchronisation.html
- Reaper ReaScript API: https://www.reaper.fm/sdk/reascript/reascripthelp.html ; Bitwig API: https://keithmcmillen.com/blog/controller-scripting-in-bitwig-studio-part-3/ ; Logic Link: https://support.apple.com/it-ch/guide/logicpro/lgcp87c3975f/mac ; Logic MIDI clock: https://support.apple.com/en-gb/102005
- MIDI beat clock: https://en.wikipedia.org/wiki/MIDI_beat_clock ; jitter perception: https://lists.linuxaudio.org/archives/linux-audio-dev/2010-March/026165.html ; E-RM jitter report: https://www.e-rm.de/data/E-RM_report_Jitter_02_14_EN.pdf ; multiclock: https://www.e-rm.de/multiclock/
- JUCE Windows MIDI timing bug: https://forum.juce.com/t/bug-report-for-juce-midioutput-sendblockofmessages-unusable-on-windows/51663 ; Windows MIDI Services: https://microsoft.github.io/MIDI/
- Elektron clock behaviour: https://www.elektronauts.com/t/digitakt-receiving-inconsistent-midi-clock-from-daw/48469 ; https://www.elektronauts.com/t/ableton-midi-clock-sync-delay/41947
- MIDI 2.0 UMP spec: https://amei.or.jp/midistandardcommittee/MIDI2.0/MIDI2.0-DOCS/M2-104-UM_v1-1-1_UMP_and_MIDI_2-0_Protocol_Specification.pdf ; JUCE MIDI 2.0 branch: https://forum.juce.com/t/midi-2-0-preview-branch/66453
- Hardware Link: https://support.akaipro.com/en/support/solutions/articles/69000814493 ; https://www.sonicstate.com/news/2019/02/25/the-missing-link-adds-ableton-link-wireless-sync-to-your-midi-hardware/ ; https://rekordbox.com/en/support/faq/ableton-link/ ; https://github.com/Deep-Symmetry/beat-link-trigger ; https://www.planet-h.com/link-to-midi-bridge/
- OSC conventions: https://resolume.com/support/en/osc ; https://docs.derivative.ca/Ableton_Link_CHOP ; https://help.malighting.com/grandMA3/2.3/HTML/sound_viewer.html
- LTC: https://www.mankier.com/3/ltc.h ; LTC to MTC M4L: https://leolabs.gumroad.com/l/ltc-to-mtc
- Latency measurement: https://www.gearnews.com/rtl-utility-for-measuring-audio-latency-gets-an-official-release/ ; https://www.soundonsound.com/techniques/round-trip-latency-rtl-j-scope-oscilloscope ; https://forum.juce.com/t/coreaudio-device-latencies-are-reported-inconsistently-with-other-audio-device-types/41820
