# Resolume, VDMX, TouchDesigner

Files and exact messages: `integrations/resolume/`, `integrations/touchdesigner/`, `integrations/lighting-magicq/`.

- **Resolume**: Pacemaker's Resolume OSC profile sends `/composition/tempocontroller/tempo` (normalised
  `(bpm-20)/480`) and `/composition/tempocontroller/resync` on each downbeat. Resolume's OSC input port defaults to
  7000 (forum/vendor-module reports). The normalisation is from ES-02 and **unverified in Resolume's own docs**: confirm
  in Resolume's OSC monitor. Link is the alternative.
- **VDMX**: use the Generic profile and map `/pacemaker/tempo`, `/pacemaker/beat` in VDMX's OSC setup
  (not verified here).
- **TouchDesigner**: OSC In DAT with `pacemaker_oscin_callbacks.py` (port 9000).
- **Lighting consoles**: MagicQ OSC setup and limits in `integrations/lighting-magicq/README.md`; Link is the
  preferred path in the stage test.

Pacemaker setup: Outputs > OSC on, host/port of the receiver, profile Resolume or Generic. Link on if the app supports it.

## Troubleshooting

`python3 integrations/common/osc_tool.py listen --port <port>` shows exactly what Pacemaker sends (run it on the receiver
machine to test the network). `simulate` sends the same stream without a drummer.
