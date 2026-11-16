# Host guides

One page per host (RS-02 section 7). Matrix first, then the pages. UI names for Pacemaker follow RS-01 and ES-02;
the plugin UI is not built yet, so treat Pacemaker-side labels as the target design. Host-side facts were checked
against vendor documentation where reachable; items marked **(unverified)** were not.

| Host | Tempo into the host | Clock out to hardware | Sidechain | Page |
|---|---|---|---|---|
| Ableton Live 11/12 | Link | OS MIDI port (virtual or real) | 1 (Audio From) | [live.md](live.md) |
| Logic Pro 10.7.5+ | Link | OS MIDI port | 1 (Side Chain menu) | [logic.md](logic.md) |
| MainStage | Link in MainStage 4 (RS-02 says unsupported: see note); or MIDI clock in | OS MIDI | 1 | [mainstage.md](mainstage.md) |
| Reaper | OSC pattern config or ReaScript (bundled); MIDI clock slave **(unverified)** | OS MIDI or Reaper MIDI out | many (channel pins) | [reaper.md](reaper.md) |
| Bitwig Studio | Link (since 2.2), or bundled controller script (OSC) | OS MIDI | multiple | [bitwig.md](bitwig.md) |
| Cubase / Nuendo | none live (does not slave to MIDI clock) | OS MIDI | 1 | [cubase-nuendo.md](cubase-nuendo.md) |
| Studio One | **(unverified)**: reports say MTC, not MIDI clock | OS MIDI | 1 | [studio-one.md](studio-one.md) |
| Pro Tools 2020.9+ | Link (host side); plugin is VST3 so use standalone until AAX | OS MIDI | n/a | [pro-tools.md](pro-tools.md) |
| Reason | Link | OS MIDI | n/a (use standalone) | [reason.md](reason.md) |
| QLab | OSC cue triggers (Link: check version) | n/a | n/a | [qlab.md](qlab.md) |
| Resolume, VDMX, TouchDesigner | Link or OSC | n/a | n/a | [resolume-vdmx-touchdesigner.md](resolume-vdmx-touchdesigner.md) |

## Discrepancies with RS-02 found while verifying (for the spec owner)

1. **MainStage**: RS-02 says Link is not supported. Apple's MainStage 4 user guide documents getting tempo from Ableton
   Link (Concert Settings > Timing > Tempo). MIDI clock input ("Get tempo from MIDI input") is documented for MainStage
   3 as well. Pages assume both exist and tell the user to check their version.
2. **Live 12**: RS-02 says Live greys Link out while its follower is on. That is the Live 11 manual. The Live 12 manual
   says Link may run alongside Tempo Follower, with Live feeding the follower's tempo into the Link session. Either way
   the advice is the same: switch Tempo Follower off so only Pacemaker drives tempo.
3. **Studio One**: RS-02 says "MIDI clock slave". Search results suggest Studio One follows MTC, not MIDI clock.
   Unverified; page treats Studio One as clock-out until confirmed on a licence copy.
4. **Reaper**: MIDI clock slave is likewise unconfirmed; the bundled OSC route is the documented path.
5. **Pro Tools**: plugin formats do not include AAX until phase 4, so a VST3 cannot load in Pro Tools; use the
   standalone with Link.

## Common setup in every page

- Pacemaker outputs: enable only what the rig needs. Link and OSC share the network; MIDI clock goes to an OS port.
- Plugin sidechain baseline (RS-02 section 3): Main = drum bus or overheads, Trigger = kick mic.
- Pacemaker does not delay audio (`getLatencySamples() = 0`).
- Windows MIDI: Windows cannot create virtual MIDI ports from a plugin. Install loopMIDI (Tobias Erichsen, free),
  create a port named `Pacemaker Clock`, then select it in Pacemaker and in the receiving program.
- Test without a drummer: `python3 integrations/common/osc_tool.py simulate` sends the OSC stream.
