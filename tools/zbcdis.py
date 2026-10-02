"""Parser/disassembler for ZAPiT .zbc bytecode (Lua 5.0.2 variant, int32 numbers).

usage: python zbcdis.py <file.zbc|unpacked> [--summary]
"""
import struct
import sys
import zlib

OPNAMES = ["MOVE", "LOADK", "LOADBOOL", "LOADNIL", "GETUPVAL", "GETGLOBAL", "GETTABLE",
           "SETGLOBAL", "SETUPVAL", "SETTABLE", "NEWTABLE", "SELF", "ADD", "SUB", "MUL",
           "DIV", "POW", "UNM", "NOT", "CONCAT", "JMP", "EQ", "LT", "LE", "TEST", "CALL",
           "TAILCALL", "RETURN", "FORLOOP", "TFORLOOP", "TFORPREP", "SETLIST", "SETLISTO",
           "CLOSE", "CLOSURE"]
MAXSTACK = 250       # RK(x): x >= MAXSTACK -> constant x-MAXSTACK
BX_BIAS = (1 << 17) - 1   # MAXARG_sBx


def load(path):
    data = open(path, "rb").read()
    if data[:4] == b"\x1bZCS":
        data = zlib.decompress(data[16:])
    return data


class Reader:
    def __init__(self, data):
        self.d = data
        self.p = 0
        self.e = "<"

    def byte(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def int(self):
        v = struct.unpack_from(self.e + "i", self.d, self.p)[0]
        self.p += 4
        return v

    def size(self):
        v = struct.unpack_from(self.e + "I", self.d, self.p)[0]
        self.p += 4
        return v

    def string(self):
        n = self.size()
        if n == 0:
            return None
        s = self.d[self.p:self.p + n - 1]
        self.p += n
        return s.decode("latin1")


class Proto:
    pass


def read_header(r):
    sig = r.d[:6]
    if sig != b"\x1bZBC\n\x1a":
        raise ValueError("bad signature %r" % sig)
    r.p = 6
    h = {"version": r.byte(), "extra": bytes(r.byte() for _ in range(3)), "endian": r.byte()}
    h["sizes"] = bytes(r.byte() for _ in range(8))
    if h["sizes"] != b"\x04\x04\x04\x06\x08\x09\x09\x04":
        raise ValueError("unexpected sizes %s" % h["sizes"].hex())
    r.e = "<" if h["endian"] == 1 else ">"
    h["test"] = r.int()
    if h["test"] != 31415926:
        raise ValueError("bad test number %d" % h["test"])
    return h


def read_function(r, parent_source):
    f = Proto()
    f.source = r.string() or parent_source
    f.line = r.int()
    f.nups, f.numparams, f.is_vararg, f.maxstack = r.byte(), r.byte(), r.byte(), r.byte()
    f.lineinfo = [r.int() for _ in range(r.int())]
    f.locvars = []
    for _ in range(r.int()):
        f.locvars.append((r.string(), r.int(), r.int()))
    f.upvalues = [r.string() for _ in range(r.int())]
    f.k = []
    for _ in range(r.int()):
        t = r.byte()
        if t == 3:
            f.k.append(r.int())
        elif t == 4:
            f.k.append(r.string())
        elif t == 0:
            f.k.append(None)
        else:
            raise ValueError("const type %d at 0x%x" % (t, r.p))
    f.p = [read_function(r, f.source) for _ in range(r.int())]
    f.code = [r.size() for _ in range(r.int())]
    return f


def parse(data):
    r = Reader(data)
    h = read_header(r)
    f = read_function(r, None)
    return h, f, r.p


def kstr(v):
    return repr(v) if isinstance(v, str) else str(v)


def rk(f, x):
    return "K(%s)" % kstr(f.k[x - MAXSTACK]) if x >= MAXSTACK else "R%d" % x


def dis_ins(f, pc, i):
    op = i & 0x3F
    c = (i >> 6) & 0x1FF
    b = (i >> 15) & 0x1FF
    a = (i >> 24) & 0xFF
    bx = (i >> 6) & 0x3FFFF
    sbx = bx - BX_BIAS
    name = OPNAMES[op] if op < len(OPNAMES) else "OP%d" % op
    if name in ("LOADK", "GETGLOBAL", "SETGLOBAL"):
        args = "R%d %s" % (a, kstr(f.k[bx]))
    elif name == "CLOSURE":
        args = "R%d proto[%d]" % (a, bx)
    elif name in ("JMP",):
        args = "-> %d" % (pc + 1 + sbx)
    elif name in ("FORLOOP", "TFORPREP"):
        args = "R%d -> %d" % (a, pc + 1 + sbx)
    elif name in ("GETTABLE",):
        args = "R%d R%d[%s]" % (a, b, rk(f, c))
    elif name in ("SETTABLE",):
        args = "R%d[%s] = %s" % (a, rk(f, b), rk(f, c))
    elif name == "SELF":
        args = "R%d R%d:%s" % (a, b, rk(f, c))
    elif name in ("ADD", "SUB", "MUL", "DIV", "POW", "EQ", "LT", "LE"):
        args = "%d %s %s" % (a, rk(f, b), rk(f, c))
    elif name in ("SETLIST", "SETLISTO"):
        args = "R%d %d" % (a, bx)
    else:
        args = "%d %d %d" % (a, b, c)
    return "%-10s %s" % (name, args)


def dump(f, path="main", out=None):
    out = out if out is not None else []
    out.append("\nfunction %s (line %d, %d params%s, %d upv, stack %d, %d ins, %d k)" % (
        path, f.line, f.numparams, ", vararg" if f.is_vararg else "", f.nups, f.maxstack,
        len(f.code), len(f.k)))
    for pc, ins in enumerate(f.code):
        ln = f.lineinfo[pc] if pc < len(f.lineinfo) else 0
        out.append("  %4d [%4d] %s" % (pc, ln, dis_ins(f, pc, ins)))
    for n, p in enumerate(f.p):
        dump(p, "%s/%d" % (path, n), out)
    return out


def walk(f):
    yield f
    for p in f.p:
        yield from walk(p)


def main():
    data = load(sys.argv[1])
    h, f, end = parse(data)
    print("header: version=%02x extra=%s endian=%d; parsed %d/%d bytes" % (
        h["version"], h["extra"].hex(), h["endian"], end, len(data)))
    if "--summary" in sys.argv:
        import collections
        ops = collections.Counter()
        globs = collections.Counter()
        nfun = 0
        for p in walk(f):
            nfun += 1
            for ins in p.code:
                op = ins & 0x3F
                ops[OPNAMES[op] if op < len(OPNAMES) else op] += 1
                if op in (5, 7):
                    globs[p.k[(ins >> 6) & 0x3FFFF]] += 1
        print("functions:", nfun)
        print("opcodes:", ops.most_common())
        print("globals:", globs.most_common(60))
    else:
        print("\n".join(dump(f)))


if __name__ == "__main__":
    main()
