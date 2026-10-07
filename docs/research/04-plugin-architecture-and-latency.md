# Research 04: Plugin architecture and latency engineering

Status: research notes, October 2026. These are inputs to the engineering specs
in `docs/specs/`. Facts that could not be verified against primary docs are
marked **uncertain**.

Scope: JUCE 8 plugin plus standalone that follows a live drummer and publishes
tempo, beat and bar via Ableton Link, MIDI clock, OSC and host transport.

---

## 1. Real-time constraints, threads and clocks

**Audio-thread rules.** No locks, allocations, I/O or syscalls in
`processBlock` / `audioDeviceIOCallbackWithContext`. Toolkit:

- `juce::AbstractFifo` (index bookkeeping only; you own the storage) for SPSC
  queues of fixed-size POD events, e.g.
  `struct OnsetEvent { int64 samplePos; int64 hostTimeNs; float strength; uint8 channel; }`.
- `std::atomic<double>` / `std::atomic<float>` (lock-free on all targets we
  care about) for "latest value" publication (tempo, phase, confidence) from
  audio to UI.
- **farbot** (hogliux, MIT): `RealtimeObject<T, nonRealtimeMutatable>` for
  larger settings structs written by UI and read on the audio thread,
  `fifo<T, ...>` with explicit producer/consumer modes, `AsyncCaller` to defer
  logging and deallocation off the audio thread. Tracktion Engine bundles
  farbot and choc.
