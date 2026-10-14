# Studio One

## How tempo gets in

**Unverified.** RS-02 states Studio One can slave to MIDI clock. Searches for current PreSonus documentation found
how Studio One *sends* MIDI clock (Options > External Devices, add an instrument/clock device, tick "Send MIDI Clock")
but nothing confirming it can *follow* incoming clock; one user report says it follows MTC only. Until a licensed copy
is tested, treat Studio One as **clock out only**, like Cubase.

If a test shows Studio One can follow clock: Song Setup / external sync settings (location unverified) with the input
set to the virtual port `Pacemaker Clock` that Pacemaker publishes (loopMIDI on Windows).

## Setup (clock out)

1. Windows: loopMIDI port `Pacemaker Clock`. macOS/Linux: Pacemaker creates it.
2. Pacemaker MIDI clock out -> hardware port or virtual port.
3. Plugin: VST3 on the drum bus. Sidechain: Studio One uses the plugin's side-chain button and a send on the
   source track pointing at the plugin's side-chain input; one input, so Main = drum bus, Trigger = kick.
4. Do not enable Studio One's own "Send MIDI Clock" to the same device.

## Known limits

- No live tempo into Studio One until verified. Use the standalone for tempo-critical live rigs.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Clock reaches nothing | port not selected in Pacemaker; loopMIDI not running |
