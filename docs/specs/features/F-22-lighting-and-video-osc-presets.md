# F-22: Lighting and video OSC presets

Status: draft 1.0 · Priority: P2 · Estimate: 8 pd · Depends on: OSC output (existing), F-09 for key/chord addresses · Related: ES-02 section 4

## 1. Goal and user value
Operators of grandMA, Resolume, MagicQ, QLC+, TouchDesigner, Millumin, QLab and similar want to connect in a minute. Ready-made profiles and templates remove the guesswork.
## 2. Requirements
- R-1 Profile format (JSON): address templates, argument types and scaling, trigger rules (every beat, downbeat only, bar), rate limits, tempo mapping function (e.g., BPM to normalised value), optional TCP framing/UDP port defaults.
- R-2 Shipped profiles: Generic, Resolume Arena/Avenue, MagicQ/Chamsys, QLC+ (WebSocket or OSC plugin), grandMA3 (OSC to command line or a macro trigger), TouchDesigner (OSC In DAT example), QLab (OSC cues), Millumin, Disguise (d3), each with a test file or sample project in `docs/integrations/<name>/` and citing the product's OSC documentation (**VERIFY** every address against current documentation before shipping).
- R-3 Profile editor in the UI (table of mappings with live send test), import/export JSON, user profiles stored in user settings.
- R-4 "Send test" buttons per mapping and a monitor of outgoing messages (last 50), with copy.
- R-5 Receiver-side helpers: TouchDesigner `.tox` and Max/Pure Data patches that print and visualise the messages; a Python receiver script for debugging.
- R-6 Timestamps: bundles carry NTP timetags; profiles can turn bundles off for receivers that do not support them.
- R-7 Latency offset per profile (ms) with the lighting-console delay calibration procedure documented.
- R-8 Key/chord/energy mappings (F-09) available to profiles when the music analysis is on.
## 3. Design
`OscProfile` loader (validated JSON schema), `ProfileOscGenerator` replaces the hard-coded profile switch in `OscGenerator`, golden-tested; profiles live in `profiles/osc/*.json`, versioned.
## 4. Tests
F22_R1 schema validation and fuzz; F22_R2 golden message streams per profile (virtual time); F22_R6 bundle/no-bundle; integration checks run manually against each product's demo/free mode and recorded in `docs/integrations/CHECKLIST.md`.
## 5. Plan
| Task | pd |
|---|---|
| Profile format, loader, generator refactor | 2.5 |
| Editor and monitor UI | 2 |
| Nine profiles with docs and sample files | 2.5 |
| Receiver helpers, tests, checklist | 1 |
Risks: receiver address mistakes (verification checklist, user-editable profiles).
