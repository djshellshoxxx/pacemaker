# Reason

## How tempo gets in

Link. Reason selects it as Sync Mode > Ableton Link (Transport panel, or Options > Sync). Reason's sequencer waits for the
Link phase before starting; tempo changes in the Reason sequencer propagate to all peers, so lock tempo editing while
Pacemaker is publishing. (Reason 12 documentation, Sound On Sound.)

## Setup

1. Reason: Sync Mode = Ableton Link; the Transport shows the number of peers.
2. Run Pacemaker **standalone** with Link output on. (Reason hosts VST plugins in recent versions; for this
   use case the standalone avoids routing sidechain audio inside a rack, **unverified**.)
3. Optional: MIDI clock to hardware via the OS MIDI port; Windows needs loopMIDI for software receivers.

## Known limits

- Tempo and phase only.
- Reason's tempo track automation must be off/flat.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Peers 0 | same network, firewall, local-network permission |
| Reason tempo jumps when you click | you edited tempo in Reason: Link peers follow the last change |
