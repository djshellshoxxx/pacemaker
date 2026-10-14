# pacemaker_headless: the Pacemaker Box

Headless build for a Raspberry Pi 4 or 5 (RS-03 section 2). A JUCE console app (no `juce_gui_basics`) that owns the
audio device, MIDI ports and the shared `Core` (engine plus every output), and serves the configuration and monitoring
UI over HTTP on port 8080.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPACEMAKER_BUILD_HEADLESS=ON
cmake --build build --target pacemaker_headless
build/headless/pacemaker_headless_artefacts/Release/pacemaker_headless --help
```

Optional CMake switches: `PACEMAKER_HEADLESS_GPIO` (status LEDs, default ON where `<linux/gpio.h>` has uAPI v2) and
`PACEMAKER_HEADLESS_OLED` (SSD1306 over `/dev/i2c-N`, default ON where `<linux/i2c-dev.h>` exists). Both compile out
cleanly otherwise; the program then logs that the feature is unavailable. The LEDs use the kernel GPIO character device
directly (the interface libgpiod wraps), so no extra library is needed on the Pi.

## Command line

| Option | Meaning |
|---|---|
| `--config FILE` | JSON configuration, default `/etc/pacemaker/config.json` (built-in defaults when the default file is absent) |
| `--check-config` | validate the file, print a summary, exit 0 (ok) or 2 (error with the offending key) |
| `--list-devices` | audio device types, devices, channel counts, rates, buffers; MIDI inputs and outputs |
| `--wav-in FILE` | feed the core from a WAV (offline demo and tests); `--realtime` paces to the wall clock and drives the real outputs, `--linger SEC` keeps the web UI up afterwards (-1 until SIGTERM), `--out-dir DIR` receives the drift log CSV, `pacemaker-tempomap.mid` and `pacemaker-report.html` |
| `--port N` | override `web.port` (0 disables the web UI) |
| `--selftest` | synthetic drums through the core: checks config parsing, the lock, every key web endpoint, SSE, token, MIDI clock, LED pulses, OLED renderer and the SIGTERM path (MIDI Stop last). Exit 0 / 1. Needs no sound card. |
| `--bench [SEC]` | engine plus outputs CPU on this machine, see "CPU budget" |
| `--stderr` | also log to stderr |

SIGTERM and SIGINT shut down cleanly: web server and indicators stop, the audio device closes, the MIDI clock thread sends
MIDI Stop (0xFC) so external gear is left stopped, the drift log is closed and the settings are saved.

## Configuration

`/etc/pacemaker/config.json` holds install-time settings (device, channel map, ports) plus an embedded `state` (a
`PluginState`: the same JSON the plugin saves). Unknown keys are ignored; a wrong type or range is an error that names
the key. `deploy/config.json` is a complete example.

```
{
  "audio": {
    "deviceType": "ALSA",          // "ALSA" (default on Linux) or "JACK" if your JUCE build has it; "" = platform default
    "inputDevice": "",             // substring of the name from --list-devices; "" = first device
    "outputDevice": "",            // only needed for the Sync pulse or loopback calibration; "" = same card as the input
    "sampleRate": 48000,
    "bufferSize": 256,             // 128 or 256 frames
    "inputs": [                    // device input channel -> role + microphone distance (overrides state.session.channels)
      { "channel": 0, "role": "kick", "distanceM": 0.10, "mute": false }   // roles: any kick snare hihat overhead trigger
    ],
    "syncLeft": -1, "syncRight": -1,   // output channels for the Sync pulse + click (ES-02 section 5); -1 = off
    "calibrationOut": -1               // output channel for the loopback click; -1 = use syncLeft
  },
  "midi": {
    "inputs": ["*"],               // "*" = every port (except Midi Through / Pacemaker's own), or name substrings
    "clockOut": "",                // exact output port name from --list-devices; sets session.midiPort and enables the clock on first run
    "virtualPort": false           // create a virtual port with that name instead (Linux ALSA sequencer)
  },
  "web": { "enabled": true, "port": 8080, "bind": "0.0.0.0", "token": "" },
  "gpio": { "enabled": false, "chip": "/dev/gpiochip0", "beatLine": 17, "downbeatLine": 27, "pulseMs": 40 },
  "oled": { "enabled": false, "device": "/dev/i2c-1", "address": 60, "rotate180": false },
  "log":  { "file": "/var/log/pacemaker/pacemaker.log", "maxKB": 1024, "keep": 3 },   // rotating; "" = stderr only
  "stateFile": "/var/lib/pacemaker/state.json",   // settings changed in the web UI are saved here
  "rtPriority": 70,                               // SCHED_FIFO priority for the audio thread, 0 = off
  "state": { "stateVersion": 2, "params": { ... }, "session": { ... } }   // PluginState: osc, link, sync, songs, offsets ...
}
```

Precedence at start: `state` from the config, then replaced by `stateFile` when it exists (everything set in the web UI
survives a reboot), then the install-time keys `audio.inputs` and `midi.clockOut` are applied on top. To go back to the
config's `state`, delete the state file. Loopback calibration results are stored per device in the user settings
(`$HOME/.config/Pacemaker/settings.json`, `HOME=/var/lib/pacemaker` under systemd) and applied automatically at start.

Gpio wiring: LED anode through 330 ohm to the GPIO line (BCM number), cathode to ground. Beat LED flashes on every
beat, the downbeat LED on beat 1, both timed from the same predicted grid as the other outputs. `chip: "fake"` runs the
logic without hardware (it only counts pulses). OLED: SSD1306 128x64 on I2C (enable `dtparam=i2c_arm=on`, user in the
`i2c` group); shows tempo, state word, bar:beat and a confidence bar. Missing hardware is logged and retried every 5 s.

## Web UI and API

`http://pacemaker.local:8080/` is the standalone's main screen reduced for a phone or laptop: tempo, state word
(LOCKED / HOLD / CHASE / LOCKING / IDLE), confidence, input level meters, outputs (Link peers, MIDI port and jitter, OSC
destination, Sync) with enable toggles and offsets, Follow, Tap, Downbeat now, Relock, Half, Double, Nudge, bar shift,
song list, profile / reference BPM / pattern / meter, and the calibration wizards (loopback, tap-along, hardware offset).
Keyboard: T D R H X F [ ] N P. `http://pacemaker.local:8080/metronome` is the drummer's page: a huge beat flash, pink
downbeat, bar:beat and tempo, kept awake with the Wake Lock API, flash offset and length adjustable and remembered.
Both pages work offline from the binary (assets are compiled in).

