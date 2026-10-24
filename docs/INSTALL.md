# Installing Pacemaker

Status: the repository currently builds the engine, services, tests and the evaluation CLI by default. Plugin and
standalone targets build with `-DPACEMAKER_BUILD_PLUGIN=ON` (JUCE 8, fetched by CMake). Release zips from
`release.yml` (RS-05 section 3) are the install route for end users; the paths below are where those files go.

## Which format do I need?

| Format | Platforms | Use it in |
|---|---|---|
| VST3 | Windows, macOS, Linux | Live, Reaper, Bitwig, Cubase/Nuendo, Studio One, Reason (recent), many others |
| CLAP | Windows, macOS, Linux | Bitwig, Reaper (recent), others |
| AU | macOS | Logic, MainStage, GarageBand |
| LV2 | Linux | Ardour, Reaper (Linux) |
| Standalone | all | any rig without a DAW; Pro Tools, Reason, hardware-only rigs |
| AAX | later (phase 4) | Pro Tools (not yet; use standalone + Link) |

## Windows (10/11, x64)

1. Unzip the release. Copy `Pacemaker.vst3` (a folder) to `C:\Program Files\Common Files\VST3\`, `Pacemaker.clap` to
   `C:\Program Files\Common Files\CLAP\`. Standalone: run the installer or unzip `Pacemaker.exe` anywhere.
2. MIDI clock to other programs needs a virtual MIDI port: install **loopMIDI** (Tobias Erichsen, free), start it, add a
   port named `Pacemaker Clock`, leave it running. Hardware MIDI interfaces need no loopMIDI.
3. Link/OSC: allow Pacemaker through Windows Defender Firewall on private networks when asked.
4. ASIO: install your interface's ASIO driver for low latency (WASAPI shared is poor, ES-03).
5. Rescan plugins in the host.

## macOS (11+, Apple Silicon and Intel, universal)

1. Release builds are signed and notarised on tags. Copy `Pacemaker.vst3` to `/Library/Audio/Plug-Ins/VST3/`,
   `Pacemaker.component` to `/Library/Audio/Plug-Ins/Components/`, `Pacemaker.clap` to
   `/Library/Audio/Plug-Ins/CLAP/` (per-user alternatives under `~/Library/Audio/Plug-Ins/`). Drag `Pacemaker.app` to
   `/Applications`.
2. First launch: allow **Local Network** access when macOS asks (needed for Link and OSC peers) and Microphone access
   for the standalone.
3. Virtual MIDI: Pacemaker creates a port called `Pacemaker Clock` itself. IAC Driver (Audio MIDI Setup) also works.
4. Logic: Logic Pro > Settings > Plug-in Manager > Reset & Rescan Selection.

## Linux (x64, ALSA/JACK/PipeWire)

1. VST3: `~/.vst3/Pacemaker.vst3`; CLAP: `~/.clap/Pacemaker.clap`; LV2: `~/.lv2/Pacemaker.lv2`. System-wide:
   `/usr/lib/vst3`, `/usr/lib/clap`, `/usr/lib/lv2`.
2. Standalone: run `./Pacemaker`. For MIDI clock Pacemaker creates a virtual port `Pacemaker Clock` (ALSA sequencer);
   connect with `aconnect -l` / `aconnect`.
3. Real-time audio: add your user to the `audio` group and raise `rtprio`/`memlock` limits (see your distribution's
   JACK/PipeWire guide).

## Host-specific extras

| Host | Extra install | Where |
|---|---|---|
| Reaper | OSC pattern config or ReaScript | `integrations/reaper/README.md` |
| Bitwig | controller script | `integrations/bitwig/README.md` |
| QLab, Resolume, TouchDesigner, MagicQ | OSC how-tos | `integrations/*/README.md` |

Per-host setup (sidechain, Link, which outputs): `docs/hosts/README.md`.

## Standalone

Choose input device and map channels to roles (Kick, Snare, Overhead, Trigger ...), choose output device only if you use
the Sync bus, then run the calibration wizard once per device (ES-03). Auto-start and logging options: RS-03 section 1.

## Pacemaker Box (Raspberry Pi 4/5)

Phase 5 deliverable (after v1.0). Design and hardware reference are in `docs/specs/RS-03-standalone-and-appliance.md`
section 2: Raspberry Pi OS Lite 64-bit image, `pacemaker_headless` systemd service, config in
`/etc/pacemaker/config.json`, web UI on `http://pacemaker.local:8080`. The image requires a Pacemaker licence. Build
target: `-DPACEMAKER_BUILD_HEADLESS=ON`.

## Building from source

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build && ctest --test-dir build
# plugin + standalone (needs JUCE via FetchContent and platform SDKs):
cmake -S . -B build -DPACEMAKER_BUILD_PLUGIN=ON -DPACEMAKER_BUILD_CLAP=ON
# without Ableton Link (e.g. GPL-free build):
cmake -S . -B build -DPACEMAKER_WITH_LINK=OFF
```

Commercial builds need the Ableton Link licence and the Steinberg VST3 agreement (RS-06 section 1).

## Licence file and trial

Drop the `.pacemaker-license` file you received onto the standalone window or into the Licence dialog. Without it you get
a 14-day trial (outputs go silent for 20 s every 5 minutes; tracking and display never stop). Design:
`docs/licensing/LICENSE-DESIGN.md`. Machine ID for bound keys: About > Copy Machine ID.

## Uninstall

1. Delete the files copied above (VST3/CLAP/AU/LV2 bundles, the standalone, loopMIDI if you installed it only for this).
2. Optional user data: presets and settings in the user settings folder, drift logs in your Documents folder
   (`pacemaker-YYYYmmdd-HHMMSS.csv`), the licence file.
3. Trial records, two small files (see LICENSE-DESIGN section 4); safe to delete after you buy or when you remove the product: Windows `%PROGRAMDATA%\Pacemaker`,
   `%APPDATA%\Pacemaker`; macOS `/Users/Shared/Pacemaker`, `~/Library/Application Support/Pacemaker`; Linux
   `~/.local/share/pacemaker`, `~/.pacemaker-trial`.
4. Host scripts: remove `Pacemaker.ReaperOSC` and the Reaper script/`Pacemaker_lib`, the Bitwig `Pacemaker` folder.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Plugin not listed | rescan; check the folder for your host; macOS Gatekeeper: allow in Privacy & Security |
| No Link peers | same network, firewall, macOS Local Network permission |
| No MIDI clock on Windows | loopMIDI port missing or not selected |
| OSC receiver silent | `python3 integrations/common/osc_tool.py listen --port <p>` on the receiver |