- **crill** (Doumler / Renn-Giles, BSL-1.0): `spin_mutex`,
  `progressive_backoff_wait`, `seqlock_object<T>` (ideal for a "tempo map
  snapshot" read by many threads), `reclaim_object`. Marked WIP by its authors.
- **choc** (Tracktion, ISC): `choc::fifo::SingleReaderSingleWriterFIFO`,
  `choc::fifo::VariableSizeFIFO` (variable-length OSC/log payloads).

Pattern used in e.g. Zrythm: the RT thread writes into pre-allocated slots and
pushes slot indices onto an SPSC fifo; a non-RT timer drains them. Use that for
onset and beat events; use seqlock or atomics for the continuously updated
beat map.

**Timestamping audio-thread events against wall clock.** Link cannot use
sample time directly: Link maintains a mapping between system time and beat
time, and `Link::Clock` exposes `micros()` (Darwin: `mach_absolute_time`,
Windows: QPC, Linux: `CLOCK_MONOTONIC`). Conversion sources in order of
preference:

1. **Host-provided timestamps.** Standalone: JUCE's
   `AudioIODeviceCallbackContext::hostTimeNs` ("a pointer to a nanosecond
   timestamp if the host provides it, otherwise nullptr"); JUCE fills it from
   CoreAudio's `AudioTimeStamp.mHostTime` on macOS/iOS. Plugin:
   `AudioPlayHead::PositionInfo::getHostTimeNs()` (JUCE 7+) maps VST3
   `ProcessContext::systemTime`, AU `mHostTime`, CLAP via
   clap-juce-extensions. **Uncertain** whether every host populates it; Live
   and Logic do on macOS, Reaper on all platforms. Treat as optional.
2. **Fallback: `ableton::link::HostTimeFilter<Clock>`**, a linear regression
   between sample time and `clock().micros()` sampled at the top of each
   callback. The Link README calls this "the best a client can do" when no
   timestamp is provided and recommends ASIO on Windows for that reason. In the
   Link example `AudioEngine` the per-buffer time is
   `mHostTimeFilter.sampleTimeToHostTime(mSampleTime)` plus `mOutputLatency`
   before `beatAtTime()` / `phaseAtTime()`.
3. `juce::Time::getHighResolutionTicks()` / `std::chrono::steady_clock`: same
   monotonic clocks underneath. Keep one clock domain. Standardise on Link's
   `clock().micros()` so every subsystem shares it, and derive OSC NTP
   timestamps from it.

Rule: the first sample of a block corresponds to `hostTime(block)`; an onset at
sample `n` in the block is at `hostTime + n/fs`, **minus** input latency
(section 3) to get the time it hit the ADC, minus acoustic delay to get the
time the stick hit the head.

**Network and UI threads.** Link runs its own asio-standalone thread (bundled
as a submodule, C++17, `Link.hpp` header-only).
`captureAudioSessionState()` / `commitAudioSessionState()` are the
realtime-safe pair; `captureAppSessionState()` is for the UI.
`juce::OSCSender` is a plain UDP socket: call it from a dedicated
`juce::Thread` or from the message thread with a `Timer`, never from the audio
callback. JUCE `Timer` / `HighResolutionTimer` are about 1 ms granularity and
jittery; use them for UI and OSC cadence only, not MIDI clock (section 4).

## 2. Multi-input: plugin buses vs standalone

**Plugin.** Declare via
`BusesProperties().withInput("Main", stereo).withInput("Kick", mono, false).withInput("Snare", mono, false).withInput("Overheads", stereo, false)`
and gate layouts in `isBusesLayoutSupported`. JUCE's VST3 wrapper exports input
0 as `kMain` and inputs >= 1 as `kAux` (sidechain). Host reality (JUCE bus
layout tutorial): Logic, Pro Tools and Cubase allow at most **one** sidechain
bus; Reaper and JUCE's AudioPluginHost allow up to 32 input buses. Ableton Live
exposes a single "Sidechain, Audio From" routing per plug-in (since 10.1) and
it only works if the plugin publishes an aux bus; mono or stereo only. Bitwig
allows multiple sidechain inputs on VST3 and CLAP (vendor claim; **uncertain**
for 3+ buses).

Consequence: in a DAW, design for **main bus plus one optional aux bus** as the
common denominator (main = drum sub-mix or overheads, aux = kick or trigger).
Offer a "multi-bus" layout for Reaper and Bitwig and let the UI label channels.
Do not rely on more than two input buses in Live or Logic.

**Standalone.** `juce::AudioDeviceManager::setAudioDeviceSetup()` with an
`inputChannels` BigInteger selects arbitrary device channels. Replace JUCE's
`StandaloneFilterWindow` (its `AudioDeviceSelectorComponent` defaults to
`showChannelsAsStereoPairs = true`, which hides single-channel selection, a
known forum issue) with a custom settings panel that lists every physical input
and lets the user map Kick / Snare / Overheads / Any to device channels. Expose
this as a routing matrix (device channel to analysis role), with plugin builds
mapping bus channels to the same roles. Note that
`AudioProcessor::setPlayConfigDetails` is not automatically re-triggered when
the device channel set changes in the standalone wrapper.

## 3. Latency calibration

**Reported latencies.** `AudioIODevice::getInputLatencyInSamples()` /
`getOutputLatencyInSamples()`. JUCE forum consensus: ASIO and WASAPI figures
are accurate; CoreAudio is "ok-ish, depends on device" and historically
reported inconsistently (JUCE's CoreAudio value omitted the buffer size; adding
one buffer gave accurate loopback figures across interfaces, forum thread
41820, fixed in later JUCE, **verify on the JUCE 8 branch**); DirectSound is
useless; ALSA/JACK report `snd_pcm_delay` / `jack_port_get_latency_range`
style numbers that vary by driver. JUCE also adds an empirical `blockSize/4` in
some device code paths. In a plugin, `AudioProcessor::getLatencySamples()` is
what you *report*; what you *need* (the DAW's input path latency including
plugin delay compensation of the upstream chain) is not available through any
plugin API, hence a user-facing calibration.

**Typical values** (round trip; roughly halve for input only):

| Driver | Typical round trip |
|---|---|
| CoreAudio, 32-sample buffer, good interface | about 3 ms |
| ASIO, 64 samples | 4 to 7 ms |
| WASAPI exclusive | about 10 ms |
| WASAPI shared | 20 to 25 ms floor |
| DirectSound | 30 to 40 ms |
| ALSA/JACK with RT kernel | about 5 ms; 15 ms typical without |
| PipeWire | `api.alsa.disable-batch=true` removed about 22 ms on USB devices; `api.alsa.headroom=0` about 1 ms |

Class-compliant USB interfaces often hide 1 to 3 ms of extra unreported DSP or
USB latency. This is exactly what the loopback catches.

**Loopback click method** (what RTL Utility, `jack_delay` and MATLAB's
`audioLatencyMeasurement` do): emit a short known signal (click or MLS/chirp)
on an output, record it on an input patched back physically, cross-correlate
or threshold to find the delay in samples.
`hidden = measured - (reportedOut + reportedIn)`. For Pacemaker the quantity of
interest is **input side**: `inputLatency = measuredRTL - reportedOutputLatency`
(assumes the output report is right; ASIO and CoreAudio usually are). Use 5 to
10 repeated clicks, take the median, reject outliers. Chirp plus
cross-correlation is robust at -20 dB SNR.

**Acoustic latency.** Sound is about 343 m/s, so **2.9 ms per metre** (about
1 ms per 30 cm). Close kick and snare mics: under 0.3 ms; overheads 1 m above
the snare: about 3 ms; room or ambient 3 to 5 m: 9 to 15 ms. The drummer's own
perception is referenced to the stick hit, so subtract mic distance per channel
(ask for "distance to drum" per role, default 0 for close mics, 1 m for
overheads).

**Ableton's Tempo Follower "Latency" slider** (Live 11 and 12, Settings >
Link/Tempo/MIDI > Tempo Follower): the manual says it "adjusts the input
latency applied to incoming audio"; Ableton's FAQ advises keeping input latency
as low as possible because "the lower the audio input latency the quicker the
source's tempo can be analyzed". Semantics: an *input delay compensation* in
ms that shifts detected onsets earlier, exactly the quantity above. Ableton
also has a separate negative **MIDI Clock Sync Delay** for clock out. Exact
slider range not confirmed (ableton.com was blocked during research).

**One-button calibration UX.** "Calibrate": (1) ask the user to patch an output
to the chosen input, or hold a mic at the monitor, (2) play 8 clicks, (3) show
measured input latency plus the hidden delta versus the driver report, (4) ask
for mic distance (preset chips: close / overhead 1 m / room 3 m), (5) store
`totalInputOffsetMs = driverIn + hidden + acoustic` keyed by device, sample
rate and buffer size. Inside a DAW, fall back to a "tap along" calibration: the
user plays along to a metronome the DAW plays, Pacemaker compares detected hits
to the DAW's `PositionInfo` beat grid and sets the offset to the median error.

## 4. MIDI clock and transport output per format

Key fact: **no plugin format can set the host's tempo**, except:

- (a) CLAP's *draft* `clap.transport-control` extension (`request_tempo`,
  `request_time_signature`, `request_start/stop/jump`). Draft, host support
  sparse; Bitwig implements some of it (**uncertain**), and
  clap-juce-extensions does not expose it.