Deviation from RS-03: cpp-httplib has no WebSocket, so live status is Server-Sent Events (`GET /api/events`, 20 Hz) with
`GET /api/status` polling as the fallback. The metronome page extrapolates beats locally between updates (it keeps a beat
counter, low-passes arrival jitter and re-synchronises on bar renumbering).

| Endpoint | |
|---|---|
| `GET /api/status` | tempo, state, confidence, bar/beat, next beat (`next.inMs`), inputs, outputs, params, song |
| `GET /api/events` | SSE, event `status` carries the same document at about 20 Hz (max 8 clients) |
| `POST /api/action/<name>` | `follow` (toggle, or `?on=0/1`), `tap`, `downbeatNow`, `relock`, `half`, `double`, `nudgeMinus`, `nudgePlus`, `nudge?ms=N`, `shiftMinus`, `shiftPlus`, `nextSong`, `prevSong`, `learn` |
| `GET` / `PUT /api/state` | the full `PluginState` JSON (tolerant: unknown keys ignored, future versions load what is understood) |
| `POST /api/state/patch` | deep-merge a partial state, e.g. `{"session":{"midi":{"enabled":true}}}` |
| `POST /api/params` | partial `Params` (profile, referenceBpm, pattern, meterNum, meterDen ...) |
| `GET /api/songs`, `PUT /api/songs` | song list; PUT takes JSON or CSV (`Content-Type: text/csv`, AbleSet style columns) |
| `POST /api/song/<i>` | select a song (applies its tempo, meter and pattern) |
| `GET /api/trace` | last 8 s of onset strength at 100 Hz |
| `GET /api/report` | drift report (HTML) |
| `GET /api/tempomap.mid` | tempo map as a standard MIDI file; `GET /api/driftlog.csv` the raw rows; `POST /api/driftlog/start|stop` |
| `GET /api/ports`, `GET /api/info` | MIDI ports, audio device, patterns, roles, actions |
| `GET /api/calibration`, `POST /api/calibration/loopback/start|finish`, `tapalong/start|accept`, `hardware/start|apply` | ES-03 wizards |

Errors are JSON `{"ok":false,"error":"..."}` with 4xx for bad input (never a crash). Set `web.token` to require
`?token=...`, `X-Pacemaker-Token: ...` or `Authorization: Bearer ...` on every `/api/*` request; the pages themselves are
public and prompt for the token (remembered in the browser). The token travels in clear over HTTP, so it keeps casual
users off the box; it is not a substitute for a private network.

## Install on the Pi

