# Research 03: Competitors, prior art and user needs

Status: research notes, October 2026. Inputs to `docs/specs/` and to
`docs/DIFFERENTIATION.md`. The research proxy blocked direct reads of
ableton.com, forum.ableton.com, gearspace, KVR, Steinberg forums, Reddit,
cycling74 and several vendor sites, so most forum content is **paraphrased
from search extracts of the threads** (URLs given). Prices are the latest
figures that surfaced; verify before quoting. Items marked *(uncertain)* have
a single source.

---

## 1. Existing products and prior art

### 1.1 Ableton Live built-in Tempo Follower (Live 11.1+, Live 12, Push 3)

- **What it is.** Analyses one external audio input in real time and adjusts
  the Set tempo to follow "fluctuating tempo changes from musicians or any
  other rhythmic sounds". Configured in Settings > Tempo & MIDI > Tempo
  Follower: an **Input Channel** selector (external audio inputs only; you
  cannot feed it from an internal track or from MIDI) and a "Show Tempo
  Follower Toggle" that puts a **Follow** button in the Control Bar. The
  current Set tempo is the **reference tempo**; the follower tracks deviations
  around it and is explicitly "not built to follow big tempo jumps". It is
  latency-compensated; lower audio buffer size means faster response.
- **Hard limits.** *Link, Tempo Follower and External Sync are mutually
  exclusive* in desktop Live (Link and Ext grey out when Follow is on), a
  frequently raised complaint. Audio input only (no MIDI or trigger input).
  Tracks **BPM only; no time signature, no downbeat or bar position**; users
  set the meter manually and the follower does not know where "one" is. Live
  12 release notes added a 10 BPM minimum, CPU savings when the toggle is
  hidden, and on **Push 3 standalone** Tempo Follower can run alongside Link
  Audio. That combination is still not available in desktop Live *(uncertain)*.
- **Reported failure modes (Ableton forum, 2022 to 2025).**
  - "Tempo Follower Problems" (t=244230): overhead mic too quiet when the
    drummer plays only hi-hat; hi-hat mic too quiet otherwise; "very erratic
    results" when asked to follow the full kit.
  - "Tempo Follower Behaving Erratically" (t=250324): worked for 4/4 and 5/4
    songs in a Live 12 backing-track rig, but a 6/8 song "dramatically
    adjusts tempo, shifting down then up in cycles without ever stabilising".
    Reply: the algorithm is "optimized for audio signals with a clear rhythm".
  - "Tempo Follow w/ Triggers" (t=246621): piezo drum triggers worked worse
    than a mic.
  - t=246317, t=242200: cannot follow MIDI tracks or MIDI drums.
  - Positive: a two-MacBook Live 11 rig reports tempo-from-drums "works
    great" (t=242269).
- **Cost of entry.** Live Intro $99; Standard about $449; Suite about $749
  (list, verify). Tempo Follower is in all editions; Max for Live only in
  Suite.

### 1.2 BeatSeeker (Max for Live, Andrew Robertson / Ableton, 2015)

- $29 pack; requires Live Standard plus Max for Live (in practice Suite).
  Ableton's pack page states **"this Pack is not compatible with Mac M1
  processors"**. Two modes: *Fixed Tempo* (drummer plays to click) and *Tempo
  Following* (Live follows push and pull). Detects BPM of any rhythmic audio.
- **Complaints**: "detects BPM fine in listening mode, but once in
  tempo-following mode, if the drummer changes tempo slightly nothing happens;
  Live is locked at the first detected tempo and drifts"; instructions "very
  unclear" about the mode switch; MIDI input "arrives but isn't used"; another
  user: despite trying mic placements "the device completely ignores the
  current tempo ... and can swing ±10 bpm" (p=1689781, t=227148). Effectively
  superseded by the built-in follower and unmaintained *(uncertain)*.

### 1.3 B-Keeper (Robertson and Plumbley, QMUL, 2007 to 2013)

- Java plus Max/MSP, Ableton integration via Max for Live. Uses **kick and
  snare mics**, separate tempo process and phase process with adaptable
  parameters, explicitly designed to stay stable through syncopation and
  fills; allows the drummer to drift about ±5 percent while keeping
  pre-programmed parts aligned. Published NIME 2007 and Computer Music
  Journal 2013 ("Synchronizing Sequencing Software to a Live Drummer").
- Reusable ideas for Pacemaker: per-instrument inputs, expectation-window
  gating on kick and snare, independent tempo vs phase correction gains.

### 1.4 Other software

