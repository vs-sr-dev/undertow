"""List the engine API used by .zbc scripts: globals read but never written, and lib.func calls.
usage: python zbcapi.py <zbc>..."""
import sys, collections
sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
import zbcdis as z

calls = collections.defaultdict(collections.Counter)
read_all, written_all = collections.Counter(), set()
for path in sys.argv[1:]:
    tag = path.replace("\\", "/").split("/")[1]
    h, f, _ = z.parse(z.load(path))
    for p in z.walk(f):
        reg = {}
        for ins in p.code:
            op, a = ins & 0x3F, (ins >> 24) & 0xFF
            b, c, bx = (ins >> 15) & 0x1FF, (ins >> 6) & 0x1FF, (ins >> 6) & 0x3FFFF
            if op == 5:   # GETGLOBAL
                g = p.k[bx]; reg[a] = g; read_all[g] += 1
            elif op == 7:
                written_all.add(p.k[bx]); reg.pop(a, None)
            elif op in (6, 11) and b in reg and c >= z.MAXSTACK:   # GETTABLE / SELF
                key = p.k[c - z.MAXSTACK]
                calls[reg[b]][key] += 1
                if op == 6:
                    reg[a] = reg[b] + "." + str(key)
                else:
                    reg.pop(a, None)
            elif op not in (1,):
                reg.pop(a, None)
engine = sorted(g for g in read_all if g not in written_all)
print("globals provided by engine/stdlib:", engine)
for lib in sorted(calls):
    if lib in engine and "." not in lib:
        print("  %-8s %s" % (lib, ", ".join("%s(%d)" % kv for kv in sorted(calls[lib].items()))))
