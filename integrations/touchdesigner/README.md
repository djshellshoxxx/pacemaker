# Pacemaker and TouchDesigner

Use an **OSC In DAT** with the callback script in `pacemaker_oscin_callbacks.py`.

1. Create `oscin1` (OSC In DAT). Parameters: Network Port `9000`, Active on.
   Set its Callbacks DAT to a Text DAT named `oscin1_callbacks` containing `pacemaker_oscin_callbacks.py`.
2. Create a Constant CHOP named `pacemaker` with channels `tempo`, `beat`, `bar`, `downbeat`, `confidence`
   (the script writes `par.value0..4`; rename as you like in the script).
3. Pacemaker: Outputs > OSC: host `127.0.0.1`, port `9000`, profile Generic.
4. Test: `python3 ../common/osc_tool.py simulate --port 9000`.

Facts: the OSC In DAT callback signature
`onReceiveOSC(dat, rowIndex, message, bytes, timeStamp, address, args, peer)` is as used in published TouchDesigner
examples; the DAT stores each packet as a row. Per the TouchDesigner documentation a row is either one message or an
entire bundle; Pacemaker sends `/beat` (and `/downbeat`) inside a bundle, so check in the DAT table what your build
shows for bundles (some builds offer a split-bundle option on the DAT; not verified here). The script therefore
handles `/pacemaker/beat` when it is delivered as its own row.

Alternative: Ableton Link is supported in TouchDesigner through its Link CHOP (not verified here).
