# Pro Tools (2020.9 and later)

## How tempo gets in

Pro Tools has supported Ableton Link since 2020.9 (Sound On Sound, CDM). Link carries tempo and phase, not sample clock.

## The plugin format issue

Pacemaker ships VST3, CLAP, AU and LV2. **Pro Tools loads AAX only**, and AAX is phase 4 (RS-02, needs Avid signing).
Today, Pro Tools users run the **Pacemaker standalone** beside Pro Tools and use Link.

## Setup

1. Pro Tools: enable Ableton Link (a Link control exists in the Pro Tools transport/sync settings; menu names vary by
   release, check the Avid guide for yours). Pro Tools follows the Link session tempo.
2. Start Pacemaker standalone; select the audio interface and map kick/snare/overheads (RS-03).
3. Outputs: Link on. Optional MIDI clock to hardware (OS MIDI port).
4. Allow local network access if the OS asks.

## Known limits

- Link tempo only; Pro Tools tempo map editing/conductor track is overridden while following Link.
- Standalone needs its own inputs: use an interface with enough inputs, or a split from the Pro Tools interface via
  a loopback driver (see ES-04 section 5).

## Troubleshooting

| Symptom | Fix |
|---|---|
| Pro Tools ignores Link | Link off in Pro Tools; Conductor Track on; Link peer count 0 |
