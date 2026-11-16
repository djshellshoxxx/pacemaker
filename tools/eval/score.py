#!/usr/bin/env python3
"""Score a pacemaker_eval result against a beat annotation (RS-05 section 1.2).

    score.py RESULT.json REFERENCE.beats [--trim 5] [--json OUT.json] [--no-mir-eval]

RESULT.json   written by `pacemaker_eval file.wav --json RESULT.json`
REFERENCE     one line per beat: "timeSeconds beatInBar" (beatInBar may be omitted)

Metrics: beat F-measure (+-70 ms), CMLt, AMLt, mean / SD / mean-absolute beat error in ms,
downbeat accuracy (fraction of reference downbeats with an estimated downbeat within +-70 ms),
plus relock count, octave-ambiguity flag and time to first Locked state from the result file.

mir_eval (MIT) is used for F-measure, CMLt and AMLt when it is importable. Otherwise a built-in
numpy implementation of the same definitions is used (Davies et al. 2009 / mir_eval.beat); the two
agree on well-behaved data but may differ by a few thousandths on edge cases. The beat-error and
downbeat statistics are always computed here. Like mir_eval's own evaluation protocol, beats
before --trim seconds are ignored on both sides.
"""
import argparse
import json
import sys

import numpy as np

F_WINDOW = 0.070
PHASE_THRESHOLD = 0.175
PERIOD_THRESHOLD = 0.175


def load_reference(path):
    times, pos = [], []
    with open(path) as f:
        for line in f:
            parts = line.split()
            if not parts or parts[0].startswith("#"):
                continue
            times.append(float(parts[0]))
            pos.append(int(float(parts[1])) if len(parts) > 1 else 0)
    return np.array(times), np.array(pos, dtype=int)


def load_result(path):
    with open(path) as f:
        r = json.load(f)
    t = np.array([b["t"] for b in r["beats"]], dtype=float)
    p = np.array([b["beat_in_bar"] for b in r["beats"]], dtype=int)
    return r, t, p


# ---------------------------------------------------------------- numpy fallback metrics

def _match(ref, est, window):
    """Greedy one-to-one nearest matching. Returns list of (ref_index, est_index)."""
    pairs, used = [], np.zeros(len(est), dtype=bool)
    for i, r in enumerate(ref):
        if len(est) == 0:
            break
        d = np.abs(est - r)
        d[used] = np.inf
        j = int(np.argmin(d))
        if d[j] <= window:
            used[j] = True
            pairs.append((i, j))
    return pairs


def f_measure(ref, est, window=F_WINDOW):
    if len(ref) == 0 or len(est) == 0:
        return 0.0
    m = len(_match(ref, est, window))
    p, r = m / len(est), m / len(ref)
    return 2 * p * r / (p + r) if p + r > 0 else 0.0


def _continuity_single(ref, est):
    """(CMLc, CMLt) for one metrical interpretation of the reference."""
    if len(ref) < 2 or len(est) < 2:
        return 0.0, 0.0
    ok = np.zeros(len(ref), dtype=bool)
    for n in range(len(ref)):
        prev_ibi = ref[n] - ref[n - 1] if n > 0 else ref[1] - ref[0]
        next_ibi = ref[n + 1] - ref[n] if n + 1 < len(ref) else prev_ibi
        ibi = prev_ibi
        j = int(np.argmin(np.abs(est - ref[n])))
        if abs(est[j] - ref[n]) > PHASE_THRESHOLD * ibi:
            continue
        # period check: the estimated inter-beat interval around this beat vs the reference one
        if j == 0:
            e_ibi = est[1] - est[0]
        else:
            e_ibi = est[j] - est[j - 1]
        if abs(1.0 - e_ibi / ibi) > PERIOD_THRESHOLD:
            continue
        ok[n] = True
    # longest run of consecutive correct beats
    best = run = 0
    for v in ok:
        run = run + 1 if v else 0
        best = max(best, run)
    return best / len(ref), ok.sum() / len(ref)


def _variations(ref):
    """Metrical interpretations of the reference used by AMLt (same set as mir_eval): the beats
    themselves, off-beats, double time, both phases of half time, and triple time."""
    if len(ref) < 2:
        return [ref]
    gap = ref[1:] - ref[:-1]
    mid = ref[:-1] + 0.5 * gap
    dbl = np.sort(np.concatenate([ref, mid]))
    third = np.sort(np.concatenate([ref, ref[:-1] + gap / 3.0, ref[:-1] + 2.0 * gap / 3.0]))
    return [ref, mid, dbl, ref[::2], ref[1::2], third]


