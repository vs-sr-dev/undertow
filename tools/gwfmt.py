"""Game Wave container/asset helpers: 'cheese' file tables, .zbm images, .zwf audio."""
import struct
import zlib

CHEESE_MAGIC = bytes.fromhex("1234567887654321")


def find_cheese(data, start=0):
    return data.find(CHEESE_MAGIC, start)


def parse_cheese(data, base):
    """Yield (name, bytes) for a cheese table at `base` (offsets relative to base, BE)."""
    count = struct.unpack(">I", data[base + 8:base + 12])[0]
    p = base + 12
    for _ in range(count):
        name = data[p:p + 40].split(b"\0")[0].decode("latin1")
        off, size = struct.unpack(">II", data[p + 40:p + 48])
        p += 48
        yield name, data[base + off:base + off + size]


def zbm_header(data):
    f = struct.unpack("<12I", data[:48])
    return dict(version=f[0], type=f[1], fmt=f[2], bpp=f[3], width=f[4], height=f[5],
                packed=f[9], unpacked=f[10])


def zbm_fw_header(data):
    """Older/firmware .zbm variant: 0x2C-byte header, first word 0x10."""
    f = struct.unpack("<11I", data[:44])
    return dict(version=f[0], width=f[1], height=f[2], unk3=f[3], key=f[4], unk5=f[5],
                packed=f[6], unpacked=f[7], unk8=f[8], bpp=2, hdr=44)


def zbm_any_header(data):
    if struct.unpack("<I", data[:4])[0] == 0x10:
        return zbm_fw_header(data)
    h = zbm_header(data)
    h["hdr"] = 48
    return h


def zbm_to_rgba(data):
    """Decode .zbm to (width, height, RGBA bytes). Handles 2bpp YCbCr 4633 only for now."""
    h = zbm_any_header(data)
    raw = zlib.decompress(data[h["hdr"]:h["hdr"] + h["packed"]])
    w, hh = h["width"], h["height"]
    if h["bpp"] != 2:
        raise NotImplementedError("zbm bpp=%d" % h["bpp"])
    if h["hdr"] == 44:
        return w, hh, _ayuv4444_le(raw, w * hh)
    n = w * hh
    px = [0] * n
    for i in range(0, n - 1, 2):            # pixel pairs are swapped
        px[i + 1] = (raw[i * 2] << 8) | raw[i * 2 + 1]
        px[i] = (raw[i * 2 + 2] << 8) | raw[i * 2 + 3]
    if n & 1:
        px[n - 1] = (raw[(n - 1) * 2] << 8) | raw[(n - 1) * 2 + 1]
    out = bytearray(n * 4)
    for i, v in enumerate(px):
        cr = (v & 7) << 5
        cb = ((v >> 3) & 7) << 5
        y = ((v >> 6) & 0x3F) << 2
        a = ((v >> 12) & 0xF) * 17
        cb1, cr1 = cb - 128, cr - 128
        r = y + (45 * cr1) // 32
        g = y - (11 * cb1 + 23 * cr1) // 32
        b = y + (113 * cb1) // 64
        out[i * 4:i * 4 + 4] = bytes((max(0, min(255, r)), max(0, min(255, g)),
                                      max(0, min(255, b)), a))
    return w, hh, bytes(out)


def _ycc_to_rgb(y, cb, cr):
    cb -= 128
    cr -= 128
    r = y + (359 * cr) // 256
    g = y - (88 * cb + 183 * cr) // 256
    b = y + (454 * cb) // 256
    return max(0, min(255, r)), max(0, min(255, g)), max(0, min(255, b))


def _ayuv4444_le(raw, n):
    """Firmware OSD format: u16 LE, A[15:12] Y[11:8] Cb[7:4] Cr[3:0]."""
    out = bytearray(n * 4)
    for i in range(n):
        v = raw[i * 2] | (raw[i * 2 + 1] << 8)
        a = (v >> 12) * 17
        y = ((v >> 8) & 15) * 17
        cb = ((v >> 4) & 15) * 16 + 8
        cr = (v & 15) * 16 + 8
        out[i * 4:i * 4 + 4] = bytes(_ycc_to_rgb(y, cb, cr) + (a,))
    return bytes(out)
