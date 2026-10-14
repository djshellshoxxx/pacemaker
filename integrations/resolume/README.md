# Pacemaker and Resolume (also VDMX, TouchDesigner pointer)

Pacemaker's OSC output has a Resolume profile (ES-02 section 4, implemented in `services/src/Osc.cpp`):

| Message | Argument | When |
|---|---|---|
| `/composition/tempocontroller/tempo` | float, `(bpm - 20) / 480` clamped to 0..1 | on tempo change and every second |
| `/composition/tempocontroller/resync` | int 1 | on every downbeat |

Generic `/pacemaker/*` messages are also sent unless "also generic" is switched off.

## Setup

1. Resolume: Preferences > OSC. Enable OSC input. Default input port is 7000 (community and forum reports; read
   it from your Preferences).
2. Pacemaker: Outputs > OSC: host `127.0.0.1` (or the Resolume machine), port = Resolume's input port, profile
   Resolume.
3. Start Pacemaker following; the composition BPM should move. Press the tap/resync-capable parts of the
   composition as you would normally.

## Not verified (read before the gig)

- The normalised range assumed by the profile (20 to 500 BPM mapped to 0..1) is from ES-02/`Osc.cpp`. A Resolume forum
  thread suggests tempo OSC behaves oddly when sent directly in some versions. Use Resolume's OSC monitor
  (Preferences > OSC, or Shortcuts > OSC mapping) and `python3 ../common/osc_tool.py simulate --port 7000` to see what
  your version does, and report the mapping if it differs.
- Resolume Link: Resolume lists Ableton Link support in recent versions (not verified here); Link from Pacemaker is
  the alternative to OSC tempo.

## VDMX

VDMX has an OSC setup in its Preferences (OSC Setup tab, input port configurable). Use the Generic profile and map
`/pacemaker/tempo`, `/pacemaker/beat` in VDMX's OSC setup. Details not verified here.

## TouchDesigner

See `../touchdesigner/`.
