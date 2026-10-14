# Pacemaker and QLab

QLab is a cue player; it does not take a tempo from OSC. What Pacemaker adds for QLab:

1. **Start cues on the drummer's count-in** (Pacemaker's count-in launcher sends an OSC message on bar 1).
2. **Tempo via Link** for anything in the rig that runs on Link (per RS-02; QLab's own Link support was not
   verified here, check your QLab version's Settings).

## Verified

- QLab receives OSC on UDP port 53000 (community-reported by several users, e.g. ETC Eos forum guides; confirm in
  QLab Workspace Settings > Network on your version).
- QLab OSC cue control addresses used below: `/go` and `/cue/<number>/start`
  (`/cue/2/start` is quoted in the ETC guide; `/go` is the standard QLab OSC go command).
  Check the OSC dictionary on qlab.app for your version before show day.
- Enable OSC access in QLab Workspace Settings > Network (the "Use OSC controls" style option; label differs by version).

## Setup

1. QLab: Workspace Settings > Network: allow OSC control. Note the machine's IP (or use `127.0.0.1` when
   Pacemaker runs on the same computer).
2. Pacemaker: Outputs > OSC: host = QLab machine, port = `53000`, profile Generic. The continuous messages
   (`/pacemaker/tempo`, `/phase`, ...) are not QLab addresses and QLab ignores them; if you want a cleaner log
   disable "also send generic" or send OSC to a second instance used only for cues.
3. Pacemaker: Count-in launcher: enable, OSC address `/cue/COUNTIN_NUMBER/start` (or `/go`), where the cue number
   is the Group or Start cue that begins the song's backing material.
4. Test without a band: `python3 ../common/osc_tool.py send --port 53000 /cue/1/start`.

See `cues-example.txt` for message examples.

## Limits

- No tempo-following of audio cues from OSC: QLab audio does not time-stretch to an external tempo in real time.
  Use Link-capable players, or trigger tempo-matched stems.
- A UDP message can be lost on busy Wi-Fi. Use wired Ethernet.