- (b) Reaper via `IReaperHostApplication::getReaperApi("GetSetTempo" /
  "SetTempoTimeSigMarker")` from a VST3 (`reaper_vst3_interfaces.h`).
- (c) **Ableton Link**: Live, Bitwig, Reason and others follow any Link peer's
  `setTempo()`, and Live propagates its Tempo Follower result into Link too.

JUCE's `AudioPlayHead::canControlTransport()` / `transportPlay()` /
`transportRecord()` only start and stop, and host support is spotty.

So: **Link is the primary "host transport" channel**. A Pacemaker plugin
running *inside* Live or Bitwig sets the DAW tempo through Link on localhost.
For hosts without Link (Logic, Pro Tools, Cubase) use MIDI clock out to a
virtual port the DAW syncs to. Logic cannot slave to MIDI clock at all, it only
sends; Cubase, Pro Tools, Reaper, Live and Bitwig can.

**VST3.** No MIDI realtime messages exist in VST3. Note on/off go to the
`Event` bus; CC, pitch bend, aftertouch and program change only via
`LegacyMIDICCOutEvent` (added to the SDK December 2018, JUCE 6+ wrapper emits
it). Host acceptance: Reaper 6.04 and later, JUCE AudioPluginHost; Live and FL
reported not honouring LegacyMIDICCOut from JUCE VST3s (forum reports 2020 to
2022). **0xF8 / 0xFA / 0xFC clock, start and stop cannot be represented and are
dropped by the JUCE VST3 wrapper.** A VST3 Pacemaker cannot emit MIDI clock
through the host.

**AU.** `kAudioUnitProperty_MIDIOutputCallback` / `MIDIOutputCallbackInfo`;
JUCE's AU wrapper implements it when `JucePlugin_ProducesMidiOutput` is set.
Logic routes AU MIDI output only from MIDI-FX (`aumi`) units and "does not
support further processing of MIDI generated inside an AU" as an audio effect.
Realtime bytes are theoretically passable as raw `MIDIPacket`s but Logic will
not forward clock to external ports (**uncertain**; no host does this
reliably).

