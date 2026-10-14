# Test corpus (RS-05 section 2)

Audio is **not committed**. Small synthetic material is generated on demand, larger sets are
fetched by script, and everything is described by manifests so the scorer finds it.

| Set | Source | Licence | How it gets here |
|---|---|---|---|
| Synthetic drum machine | generated in-repo (`Tests/DrumMachine.h`) | ours | `pacemaker_eval gen`, done by `tools/eval/run_corpus.py` into a temp dir |
| GMD renders | Groove MIDI Dataset (Magenta), rendered through our own synthetic kit | CC BY 4.0 | `python3 tools/eval/fetch_gmd.py --confirm` into `Tests/corpus/gmd/` (git-ignored) |
| E-GMD | Magenta, real e-drum audio | CC BY 4.0 | download manually (about 90 GB), reference it from a manifest |
| Own recordings | 3 drummers, kick / snare / overhead stems, hand-tapped annotations | ours | copy into `Tests/corpus/own/` and list in a manifest; stays out of git |
| Ballroom, GTZAN subsets | public | academic | sanity only, full-mix robustness |

## Annotation format

One line per beat, plain text, UTF-8:

```
timeSeconds beatInBar
```

`beatInBar` is 1-based and optional (a file with only times can be scored for beat metrics but
not downbeat accuracy). Lines starting with `#` are comments. Times are the *annotated beat
positions* (what a listener taps), not the engine's predicted times. File name: `ID.beats` next
to `ID.wav`. `pacemaker_eval gen --out PREFIX` writes the same format for synthetic audio.

## Manifest format

`manifest.json` (JSON, version 1). Paths are relative to the manifest file.

```json
{
  "version": 1,
  "includes": ["gmd/manifest.json", "own/manifest.json"],
  "sets": [
    {
      "name": "own-recordings",
      "licence": "ours",
      "source": "recorded 2026-03, studio A",
      "files": [
        {
          "id": "drummer1_rock_120",
          "audio": "own/drummer1_rock_120.wav",
          "annotation": "own/drummer1_rock_120.beats",
          "bpm": 120,
          "meter": "4/4",
          "roles": ["kick", "snare", "overhead"],
          "tags": ["rock44"],
          "trim": 5.0,
          "min_f": 0.90,
          "min_downbeat": 0.50,
          "sha256": "optional checksum of the audio file"
        }
      ]
    }
  ]
}
```

| Field | Meaning |
|---|---|
| `id` | unique name, used in reports |
| `audio`, `annotation` | WAV (PCM 16/24/32 or float32, 1 to 4 channels) and its `.beats` file |
| `roles` | optional, one engine input role per channel (`kick snare hihat overhead trigger any`) |
| `tags` | free labels; `rock44` marks the T9 subset (4/4 rock) |
| `trim` | seconds ignored at the start for scoring (default 5, the mir_eval convention) |
| `min_f`, `min_downbeat` | optional per-file regression pins, enforced by `run_corpus.py` |

Entries whose audio is missing are skipped with a notice, so a checkout without the fetched data
still runs. `includes` lists further manifests (missing ones are ignored).

## Running

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
python3 tools/eval/run_corpus.py --eval build/tools/eval/pacemaker_eval          # synthetic corpus, CI gate
python3 tools/eval/run_corpus.py --eval build/tools/eval/pacemaker_eval --manifest Tests/corpus/manifest.json
python3 tools/eval/score.py result.json annotation.beats                          # one file
```

`run_corpus.py` exits nonzero when an ES-01 section 12 threshold regresses. `score.py` uses
`mir_eval` when installed (`pip install mir_eval`) and a built-in numpy implementation otherwise.
