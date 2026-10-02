"""Annotated MIPS BE disassembler for Game Wave binaries (capstone-based).

usage: python mdis.py <file> <load_base_hex> <start_hex> [count]
       start is a virtual address. Annotates lui-pair constants with the string
       they point to (if inside the image) and known MMIO regions.
"""
import re
import sys

import capstone

MMIO = [
    (0xA8000000, 0xA8100000, "SoC regs"),
    (0xA5000000, 0xA5800000, "dev@05xx"),
    (0xBFC00000, 0xBFE00000, "flash"),
]


class Image:
    def __init__(self, data, base):
        self.data = data
        self.base = base

    def contains(self, addr):
        a = addr & 0x9FFFFFFF if addr >= 0xA0000000 else addr  # kseg1 -> kseg0
        return self.base <= a < self.base + len(self.data)

    def cstring(self, addr, maxlen=60):
        a = addr & 0x9FFFFFFF if addr >= 0xA0000000 else addr
        off = a - self.base
        m = re.match(rb"[\x20-\x7e\t\n\r]{3,}", self.data[off:off + maxlen])
        if m and (off + len(m.group()) < len(self.data)) and self.data[off + len(m.group())] in (0, 0x0a):
            return m.group().decode()
        return None


def annotate(img, addr):
    for lo, hi, name in MMIO:
        if lo <= addr < hi:
            return name
    if img.contains(addr):
        s = img.cstring(addr)
        if s is not None:
            return repr(s)
    return None


def disasm(img, start, count):
    md = capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS32 + capstone.CS_MODE_BIG_ENDIAN)
    md.detail = True
    md.skipdata = True
    off = start - img.base
    regs = {}
    out = []
    for ins in md.disasm(img.data[off:off + count * 4], start):
        note = ""
        word = int.from_bytes(ins.bytes, "big")
        op = word >> 26
        rs, rt, imm = (word >> 21) & 31, (word >> 16) & 31, word & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        if op == 0x0F:
            regs[rt] = imm << 16
        elif op in (0x09, 0x0D) and rs in regs:
            val = (regs[rs] | imm) if op == 0x0D else (regs[rs] + simm) & 0xFFFFFFFF
            regs[rt] = val
            a = annotate(img, val)
            note = "; =0x%08x %s" % (val, a or "")
        elif op in (0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2B) and rs in regs:
            val = (regs[rs] + simm) & 0xFFFFFFFF
            a = annotate(img, val)
            note = "; [0x%08x] %s" % (val, a or "")
        if word == 0x42000010:
            ins_txt = "rfe"
        else:
            ins_txt = "%s %s" % (ins.mnemonic, ins.op_str)
        out.append("%08x: %08x  %-40s %s" % (ins.address, word, ins_txt, note))
        if ins.mnemonic in ("jr", "j", "b", "jal", "jalr"):
            regs = {}
    return out


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    data = open(sys.argv[1], "rb").read()
    img = Image(data, int(sys.argv[2], 16))
    count = int(sys.argv[4]) if len(sys.argv) > 4 else 64
    print("\n".join(disasm(img, int(sys.argv[3], 16), count)))


if __name__ == "__main__":
    main()
