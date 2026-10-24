# Pacemaker and lighting consoles (MagicQ)

Two routes. The Link route is the one RS-05's stage test uses ("one lighting app on Link").

## Route 1: Link (preferred where the lighting app supports it)

Enable Pacemaker's Link output. Any Link-capable lighting or VJ app joins the same session on the network.
Whether MagicQ itself joins Link was not verified here.

## Route 2: OSC to MagicQ

Verified from ChamSys documentation (docs.chamsys.co.uk/magicq/manual/OSC.html, via search summary):
- OSC is enabled in MagicQ with Setup > View Settings > Network: set the OSC mode and the receive port
  (a port of 0 disables). ChamSys suggests 9000 for transmit and 8000 for receive as defaults; use ports above 1024.
- Built-in addresses cover playbacks: `/pb/<n>` (fader level, float 0..1 or int 0..100) and `/pb/<n>/go`, for the
  first 10 playbacks and first 10 execute grids. Beyond that, the AUTOM window extends OSC mapping (see manual).

Not found in the documentation we could read: a built-in "tap tempo" OSC address. Pacemaker's MagicQ profile
(`OscConfig.profile = MagicQ`) sends an `int 1` to a **configurable address** (`tapAddress`, default `/tap`) on every
beat. To use it, set `tapAddress` to an address you have mapped in MagicQ (via the AUTOM window or a playback `/go`
per the manual) and test with `python3 ../common/osc_tool.py listen`.

## Setup

1. MagicQ: Setup > View Settings > Network: OSC receive port, e.g. `8000`.
2. Pacemaker: Outputs > OSC: host = MagicQ IP, port `8000`, profile MagicQ, tapAddress as mapped.
3. Test: `python3 ../common/osc_tool.py send --host <ip> --port 8000 /pb/1/go`.

Use wired Ethernet on the lighting network; OSC over UDP is not retransmitted.
