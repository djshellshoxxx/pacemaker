# RS-07: Web UI, control API and host app (regular spec)

Status: draft 0.1, normative for `pacemaker_server`.
Extends RS-01 section 4 (main screen) and RS-03 section 2 (Box web UI).

`pacemaker_server` is the first runnable host of the engine and outputs.
It has no JUCE dependency: plain C++17, POSIX sockets, one embedded
single-page UI. It is the Box's web UI, the development harness for the
plugin UI, and a demo mode (built-in simulated drummer) that needs no
audio hardware. The JUCE plugin and standalone (Phase 3) reuse the same
`pacemaker_outputs` library and the same visual design.

## 1. Process model

| Thread | Does |
|---|---|
| Audio/sim | Pulls 256-sample blocks from the simulated drummer, paces to wall clock (or `--speed N`), runs `Engine::process`, feeds `ClockMap`, records trace, onsets, beats and the drift log. |
| Clock | `ClockThread` (ES-02 section 3): MIDI clock ticks to a `MidiSink`. |
| OSC | `OscRunner` (ES-02 section 4): generates packets from the snapshot, sends UDP at their due time. |
| HTTP | `HttpServer`: one accept thread, one short-lived thread per request. SSE clients are kept as open sockets and written to from the status thread. |
| Status | Every 50 ms builds the status delta and broadcasts it; debounces config writes. |

All settings changes arrive on the HTTP thread, are validated, stored in
`Settings` under a mutex, and applied by the audio thread at the next
block boundary (engine re-prepare happens between blocks, never inside
one).

## 2. HTTP API (JSON, UTF-8)

`GET /` UI page. `GET /api/state` full state (status plus `settings`, `songs`,
`presets`). `GET /api/events` SSE stream with one event type, `status`,
every 50 ms carrying only what changed (section 3). All POSTs return `{"ok":true}` or
`{"ok":false,"error":"..."}` with HTTP 400.

| Route | Body | Effect |
|---|---|---|
| `POST /api/action` | `{"name":"tap\|downbeat\|nudgeMinus\|nudgePlus\|half\|double\|relock\|follow","value":bool?}` | engine user action |
| `POST /api/settings` | partial settings object (section 4) | merge, validate, apply |
| `POST /api/preset` | `{"name":"..."}` | parameters only (RS-04 section 2) |
| `POST /api/song` | `{"index":n}` or `{"name":"..."}` | apply song (RS-01 section 6) |
| `POST /api/songs` | `{"csv":"..."}` or `{"songs":[...]}` | replace the song map |
| `POST /api/sim` | `{"bpm":..,"jitterMs":..,"drift":..,"playing":bool,"fill":bool,"pattern":..}` | control the simulated drummer |
| `POST /api/calibrate` | `{"mode":"loopback\|reset"}` | run the loopback measurement against the simulated interface (hidden 37 samples) and apply the result, or clear the calibration |
| `GET /api/drift.csv` | | drift log (RS-04 section 6) |
| `GET /api/report` | | per-song drift report JSON (RS-01 section 7) |
| `GET /api/tempomap.mid` | | standard MIDI file with tempo events |

No authentication in v1 (LAN appliance); binds to `127.0.0.1` unless
`--bind 0.0.0.0` is given. Request bodies are limited to 256 KiB.

## 3. Status object

```
{ "epoch": 3, "t": 41.2, "state": "LOCKED", "bpm": 120.3, "confidence": 0.91,
  "bar": 12, "beat": 3, "beatsPerBar": 4, "meterDen": 4, "follow": true,
  "flags": {"octave": false, "barConfirmed": true, "relock": false},
  "grid": {"origin": 41.0, "period": 0.499},        // last beat time and period, seconds
  "levels": [{"role":"Drum bus","peak":0.4,"onset":false}],
  "trace":  {"from": 4120, "hz": 100, "v": [0.12, 0.31, ...]},   // envelope, index i is t = i / hz
  "onsets": [[id, t, strength], ...], "beats": [[id, t, beatInBar, bar], ...],
  "bpmHistory": [[id, t, bpm], ...],
  "song": 1, "songName": "...",
  "outputs": { "link": {...}, "midi": {...}, "osc": {...}, "sync": {...} },
  "calibration": {...}, "warnings": ["..."], "sim": {...} }
```

`t` is session time in seconds of audio processed. `epoch` increments
whenever the engine is re-prepared (settings that affect tracking); the
client clears its history when the epoch changes. SSE events carry only
new `trace` samples (`from` is the index of the first value) and
onsets/beats/history entries with an `id` greater than the last one sent;
`/api/state` carries the last 8 s of each. A client that sees
`trace.from` beyond its last index refetches `/api/state`. The grid is
extrapolated by the client: beat `k` is at `origin + k * period`, with
beat number `((beat - 1 + k) mod beatsPerBar) + 1`.