- **InTime** (early 2000s, Windows): tempo tracking from MIDI drums, outputs
  MIDI clock, "Tracking Bias" ms control; the drummer listens to its click or
  loop to close the feedback loop. Defunct *(uncertain)*.
- **HeartSync (Conscious Audio, February 2026)**: not a drum follower; a free
  VST3 beta mapping a BLE heart-rate monitor to DAW tempo in Live, Logic and
  Reaper; paid version in development. Relevant as proof that a third-party
  plugin can push tempo into Logic, Reaper and Live.
- **LiveLink (VST3/AU, AAX coming)**: makes any DAW a Link master, with "drift
  control" and quantum 4. Competitor for the *output* half, not the listening
  half.
- **Reaper**: no live follower. ReaBeat (free, MIT, 2025 to 2026) is
  **offline** neural beat and downbeat detection (beat-this) that writes
  tempo maps.
- **Bitwig**: no follower; community wishlist item "Tempo follower ... adjust
  tempo to an incoming audio or MIDI signal ... for live performance with a
  band"; the workaround chain of Tap Tempo plus Tool plus Replacer plus HW
  Instrument is described as cumbersome.
- **Cubase/Nuendo**: Steinberg rep on "tempo change in project by live
  drummer": "Not in real time". Also "Sync bpm with live drum input" and
  "Live tempo tracker" requests. Tempo Detection is offline and "creates a
  straight line that does not follow drift".
- **Logic/MainStage**: MainStage can take tempo from incoming MIDI clock or
  MIDI notes (tap) but has no audio follower; Logic's Smart Tempo is offline.
- **Theatre**: *Sinfonia / OrchExtra* (Realtime Music Solutions, about 25
  years, "200,000 performances"): keyboard "tap keys" let a musical director
  conduct a virtual orchestra; quote-based pricing tied to the performance
  licence. *Antescofo* (IRCAM) is score following with tempo inference for
  contemporary music. QLab users rely on markers plus a manual GO per section.
- **Academic and open-source building blocks**: Stark/Davies/Plumbley
  real-time beat-synchronous VST effects (NIME 2007) and **BTrack** (C++
  real-time, GPL); IBT (Marsyas); madmom DBNBeatTracker runs on a Raspberry Pi
  3 at about 80 percent of one core; beat-this (2024) is state of the art for
  beat plus downbeat but offline and under a non-commercial licence.

### 1.5 Hardware

- **No shipping drum module or trigger box derives MIDI clock from free
  playing.** Roland SPD-SX PRO and TM-6 PRO: a pad can be assigned as Tap
  Tempo for the internal click and Sync Out can emit F8 clock, but users
  report the clock output is unreliable or limited. Alesis Strike Multipad:
  AbletonDrummer could not get usable clock out. Sunhouse Sensory Percussion:
  rich triggering and MIDI, no documented tempo-following or Link feature.
  Elektron and Pioneer: nothing comparable; Pioneer tempo flows the other way
  (Pro DJ Link to Link via Beat Carabiner).
- Tap-tempo pedals ($16 to $45; MXR M199 $44.95) and BeatBuddy ($449) are the
  hardware "solutions" in practice.
- Apps: Tempi (iOS "reverse metronome"), LiveBPM (listens via mic, visual
  metronome), Drummer's Metronome. Practice tools, no sync output.

---

## 2. User needs and pain points

**Why they dislike clicks.** Recurrent phrases across drumforum.org,
TalkBass, UltimateMetal and Gearspace: "playing to a click makes you robotic",
"takes away a lot of the feel", a drummer of 27 years "never used either,
follows the band leader"; fear of losing the click mid-song; a worship
drummer's in-ear monitors died mid-song (Christianity Today 2024); Ultimate
Ears: most "can't hear the click" problems are isolation problems; a
worship-tech writer notes a "noticeable disdain for the click ... specifically
among worship drummers".

**What they ask for.**

- "Can Ableton Live follow a live band?" (t=145730); "What's the best way to
  keep Live in time with live drums?" (t=170682); "Live listening to drummer
  for tempo changes?" (t=90677).
- "ADVANCE TAP TEMPO: LIVE TO FOLLOW LIVE DRUMMER" (t=205427): using tap
  tempo live, "if I miss a tap or tap slightly early the tempo jumps
  erratically"; the band ended up playing to a click, "disappointing for
  everyone".
- Theatre (Gearspace 2016): 100+ singers and pit instrumentalists "drift from
  the tempo of the backing tracks"; wants playback that follows; replies: "a
  plugin is not the answer, only MIDI", use a groovebox tap tempo as master.
