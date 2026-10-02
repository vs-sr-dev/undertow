#include "zbm.h"

#include <stdlib.h>
#include <zlib.h>

static uint32_t rd32le(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint8_t clamp8(int v)
{
    return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v;
}

/* Full-range YCbCr -> RGB, matching the conversion that gives correct-looking assets. */
static void put_ycc(uint8_t *out, int y, int cb, int cr, int a)
{
    cb -= 128;
    cr -= 128;
    out[0] = clamp8(y + (45 * cr) / 32);
    out[1] = clamp8(y - (11 * cb + 23 * cr) / 32);
    out[2] = clamp8(y + (113 * cb) / 64);
    out[3] = (uint8_t)a;
}

uint8_t *zbm_decode(const uint8_t *data, size_t size, int *pw, int *ph)
{
    int fw = size >= 4 && rd32le(data) == 0x10;
    size_t hdr = fw ? 0x2C : 0x30;
    uint32_t w, h, packed, unpacked;
    uLongf rawlen;
    uint8_t *raw, *out;
    size_t i, n;

    if (size < hdr)
        return NULL;
    if (fw) {
        w = rd32le(data + 0x04);
        h = rd32le(data + 0x08);
        packed = rd32le(data + 0x18);
        unpacked = rd32le(data + 0x1C);
    } else {
        if (rd32le(data + 0x0C) != 2)   /* bytes per pixel */
            return NULL;
        w = rd32le(data + 0x10);
        h = rd32le(data + 0x14);
        packed = rd32le(data + 0x24);
        unpacked = rd32le(data + 0x28);
    }
    n = (size_t)w * h;
    if (!w || !h || unpacked < n * 2 || packed > size - hdr)
        return NULL;
    rawlen = unpacked;
    raw = malloc(unpacked);
    out = malloc(n * 4);
    if (!raw || !out || uncompress(raw, &rawlen, data + hdr, packed) != Z_OK) {
        free(raw);
        free(out);
        return NULL;
    }
    for (i = 0; i < n; i++) {
        unsigned v;
        if (fw) {
            v = raw[i * 2] | (raw[i * 2 + 1] << 8);
            put_ycc(out + i * 4, ((v >> 8) & 15) * 17, ((v >> 4) & 15) * 16 + 8,
                    (v & 15) * 16 + 8, (v >> 12) * 17);
        } else {
            /* pixel pairs are stored swapped: (p1, p0) as big-endian u16s */
            size_t j = (i ^ 1) < n ? (i ^ 1) : i;
            v = (raw[j * 2] << 8) | raw[j * 2 + 1];
            put_ycc(out + i * 4, ((v >> 6) & 63) << 2, ((v >> 3) & 7) << 5, (v & 7) << 5,
                    (v >> 12) * 17);
        }
    }
    free(raw);
    *pw = (int)w;
    *ph = (int)h;
    return out;
}
