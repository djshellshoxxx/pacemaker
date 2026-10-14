#!/usr/bin/env python3
"""Pacemaker licence tool (RS-06 section 2). Offline signing of key files with Ed25519.

  keygen.py genkey  --private /safe/place/pacemaker-signing.key --public-out pubkey.txt
  keygen.py sign    --private KEY --tier full --licensee "Jane Drummer" --email jane@example.com \
                    --order PM-000123 [--machines 3] [--major 1] [--bind HASH --bind HASH] [--issued 2026-10-14] \
                    --out jane.pacemaker-license
  keygen.py pubkey-c --private KEY            # prints a C array for PublicKey.h
  keygen.py selftest                          # signs/verifies with the built-in TEST key only

The PRIVATE KEY (a 32 byte seed, hex) must live outside the repository (a password manager, an offline
disk, or the merchant-of-record webhook host's secret store). genkey and sign refuse a private-key path
inside a git work tree. The only key this repo contains is the TEST key used by the unit tests.

Uses the `cryptography` package when installed, otherwise a built-in pure Python RFC 8032 implementation.
"""
import argparse, base64, datetime, hashlib, os, subprocess, sys

# ---------------------------------------------------------------- Ed25519 (RFC 8032, pure Python fallback)
_p = 2**255 - 19
_d = -121665 * pow(121666, _p - 2, _p) % _p
_q = 2**252 + 27742317777372353535851937790883648493
_I = pow(2, (_p - 1) // 4, _p)

def _inv(x): return pow(x, _p - 2, _p)
def _xrecover(y):
    xx = (y * y - 1) * _inv(_d * y * y + 1)
    x = pow(xx, (_p + 3) // 8, _p)
    if (x * x - xx) % _p != 0: x = x * _I % _p
    if x % 2 != 0: x = _p - x
    return x
_By = 4 * _inv(5) % _p
_B = (_xrecover(_By) % _p, _By)

def _add(P, Q):
    x1, y1 = P; x2, y2 = Q
    x3 = (x1 * y2 + x2 * y1) * _inv(1 + _d * x1 * x2 * y1 * y2)
    y3 = (y1 * y2 + x1 * x2) * _inv(1 - _d * x1 * x2 * y1 * y2)
    return x3 % _p, y3 % _p

def _mul(P, e):
    Q = (0, 1)
    while e:
        if e & 1: Q = _add(Q, P)
        P = _add(P, P); e >>= 1
    return Q

def _enc(P):
    x, y = P
    return (y | ((x & 1) << 255)).to_bytes(32, "little")

def _h(m): return hashlib.sha512(m).digest()
def _int(b): return int.from_bytes(b, "little")

def _expand(seed):
    h = _h(seed)
    a = 2**254 + (_int(h[:32]) & ((1 << 254) - 8))
    return a, h[32:]

def _pure_public(seed):
    a, _ = _expand(seed); return _enc(_mul(_B, a))

def _pure_sign(seed, msg):
    a, prefix = _expand(seed)
    A = _enc(_mul(_B, a))
    r = _int(_h(prefix + msg)) % _q
    R = _enc(_mul(_B, r))
    S = (r + _int(_h(R + A + msg)) * a) % _q
    return R + S.to_bytes(32, "little")

try:
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    from cryptography.hazmat.primitives import serialization
    def public_from_seed(seed):
        return Ed25519PrivateKey.from_private_bytes(seed).public_key().public_bytes(
            serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    def sign(seed, msg): return Ed25519PrivateKey.from_private_bytes(seed).sign(msg)
except Exception:
    public_from_seed, sign = _pure_public, _pure_sign

# ---------------------------------------------------------------- licence format
MAGIC = "PACEMAKER-LICENSE-1"
TEST_SEED = b"PACEMAKER-TEST-KEY-NOT-SECRET!!!"      # exactly 32 bytes; unit tests only

def b64u(b): return base64.urlsafe_b64encode(b).rstrip(b"=").decode()

def build_message(tier, licensee, email, order, machines, major, issued, bind):
    for v in (licensee, email, order):
        if "\n" in v or "\r" in v: raise SystemExit("newline in field")
    lines = [MAGIC, "product=pacemaker", f"tier={tier}", f"licensee={licensee}", f"email={email}",
             f"order={order}", f"issued={issued}", f"machines={machines}", f"major={major}"]
    if bind:
        if len(bind) > machines: raise SystemExit("more --bind values than --machines")
        lines.append("bind=" + ",".join(h.replace("-", "").lower() for h in bind))
    return "".join(l + "\n" for l in lines)

def make_license(seed, **kw):
    msg = build_message(**kw)
    return msg + "signature=" + b64u(sign(seed, msg.encode())) + "\n"

def inside_git_tree(path):
    d = os.path.dirname(os.path.abspath(path)) or "."
    while not os.path.isdir(d): d = os.path.dirname(d)
    r = subprocess.run(["git", "-C", d, "rev-parse", "--is-inside-work-tree"], capture_output=True, text=True)
    return r.returncode == 0 and r.stdout.strip() == "true"

def read_seed(path):
    if inside_git_tree(path): sys.exit(f"refusing: {path} is inside a git work tree; keep the private key outside the repo")
    s = bytes.fromhex(open(path).read().strip())
    if len(s) != 32: sys.exit("private key file must hold 64 hex chars (32 byte seed)")
    return s

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    g = sp.add_parser("genkey"); g.add_argument("--private", required=True); g.add_argument("--public-out")
    s = sp.add_parser("sign"); s.add_argument("--private", required=True)
    s.add_argument("--tier", choices=["lite", "full"], required=True)
    s.add_argument("--licensee", required=True); s.add_argument("--email", default="")
    s.add_argument("--order", required=True); s.add_argument("--machines", type=int, default=3, choices=[1, 2, 3])
    s.add_argument("--major", type=int, default=1); s.add_argument("--bind", action="append", default=[])
    s.add_argument("--issued", default=datetime.date.today().isoformat()); s.add_argument("--out", required=True)
    c = sp.add_parser("pubkey-c"); c.add_argument("--private", required=True)
    sp.add_parser("selftest")
    t = sp.add_parser("test-license", help="write a licence signed with the TEST key (for fixtures)")
    t.add_argument("--tier", choices=["lite", "full"], default="full"); t.add_argument("--machines", type=int, default=3)
    t.add_argument("--major", type=int, default=1); t.add_argument("--bind", action="append", default=[])
    t.add_argument("--out", required=True)
    a = ap.parse_args()

    if a.cmd == "genkey":
        if inside_git_tree(a.private): sys.exit("refusing to write a private key inside a git work tree")
        if os.path.exists(a.private): sys.exit("refusing to overwrite existing key")
        seed = os.urandom(32)
        fd = os.open(a.private, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "w") as f: f.write(seed.hex() + "\n")
        pub = public_from_seed(seed)
        print("public key (hex):", pub.hex())
        if a.public_out: open(a.public_out, "w").write(pub.hex() + "\n")
    elif a.cmd == "sign":
        seed = read_seed(a.private)
        text = make_license(seed, tier=a.tier, licensee=a.licensee, email=a.email, order=a.order,
                            machines=a.machines, major=a.major, issued=a.issued, bind=a.bind)
        open(a.out, "w").write(text); print("wrote", a.out)
    elif a.cmd == "pubkey-c":
        pub = public_from_seed(read_seed(a.private))
        print("static const uint8_t kPacemakerLicensePublicKey[32] = {\n    " +
              ", ".join(f"0x{b:02x}" for b in pub) + " };")
    elif a.cmd == "test-license":
        text = make_license(TEST_SEED, tier=a.tier, licensee="Test Licensee", email="test@example.invalid",
                            order="PM-TEST-0001", machines=a.machines, major=a.major, issued="2026-01-01", bind=a.bind)
        open(a.out, "w").write(text)
    elif a.cmd == "selftest":
        assert _pure_public(TEST_SEED) == public_from_seed(TEST_SEED), "public key mismatch"
        m = b"hello"; assert _pure_sign(TEST_SEED, m) == sign(TEST_SEED, m), "signature mismatch"
        print("ok; TEST public key:", public_from_seed(TEST_SEED).hex())

if __name__ == "__main__":
    main()