- Yamaha Montage owners: "having the arpeggiator follow the drummer in a live
  situation". Elektronauts: "sync hardware to a drummer's beat". Mod Wiggler:
  "Modular sync with an actual drummer" (multi-page).
- Lighting and visuals people want tempo from the band: Resolume, grandMA (via
  ProDJLink or TimeCode Sync) and StageLight all consume **Ableton Link**; a
  2026 LED dance-floor project specified "Link > MIDI clock > tap tempo"
  sources with **beat phase, bar phase and a downbeat flag that fires exactly
  once per bar** (dance-more issue 125).

**Current workarounds.** Drummer-only click with the band following the
drummer; tap-tempo pedal or pad (SPD-SX pad, Boss FS-5U into a MIDI
controller); MD or keys "conducting" via Sinfonia tap keys or QLab GO per
section with markers; Ableton Nudge ± buttons mapped to a footswitch (often
triggered from QLab via MIDI note); AbleSet or Playback count-ins; or giving up
and playing to click. Worship: the drummer is the "bus driver"; Loop
Community, MultiTracks and Churchfront guides all assume a click bus to IEMs.

**Pattern of needs**, ranked by frequency in threads:

1. Follow small push and pull without jumps.
2. Survive fills, breakdowns, half-time, 6/8 and sparse intros.
3. Know where the **bar and downbeat** are so clips and lights launch on
   "one".
4. Work with **MIDI e-drums and triggers**, not just mics.
5. Keep Link and MIDI clock **output while following**.
6. Work outside Ableton (Logic/MainStage, Cubase, Bitwig, Reaper, hardware).
7. Manual override, nudge and "lock" when the algorithm is unsure.

---

## 3. Market and pricing

| Product | Price | Notes |
|---|---|---|
| AbleSet 3 (Ableton setlist, lyrics, OSC controller) | Intro $129 / Standard $179 / Pro $269; perpetual with paid feature renewals | Configurable count-ins, autoplay, OSC and MIDI control, multi-computer redundancy |
| MultiTracks Playback | Premium from $9.99/mo; Rentals $53.99/mo; One bundle from $134.99/mo | Key and tempo change offline; no live follow |
| Loop Community Prime | app free; Prime about $30/mo; Cloud/Pro $4.99 to $49.99/mo; Looptimus pedal $199 to $229 | |
| iConnectivity PlayAUDIO1U / 2U | $1,199.99 / $2,499.99 | Redundant playback interfaces; touring and worship buyers pay four figures for reliability |
| iQ Pro Show Control (iOS) | $29.99 to $199.99/yr | |
| Sinfonia / OrchExtra | quote per production | theatre pits |
| BeatSeeker | $29 plus Live Suite | the only direct precedent for price anchoring |
| Tap-tempo pedals | $16 to $45; BeatBuddy $449 | |
| Max for Live tempo utilities (AbletonDrummer etc.) | about $10 to $40 on Gumroad | |

Implication: live-performance utilities sell at **$29 to $269 perpetual**
with upgrade fees; worship and touring buyers accept subscriptions and premium
hardware when it de-risks a show. A $79 to $149 Pacemaker Pro with a $29 to
$49 Lite tier, plus a Pi box edition, sits inside observed bands.

**Channels and licensing used by small JUCE devs** (KVR DSP forum, JUCE
forum): Plugin Boutique takes about 40 percent commission but drives the most
traffic and has slow approval; Gumroad about 10 to 15 percent plus processing
and acts as merchant of record for VAT; own site via Paddle, FastSpring or
Lemon Squeezy is the common endgame; a KVR product listing for discovery.
Licensing: iLok "easiest but most expensive, welcomes small devs";
alternatives Keygen, LicenseSpring, Cryptlex, Keyzy; many use a simple key
file plus machine count. **Ableton Link is GPLv2+ or a proprietary licence via
link-devs@ableton.com**; a closed-source Pacemaker must obtain the commercial
licence (free in practice for shipped products, per CDM).

---

## 4. Differentiators and gaps

### What makes Pacemaker different (none of the shipping tools do these)

1. **Bar and downbeat awareness and beat phase**, not just BPM. Ableton's
   follower and BeatSeeker publish tempo only; lighting and VJ integrators are
   already asking for phase plus downbeat flags.
2. **Multi-input fusion**: kick mic plus snare mic plus e-drum MIDI or
   triggers plus an optional tap pedal, weighted by confidence. Directly
   answers the "overhead too quiet on hi-hat" and "triggers don't work"
   threads and the B-Keeper kick/snare lineage.
