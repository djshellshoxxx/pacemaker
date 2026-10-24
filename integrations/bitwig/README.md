# Pacemaker for Bitwig Studio

`Pacemaker.control.js` is a controller script that receives Pacemaker's OSC output and sets Bitwig's transport
tempo (smoothing alpha 0.35, deadband 0.05 BPM, confidence gate 0.35, ignores HOLD/IDLE).

## Install

1. Copy `Pacemaker.control.js` into Bitwig's user controller-scripts folder, in a subfolder named `Pacemaker`:
   - Windows: `%USERPROFILE%\Documents\Bitwig Studio\Controller Scripts\Pacemaker\`
   - macOS: `~/Documents/Bitwig Studio/Controller Scripts/Pacemaker/`
   - Linux: `~/Bitwig Studio/Controller Scripts/Pacemaker/`
   (Settings > Locations shows the actual path on your machine.)
2. Bitwig: Settings > Controllers > Add controller. Hardware vendor `Pacemaker`, product `OSC Tempo Follow`. No MIDI
   ports are needed.
3. Open the controller's settings (gear icon): "Follow Pacemaker tempo" On/Off, "OSC listen port" (default 9000),
   "OSC root" (default `/pacemaker`). Changing port or root restarts the script (Bitwig requires OSC servers to be
   created at start-up).
4. Pacemaker: Outputs > OSC, enable, host `127.0.0.1`, port as above, profile Generic.
5. Check Bitwig's Controller Script Console (Dashboard > Settings > Controllers, or the Help menu's console entry)
   for `Pacemaker: listening on UDP 9000`.

## Verified vs not verified

Verified against a published working Bitwig OSC controller script (Dewb/bitwig-serialosc-example, `loadAPI(16)`):
`host.getOscModule()`, `createAddressSpace()`, `registerMethod(addr, ",f", desc, function(source, message))`,
`message.getArguments()`, `createUdpServer(port, space)`, preferences string settings, `host.restart()`.

Not run inside Bitwig here: `host.createTransport().tempo()` and the `setRaw(bpm)` setter on the tempo Parameter
(the script falls back to `value().setRaw`). Bitwig's tempo range is 20 to 666 BPM; if setting fails the console shows
`Pacemaker: cannot set tempo`. Report it, the fix is one line in `setTempo`.

## Alternative without a script

Bitwig supports Ableton Link (Settings > Synchronization). Pacemaker's Link output works in Bitwig without this
script; use the script only if you want Bitwig's tempo driven without joining a Link session. See
`docs/hosts/bitwig.md`.
