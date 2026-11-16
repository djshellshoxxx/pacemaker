#!/usr/bin/env python3
"""osc_tool.py - stdlib-only OSC monitor and Pacemaker output simulator, for testing receivers.

  python3 osc_tool.py listen [--port 9000]
      Prints every OSC message/bundle received (what a host or lighting app would see).
  python3 osc_tool.py simulate [--host 127.0.0.1] [--port 9000] [--bpm 120] [--ramp-to 126] [--bars 8] [--root /pacemaker]
      Sends the ES-02 section 4 messages: /tempo, /beat (bundled, NTP timetag), /downbeat, /phase,
      /confidence, /state, so you can test Reaper, Bitwig, QLab, TouchDesigner without a drummer.
  python3 osc_tool.py send --host H --port P /address [i:1 f:0.5 s:text ...]
      One-off message (e.g. QLab: send --port 53000 /go).
"""
import argparse, socket, struct, sys, time

def pad(b): b += b"\0"; return b + b"\0" * ((4 - len(b) % 4) % 4)

def msg(addr, *args):
    tags, body = ",", b""
    for a in args:
        if isinstance(a, bool) or isinstance(a, int): tags += "i"; body += struct.pack(">i", a)
        elif isinstance(a, float): tags += "f"; body += struct.pack(">f", a)
        else: tags += "s"; body += pad(str(a).encode())
    return pad(addr.encode()) + pad(tags.encode()) + body

def ntp(unix_s):
    sec = int(unix_s) + 2208988800
    frac = int((unix_s % 1) * (1 << 32))
    return struct.pack(">II", sec & 0xFFFFFFFF, frac & 0xFFFFFFFF)

def bundle(unix_s, msgs):
    return b"#bundle\0" + ntp(unix_s) + b"".join(struct.pack(">i", len(m)) + m for m in msgs)

def parse(data, off=0, end=None):
    end = len(data) if end is None else end
    def s(o):
        z = data.index(b"\0", o); return data[o:z].decode("utf8", "replace"), (z + 4) // 4 * 4
    if data[off:off + 8] == b"#bundle\0":
        sec, frac = struct.unpack(">II", data[off + 8:off + 16])
        out = [("bundle", sec - 2208988800 + frac / 2**32)]
        p = off + 16
        while p < end:
            (n,) = struct.unpack(">i", data[p:p + 4]); p += 4
            out += parse(data, p, p + n); p += n
        return out
    a, p = s(off); t, p = s(p); vals = []
    for c in t[1:]:
        if c == "i": vals.append(struct.unpack(">i", data[p:p + 4])[0]); p += 4
        elif c == "f": vals.append(round(struct.unpack(">f", data[p:p + 4])[0], 4)); p += 4
        elif c == "s": v, p = s(p); vals.append(v)
        else: vals.append("?" + c); break
    return [(a, vals)]

def cmd_listen(a):
    so = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    so.bind(("0.0.0.0", a.port)); print("listening on UDP", a.port)
    while True:
        d, peer = so.recvfrom(65535)
        try: print(f"{time.time():.3f} {peer[0]}", parse(d))
        except Exception as e: print("undecodable packet:", e, d[:32])

def cmd_simulate(a):
    so = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); dest = (a.host, a.port)
    R, bpb = a.root, 4
    total = a.bars * bpb; t0 = time.time(); beat = 0; nxt = t0 + 0.2
    so.sendto(msg(R + "/state", "LOCKED"), dest)
    while beat < total:
        bpm = a.bpm + (a.ramp_to - a.bpm) * beat / total if a.ramp_to else a.bpm
        per = 60.0 / bpm
        so.sendto(msg(R + "/tempo", float(bpm)), dest)
        so.sendto(msg(R + "/confidence", 0.9), dest)
        bar, b = beat // bpb + 1, beat % bpb + 1
        ms = [msg(R + "/beat", bar, b, bpb)] + ([msg(R + "/downbeat", bar)] if b == 1 else [])
        time.sleep(max(0, nxt - 0.01 - time.time()))
        so.sendto(bundle(nxt, ms), dest)
        print(f"bar {bar} beat {b} bpm {bpm:.2f}")
        nxt += per; beat += 1

def cmd_send(a):
    args = []
    for x in a.args:
        k, _, v = x.partition(":")
        args.append(int(v) if k == "i" else float(v) if k == "f" else v)
    socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(msg(a.address, *args), (a.host, a.port))

p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
sp = p.add_subparsers(dest="cmd", required=True)
l = sp.add_parser("listen"); l.add_argument("--port", type=int, default=9000); l.set_defaults(f=cmd_listen)
s = sp.add_parser("simulate"); s.add_argument("--host", default="127.0.0.1"); s.add_argument("--port", type=int, default=9000)
s.add_argument("--bpm", type=float, default=120.0); s.add_argument("--ramp-to", type=float, default=0.0)
s.add_argument("--bars", type=int, default=8); s.add_argument("--root", default="/pacemaker"); s.set_defaults(f=cmd_simulate)
n = sp.add_parser("send"); n.add_argument("--host", default="127.0.0.1"); n.add_argument("--port", type=int, required=True)
n.add_argument("address"); n.add_argument("args", nargs="*"); n.set_defaults(f=cmd_send)
a = p.parse_args(); a.f(a)
