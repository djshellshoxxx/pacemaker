# Reaper

## How tempo gets in

Two bundled routes (details and files in `integrations/reaper/README.md`):

1. **OSC pattern config** (`Pacemaker.ReaperOSC`): REAPER's built-in OSC surface maps `/pacemaker/tempo` onto REAPER's
   tempo (`TEMPO f/pacemaker/tempo`, syntax from REAPER's `Default.ReaperOSC`). No dependencies.
2. **ReaScript** (`Pacemaker_TempoFollow.lua`): smoothing, deadband, confidence gate, run-to-toggle. Needs a UDP socket
   library because ReaScript has none built in: ReaPack package "Mavriq Lua Batteries" (`require("socket.core")`).

Link is not built into Reaper. MIDI clock slaving of tempo is listed in RS-02 but **unverified**; user reports are mixed.

## Setup

1. Follow `integrations/reaper/README.md` method A or B.
2. Insert Pacemaker (VST3, CLAP or LV2 on Linux) on the drum bus track.
3. Sidechain: Reaper plugin pins. Route the kick track to the Pacemaker track with a send, destination channels 3/4
   (Send > Audio > Mix to channels 3/4), then in the plugin's Pin Connector wire inputs 3/4 to the plugin's sidechain
   input. The track needs at least 4 channels (Track > Track channels = 4).
4. Pacemaker: Outputs > OSC on, host 127.0.0.1, port 9000. For hardware: MIDI clock to the OS port or via Reaper's
   MIDI out (Preferences > Audio > MIDI Devices: tick "Enable output to this device", "Send clock/SPP to this device"
   is a Reaper-as-master feature; do not enable it, Pacemaker is the master).
5. Remove tempo envelope points/markers in the project (SetCurrentBPM sets the master tempo).

## Known limits

- Tempo only. Phase and downbeat do not move Reaper's timeline.
- Method A applies every tempo message unsmoothed.
- Pacemaker's offline-render behaviour: outputs suppressed during bounce, so a render does not retempo the project.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Nothing happens | `python3 integrations/common/osc_tool.py listen --port 9000` while Pacemaker runs; Reaper's OSC log |
| Script prints "No UDP socket library" | install Mavriq Lua Batteries, or use method A |
| "Cannot listen on port" | Method A and B both on 9000, or another program |
| Tempo wobbles | use the script (smoothing), raise `deadband`, or Pacemaker Stage profile |