3. **Confidence gating plus hold/lock plus manual nudge**: freeze tempo
   through fills, breakdowns and 6/8 ambiguity; expose confidence to the
   operator; explicit Lock footswitch. The 6/8 oscillation and ±10 bpm swings
   are the loudest complaints.
4. **Outputs while following**: Link plus MIDI clock/SPP plus host transport
   plus OSC simultaneously. Desktop Live forbids Link while following.
5. **Host-agnostic**: Logic/MainStage, Cubase ("not in real time"), Bitwig
   (wishlist), Reaper (nothing live), QLab (via Link or OSC), hardware
   grooveboxes and modular (MIDI clock, analog clock).
6. **Standalone box**: Raspberry Pi 4/5 appliance emitting Link and DIN MIDI
   clock, no laptop on stage. madmom and BTrack already run on Pi-class CPUs;
   no commercial equivalent exists.
7. **Apple Silicon, Windows and Linux native** (BeatSeeker is officially "not
   compatible with M1").
8. **Lighting and video OSC** (bar, beat, downbeat, section cues) for grandMA,
   Resolume and QLab users who today bridge Link with third-party tools.

### Additional feature candidates, ranked by value divided by effort

| # | Feature | Value | Effort | Note |
|---|---|---|---|---|
| 1 | Reference-tempo "song map" (expected BPM and meter per song, loaded from setlist, AbleSet or OSC) | High | Low | Mirrors Live's reference-tempo approach; kills octave and half-time errors |
| 2 | Confidence meter plus Lock/Free footswitch plus nudge ± | High | Low | Operator trust; replaces the current Nudge workaround |
| 3 | Visual metronome or tempo display for the drummer (LED strip or phone page) | High | Low | Closes the feedback loop (InTime insight) |
| 4 | Count-in detection, auto-launch song or scene on the first downbeat (stick clicks or hi-hat count) | High | Medium | AbleSet and Playback do configurable count-ins but from a fixed click |
| 5 | Tempo-map or Link-session recording export (MIDI tempo track, Reaper, Logic, Cubase tempo map) for post-production | Medium-High | Low | No live tool does this; Cubase and Reaper users ask for drift-following maps |
| 6 | "Drift report" after the show (per-song tempo curve, max deviation, lost-lock events) | Medium | Low | Useful for MDs, worship teams and rehearsal review |
| 7 | Time-signature profiles (6/8, 12/8, 5/4, half-time feel) with per-song selection | High | Medium | Directly addresses the 6/8 failure thread |
| 8 | Section detection (verse, chorus, breakdown via energy and pattern change) to OSC section cues | Medium | High | Nice for lights; risky to auto-fire; ship as suggestions first |
| 9 | Headless Pi "Pacemaker Box" with DIN clock, Link, USB-MIDI and a Wi-Fi config page | High | High | Market gap; parallels PlayAUDIO buyers' appetite for dedicated hardware |
| 10 | Redundant dual-instance sync (two machines agree on tempo) | Medium | Medium | AbleSet AbleNet shows pro users expect redundancy |
| 11 | MIDI-drum / e-kit mode (note-on timestamps, no audio) | High | Low-Medium | Repeated request (Live 11 can't; InTime did) |
| 12 | Score or structure following (Antescofo-style) for theatre | High for pits | Very High | Defer; partner with the QLab marker workflow instead |

---

## Sources

- Ableton manual: https://www.ableton.com/en/manual/synchronizing-with-link-tempo-follower-and-midi/
- Ableton Live 11 Tempo Following FAQ: https://help.ableton.com/hc/en-us/articles/360019100900-Tempo-Following-in-Live-11-FAQ
- Live 12 and Push release notes: https://www.ableton.com/en/release-notes/live-12/ ; https://www.ableton.com/en/release-notes/push-12/
- AbletonDrummer Tempo Follow guide: https://blog.abletondrummer.com/ableton-tempo-follow/
- Ableton forum threads (https://forum.ableton.com/viewtopic.php?t=...): t=244230, t=250324, t=246621, t=242269, t=246317, t=242200, t=205427, t=145730, t=170682, t=90677, t=227148, p=1689781, p=327888
- BeatSeeker: https://www.ableton.com/en/packs/beatseeker/ ; https://www.kvraudio.com/news/ableton-introduces-beatseeker-by-andrew-robertson-30854 ; https://www.synthtopia.com/content/2015/08/28/beatseeker-syncs-ableton-live-to-live-drumming/
- B-Keeper: https://nime.org/proc/nime2007_robertson/ ; https://sonicfield.org/library/synchronizing-sequencing-software-to-a-live-drummer ; https://phys.org/news/2011-01-responsive-click-track-software-drummers-pace.html
- HeartSync: https://bedroomproducersblog.com/2026/02/05/heartsync/ ; https://consciousaudio.com.au/heartsync
- LiveLink: https://www.kvraudio.com/forum/viewtopic.php?p=9272825
- ReaBeat: https://github.com/b451c/ReaBeat ; BTrack: https://github.com/adamstark/BTrack ; madmom on Pi: https://groups.google.com/g/madmom-users/c/yYtp6Y43yWQ ; https://github.com/Sma1033/Realtime-beat-tracking
- Bitwig wishlist: https://bitwish.top/t/tempo-follower/58 ; https://www.kvraudio.com/forum/viewtopic.php?t=585540
- Steinberg forums: https://forums.steinberg.net/t/tempo-change-in-project-by-live-drummer/145188/1 ; https://forums.steinberg.net/t/sync-bpm-with-live-drum-input/623709 ; https://forums.steinberg.net/t/live-tempo-tracker/1019189
- MainStage tempo from MIDI: https://help.apple.com/mainstage/mac/3.1/en.lproj/mstga6bb0cb5.html
- Theatre: https://gearspace.com/board/music-computers/1072626-plugin-senses-tempo-midi-tap-tempo-adjusts-playback.html ; https://rms.biz/products/sinfonia ; https://www.mtishows.com/node/134625 ; https://antescofo-doc.ircam.fr/Reference/tempo_inference/ ; https://mattotto.co/ottoble/2013/1/11/ogres-ableton-qlab-oh-my
- Hardware: https://support.roland.com/hc/en-us/articles/16722112271259-SPD-SX-PRO-How-do-I-dedicate-a-Pad-to-control-Tap-Tempo ; https://www.vdrums.com/forum/general/products/1275892-roland-spd-sx-pro-midi-bpm-out ; https://blog.abletondrummer.com/alesis-strike-multipad-review/ ; https://modwiggler.com/forum/viewtopic.php?t=269967&start=50 ; https://www.elektronauts.com/t/help-for-rhythm-tracking-sync-hardware-to-a-drummer-s-beat/228465 ; https://yamahasynth.com/community/montage-series-synthesizers/having-the-arpeggiator-follow-the-drummer-in-a-live-situation
- Apps: https://apps.apple.com/app/id1257590915 (Tempi) ; https://apps.apple.com/app/id6480474508 (Live BPM)
- Click-track sentiment: https://www.drumforum.org/threads/could-this-be-why-some-find-a-click-track-unnatural-feeling-and-lifeless.186371/page-2 ; https://www.talkbass.com/threads/click-tracks-just-the-drummer-or-everyone.1282676/ ; https://ultimatemetal.com/threads/question-to-crew-about-click-tracks.611556/ ; https://gearspace.com/threads/drummers-that-dont-play-to-clicks.227592/ ; https://www.christianitytoday.com/2024/04/contemporary-worship-music-drummer-church-band-tech-genre/ ; https://pro.ultimateears.com/blogs/pro/why-drummers-cant-hear-the-click-track-and-how-to-fix-it ; https://joshuayuvaraj.substack.com/p/using-a-click-track-in-worship-intro ; https://churchfront.com/2023/06/11/how-to-setup-a-click-and-tracks-for-worship-bands/
- Lighting and Link: https://github.com/tennessee-garage/dance-more/issues/125 ; https://resolume.com/support/en/link ; https://www.ableton.com/en/link/products/
- Market and pricing: https://ableset.com/ ; https://isotonikstudios.com/product/ableset-3-by-leolabs/ ; https://forum.ableset.app/t/feedback-wanted-ableset-3-pricing-model/2587 ; https://www.multitracks.com/pricing/ ; https://loopcommunity.com/en-us/prime ; https://sundaysounds.com/pages/looptimus-usb-midi-foot-controller-for-worship ; https://www.sweetwater.com/store/detail/PlayAUDIO1U--iconnectivity-playaudio1u-audio-midi-interface ; https://apps.apple.com/us/app/iq-pro-show-control/id6444418637
- Channels and licensing: https://www.kvraudio.com/forum/viewtopic.php?t=574241 ; https://forum.juce.com/t/plugin-selling-partner-platform/46826 ; https://forum.juce.com/t/best-way-to-sell-plugins-vsts-via-e-commerce/55939 ; Link licence: https://browse.dgit.debian.org/ableton-link.git/tree/LICENSE.md ; https://cdm.link/2016/11/free-jazz-now-ableton-link-sync-works-pure-data/
