#!/usr/bin/env python3
"""Download the Groove MIDI Dataset (GMD) and render a test subset (RS-05 section 2).

NOT part of CI. It downloads about 3 MB, so it refuses to do anything unless you pass
--confirm (or set PACEMAKER_FETCH_GMD=1) and it exits immediately when the CI environment
variable is set unless --confirm is given explicitly on the command line.

    python3 tools/eval/fetch_gmd.py --confirm [--out Tests/corpus/gmd] [--style rock]
            [--beat-type beat] [--split test] [--limit 40] [--seed 1]

What it does
  1. Downloads groove-v1.0.0-midionly.zip from storage.googleapis.com/magentadata (Magenta,
     Creative Commons BY 4.0; credit "Groove MIDI Dataset, Magenta" if you redistribute renders).
  2. Selects 4/4 performances by style / beat type / split from info.csv.
  3. Renders each MIDI performance to a mono 48 kHz float WAV through *our own* synthetic drum
     samples (numpy, no third-party sounds), so human microtiming is kept but no audio licence
     question arises.
  4. Writes the annotation next to it (`ID.beats`, one "timeSeconds beatInBar" line per beat)
     and a manifest.json in the format of Tests/corpus/README.md.

Annotation assumption: GMD performances were recorded against a click that starts at time 0 of
the MIDI file with the tempo given in info.csv, so beat k is at k * 60 / bpm and its bar position
is k % 4 + 1. Beat times are therefore the *click grid*, not the drummer's microtiming, which is
exactly what the engine is supposed to follow.

Files with `"tags": ["rock44"]` are the T9 subset (F >= 0.95, downbeat accuracy >= 0.85): 4/4,
style "rock", beat type "beat". Run the regression with
    python3 tools/eval/run_corpus.py --manifest Tests/corpus/manifest.json

E-GMD (real e-drum audio, same MIDI, about 90 GB) can be used instead by downloading
https://storage.googleapis.com/magentadata/datasets/e-gmd/v1.0.0/e-gmd-v1.0.0.zip yourself and
pointing a manifest entry's "audio" at the extracted wav; the annotation rule is the same.

Dependencies: numpy only.
"""
import argparse
import csv
import hashlib
import io
import json
import os
import struct
import sys
import urllib.request
import wave
import zipfile

import numpy as np

URL = "https://storage.googleapis.com/magentadata/datasets/groove/groove-v1.0.0-midionly.zip"
SR = 48000
HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_OUT = os.path.join(HERE, "..", "..", "Tests", "corpus", "gmd")


# ---------------------------------------------------------------- minimal MIDI reader

def _vlq(data, pos):
    v = 0
    while True:
        b = data[pos]
        pos += 1
        v = (v << 7) | (b & 0x7F)
        if not b & 0x80:
            return v, pos


def read_drum_hits(data):
    """Returns [(seconds, gm_note, velocity)] for all note-ons, honouring the tempo map."""
    if data[:4] != b"MThd":
        raise ValueError("not a MIDI file")
    _, ntracks, division = struct.unpack(">HHH", data[8:14])
    if division & 0x8000:
        raise ValueError("SMPTE time division not supported")
    pos = 8 + struct.unpack(">I", data[4:8])[0]
    events, tempos = [], [(0, 500000)]
    for _ in range(ntracks):
        assert data[pos:pos + 4] == b"MTrk"
        length = struct.unpack(">I", data[pos + 4:pos + 8])[0]
        p, end, tick, status = pos + 8, pos + 8 + length, 0, 0
        while p < end:
            delta, p = _vlq(data, p)
            tick += delta
            if data[p] & 0x80:
                status = data[p]
                p += 1
            if status == 0xFF:
                mtype = data[p]
                ln, p = _vlq(data, p + 1)
                if mtype == 0x51 and ln == 3:
                    tempos.append((tick, int.from_bytes(data[p:p + 3], "big")))
                p += ln
            elif status in (0xF0, 0xF7):
                ln, p = _vlq(data, p)
                p += ln
            else:
                kind = status & 0xF0
                n = 1 if kind in (0xC0, 0xD0) else 2
                if kind == 0x90 and data[p + 1] > 0:
                    events.append((tick, data[p], data[p + 1]))
                p += n
        pos = end
    tempos.sort()
    out = []
    for tick, note, vel in sorted(events):
        t, last_tick, usec = 0.0, 0, 500000
        for tt, uu in tempos:
            if tt >= tick:
                break
            t += (tt - last_tick) * usec / division / 1e6
            last_tick, usec = tt, uu
        t += (tick - last_tick) * usec / division / 1e6
        out.append((t, note, vel))
    return out


# ---------------------------------------------------------------- synthetic kit

def _env(n, tau):
    return np.exp(-np.arange(n) / (tau * SR))


