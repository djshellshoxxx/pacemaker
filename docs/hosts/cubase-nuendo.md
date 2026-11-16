# Cubase / Nuendo

## How tempo gets in

Not live. Cubase does not follow incoming MIDI clock (forum and Steinberg threads agree it syncs to MTC, not clock; not
confirmed with a current official article), and has no Link. Pacemaker therefore acts as **clock out only** for
Cubase rigs: it drives hardware and lights while Cubase plays its own tempo. Use Cubase to *record* the band with the
tempo map export (Pacemaker's "Export tempo map" writes a MIDI tempo track you can import after the show).

## Setup

1. Windows: install loopMIDI, create a port `Pacemaker Clock`. macOS/Linux: Pacemaker creates a virtual
   port "Pacemaker Clock".
2. Pacemaker (standalone is simplest) -> MIDI clock out -> chosen port or your hardware's port.
3. If you load the plugin in Cubase: VST3 on the drum bus. Sidechain: enable the sidechain input in the plugin
   (Activate Side-Chain button) and add a Send on the kick track targeting the plugin's side-chain. One sidechain
   input; Main = drum bus, Trigger = kick.
4. Do **not** also enable Cubase's MIDI Clock Destinations (Transport > Project Synchronization Setup) to the same
   hardware; two clock sources on one device make it hunt.

## Known limits

- No tempo into Cubase while playing live; Pacemaker's tempo-map export is the post-production route.
- VST3 cannot carry MIDI clock bytes (0xF8) through the host, which is why Pacemaker sends clock by an OS MIDI port.
- Steinberg forum reports of clock jitter in Cubase 14 concern Cubase's own clock output; they do not affect
  Pacemaker's.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Hardware receives no clock | selected the wrong port; Windows: loopMIDI not running; box set to "external clock" |
| Plugin shows no trigger | sidechain not activated or Send on kick not post-fader as intended |
