# F-04: Ableton Link output

Status: draft 1.0 · Priority: P0 · Estimate: 14 pd (plus licence lead time) · Owner: I/O
Depends on: F-30, Ableton licence for commercial builds · Related: ES-02 section 2, RS-06, research 05 section 1

## 1. Goal and user value
Link lets Live, Resolume, Reason, many DJ and iOS apps follow Pacemaker's tempo and beat phase over the network. The publishing policy is written and tested
against a mock (`LinkPolicy`, O2). This feature connects it to the real SDK, makes the clocks consistent, handles several instances, and gives the user clear status.

## 2. Scope and non-goals
In: SDK vendoring, `AbletonLinkSession` adapter, `LinkService`, `LinkHostClock`/`ClockBridge`, start/stop sync option, arbitration across instances, tempo slew, UI tile and API,
two-process tests, compatibility notes, licence request. Out: Link Audio, being a passive follower of Link tempo (Pacemaker is a publisher; a "Link follower" mode is a possible later feature).

## 3. Requirements
- R-1 `PACEMAKER_WITH_LINK=ON` builds `pacemaker_link` with the Ableton Link SDK (pinned submodule, by hash); `OFF` builds and passes all non-Link tests and hides Link UI (O6).
- R-2 One `ableton::Link` per process through a ref-counted `LinkService`; created on first enable, destroyed on last release; the enable state is per user setting.
- R-3 `AbletonLinkSession` implements `LinkSession` using `captureAudioSessionState`/`commitAudioSessionState` on the audio thread and `captureAppSessionState` elsewhere; no allocation or lock on the audio thread.
- R-4 Time base: when Link is enabled, `ClockMap` host time is the Link clock (`link.clock().micros()`); if the process clock differs, a `ClockBridge` estimates the offset (median of 32 paired reads, refreshed every 2 s) and the bridge error is under 50 us.
- R-5 Beat timestamps passed to Link are the **acoustic** times of the drummer's beats (input and acoustic compensation applied, no output latency added), so other apps that compensate their own output latency land on the drummer.
- R-6 Tempo commits obey the policy: gate 0.35 confidence, interval 50 ms (100/30 ms stage/rehearsal presets), deadband 0.05 BPM (0.1/0.02), plus a **slew limit** of 8 BPM per second so a wrong estimate cannot yank peers; relock forces the beat once per `sequence` (never otherwise).
- R-7 Quantum default `beatsPerBar`, user selectable 1 to 16; start/stop sync off by default; when on, Start is requested the first time the engine reaches Locked after Idle and Stop after Idle longer than 10 s.
- R-8 Arbitration: with several instances in one process the publisher is the instance with Follow on and highest confidence, re-evaluated every 500 ms with hysteresis (needs a 0.1 lead for 2 s to switch); others read-only; UI shows a "Publisher" badge.
- R-9 Status: enabled, peer count, publisher flag, session tempo, last error, "another app is setting tempo" warning (two jumps over 2 BPM within 10 s).
- R-10 When Link is enabled and Follow is off (Hold), tempo is frozen and no commits are made; relock events are suppressed.
- R-11 Disabling Link leaves the session cleanly (no forced tempo change); enabling joins an existing session without changing its tempo until confidence is above the gate.
- R-12 Compatibility: documented behaviour with Live (tempo follow), Resolume, Reason, and one iOS Link app; the Link test plan published with the SDK is run once per release (**VERIFY** the current document).
- R-13 CPU: Link commit path under 0.2 percent of a core.

## 4. Design
```cpp
class AbletonLinkSession : public LinkSession {   // pacemaker_link, wraps ableton::Link&
  double tempo() override;                    // captureAudioSessionState().tempo()
  void setTempo(double bpm, int64_t atUs);    // state.setTempo(bpm, micros); commitAudioSessionState
  void forceBeatAtTime(double beat, int64_t us, double quantum);
  double beatAtTime(int64_t us, double quantum);
  int numPeers();
};
class LinkService { static std::shared_ptr<LinkService> acquire(); ableton::Link& link(); void setEnabled(bool); ... };
struct PublisherArbiter { void report(instanceId, followOn, confidence, seq); InstanceId current(); };
```
`LinkPolicy` is unchanged apart from the slew limiter and the Hold/Follow rule. Start/stop sync uses `enableStartStopSync` and `setIsPlaying` with `timeForIsPlaying`. A separate `link_probe` test binary joins the session, prints `tempo,beatAtTime(now)` every 10 ms, and exits on a signal; the two-process test launches it.
Licensing: the proprietary licence is required for any closed-source distribution; the GPL path means the combined work must be GPLv2+. Decision recorded in an ADR; the default distribution without a licence ships without Link.

## 5. Test plan
| ID | Method | Pass |
|---|---|---|
| F04_O2 | Existing mock test plus slew limiter and Hold rules | never forces without relock; slew <= 8 BPM/s |
| F04_O1 | Two processes (server driven by file replay with 120 to 126 BPM ramp, `link_probe`) | probe tempo within 0.1 BPM within 2 s of ramp end; probe phase at predicted beat within 0.01 beats after relock |
| F04_R4 | Bridge unit test with synthetic clocks (offset, drift 100 ppm) | error under 50 us |
| F04_R8 | Arbiter unit test with scripted reports | no flapping; one publisher |
| F04_O6 | CI job with Link OFF | all non-Link tests pass |
| F04_R7 | Start/stop sync behaviour with mock | exactly one start per Idle to Locked transition |
Link discovery inside containers can fail; the O1 job has a retry on infrastructure failure only (peer not found) and records it. Manual: Live 12 and Resolume session on the same LAN, Wi-Fi and Ethernet.

## 6. Acceptance criteria
Live following Pacemaker through a 4-minute song with fills shows no tempo jumps larger than 1 BPM and the Live metronome lands on the drummer within 20 ms (measured with the loopback method); disabling Link or killing Pacemaker leaves Live at its last tempo.

## 7. Development plan
| # | Task | pd | Notes |
|---|---|---|---|
| L-1 | Licence request email and tracking; ADR on GPL versus proprietary | 0.5 | **send in week 1**; include product, platforms, volume |
| L-2 | Vendor SDK, CMake target, CI job ON and OFF | 1.5 | |
| L-3 | `LinkService` and `AbletonLinkSession` | 3 | |
| L-4 | `ClockBridge`, Link clock integration with `ClockMap` | 2 | |
| L-5 | Slew limiter, Hold rule, start/stop sync | 1.5 | |
| L-6 | Arbiter and multi-instance behaviour | 1.5 | |
| L-7 | UI tile, API, settings, warnings | 1.5 | |
| L-8 | Two-process tests, compatibility run, docs | 2.5 | |
Risks: licence delay (build flag, MIDI clock and OSC still ship); multicast problems on corporate Wi-Fi (documented, diagnostics show peers); clock mismatch bugs (bridge test, soak test of 1 hour).
Erratum to check: ES-02 section 6 lists Logic Pro and Pro Tools under Link; to our knowledge neither supports Link natively. Verify per host before publishing host guides (F-27).
