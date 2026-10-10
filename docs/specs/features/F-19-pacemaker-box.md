# F-19: Pacemaker Box (Raspberry Pi appliance)

Status: draft 1.0 · Priority: P3 (v1.2) · Estimate: 30 pd · Depends on: F-01, F-05, server · Related: RS-03 section 2, F-11, F-14

## 1. Goal and user value
No laptop on stage: a small box with audio inputs and DIN MIDI out that boots in seconds, follows the drummer, and is configured from a phone.
## 2. Requirements
- R-1 Images for Raspberry Pi 5 and 4 (64-bit Raspberry Pi OS Lite base), read-only root with overlay for config/logs, boots to ready in under 25 s, survives power cuts (fsck-free).
- R-2 `pacemaker_headless` (the server without desktop dependencies) as a systemd service with restart on failure, watchdog, RT priority limits, CPU governor performance, IRQ affinity notes.
- R-3 Audio: ALSA direct, class-compliant USB interfaces and Pisound; buffer 128 or 256; auto-detects the last used device; channel map in config.
- R-4 MIDI out: Pisound DIN or USB MIDI via ALSA sequencer (F-05), Link and OSC over Ethernet; jitter targets of F-05.
- R-5 Network: Ethernet DHCP with fallback link-local, optional Wi-Fi hotspot "Pacemaker-XXXX" for the config page, mDNS `pacemaker.local`, web UI on port 8080 with the stage page (F-11) and PIN.
- R-6 Status hardware: 2 GPIO LEDs (beat, downbeat), optional SSD1306 OLED showing tempo, state and confidence, physical buttons for Follow and Tap (GPIO).
- R-7 Config: `/etc/pacemaker/config.json` (same schema as settings), backup/restore from the UI, factory reset button.
- R-8 Updates: signed update packages applied from the UI (A/B partition or atomic overlay swap), rollback on failed boot.
- R-9 CPU budget: engine plus outputs at most 30 percent of one core on Pi 4 (RS-03), music analysis optional and off by default.
- R-10 Logging and diagnostics bundle (F-14) accessible by the UI; no SSH enabled by default, a documented opt-in.
- R-11 Image build is reproducible in CI (pi-gen or Yocto-lite), with a smoke test in QEMU (boot, service up, UI reachable).
- R-12 Field test: 4-hour continuous session with a hardware-only act: no dropouts, no clock gaps.
## 3. Design
Cross-compile target `linux-arm64`; `pacemaker_headless` links no GUI libs; GPIO via libgpiod; OLED over I2C; systemd units (`pacemaker.service`, `pacemaker-hotspot.service`); overlayfs mount script; update agent (RAUC-like or simple A/B). The web UI is the existing one with Box-specific panels (device, network, update, hardware).
## 4. Tests
F19_R1 boot time on hardware; F19_R9 CPU on Pi 4 with the benchmark; F19_R11 QEMU smoke in CI; F19_R8 update and rollback with power-cut injection (smart plug rig); F19_R4 jitter probe on the DIN line (logic analyser rig); F19_R12 soak.
## 5. Plan
| Task | pd |
|---|---|
| Headless target and cross-build in CI | 3 |
| Image build pipeline and read-only overlay | 5 |
| Network (hotspot, mDNS), config UI panels | 4 |
| GPIO LEDs, OLED, buttons | 3 |
| Update system with rollback | 5 |
| Power-cut and soak tests, jitter measurements | 4 |
| Docs, support bundle, product page content | 2 |
| Field test and fixes | 4 |
Risks: component supply (Pisound/Pi availability), SD card corruption (read-only, industrial cards), audio interface quirks (tested list), support burden (diagnostics bundle).
