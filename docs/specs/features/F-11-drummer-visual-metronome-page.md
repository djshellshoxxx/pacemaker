# F-11: Drummer visual metronome page

Status: draft 1.0 · Priority: P1 · Estimate: 5 pd · Depends on: server (existing) · Related: RS-07, F-03

## 1. Goal and user value
Closes the feedback loop: the drummer (or the band) sees what Pacemaker thinks, on a phone or tablet on the stage Wi-Fi: big tempo, beat flashes, bar number, and whether they are rushing or dragging. Also the bandleader's remote.

## 2. Requirements
- R-1 Route `/stage` (read-only) and `/stage?control=1` (with Follow, Tap, Downbeat, Relock) served by the same host; works offline (no external assets).
- R-2 Full-screen layout, landscape and portrait: giant BPM, bar:beat dots (downbeat bigger), pulsing flash on each beat synced to the predicted beat time (client extrapolation with measured clock offset; flash within 25 ms of the audible beat on Wi-Fi).
- R-3 Rush/drag indicator: arrow and ms value from the phase error of recent hits versus the grid (needs F-08 microtiming or tracker phase error); colour-coded with thresholds (green within 10 ms, amber 10 to 25, red beyond).
- R-4 Wake lock (screen stays on), PWA manifest (add to home screen), dark theme, 3 sizes of type, optional click sound off by default (never autoplays).
- R-5 Connection: shows latency and "reconnecting"; keeps last values dimmed; auto-reconnect.
- R-6 Security: optional PIN for control mode (4 digits) stored in settings; read-only view needs none; rate limit on control calls.
- R-7 Clock sync: client estimates server clock offset with 8 ping round trips (median), re-estimates every 30 s; beat flash uses offset-corrected times.
- R-8 mDNS name shown (`pacemaker.local`) and a QR code in settings for quick join.
## 3. Design
Reuses the status stream; adds `/api/time` (server monotonic micros) for offset estimation; a new HTML file embedded like the main UI; control endpoints already exist, wrapped by a PIN check.
## 4. Tests
F11_R1 route and offline (no external requests); F11_R2 flash timing in a Playwright run with a virtual server clock (error under 25 ms); F11_R3 thresholds; F11_R6 PIN and rate limit; F11_R7 offset estimator unit tests with synthetic jitter; screenshots portrait and landscape.
## 5. Acceptance
Three phones on stage Wi-Fi show synchronous flashes within 30 ms of each other; the drummer reads tempo at 3 m.
## 6. Plan
| Task | pd |
|---|---|
| Page, layout, flash engine, wake lock, PWA | 2 |
| Clock sync endpoint and estimator | 1 |
| Rush/drag metric and indicator | 1 |
| PIN, rate limit, QR, tests | 1 |
Risks: Wi-Fi jitter (offset estimator, median); phones throttling timers in background (wake lock, visible only).