**CLAP.** `clap_event_midi` carries 3 raw bytes on an output note port, pushed
with `clap_output_events.try_push` in sample order. The spec does not forbid
0xF8 but hosts treat it as note-port data, not clock. Bitwig does not forward
plugin-generated clock to hardware.

**LV2.** Atom MIDI output can carry any bytes; Ardour forwards plugin MIDI to
tracks but not as a transport source; LV2 `time:Position` is host to plugin
only.

**Conclusion for MIDI clock: generate it directly on an OS MIDI port in every
build, plugin and standalone, bypassing the host.** JUCE:
`juce::MidiOutput::openDevice()` / `createNewDevice("Pacemaker Clock")`
(virtual ports on macOS and Linux; Windows needs loopMIDI or a driver).
Timing: `sendMessageNow()` from a `Timer` is ms-granular (24 PPQN at 140 BPM
is 17.857 ms per tick, which rounds to 139 or 147 BPM, useless). Use
`MidiOutput::sendBlockOfMessages(MidiBuffer, millisecondCounterToStartAt, samplesPerSecondForBuffer)`
after `startBackgroundThread()`: JUCE schedules on its own high-priority thread
with sub-ms interpolation. Or run your own `juce::Thread` at
`realtimeAudioPriority` using `std::this_thread::sleep_until` plus a spin for
the last ~200 µs, converting the Link beat timeline to clock ticks
(`beatAtTime` to tick n at `timeAtBeat(n/24)`). Expected jitter on CoreMIDI and
ALSA seq: at most 0.3 ms with a spin-wait, about 1 ms with plain sleeps.
CoreMIDI accepts future timestamps (`MIDITimeStamp`), which JUCE uses when
`sendBlockOfMessages` is given a future start time. On Linux call
`snd_seq_drain_output()` promptly (JUCE does). Also emit **Song Position
Pointer plus Start/Continue** at bar boundaries and keep Pacemaker's own
"clock offset ms" (same role as Live's negative Sync Delay).

## 5. Testing and CI

**Offline regression harness.** Build the engine as a plain C++ static lib
(no JUCE GUI dependency) with a
`process(const float* const* in, int n, int64 samplePos)` entry. A CLI
(`pacemaker-eval`) streams WAVs through it in host-sized blocks (64 to 1024)
and emits beat timestamps. A Python script runs `mir_eval.beat` (F-measure with
the **standard ±70 ms window**, CMLt, AMLt) plus Pacemaker-specific
**latency-to-lock** (beats until first correct) and a **phase error
histogram** against ground truth. Also run the real `AudioProcessor` offline
(`prepareToPlay` plus a `processBlock` loop with a fake `AudioPlayHead`) so
wrapper-level bugs (bus mapping, latency reporting) are covered.

Reference numbers: state-of-the-art offline trackers reach F about 0.9+ on
Ballroom and Beatles and about 0.55 to 0.6 on SMC ("Beat This!" 2024);
online/causal trackers (BeatNet) are lower. Set targets per dataset.

**Datasets.** Groove MIDI Dataset (13.6 h, CC BY 4.0, MIDI plus synthesized
audio; E-GMD 444 h over 43 kits, CC BY 4.0) is the gold mine: render MIDI
through our own drum samples with known onset times to get perfect per-channel
stems and beat/bar truth, with controllable tempo drift. ENST-Drums (3
drummers, 20 hit classes, wet/dry multi-mic, academic licence, request access).
MUSDB18 (150 songs with drum stems, academic use only; beats not annotated,
pair with madmom-generated pseudo-labels). Ballroom (698 excerpts), Beatles,
GTZAN-Rhythm, SMC MIREX (hard, non-percussive) have beat annotations but are
mixes, not stems. Hainsworth is also common.

**pluginval** (Tracktion, GPLv3, fine for a test tool):
`pluginval --strictness-level 5 --validate <plugin>`; exit 0/1; VST3, AU, VST
on all three OSes. No CLAP or LV2: use `clap-validator` from free-audio and
`lv2lint`. Level above 5 adds stress and multithread tests; it runs
`auval -strict` for AUs.

**GitHub Actions.** Extend the existing Vivisect matrix with `macos-14`
(Apple silicon runners; build universal via
`CMAKE_OSX_ARCHITECTURES="arm64;x86_64"`, formats AU VST3 CLAP Standalone).
Costs: macOS minutes bill at 10x (2,000 free private minutes is about 200 macOS
minutes per month); **free for public repos**. Notarisation requires an Apple
Developer Program membership (US$99/yr) and a Developer ID Application cert.
CI steps: import `.p12` into a temp keychain, `codesign --timestamp --options
runtime` each bundle, zip, `xcrun notarytool submit --wait` (App Store Connect
API key or app-specific password), `stapler staple`. Linux: build LV2, CLAP and
VST3, run `pluginval` headless with xvfb. Windows: VST3, CLAP and Standalone;
optionally signtool.

## 6. Standalone and embedded (Raspberry Pi, headless)

JUCE builds on armv7l and aarch64 without extra effort (reports: Pi 4, 64-bit,
`-march=armv8-a`). Audio: ALSA direct (JUCE ignores PulseAudio; PipeWire
interferes unless you use the JACK shim; JUCE's `JUCE_JACK` client works but
is minimal). Community numbers: a Pi 4 ran Guitarix at 48 kHz with 16 frames x
2 periods, about 1.8 ms plus about 1 ms USB, but USB interfaces are generally
stable at **128 to 256 frames x 3 periods** (about 8 to 16 ms); hubs cause
xruns; use a `PREEMPT_RT` kernel and `rtprio` for the audio thread. For a tempo
follower, 256-frame buffers are irrelevant provided input latency is
calibrated. The Pi 5 (Cortex-A76 at 2.4 GHz) is roughly 2 to 3x a Pi 4 and
comfortably runs an STFT-based onset and beat tracker (madmom-class CRNNs are
heavier; a BTrack-style or small-conv ODF at 100 fps is a few percent of one
core). Budget target: at most 10 percent of one core at 48 kHz for the engine
on x86; at most 30 percent on a Pi 4.

Headless: a `juce::ConsoleApplication`-style binary using
`AudioDeviceManager` plus `MidiOutput` plus Link plus an embedded
HTTP/WebSocket server (civetweb or a minimal `juce::StreamingSocket` server)
serving a static web UI; or Link plus OSC only, configured from a phone via
OSC. Both require the engine library to be JUCE-GUI-free (same as the test
harness), which is the main architectural prerequisite. Running JUCE headless
on Linux without X11 needs no `juce_gui_basics` on that target.

## 7. Licensing

- **JUCE 8** (EULA 2024): Starter **free up to US$20k** annual revenue, Indie
  up to $300k ($40/mo or $800 perpetual), Pro unlimited ($175/mo or $3,500
  perpetual). Splash screen removed from Starter. Revenue is counted across all
  sources related to use of the framework. GPLv3 option remains. The Starter
  cap is $20k, not $50k.
- **Ableton Link**: dual **GPLv2+ or proprietary** (contact
  link-devs@ableton.com). Bundled asio-standalone is BSL-1.0. If Pacemaker
  ships closed-source it needs the Ableton licence (historically granted free
  for apps, **uncertain terms**). GPLv2 Link plus GPLv3 JUCE is a licence
  conflict unless "GPLv2 or later" is exercised as v3.
- **clap-juce-extensions**: MIT. **CLAP SDK**: MIT. **LV2**: ISC; JUCE's LV2
  wrapper is part of JUCE. **VST3 SDK**: GPLv3 or Steinberg proprietary (free,
  requires a signed agreement). **pluginval**: GPLv3 (tool only).
- Beat-tracking libraries: **BTrack** GPLv3 (C++, real-time; viral for a closed
  plugin), **aubio** GPLv3+, **madmom** source BSD but **models CC BY-NC-SA
  4.0** (contact Gerhard Widmer for commercial), **Essentia** AGPLv3 with a
  commercial licence from UPF, **librosa** ISC (Python only), BeatNet licence
  not confirmed (**uncertain**). farbot MIT, crill BSL-1.0, choc ISC, mir_eval
  MIT.

Practical consequence: write Pacemaker's tracker in-house (ODF plus comb-filter
or autocorrelation plus Kalman or particle phase lock) or under a permissive
licence. Use GPL libs only in the offline evaluation tooling.