def _voice(kind, rng):
    if kind == "kick":
        n = int(0.12 * SR)
        t = np.arange(n) / SR
        ph = 2 * np.pi * np.cumsum(55 + 45 * np.exp(-t / 0.012)) / SR
        return 0.9 * np.sin(ph) * _env(n, 0.04)
    if kind == "snare":
        n = int(0.15 * SR)
        t = np.arange(n) / SR
        noise = rng.uniform(-1, 1, n)
        return (0.35 * noise + 0.4 * np.sin(2 * np.pi * 190 * t)) * _env(n, 0.05)
    if kind == "hat":
        n = int(0.05 * SR)
        noise = rng.uniform(-1, 1, n)
        return 0.25 * np.diff(noise, prepend=0.0) * _env(n, 0.012)
    if kind == "openhat":
        n = int(0.25 * SR)
        noise = rng.uniform(-1, 1, n)
        return 0.2 * np.diff(noise, prepend=0.0) * _env(n, 0.09)
    if kind == "tom":
        n = int(0.2 * SR)
        t = np.arange(n) / SR
        return 0.6 * np.sin(2 * np.pi * 110 * t) * _env(n, 0.08)
    n = int(0.5 * SR)  # cymbal
    noise = rng.uniform(-1, 1, n)
    return 0.25 * np.diff(noise, prepend=0.0) * _env(n, 0.15)


KIT_MAP = {36: "kick", 35: "kick", 38: "snare", 40: "snare", 37: "snare", 39: "snare", 42: "hat", 44: "hat",
           46: "openhat", 41: "tom", 43: "tom", 45: "tom", 47: "tom", 48: "tom", 50: "tom"}


def render(hits, seed):
    rng = np.random.RandomState(seed)
    kit = {k: _voice(k, rng) for k in ("kick", "snare", "hat", "openhat", "tom", "cymbal")}
    length = int((max(t for t, _, _ in hits) + 1.0) * SR)
    y = np.zeros(length, dtype=np.float32)
    for t, note, vel in hits:
        v = kit[KIT_MAP.get(note, "cymbal")]
        s = int(t * SR)
        e = min(length, s + len(v))
        y[s:e] += (vel / 127.0) * v[:e - s]
    y += rng.uniform(-1, 1, length).astype(np.float32) * 1e-4
    return np.clip(y, -1.0, 1.0)


def write_wav(path, y):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((y * 32767.0).astype("<i2").tobytes())


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--confirm", action="store_true", help="actually download and render (required)")
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--style", default="rock", help="info.csv style prefix, e.g. rock, funk (default rock)")
    ap.add_argument("--beat-type", default="beat", choices=["beat", "fill", "any"])
    ap.add_argument("--split", default="test", choices=["train", "validation", "test", "any"])
    ap.add_argument("--limit", type=int, default=40)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--zip", help="use an already downloaded groove-v1.0.0-midionly.zip")
    a = ap.parse_args()
    if not (a.confirm or os.environ.get("PACEMAKER_FETCH_GMD") == "1"):
        sys.exit("fetch_gmd.py downloads about 3 MB and renders audio. It does not run by default; pass --confirm.")
    if os.environ.get("CI") and not a.confirm:
        sys.exit("refusing to run in CI without an explicit --confirm")

    os.makedirs(a.out, exist_ok=True)
    zpath = a.zip or os.path.join(a.out, "groove-v1.0.0-midionly.zip")
    if not os.path.isfile(zpath):
        print(f"downloading {URL}")
        with urllib.request.urlopen(URL, timeout=120) as r, open(zpath, "wb") as f:
            f.write(r.read())
    print("zip sha256", hashlib.sha256(open(zpath, "rb").read()).hexdigest())
    zf = zipfile.ZipFile(zpath)
    rows = list(csv.DictReader(io.StringIO(zf.read("groove/info.csv").decode())))
    sel = [r for r in rows if r["time_signature"] == "4-4" and r["style"].split("/")[0] == a.style
           and (a.beat_type == "any" or r["beat_type"] == a.beat_type) and (a.split == "any" or r["split"] == a.split)]
    rng = np.random.RandomState(a.seed)
    rng.shuffle(sel)
    sel = sel[:a.limit]
    print(f"{len(sel)} performances selected")

    files = []
    for r in sel:
        pid = r["id"].replace("/", "_")
        data = zf.read("groove/" + r["midi_filename"])
        hits = read_drum_hits(data)
        if not hits:
            continue
        y = render(hits, a.seed)
        bpm = float(r["bpm"])
        wav_rel, ann_rel = pid + ".wav", pid + ".beats"
        write_wav(os.path.join(a.out, wav_rel), y)
        n_beats = int(len(y) / SR * bpm / 60.0)
        with open(os.path.join(a.out, ann_rel), "w") as f:
            for k in range(n_beats):
                f.write(f"{k * 60.0 / bpm:.4f} {k % 4 + 1}\n")
        tags = ["4-4", r["style"].split("/")[0], r["beat_type"]]
        if r["style"].startswith("rock") and r["beat_type"] == "beat":
            tags.append("rock44")
        files.append({"id": pid, "audio": wav_rel, "annotation": ann_rel, "bpm": bpm, "meter": "4/4",
                      "roles": ["any"], "tags": tags, "trim": 5.0})
    manifest = {"version": 1, "sets": [{
        "name": "gmd-rendered", "licence": "CC BY 4.0 (Groove MIDI Dataset, Magenta); renders by Pacemaker synthetic kit",
        "source": URL, "files": files}]}
    with open(os.path.join(a.out, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
    print(f"wrote {len(files)} renders and {os.path.join(a.out, 'manifest.json')}")


if __name__ == "__main__":
    main()
