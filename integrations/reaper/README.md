# Pacemaker for REAPER

Two ways to make REAPER's project tempo follow Pacemaker. Pick one; do not run both at once.

| | Method A: OSC pattern config | Method B: ReaScript |
|---|---|---|
| Files | `Pacemaker.ReaperOSC` | `Scripts/Pacemaker_TempoFollow.lua` + `Scripts/Pacemaker_lib/` |
| Dependencies | none (REAPER built-in OSC surface) | a UDP socket library: ReaPack package "Mavriq Lua Batteries" |
| Smoothing / deadband / confidence gate | none: every `/pacemaker/tempo` is applied | yes |
| Enable / disable | add or remove the control surface | run the script / run it again |

Start with Method A, it needs nothing else. Use Method B if the tempo moves too jittery in your project.

Pacemaker side (both methods): Outputs > OSC, enable, host `127.0.0.1`, port `9000` (default), root `/pacemaker`,
profile Generic. REAPER sends nothing back to Pacemaker.

## Facts this folder relies on

- REAPER pattern config syntax: from the comments in REAPER's own `Default.ReaperOSC`: `f` = raw float argument,
  example `TEMPO f/tempo/raw` ("the device sends /tempo/raw 100.351 to change the REAPER tempo to 100.351 bpm").
  Our line is `TEMPO f/pacemaker/tempo`.
- ReaScript has no built-in socket API. LuaSocket is not shipped with REAPER. "Mavriq Lua Batteries" (ReaPack) provides
  `require("socket.core")` once its `batteries_header.lua` has been `dofile`d. The script does this itself.
- `reaper.SetCurrentBPM(proj, bpm, wantUndo)` is part of the ReaScript API.
- Not verified on a live REAPER install in this repo's CI: the REAPER UI labels below can differ by REAPER version.

## Method A: OSC pattern config (no script)

1. In REAPER: Options > Show REAPER resource path in explorer/finder. Open (or create) the folder `OSC`.
2. Copy `Pacemaker.ReaperOSC` into that `OSC` folder.
3. Preferences > Control/OSC/web > Add. Control surface mode: OSC (Open Sound Control).
4. Pattern config: choose `Pacemaker` from the list (it appears after step 2; reopen the dialog if it does not).
5. Mode: choose the option that listens on a local port without sending (leave the device IP empty or the send
   port at 0 if your version offers it). Local listen port: `9000`. Leave "Allow binding messages to REAPER actions
   and FX learn" off.
6. OK. In Preferences > Control/OSC/web > select the surface > Edit, the "Log" or listen view (if shown) lists incoming
   messages; with Pacemaker following you should see `/pacemaker/tempo`.
7. Remove any project tempo envelope or tempo markers (REAPER will otherwise keep its own map). Test: with Pacemaker
   locked, REAPER's tempo box should follow.

Limit: the pattern config applies each tempo message directly. If REAPER's tempo wobbles, use Method B.

## Method B: ReaScript

1. Install the socket library: Extensions > ReaPack > Import repositories..., add the Mavriq ReaScript repository
   (index URL is on https://github.com/mavriq-dev/mavriq-lua-batteries; check the current URL there), Browse packages,
   install "Mavriq Lua Batteries", restart REAPER. Without ReaPack: ReaPack itself is at https://reapack.com.
2. Copy `Scripts/Pacemaker_TempoFollow.lua` and the `Scripts/Pacemaker_lib/` folder into `<resource path>/Scripts/`
   (same folder, keep the `Pacemaker_lib` subfolder next to the script).
3. Actions > Show action list > New action > Load ReaScript..., choose `Pacemaker_TempoFollow.lua`.
4. Edit the `CONFIG` table at the top if you changed port or root. Port 9000 must be free: do not also add the OSC
   control surface from Method A on the same port.
5. Run the action. The ReaScript console prints `Listening for /pacemaker/tempo on 127.0.0.1:9000`. Run it again to
   stop (the action shows as on/off in toolbars; assign it to a toolbar button or shortcut for a toggle).

If the socket library is missing the script prints the install steps and points at Method A, then exits.

Behaviour: exponential smoothing (`alpha` 0.35), deadband 0.05 BPM, at most 10 changes per second, ignores
`/pacemaker/tempo` when `/pacemaker/confidence` is below 0.35 or `/pacemaker/state` is `HOLD`/`IDLE`, restarts
smoothing on a jump over 12 BPM, clamps to 30 to 300 BPM.

## Tests

```
cd integrations/reaper
lua5.4 tests/test_osc.lua        # also works with luajit / lua5.3
```

Covers the OSC decoder (messages, bundles, timetags, strings, malformed input) and the smoothing logic.
`tests/gen_fixtures.py` prints the independent Python-encoded byte fixture used by the test.

## Known limits

- The SetCurrentBPM path changes the master tempo only; it does not move tempo envelope points.
- Tempo only. Beat phase and downbeat are not applied to REAPER's timeline; for hardware use the MIDI clock output.
- REAPER can also slave to MIDI clock (Preferences > Audio > MIDI Devices, "Send clock to"/receive options differ
  by version); see `docs/hosts/reaper.md`.
