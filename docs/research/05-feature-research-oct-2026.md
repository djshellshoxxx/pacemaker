# Research 05: licensing, platform and algorithm findings for the next feature wave

Date: 2026-10-10. Method: web searches plus engineering knowledge. Items
marked **VERIFY** were not confirmed from a primary source and must be
checked before the spec that depends on them is implemented. This is not
legal advice.

## 1. Licensing and platform

| Topic | Finding | Source | Consequence |
|---|---|---|---|
| Ableton Link | Dual licensed GPLv2+ and a proprietary licence. Proprietary software must contact link-devs@ableton.com. Current commercial terms and pricing were not found. | [Link README mirror](https://github.com/lithqube/link), [CDM](https://cdm.link/?p=44725) | Send the licence request now (F-04 task L-1). Keep Link behind `PACEMAKER_WITH_LINK`. **VERIFY** terms, fees and the compatibility test plan. |
| Windows MIDI Services | Moved from Insider-only preview (Nov 2025 blog) to a retail rollout via Windows Update on 24H2/25H2 in early 2026. Timestamped scheduling API names not confirmed. | [MS blog](https://devblogs.microsoft.com/windows-music-dev/troubleshooting-recent-midi-issues-in-windows-11/), [MIDI.org](https://midi.org/midi-2-0-coming-to-windows-11) | F-05 keeps a WinMM spin path as the baseline and adds a Windows MIDI Services backend behind a runtime check. **VERIFY** the SDK scheduling calls. |
| clap-juce-extensions | MIT licence, works with JUCE 6/7 (JUCE 8 use reported), not supported by the JUCE team. JUCE roadmap (Q3 2024) planned native CLAP in JUCE 9. 2026 status unknown. | [repo mirror](https://git.tdem.in/free-audio/clap-juce-extensions), [JUCE roadmap](https://juce.com/?p=1627) | F-03 builds CLAP through the extension and isolates it so native CLAP can replace it. **VERIFY** JUCE version, licence tier and price before purchase. |
| Essentia | AGPLv3 for non-commercial use, commercial licence on request; pretrained models CC BY-NC-ND 4.0 (non-commercial); some dependencies are GPL/LGPL. | [Essentia licensing](https://essentia.upf.edu/licensing_information.html) | Do not link Essentia or ship its models. Write our own DSP (F-08). Use it only as an offline reference for accuracy comparison on a developer machine. |
| aubio, Vamp pYIN, MELODIA | GPL or non-commercial in the original distributions (from memory). | not confirmed | **VERIFY**. Implement YIN/pYIN ourselves from the papers; do not use MELODIA. |
| Basic Pitch | Apache-2.0 code and weights, about 17k parameters, ONNX file about 230 KB, ports to CoreML/GGUF/TFLite exist. Real-time latency unverified. | [Hugging Face](https://huggingface.co/spotify/basic-pitch), [GitHub](https://github.com/spotify/basic-pitch) | Candidate optional polyphonic transcriber (F-08 phase 6). Spike S-1 measures latency and CPU. |
| CREPE | MIT licence, TensorFlow; ONNX ports exist. | [PyPI](https://pypi.org/project/crepe/) | Optional ML pitch tracker; the default is our own YIN/pYIN. |
| Drum datasets | Licences for E-GMD, ENST-Drums, MDB Drums, IDMT-SMT-Drums, ADTOF could not be confirmed. Public drum data is limited by licensing. | [ISMIR TISMIR 244](https://transactions.ismir.net/articles/244) | F-02 keeps a licence ledger per dataset; commercial use of any dataset needs a checked licence. The main corpus is our own recordings. |

## 2. Algorithms

- **Key detection.** Profile correlation (Krumhansl-Kessler) over chroma is
  the baseline. A MIREX-style comparison on loops found Essentia's
  EDM-tuned profiles best (about 72.4 on one subset, 88.3 on another),
  Krumhansl close behind (67.0, 84.9) and Temperley weaker on that data
  (61.5, 71.8); profile choice depends on genre. Source:
  [Freesound Loop Dataset paper](https://arxiv.org/pdf/2008.11507). The
  weighted score gives partial credit for fifth, relative and parallel
  errors; `mir_eval.key` implements it (**VERIFY** the weights against
  the MIREX documentation).
- **Chords.** Template matching is the usual baseline; HMM/Viterbi
  smoothing beats frame-wise decisions; deep models help mostly on common
  chords and are weaker on rare ones ([FMP notebook](https://audiolabs-erlangen.de/resources/MIR/FMP/C5/C5S3_ChordRec_HMM.html),
  [2025 survey](https://arxiv.org/pdf/2512.22621)). Standard Viterbi is
  offline; the live version needs fixed-lag decoding. Our unusual
  advantage: the tracker already provides beat positions, so chroma can be
  averaged beat-synchronously and chord changes snapped to beats.
- **Drum transcription.** Per-class recurrent onset detectors (Southall et
  al., ISMIR 2016) work online; snare is the hardest class; MP3 encoding
  hurts generalisation ([Wu et al. survey](https://musicinformatics.gatech.edu/wp-content_nondefault/uploads/2018/05/Wu-et-al.-2018-A-review-of-automatic-drum-transcription.pdf),
  [Riley and Dixon 2024](https://webspace.eecs.qmul.ac.uk/s.e.dixon/pub/2024/482_lbd.pdf)). With close mics Pacemaker has per-instrument
  channels, which makes classification far easier than the full-mix case.
- **Pitch.** YIN (de Cheveigne and Kawahara 2002) and pYIN (Mauch and
  Dixon 2014) are the standard classical monophonic trackers; CREPE is
  claimed to equal or beat pYIN.

## 3. Decisions taken

1. No GPL, AGPL or non-commercial code or models in the shipped product.
2. Own DSP for key, chords, pitch, spectrum, percussion statistics.
3. Optional ML (Basic Pitch, CREPE) only if Apache/MIT and only after the
   S-1 spike proves CPU and latency budgets.
4. Platform-neutral `HostClock` and `MidiSink` abstractions so macOS,
   Windows and Linux backends can be developed and tested independently.
5. A licence ledger (F-02 section 5) is a release gate.
