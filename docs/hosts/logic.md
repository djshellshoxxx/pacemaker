# Logic Pro (10.7.5 and later)

## How tempo gets in

Ableton Link, supported since Logic Pro 10.7.5 (Apple's 10.7.5 notes). Logic cannot slave to MIDI clock for tempo, so
Link is the only live tempo path.

## Setup

1. Project Settings > Synchronization (Logic Pro for iPad calls the control "Sync Mode: Ableton Link"; the Mac menu
   wording and path differ by version, **verify in your build**) and enable Link. Logic shows Link peers in the control bar.
2. With Link on, Logic's tempo track is overridden by the Link tempo (Apple's iPad guide: Link turns the Tempo track
   off). Delete or ignore tempo automation for the session.
3. Insert Pacemaker as an **Audio Units > Audio FX** plugin on the drum bus or overhead track.
4. Sidechain: in the plugin window header, open the **Side Chain** menu and pick the kick track (input). One side chain
   input per plugin: Main = drum bus, Trigger = kick.
5. macOS: allow Local Network access for Logic and Pacemaker (System Settings > Privacy & Security) or Link peers will
   not see each other.
6. Pacemaker: Outputs > Link on. For hardware, enable MIDI clock and choose the real output port or the
   virtual "Pacemaker Clock" port. Logic is not involved in the hardware clock path.

## Syncing hardware

Pacemaker sends MIDI clock directly to a MIDI port, independent of Logic. If a hardware box needs audio-rate sync,
route Pacemaker's Sync bus (stereo output) to an audio interface output feeding the box (a multiclock-type device);
see ES-02 section 5.

## Known limits

- Logic follows Link tempo but quantises startup to the Link phase; do not press Play on a count-in expecting it to
  start immediately.
- Smart Tempo and Flex Time overrides fight Link; set Smart Tempo to Keep.
- AU validation: pluginval/auval status is tracked in RS-05; run Logic's plugin manager scan after install.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Plugin missing | Logic Settings > Plug-in Manager > Reset & Rescan Selection |
| Tempo does not follow | Link not enabled in Project Settings; Link peers 0 (permissions) |
| No input from kick | Side Chain input not set, or kick track is muted/solo-excluded |