---

## Recommended architecture for Pacemaker

**Targets.** One CMake project:

- `pacemaker_engine`: static lib, no JUCE GUI, depends on juce_core/juce_dsp
  only (or header-only).
- `pacemaker_plugin`: `juce_add_plugin` FORMATS VST3 AU LV2 Standalone plus
  `clap_juce_extensions_plugin`.
- `pacemaker_headless`: Linux/Pi CLI.
- `pacemaker_eval`: test CLI.

Link via FetchContent (header-only, `AbletonLinkConfig.cmake`).

**Buses.** Plugin: `Main` stereo (default = drum bus or overheads) plus
optional aux `Trigger` mono/stereo (kick or snare); extra optional buses
`Kick`, `Snare`, `OH` exposed for Reaper and Bitwig; a channel-to-role map in
the processor state. Standalone and headless: custom device panel, per-channel
role plus mic distance. No audio output needed except optional pass-through of
main and a calibration click generator on an output bus.

**Threads and data flow.**

1. *Audio thread* (host or `AudioDeviceManager`): per block, compute
   `blockHostTimeUs` (from `PositionInfo::getHostTimeNs` /
   `AudioIODeviceCallbackContext::hostTimeNs`, else `HostTimeFilter`), run
   ODF and onset detection per role, run the online beat and tempo tracker,
   `captureAudioSessionState()`; if locked, `setTempo(bpm, t)` and
   `forceBeatAtTime` once on lock, then `commitAudioSessionState()`. Publish
   `BeatMapSnapshot{tempo, beatOriginUs, bar, beatsPerBar, confidence}`
   through a `crill::seqlock_object` or atomics. Push `OnsetEvent`s into a
   farbot SPSC fifo for UI and OSC. Every timestamp carries
   `- inputLatencyUs - acousticUs(role)` compensation.
