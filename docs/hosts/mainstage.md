# MainStage

## How tempo gets in

Two paths documented by Apple (check your version):

- **MIDI clock in**: Concert Settings > Timing: enable "Get tempo from MIDI input" and choose the device. MainStage
  then follows incoming clock; the Tempo slider is dimmed. If clock stops, MainStage keeps the last tempo until you
  change patch or set a tempo (Apple, MainStage 3 guide).
- **Ableton Link**: documented in MainStage 4 (Concert Settings Inspector > Timing > Tempo > Ableton Link;
  allow network discovery). RS-02 lists Link as unsupported; this conflicts, so confirm on your version.

## Setup (MIDI clock)

1. macOS: in Pacemaker, MIDI clock output, port = the virtual port "Pacemaker Clock" (Pacemaker creates it) or an
   IAC Driver bus (Audio MIDI Setup > MIDI Studio > IAC Driver > Device is online).
2. MainStage: Concert Settings > Timing > Get tempo from MIDI input, device = that port.
3. Insert Pacemaker (AU) in a channel strip carrying the drum bus/overheads; Side Chain menu for the kick.
   MainStage allows one sidechain input per plugin.
4. Enable the hardware clock output too if needed (second port, or route the real port).

## Known limits

- Clock-only tempo: no beat phase or bar position reach MainStage. Start/Stop message use depends on "Pacemaker
  sends Start/Stop" and MainStage's transport handling (unverified).
- Patch changes can reset tempo to the patch's stored tempo; keep tempo "from MIDI" on at concert level.
- Arpeggiators and delays stepping on tempo change: reported anecdotally to glitch during rapid tempo moves; raise
  Pacemaker's tempo deadband in Rehearsal vs Stage profile if needed.

## Troubleshooting

| Symptom | Fix |
|---|---|
| MainStage ignores clock | wrong device chosen; clock is only sent while Follow is on and Locked |
| Tempo slider moves but audio stays | tracks are not tempo-synced; use tempo-synced patches |