def continuity(ref, est):
    cmlc, cmlt = _continuity_single(ref, est)
    amlc = amlt = 0.0
    for v in _variations(ref):
        c, t = _continuity_single(v, est)
        amlc, amlt = max(amlc, c), max(amlt, t)
    return cmlc, cmlt, amlc, amlt


# ---------------------------------------------------------------- scoring

def trim(x, t0, *others):
    keep = x >= t0
    return (x[keep],) + tuple(o[keep] for o in others)


def score(est_t, est_p, ref_t, ref_p, trim_s=5.0, use_mir_eval=True):
    est_t, est_p = trim(est_t, trim_s, est_p)
    ref_t, ref_p = trim(ref_t, trim_s, ref_p)
    out = {"n_ref": int(len(ref_t)), "n_est": int(len(est_t)), "backend": "numpy"}
    mir = None
    if use_mir_eval:
        try:
            import mir_eval  # noqa: F401
            mir = mir_eval.beat
        except Exception:
            mir = None
    if mir is not None and len(ref_t) > 1 and len(est_t) > 1:
        out["backend"] = "mir_eval"
        out["f_measure"] = float(mir.f_measure(ref_t, est_t, f_measure_threshold=F_WINDOW))
        cmlc, cmlt, amlc, amlt = mir.continuity(ref_t, est_t)
        out.update(cmlc=float(cmlc), cmlt=float(cmlt), amlc=float(amlc), amlt=float(amlt))
    else:
        out["f_measure"] = f_measure(ref_t, est_t)
        cmlc, cmlt, amlc, amlt = continuity(ref_t, est_t)
        out.update(cmlc=cmlc, cmlt=cmlt, amlc=amlc, amlt=amlt)

    pairs = _match(ref_t, est_t, F_WINDOW)
    errs = np.array([(est_t[j] - ref_t[i]) * 1000.0 for i, j in pairs])
    out["mean_error_ms"] = float(errs.mean()) if len(errs) else None
    out["sd_error_ms"] = float(errs.std()) if len(errs) else None
    out["mean_abs_error_ms"] = float(np.abs(errs).mean()) if len(errs) else None

    ref_db = ref_t[ref_p == 1]
    est_db = est_t[est_p == 1]
    if len(ref_db) and ref_p.max() > 0:
        hit = sum(1 for r in ref_db if len(est_db) and np.min(np.abs(est_db - r)) <= F_WINDOW)
        out["downbeat_accuracy"] = hit / len(ref_db)
        out["downbeat_f_measure"] = f_measure(ref_db, est_db)
    else:
        out["downbeat_accuracy"] = None
        out["downbeat_f_measure"] = None
    return out


def score_files(result_path, ref_path, trim_s=5.0, use_mir_eval=True):
    res, est_t, est_p = load_result(result_path)
    ref_t, ref_p = load_reference(ref_path)
    s = score(est_t, est_p, ref_t, ref_p, trim_s, use_mir_eval)
    s["relocks_after_trim"] = int(sum(1 for b in res["beats"] if b["t"] >= trim_s and b["relock"]))
    s["octave_ambiguity"] = any(c["octave_ambiguity"] for c in res.get("flag_changes", []))
    lock = [c["t"] for c in res.get("state_changes", []) if c["to"] in ("Locked", "Chase")]
    s["time_to_lock_s"] = lock[0] if lock else None
    s["state_changes"] = len(res.get("state_changes", []))
    s["file"] = res.get("file")
    s["block"] = res.get("block")
    return s


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("result")
    ap.add_argument("reference")
    ap.add_argument("--trim", type=float, default=5.0, help="ignore beats before this time (s), default 5")
    ap.add_argument("--json", help="write the scores here")
    ap.add_argument("--no-mir-eval", action="store_true", help="force the built-in numpy metrics")
    a = ap.parse_args(argv)
    s = score_files(a.result, a.reference, a.trim, not a.no_mir_eval)
    text = json.dumps(s, indent=2)
    if a.json:
        with open(a.json, "w") as f:
            f.write(text + "\n")
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
