# ES-03: Latency and Calibration (detailed engineering spec)

Status: draft 0.1, normative.
Research inputs: `docs/research/04-plugin-architecture-and-latency.md`
sections 1 and 3; `docs/research/02-clock-output-mechanisms.md` section 6.

Pacemaker's accuracy is bounded not by the tracker but by knowing *when*
the drummer's hit actually happened relative to the clock we publish. This
spec defines every term in that chain and how each is measured or entered.

---

## 1. The timing chain

For a stick hit at acoustic time `T0` at the drum:

```
T0 ─ acoustic path (mic distance) ─> T_mic
T_mic ─ ADC + driver input buffering (inputLatency) ─> T_in (first sample in a callback)
T_in ─ detector delay (constant, ES-01 section 4) ─> T_detected
```

The engine wants `T0` in the sample clock. So every onset sample is
corrected:

```
onsetSampleCorrected = onsetSampleRaw
                     - detectorDelaySamples          (known, constant)
                     - inputLatencySamples           (driver report + hidden)
                     - acousticDelaySamples[role]    (user entered)
```

On the output side, a published beat must arrive at the listener at the
same moment as the drummer's next hit. Link and OSC already carry host
times; MIDI clock and the audio Sync bus each add their own measured
`offsetMs`.

## 2. Input latency

`inputLatencySamples = reportedInputLatency + hiddenInputLatency`.

**Reported**: standalone reads `AudioIODevice::getInputLatencyInSamples()`;
plugins have no reliable value (the host's input chain is invisible), so
`reported = 0` and the whole value comes from calibration or manual entry.

**Hidden** (driver and interface DSP that the API does not report, 1 to 3
ms on many USB interfaces): measured by loopback calibration (section 4)
and stored per device key.

Driver notes used in UI hints (from research 04):

| Driver | Report trust | Typical total input latency |
|---|---|---|
| ASIO | good | 2 to 4 ms at 64 samples |
| CoreAudio | fair, historically omitted buffer size in JUCE; verify on JUCE 8 | 2 to 4 ms at 32 to 64 samples |
| WASAPI exclusive | good | 5 ms |
| WASAPI shared | poor | 10 to 12 ms |
| DirectSound | useless | 15 to 20 ms |
| ALSA / JACK | depends on driver | 3 to 8 ms |
| PipeWire | depends on config | 1 to 11 ms |

## 3. Acoustic delay per role

Speed of sound 343 m/s: 2.9 ms per metre. Entered per role as a distance
with preset chips: Close (0.1 m, 0.3 ms), Overhead (1 m, 2.9 ms), Room
(3 m, 8.7 ms), Custom. Trigger and MIDI roles are 0. E-drum modules add
their own trigger-to-MIDI latency (typically 2 to 5 ms): a "Module
latency" field per Trigger role, default 3 ms.

## 4. Loopback calibration (standalone and headless)

Measures the hidden input latency to ±1 sample.

1. UI asks the user to patch the device's selected output to the selected
   input (or hold the kick mic at a monitor speaker for an acoustic
   variant, section 6).
2. Pacemaker emits 8 clicks (1 ms, 0 dBFS, spaced 500 ms) on the chosen
   output while recording the input.
3. For each click, cross-correlate a 20 ms input window against the click
   template; take the peak lag. Median across clicks, reject outliers more
   than 2 samples from the median.
4. `measuredRoundTrip = medianLag`
   `hidden = measuredRoundTrip - (reportedInput + reportedOutput)`
   If `hidden < 0`, the driver over-reports; store the negative value (it
   is still the right correction).
5. Display: measured round trip, reported sum, hidden delta, and a
   pass/fail (fail when fewer than 6 clicks were detected or spread > 4
   samples).
6. Store `{deviceName, sampleRate, bufferSize} -> hiddenInputSamples`,
   `hiddenOutputSamples` (assumed equal split unless the user chooses
   "all on input").

The same measurement yields the MIDI clock offset starting point
(section 5) and the Sync bus offset (zero by construction, since it is
rendered in the same output stream).

## 5. Plugin calibration ("Tap along")

Inside a DAW the input path is unknown. Procedure:

1. The user arms a metronome in the DAW and plays along on the kit for 8
   bars.
2. Pacemaker reads the host beat grid from `AudioPlayHead::PositionInfo`
   (ppq position and tempo) and computes, for each detected onset nearest
   a host beat, the error `onset - hostBeat`.
3. The median error over the 8 bars is shown as "Your hits land X ms
   after the DAW beat". The user accepts it as `inputLatency` (knowing it
   also contains their own anticipation, typically 10 to 30 ms early,
   which the UI explains) or enters a value manually.
4. Alternatively "Click loopback": the user routes the DAW metronome into
   Pacemaker's input bus; the measured error is then the pure DAW input
   latency without human bias. Preferred when the host allows the routing.

## 6. Hardware clock calibration

For MIDI clock receivers: the user runs a hardware metronome or sequencer
click (slaved to Pacemaker's clock) into a spare Pacemaker input, plays a
steady beat, and the UI shows the offset between the hardware click and
the drummer's hits. A "Set offset" button writes the negative of that
value into `midiClockOffsetMs`. Same mechanism as Live's documented MIDI
Clock Sync Delay procedure, automated.

## 7. Lookahead and prediction

Outputs need to know a beat before it happens. `lookaheadMs` (default 30)
is the minimum lead. The engine always has a full period of prediction, so
the constraint only bites after Relock; outputs then skip the first beat if
it is less than `lookaheadMs` away.

## 8. Storage

Calibration lives in user settings (not in plugin state), keyed as above,
so the same interface on a different session is already calibrated.
Plugin builds store the tap-along value in the plugin's state (it is
session-specific).

## 9. Test requirements

- L1. Offline loopback simulation: a synthetic device with known input and
  output delays and a hidden 37-sample term: calibration reports 37 ±1.
- L2. Noise robustness: the same at -20 dB SNR pink noise: still ±1.
- L3. Role acoustic delay: an onset rendered 1 m "away" (2.9 ms later)
  with the Overhead preset lands at the same corrected sample as the Close
  role's onset ±1 sample.
- L4. End-to-end: a rendered GMD track played into the standalone through a
  simulated device with known delays; the Sync bus click aligns with the
  ground-truth beat within 2 ms after calibration.
