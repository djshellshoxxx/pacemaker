# F-18: Dual-instance redundancy

Status: draft 1.0 · Priority: P3 · Estimate: 15 pd · Depends on: F-03, F-04, F-05 · Related: DIFFERENTIATION rank 10

## 1. Goal and user value
Professional rigs expect failover. Two machines (or two instances) listen to the same drummer, agree on the clock and one publishes; if it fails the other takes over without a tempo jump.
## 2. Requirements
- R-1 Roles: Primary and Secondary, chosen by the user or by election (highest health score wins, ties by id). Both track; only the publisher drives outputs; the other runs "shadow" outputs muted.
- R-2 Peer protocol (UDP on the LAN, port configurable): heartbeat every 50 ms with `{instanceId, role, healthScore, snapshot(bpm, origin in shared time, beatInBar, bar, confidence, sequence)}`; shared time base from Link clock when available, else a built-in two-way time sync (NTP-like, error under 200 us on wired LAN).
- R-3 Failure detection: heartbeat missing for 300 ms or health below threshold for 1 s triggers takeover; takeover is phase-continuous: the new publisher adopts the old publisher's last published phase and tempo and slews to its own estimate within 4 beats (no Relock, no `forceBeatAtTime`).
- R-4 Split-brain protection: on network partition both keep publishing only if they own different outputs (configurable); default: the one with local audio health wins; on rejoin, the lower-priority one yields after agreement of 2 s.
- R-5 MIDI clock handover: no extra Start/Stop; ticks continue seamlessly (same grid); Link publisher change is invisible to peers (same session).
- R-6 Health score from: audio device ok, xruns, CPU, input signal present, confidence, output errors.
- R-7 UI: Redundancy card shows peer status, roles, last heartbeat, takeover log; manual "Take over" and "Yield" buttons; failover test button (simulated).
- R-8 Security: optional shared secret (HMAC on packets), LAN only, no remote control.
## 3. Design
`PeerLink` thread (UDP, HMAC), `PublisherElector` (pure state machine), `TimeSync`; outputs consult `isPublisher()` (atomic). Phase adoption reuses `ClockMap` and a handover slew in the publish stage.
## 4. Tests
F18_R3 virtual-time simulation of two engines with scripted failures: tempo jump under 0.5 BPM and phase jump under 5 ms at takeover; F18_R4 partition scenarios; F18_R2 time sync accuracy with simulated jitter; F18_R8 HMAC rejection; chaos test (random kills, 1 hour simulated); real two-machine test on a switch.
## 5. Plan
| Task | pd |
|---|---|
| Peer protocol, HMAC, time sync | 4 |
| Elector state machine, health score | 3 |
| Handover slew and output gating | 3 |
| UI and settings | 2 |
| Simulation and chaos tests, two-machine run | 3 |
Risks: split brain (explicit ownership rules, tests); time sync on Wi-Fi (recommend wired, warn).