2. *Link thread* (owned by Link).
3. *Clock thread* (`juce::Thread`, realtime priority): reads
   `BeatMapSnapshot`, computes next 24 PPQN tick times, issues
   `MidiOutput::sendBlockOfMessages` about 20 ms ahead (or spin-wait plus
   `sendMessageNow`), plus Start/Stop/SPP at bars; applies user clock offset.
4. *OSC/network thread* (`juce::Thread` with `WaitableEvent`): drains onset
   fifo, sends `/pacemaker/beat`, `/tempo`, `/bar` with bundles timestamped
   from the Link clock (NTP conversion); optional WebSocket for the headless UI.
5. *Message thread*: UI (JUCE), settings (`RealtimeObject` publish to audio),
   calibration state machine, device selection.

Host transport: report `AudioPlayHead` info read-only; set DAW tempo via Link
(Live, Bitwig), Reaper API (optional), MIDI clock virtual port elsewhere.

**Calibration module.** Click generator on the audio thread; captured click
cross-correlated on a worker thread; stores
`{device, sr, buffer} -> hiddenLatencySamples`; tap-along mode in plugin
builds.

**Testing.** `pacemaker_eval` plus mir_eval in CI (Linux job) on a curated
subset (GMD-rendered stems with tempo ramps, ENST, Ballroom); pluginval level 5
for VST3 and AU, clap-validator, lv2lint; macOS job builds universal and
notarises on tags only.

## Sources

