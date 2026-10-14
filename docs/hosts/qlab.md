# QLab

QLab needs cue triggers more than a tempo. The OSC contract and examples are in `integrations/qlab/README.md` and
`integrations/qlab/cues-example.txt`.

## Setup

1. QLab Workspace Settings > Network: allow OSC control. QLab listens on UDP 53000 (community-reported, confirm in
   your version).
2. Pacemaker: Outputs > OSC: host = QLab machine, port 53000. Count-in launcher: enable, OSC address
   `/cue/<number>/start` of the cue that begins the song's backing material (or `/go`).
3. Link: RS-02 lists "Link or OSC" for QLab; Link support in QLab was not verified. If your QLab version supports Link
   it appears in its settings, and Pacemaker's Link output would drive it.

## Known limits

- QLab does not time-stretch audio cues to an external tempo in real time. Trigger tempo-matched stems or use a
  Link-capable player for tempo-following material.
- Wired Ethernet recommended; OSC is UDP.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Cue does not fire | OSC access not enabled; wrong cue number; test with `osc_tool.py send --port 53000 /cue/1/start` |
| QLab log shows unknown addresses | expected; QLab ignores `/pacemaker/*`, only the launcher address matters |
