# RS-03: Standalone app and Pacemaker Box appliance (regular spec)

Status: draft 0.1. Research: `docs/research/04-plugin-architecture-and-latency.md` section 6.

## 1. Standalone desktop app

- Own audio device selection with per-channel role mapping (ES-04
  section 5). Supports ASIO, WASAPI, CoreAudio, ALSA, JACK.
- Any number of MIDI inputs (e-drum modules, footswitches) and one MIDI
  clock output (plus mirrored outputs as a later feature).
- Runs as a normal window; optional "always on top" and "stage mode".
- Auto-start option on login; remembers last devices and calibration.
- Logs to a rotating file; "Copy diagnostics" button for support.

## 2. Pacemaker Box

A headless build for Raspberry Pi 4 or 5 (64-bit Raspberry Pi OS Lite).

**Hardware reference design**

| Part | Choice | Notes |
|---|---|---|
| Computer | Pi 5 4 GB (Pi 4 supported) | Pi 5 is 2 to 3 times faster; both fine for the engine |
| Audio in | class-compliant USB interface, 2 to 4 inputs | kick, snare, overhead |
| MIDI out | Pisound HAT (DIN in/out plus audio) or USB MIDI interface | Pisound gives both audio and DIN |
| Network | onboard Ethernet to the stage switch for Link; Wi-Fi hotspot for the config page | |
| Status | 2 LEDs on GPIO (beat, downbeat) and an optional SSD1306 OLED showing tempo and state | |
| Power | official PSU; read-only root filesystem with overlayfs so power cuts do not corrupt | |

**Software**

- `pacemaker_headless` as a systemd service, restarts on failure.
- Config in `/etc/pacemaker/config.json`; live settings through an
  embedded web UI (cpp-httplib plus a WebSocket for status) on port 8080,
  advertised via avahi as `pacemaker.local`.
- The web page is the standalone's main screen reduced to tempo, state,
  confidence, inputs levels, outputs, Follow, Tap, Downbeat now, Relock
  and the calibration wizard.
- ALSA direct; buffer 128 or 256 frames; RT priority via `rtprio` limits.
- CPU budget: engine plus outputs at most 30 percent of one core on Pi 4.

**Out of scope for v1 of the Box**: USB gadget mode (the Pi appearing as a
USB MIDI device), battery power, custom enclosure. Documented as later
options.

## 3. Shared behaviour

Both builds use the same engine, outputs and calibration code. The only
differences are the UI shell and the device layer.