- Ableton Link README (licensing, Clock, HostTimeFilter, latency): https://github.com/Ableton/link
- JUCE forum Link tutorial: https://forum.juce.com/t/ableton-link-tutorial-how-to-build-and-some-tips/31242
- JUCE forum, transport control with Link: https://forum.juce.com/t/transport-control-with-link/46098
- JUCE AudioIODeviceCallbackContext: https://docs.juce.com/master/structAudioIODeviceCallbackContext.html
- JUCE AudioPlayHead: https://docs.juce.com/master/classAudioPlayHead.html
- JUCE bus layouts tutorial: https://docs.juce.com/master/tutorial_audio_bus_layouts.html
- JUCE forum, standalone mono input selection: https://forum.juce.com/t/standalone-app-can-not-select-1-input-channel-only-stereo/45876
- JUCE forum, CoreAudio latency reporting: https://forum.juce.com/t/coreaudio-device-latencies-are-reported-inconsistently-with-other-audio-device-types/41820
- JUCE forum, ASIO latencies: https://forum.juce.com/t/asio-wrong-latencies-reported/8668 and https://forum.juce.com/t/what-is-the-correct-way-to-calculate-audio-delay/12737
- JUCE forum, macOS round-trip latency: https://forum.juce.com/t/macos-round-trip-latency/45278
- RTL Utility: https://www.gearnews.com/rtl-utility-for-measuring-audio-latency-gets-an-official-release/ and https://archimago.blogspot.com/2021/11/rtl-utility-look-at-audio-interface.html
- RtAudio API latency notes: https://caml.music.mcgill.ca/~gary/rtaudio/apinotes.html
- Microsoft low-latency audio: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio
- PipeWire latency issue: https://gitlab.freedesktop.org/pipewire/pipewire/-/issues/940
- MATLAB audio latency measurement: https://www.mathworks.com/help/audio/ug/measure-audio-latency.html
- Ableton manual, Link/Tempo Follower/MIDI: https://www.ableton.com/en/live-manual/12/synchronizing-with-link-tempo-follower-and-midi and FAQ https://help.ableton.com/hc/en-us/articles/360019100900-Tempo-Following-in-Live-11-FAQ
- Acoustic latency: https://www.audiomasterclass.com/blog/q-there-is-a-delay-in-my-drum-overheads-how-can-i-fix-it
- VST3 MIDI CC out threads: https://forum.juce.com/t/add-support-for-sending-midi-ccs-out-of-vst3-plugins/35781 , https://forum.juce.com/t/vst3-midi-and-juce/31100 , https://forums.steinberg.net/t/vst3-and-midi-cc-pitfall/201879
- AU MIDI output in JUCE: https://forum.juce.com/t/patch-for-adding-midi-output-support-to-audiounit-effects/11492
- CLAP events: https://github.com/free-audio/clap/blob/main/include/clap/events.h ; transport-control draft: https://github.com/free-audio/clap/blob/main/include/clap/ext/draft/transport-control.h
- clap-juce-extensions: https://github.com/free-audio/clap-juce-extensions
- Reaper VST3 API access: https://forum.juce.com/t/reaperembeddedviewplugindemo-example-getting-parent-track-develop-branch/46571
- JUCE MIDI clock timing threads: https://forum.juce.com/t/sending-midi-clock-from-standalone-app/8025 , https://forum.juce.com/t/high-accuracy-scheduling-for-midi-only-so-no-audio-interface-or-process-block-callback/63659 , https://forum.juce.com/t/midiclock-class/11146
- E-RM MIDI clock jitter report: https://www.e-rm.de/data/E-RM_report_Jitter_02_14_EN.pdf
- LV2 transport sync: https://drobilla.net/2012/11/17/lv2-plugin-transport-synchronisation.html
- farbot: https://github.com/hogliux/farbot ; crill: https://github.com/crill-dev/crill
- pluginval: https://github.com/Tracktion/pluginval/blob/master/README.md
- Code-sign and notarise plugins in CI: https://forum.juce.com/t/article-how-to-code-sign-and-notarize-macos-audio-plugins-in-ci/53131
- GitHub Actions runner pricing: https://docs.github.com/en/enterprise-cloud@latest/billing/reference/actions-runner-pricing
- Beat This! (2024): https://arxiv.org/pdf/2407.21658 ; BeatNet: https://arxiv.org/pdf/2108.03576
- Groove MIDI Dataset: https://magenta.tensorflow.org/datasets/groove ; E-GMD: https://magenta.tensorflow.org/datasets/e-gmd ; MUSDB18: https://zenodo.org/records/1117372
- Raspberry Pi audio: https://wiki.linuxaudio.org/wiki/raspberrypi ; https://discourse.ardour.org/t/lowering-buffer-on-rpi4/104671
- JUCE JACK on Linux: https://forum.juce.com/t/understanding-how-juce-handles-jack-on-linux/65998
- JUCE 8 EULA tiers: https://forum.juce.com/t/important-changes-to-the-juce-end-user-licence-agreement-for-juce-8/61265
- Link licensing (CDM): https://cdm.link/2016/09/ableton-opening-link-everyone-starting-today/
- BTrack: https://code.soundsoftware.ac.uk/hg/btrack/file/tip/README.md ; madmom: https://pypi.org/project/madmom ; Essentia licensing: https://essentia.upf.edu/licensing_information.html
