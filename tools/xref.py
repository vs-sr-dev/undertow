"""Find code references (lui+addiu/ori pairs) to an address or to a string.
usage: python xref.py <file> <base_hex> <addr_hex | "string">"""
import struct, sys
data = open(sys.argv[1], "rb").read()
base = int(sys.argv[2], 16)
tgt = sys.argv[3]
try:
    targets = {int(tgt, 16)}
except ValueError:
    targets = set()
    p = -1
    while True:
        p = data.find(tgt.encode() + b"\0", p + 1)
        if p < 0:
            break
        targets.add(base + p)
print("targets:", [hex(t) for t in sorted(targets)])
n = len(data) // 4
w = struct.unpack(">%dI" % n, data[:n * 4])
for i in range(n):
    if w[i] >> 26 != 0x0F:
        continue
    rt, hi = (w[i] >> 16) & 31, w[i] & 0xFFFF
    for j in range(i + 1, min(i + 12, n)):
        v = w[j]
        op = v >> 26
        if (v >> 21) & 31 == rt and op in (0x09, 0x0D):
            lo = v & 0xFFFF
            a = (hi << 16) | lo if op == 0x0D else ((hi << 16) + (lo - 0x10000 if lo & 0x8000 else lo)) & 0xFFFFFFFF
            if a in targets or (a | 0x20000000) in targets or (a & 0xDFFFFFFF) in targets:
                print("  ref at 0x%08x -> 0x%08x" % (base + j * 4, a))
            break
