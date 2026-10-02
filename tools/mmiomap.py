"""Associate SoC MMIO blocks with nearby debug strings, per function.
usage: python mmiomap.py <file> <base_hex> <text_end_hex>"""
import re, struct, sys, collections
data = open(sys.argv[1], "rb").read(); base = int(sys.argv[2], 16); tend = int(sys.argv[3], 16)
n = (tend - base) // 4
w = struct.unpack(">%dI" % n, data[:n * 4])

def cstr(a):
    a &= 0x9FFFFFFF
    o = a - base
    if not 0 <= o < len(data): return None
    m = re.match(rb"[\x20-\x7e]{4,}", data[o:o + 80])
    return m.group().decode() if m and data[o + len(m.group())] in (0, 10) else None

# function starts: 'addiu sp,sp,-N'
starts = [i for i, v in enumerate(w) if (v & 0xFFFF8000) == 0x27BD8000]
starts.append(n)
blocks = collections.defaultdict(lambda: {"funcs": 0, "strings": collections.Counter()})
for fi in range(len(starts) - 1):
    a, b = starts[fi], starts[fi + 1]
    regs = {}; mm = set(); ss = set()
    for i in range(a, b):
        v = w[i]; op = v >> 26; rs = (v >> 21) & 31; rt = (v >> 16) & 31; imm = v & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        val = None
        if op == 0x0F: regs[rt] = imm << 16; continue
        if op in (0x09, 0x0D) and rs in regs:
            val = (regs[rs] | imm) if op == 0x0D else (regs[rs] + simm) & 0xFFFFFFFF
            regs[rt] = val
        elif op in (0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2B) and rs in regs:
            val = (regs[rs] + simm) & 0xFFFFFFFF
        if val is None: continue
        if 0xA8000000 <= val < 0xA8100000 or 0xA5000000 <= val < 0xA5800000 or 0xA0000000 <= val < 0xA0010000:
            mm.add(val & 0xFFFFF000 if val < 0xA8000000 else val & 0xFFFF0000 | ((val >> 12) & 0xF) << 12)
        else:
            s = cstr(val)
            if s: ss.add(s)
    for m in mm:
        blocks[m]["funcs"] += 1
        for s in ss: blocks[m]["strings"][s] += 1
for m in sorted(blocks):
    b = blocks[m]
    hints = [s for s, _ in b["strings"].most_common(60) if re.search(r"\.c|::|\[|_", s)][:6]
    print("%08x  funcs=%-3d %s" % (m, b["funcs"], " | ".join(h[:48] for h in hints)))
