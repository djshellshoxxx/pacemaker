# Bitwig Studio

## How tempo gets in

- **Link**: Bitwig has supported Ableton Link since version 2.2 (2017; start/stop sync arrived with Link 3). Enable it in
  Bitwig's synchronisation settings (exact menu path: check your version). Pacemaker's Link output then drives the tempo.
- **Controller script**: `integrations/bitwig/Pacemaker.control.js` receives `/pacemaker/tempo` by OSC and sets the
  transport tempo, without joining a Link session. Install steps in `integrations/bitwig/README.md`.

Use one or the other, not both.

## Setup

1. Choose Link or the script. For the script: copy it to `Documents/Bitwig Studio/Controller Scripts/Pacemaker/`, add
   it under Settings > Controllers (vendor Pacemaker, product OSC Tempo Follow), check the console for
   `Pacemaker: listening on UDP 9000`.
2. Insert Pacemaker (VST3 or CLAP) as an audio effect on the drum bus.
3. Sidechain: Bitwig's VST3/CLAP sidechain input appears in the device header (Sidechain selector in the device's
   input section **(unverified wording)**); pick the kick track. Bitwig allows multiple sidechain inputs on VST3
   plugins that declare them.
4. Pacemaker: Outputs > Link on, or OSC on (host 127.0.0.1, port 9000). MIDI clock to an OS port for hardware.

## Known limits

- Script sets tempo only (no phase). The `transport.tempo()` setter was not run inside Bitwig by us (see README).
- OSC servers are created at script start, so changing port/root in the script settings restarts the script.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Controller shows but tempo static | Follow off in the script setting; Pacemaker in HOLD (script ignores HOLD/IDLE) |
| "cannot open UDP port" | port in use; change in both places |
| Link peers 0 | firewall; same network |