Output blocks report `available` (false for Link and the Sync bus in the
server build: Link needs the Ableton licence, the Sync bus needs an audio
device), `enabled`, `connected`, `sent`, and for MIDI `jitterUs` (standard
deviation of how late each tick left versus its due time).

## 4. Settings object (state version 1)

```
{ "stateVersion": 1,
  "engine": { "profile":"stage|rehearsal", "meterNum":4, "meterDen":4,
              "pattern":"rock44|halftime44|fouronfloor44|waltz34|compound68",
              "referenceBpm":0, "minBpm":50, "maxBpm":220, "sensitivity":0.5 },
  "outputs": {
    "link": {"enabled":false,"quantum":4,"publishTempo":true,"gate":0.35},
    "midi": {"enabled":false,"port":"","offsetMs":0,"sppOnRelock":false},
    "osc":  {"enabled":false,"host":"127.0.0.1","port":9000,"root":"/pacemaker","profile":"generic|resolume|magicq"},
    "sync": {"enabled":false} },
  "calibration": {"inputLatencySamples":0,"hiddenOutputSamples":0,"distanceM":[0.1],"moduleLatencyMs":3},
  "ui": {"stage":false, "theme":"dark"} }
```

Unknown keys are ignored, missing keys default, a higher `stateVersion`
loads what it can and adds a warning (RS-04 section 4). The server
persists settings to `--config path` (default `pacemaker.json`) after each
change, debounced to 1 s, written atomically (temp file plus rename).

## 5. UI layout and behaviour

Dark, high contrast, medical (RS-01 section 4). Single file, no external
requests (fonts are the system stack, charts are canvas). Responsive:
header and trace stack on phones; inputs and outputs become two columns on
widths over 900 px.

1. **Header**: state word (LISTENING, COUNT-IN, LOCKING, LOCKED, HOLD,
   CHASE) in the state colour, BPM in tabular digits (one decimal), meter,
   `bar:beat` with beat dots (downbeat dot larger), confidence arc (SVG),
   Follow toggle (large; off = Hold).
2. **Trace**: canvas, 8 s window scrolling right to left. Envelope line,
   onset spikes, predicted beat grid (downbeats thicker) with faint
   expectancy bands; the footer shows the tempo range of the last 60 s. `requestAnimationFrame` redraw; the
   timeline is extrapolated between 50 ms updates so motion is smooth.
3. **Inputs strip**: one tile per role with level meter, onset LED, distance
   chip (Close, Overhead, Room) feeding calibration.
4. **Outputs strip**: Link, MIDI clock, OSC, Sync tiles: enable switch
   (disabled with an explanation when unavailable), status line
   (connected/error/jitter), MIDI offset slider, and the settings dialog
   for port, host, port, profile. A per-output OSC offset is a later item.
5. **Transport**: Tap, Downbeat now, Nudge -, Nudge +, Half, Double,
   Relock. Keyboard: `T` tap, `D` downbeat, `[`/`]` nudge, `H` half, `X`
   double, `R` relock, `F` follow, `S` stage mode, `?` help overlay.
6. **Footer**: profile, reference BPM, pattern, meter, Calibrate (badge
   until calibrated), Songs drawer, Report drawer, settings.
7. **Simulator card** (shown when the source is the simulated drummer):
   tempo slider, humanise, drift, Play/Stop, Fill.
8. **Stage mode**: header, trace, transport only; fonts scale to 2 m
   readability.
9. **Connection loss**: red banner, auto reconnect with backoff; the last
   state stays visible.
10. Respect `prefers-reduced-motion` (no pulse animation), all controls
    focusable with visible focus rings, `aria-live` on the state word.

## 6. Song map, drift log, report

Song map as RS-04 section 3 (CSV columns `name,bpm,meterNum,meterDen,
pattern,countInBeats,programChange`). Drift log rows are
`timestampUs,bpm,confidence,state,bar,beat,event` and are kept in memory
(bounded to 200 000 rows) and exported on request. The report groups rows
by song and returns `{minBpm,maxBpm,meanBpm,maxDeviationBpm,
secondsInHold,relocks,beats}`. The tempo map export writes one tempo event
per beat whose tempo differs by more than 0.05 BPM from the previous
event, resolution 480 ticks per quarter.

## 7. Tests

- W1. JSON parse/serialise round trip, including unicode escapes and
  rejection of malformed input without a crash.
- W2. Settings: defaults, merge, clamping, unknown keys ignored, future
  version accepted with a warning (A4).
- W3. Song map CSV import and selection.
- W4. Drift log CSV, report numbers on a constructed series, MIDI file
  structure.
- W5. HTTP: loopback server serves `/`, `/api/state` parses as JSON, an
  action POST changes state, oversize bodies are rejected.
- W6. End to end: the host at 10x speed locks onto the simulated drummer
  within 2 BPM, fills the drift log, and the calibration endpoint finds the
  hidden 37 samples.
- W7. Settings persist across a restart (atomic write, flushed on stop).
