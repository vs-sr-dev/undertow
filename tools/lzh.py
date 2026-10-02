"""Decoder for ZAPiT/Mediamatics .lzh blobs: 8-byte header (u32 LE unpacked size,
u32 LE packed size) followed by an LHA -lh5-style static-Huffman LZSS stream."""
import struct

NC = 256 + 256 - 2   # literals + match lengths (MAXMATCH 256, THRESHOLD 3)
NT = 19              # 16 + 3
TBIT = 5
CBIT = 9


class BitReader:
    def __init__(self, data):
        self.data = data
        self.pos = 0        # bit position

    def bits(self, n):
        if n == 0:
            return 0
        v = 0
        for _ in range(n):
            byte = self.pos >> 3
            b = self.data[byte] if byte < len(self.data) else 0
            v = (v << 1) | ((b >> (7 - (self.pos & 7))) & 1)
            self.pos += 1
        return v

    def peek(self, n):
        p = self.pos
        v = self.bits(n)
        self.pos = p
        return v


def make_table(lengths):
    """Canonical Huffman: returns dict (len, code) -> symbol, plus max len."""
    maxlen = max(lengths) if lengths else 0
    count = [0] * 17
    for l in lengths:
        if l:
            count[l] += 1
    code = 0
    start = [0] * 18
    for l in range(1, 17):
        start[l] = code
        code = (code + count[l]) << 1
    table = {}
    nxt = start[:]
    for sym, l in enumerate(lengths):
        if l:
            table[(l, nxt[l])] = sym
            nxt[l] += 1
    return table, maxlen


def decode_sym(br, table, maxlen):
    code = 0
    for l in range(1, maxlen + 1):
        code = (code << 1) | br.bits(1)
        s = table.get((l, code))
        if s is not None:
            return s
    raise ValueError("bad huffman code at bit %d" % br.pos)


def read_pt_len(br, nn, nbit, special):
    n = br.bits(nbit)
    if n == 0:
        c = br.bits(nbit)
        return None, c
    lens = [0] * nn
    i = 0
    while i < n:
        c = br.bits(3)
        if c == 7:
            while br.bits(1):
                c += 1
        lens[i] = c
        i += 1
        if i == special:
            z = br.bits(2)
            for _ in range(z):
                lens[i] = 0
                i += 1
    return lens, None


def read_c_len(br, pt):
    n = br.bits(CBIT)
    if n == 0:
        c = br.bits(CBIT)
        return None, c
    lens = [0] * NC
    i = 0
    while i < n:
        c = pt()
        if c <= 2:
            if c == 0:
                c = 1
            elif c == 1:
                c = br.bits(4) + 3
            else:
                c = br.bits(CBIT) + 20
            for _ in range(c):
                lens[i] = 0
                i += 1
        else:
            lens[i] = c - 2
            i += 1
    return lens, None


def decompress_lh(stream, outsize, dicbit=13):
    np_ = dicbit + 1
    pbit = 4 if dicbit <= 13 else 5
    br = BitReader(stream)
    out = bytearray()
    while len(out) < outsize:
        blocksize = br.bits(16)
        # temp (pt) table for c lengths
        ptl, ptconst = read_pt_len(br, NT, TBIT, 3)
        if ptl is None:
            pt = lambda: ptconst
        else:
            t, m = make_table(ptl)
            pt = lambda t=t, m=m: decode_sym(br, t, m)
        cl, cconst = read_c_len(br, pt)
        if cl is None:
            dc = lambda: cconst
        else:
            t, m = make_table(cl)
            dc = lambda t=t, m=m: decode_sym(br, t, m)
        pl, pconst = read_pt_len(br, np_, pbit, -1)
        if pl is None:
            dp = lambda: pconst
        else:
            t, m = make_table(pl)
            dp = lambda t=t, m=m: decode_sym(br, t, m)
        for _ in range(blocksize):
            c = dc()
            if c < 256:
                out.append(c)
            else:
                length = c - 256 + 3
                p = dp()
                if p > 1:
                    p = (1 << (p - 1)) + br.bits(p - 1)
                src = len(out) - p - 1
                for k in range(length):
                    out.append(out[src + k])
            if len(out) >= outsize:
                break
    return bytes(out[:outsize])


def unpack(blob, dicbit=13):
    unpacked, packed = struct.unpack("<II", blob[:8])
    return decompress_lh(blob[8:8 + packed], unpacked, dicbit)