```
sudo headless/deploy/install.sh --binary build/headless/pacemaker_headless_artefacts/Release/pacemaker_headless
sudo nano /etc/pacemaker/config.json          # run pacemaker_headless --list-devices for names
sudo systemctl restart pacemaker
journalctl -u pacemaker -f
```

`deploy/` contains `pacemaker.service` (Restart=always, `LimitRTPRIO=95`, `LimitMEMLOCK`, SIGTERM handling),
`99-pacemaker-limits.conf` (for `/etc/security/limits.d`, interactive runs), `pacemaker.avahi.service` (advertises
`_http._tcp` on 8080; the host name `pacemaker` gives `pacemaker.local`), `install.sh`, `overlay-root.sh` and
`setup-hotspot.sh`.

**Read-only root.** `overlay-root.sh --data-device /dev/mmcblk0p3 --boot-ro` mounts a small ext4 data partition at
`/var/lib/pacemaker` (settings, calibration, drift logs), enables raspi-config's overlay file system and makes
`/boot/firmware` read-only. Everything else, including `/var/log`, lives in RAM and is gone after a power cut, which is the
point. Use `--disable` to update software, then enable again.

**Wi-Fi hotspot** (Bookworm, NetworkManager): `sudo headless/deploy/setup-hotspot.sh` creates the access point
`Pacemaker-XXXX` on `wlan0` with `ipv4.method shared` (DHCP and DNS from NetworkManager, the Pi is `10.42.0.1`), WPA2 with
a generated passphrase saved in `/var/lib/pacemaker/wifi-passphrase.txt`. Phones open `http://10.42.0.1:8080/metronome`.
The Ethernet port stays on the stage network for Ableton Link and OSC; Link peers are found on the wired interface.
On older images without NetworkManager use `hostapd` + `dnsmasq` with the same addresses. Pick a quiet 5 GHz channel
(`--band a --channel 36`) on gigs where 2.4 GHz is crowded; the Pi's radio is a convenience, not a lighting link.

**Pisound HAT (DIN MIDI and audio).** Install Blokas' driver (`curl https://blokas.io/pisound/install.sh | sh`, or from
their apt repository), add `dtoverlay=pisound` to `/boot/firmware/config.txt`, reboot. The card shows up as `pisound` in
`--list-devices` (2 in, 2 out) and its DIN ports as ALSA MIDI ports named like `pisound MIDI PS-XXXXXXX`. Put the exact
output name in `midi.clockOut` (DIN OUT drives the drum machine or synth), and the same name substring in `midi.inputs`
if the DIN IN carries a footswitch or e-drum module. The Pisound button can be left alone; it needs nothing from Pacemaker.
Use a class-compliant USB interface for 3 or 4 input channels and the Pisound only for MIDI (set `audio.inputDevice` to
the interface). MIDI clock timing is limited by the ALSA sequencer and the DIN UART (31250 baud, about 1 ms per message);
`outputs.midi.jitterUs` in the status shows the measured spread.

## CPU budget (RS-03: at most 30 percent of one Pi 4 core)

Measure on the target with the same synthetic 3-channel kit, all outputs on (MIDI clock, OSC, Sync, Link):

```
pacemaker_headless --bench 60                       # A: audio thread, faster than real time; B: whole process in real time for 10 s
pacemaker_headless --bench 30 --bench-outputs midi  # what one output costs (none, midi, osc, link, sync)
```

`A` is the audio callback: percent of one core and the worst block against the buffer deadline (block / sample rate).
`B` is the number to compare with the budget: process CPU in real time, including the MIDI clock thread, OSC thread, Link
and housekeeping. On a desktop Xeon at 2.1 GHz (this repository's CI container): A about 2.3 percent, worst block 36 percent
of the 256-sample deadline, B about 12 percent, of which about 5 points are the MIDI clock thread's timing wait. A Pi 4
core is roughly 3 to 5 times slower, so B is likely to land near or above the budget until the clock thread's spin wait
is shortened: run `--bench` on the device and trust that number, not this estimate. For a live check use
`top -H -p $(pidof pacemaker_headless)` (one line per thread) or `pidstat -t -p ... 5`.

## Tests

`PacemakerTests` includes `Tests/WebTests.cpp` (a real server on an ephemeral port, every endpoint over sockets, SSE, token,
bad input, many clients). `pacemaker_headless --selftest` runs without hardware and is what the Pi image build uses as a
smoke test. Not testable off-device: real ALSA devices, DIN MIDI, GPIO LEDs, the SSD1306, the overlay root and the
hotspot scripts; the image build (`headless/image`) has never been run in this repository's CI container.
