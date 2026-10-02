"""Find luaL_reg tables ({char* name, lua_CFunction f}, NULL-terminated) in a MIPS BE image.
usage: python luaregs.py <image> <base_hex>"""
import re, struct, sys
d = open(sys.argv[1], "rb").read(); base = int(sys.argv[2], 16)
n = len(d) // 4
w = struct.unpack(">%dI" % n, d[:n * 4])
def s_at(a):
    o = a - base
    if not 0 <= o < len(d): return None
    m = re.match(rb"[A-Za-z_][A-Za-z0-9_]{1,40}\x00", d[o:o + 42])
    return m.group()[:-1].decode() if m else None
def is_code(a): return base <= a < base + len(d) and a % 4 == 0
i = 0
tables = []
while i < n - 1:
    j = i; ents = []
    while j < n - 1 and s_at(w[j]) and is_code(w[j + 1]):
        ents.append((s_at(w[j]), w[j + 1])); j += 2
    if len(ents) >= 2 and j < n - 1 and w[j] == 0:
        tables.append((base + i * 4, ents)); i = j
    i += 1
for addr, ents in tables:
    print("table @%08x (%d): %s" % (addr, len(ents), ", ".join("%s=%08x" % e for e in ents)))
