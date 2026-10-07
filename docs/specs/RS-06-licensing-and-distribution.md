# RS-06: Licensing and distribution (regular spec)

Status: draft 0.1. Research: `docs/research/03-competitors-and-user-needs.md` section 3, `docs/research/04-plugin-architecture-and-latency.md` section 7.

## 1. Third-party licences

| Component | Licence | Action |
|---|---|---|
| JUCE 8 | Starter free under US$20k revenue, Indie to $300k ($40/mo or $800), Pro above | Start on Starter; move to Indie when revenue requires |
| Ableton Link | GPLv2+ or proprietary | Request the proprietary licence from link-devs@ableton.com before the first paid release; keep Link behind a build flag |
| asio-standalone (Link dep) | BSL-1.0 | fine |
| clap-juce-extensions, CLAP SDK | MIT | fine |
| LV2 | ISC | fine |
| VST3 SDK | GPLv3 or Steinberg agreement | sign the Steinberg VST3 licence agreement |
| cpp-httplib | MIT | fine |
| pffft or JUCE FFT | BSD / JUCE | fine |
| farbot, crill, choc | MIT, BSL, ISC | fine |
| mir_eval (test only) | MIT | fine |
| pluginval (test only) | GPLv3 | tool only, not linked |
| Real-time PLP reference | MIT (Python) | port, keep attribution |
| Krzyzaniak beat tracking | MIT (C) | may reuse with attribution |
| BTrack, aubio, madmom models, Essentia | GPL / NC / AGPL | **never linked**; papers only |

## 2. Product licence

Closed source, perpetual licence per user, 3 machines, offline key file
with machine count (no iLok in v1). Free trial 14 days with periodic
silence in outputs (never in the detector, so users can judge tracking).

## 3. Pricing (from research 03)

| Tier | Price | Contents |
|---|---|---|
| Pacemaker Lite | $39 | plugin and standalone, Link output only, one input role |
| Pacemaker | $99 | all outputs, all roles, song map, drift report |
| Pacemaker Box image | $59 | Raspberry Pi image, requires a Pacemaker licence |
| Upgrades | 50 percent of the difference | |

Prices are hypotheses to validate against AbleSet ($129 to $269) and
BeatSeeker ($29).

## 4. Channels

Own site via Paddle or Lemon Squeezy (merchant of record), KVR listing,
Plugin Boutique after launch (40 percent commission). Gumroad for the Pi
image.

## 5. Trademark and naming

Search "Pacemaker" in audio software classes in the US, UK and EU before
launch (the 2008 Tonium "Pacemaker" DJ device is prior use in class 9).
Fallback names recorded in the project notes.
