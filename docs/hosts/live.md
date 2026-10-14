# Ableton Live 11 / 12

## How tempo gets in

Link. Pacemaker joins the Link session on the network (same computer is fine). Live follows Link tempo and phase.

## Setup

1. Live Settings > Link, Tempo & MIDI (Live 12: tab "Link"): turn on Link. If the Link button is not in the control
   bar, enable "Show Link Toggle" there. Optional: Start Stop Sync on, if you want transport shared.
2. **Turn Tempo Follower off.** Live 12: enable "Show Tempo Follower Toggle" so you can see it, and make sure it is off.
   Live 11's manual says Link, Tempo Follower and External Sync are mutually exclusive (Link is disabled while
   Tempo Follower is on). Live 12's manual allows both together but then Live pushes the follower's tempo into the
   Link session, which fights Pacemaker. Only one tempo source should be active.
3. Add Pacemaker (VST3 or AU) to an audio track that carries the drum bus or overheads.
4. Sidechain: in the plugin device header, open the sidechain section and set **Audio From** to the kick-mic track
   (post-FX or pre-FX as you prefer; use "Pre FX" if the track is gated or compressed). Live offers one sidechain input,
   so: Main = drum bus/overheads, Trigger = kick.
5. Pacemaker: Outputs > Link on (quantum 4 or your meter), publish tempo on, publish phase on. Leave Stage profile.
6. Set Live's metronome off. Tracks with warped audio now follow the band; set clips to Warp.

## Outputs to enable

- Link: yes. MIDI clock: only for hardware; select the real MIDI output port directly in Pacemaker (do not route
  clock through Live's MIDI sync, Live does not forward Pacemaker's clock). OSC: for lights/video. Sync bus: only
  with a hardware clock box.

## Known limits

- One sidechain input, so only two roles. Use the standalone for four mics (Kick, Snare, Overhead L/R).
- Link has no master: another peer changing tempo by more than 2 BPM twice in 10 s triggers Pacemaker's "Another app
  is setting tempo" warning. Check Tempo Follower, DJ apps, iPad apps.
- Link start/stop sync, if on, starts Live's transport when Pacemaker starts; leave off unless you want that.
- Pacemaker never calls forceBeatAtTime except on Relock, so Live's beat grid may sit an offset from the drummer's
  downbeat until you press Downbeat now or Relock in Pacemaker.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Live tempo does not move | Link off in Live; or Tempo Follower on; or Pacemaker Follow is off/HOLD; check the Link peer count in Pacemaker's Outputs strip |
| Tempo jumps by 2x or 0.5x | set reference BPM and meter in Pacemaker (song map) |
| Link peers = 0 | firewall; same network; macOS "Local network" permission |
| Plugin sees no sidechain | Audio From not set; track monitoring/arm state; re-add plugin |
