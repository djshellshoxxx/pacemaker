"""Prints the hex fixture used in test_osc.lua (independent encoder, Python struct)."""
import struct
def pad(b): b += b"\0"; return b + b"\0" * ((4 - len(b) % 4) % 4)
print((pad(b"/pacemaker/tempo") + pad(b",f") + struct.pack(">f", 120.5)).hex())
