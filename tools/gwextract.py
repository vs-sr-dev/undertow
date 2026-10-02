"""Unpack Game Wave binaries (*.cat.bin, upgrade.bin, decompressed app_sdram images).

For every 'cheese' file table found in the input:
  - writes each entry to <out>/<table>/<name>
  - decompresses .lzh entries (and recurses into the result)
  - converts .zbm images to PNG next to them

usage: python gwextract.py <input.bin> <outdir>
"""
import os
import sys

import gwfmt
import lzh

try:
    from PIL import Image
except ImportError:
    Image = None


def save_png(path, blob):
    if Image is None:
        return
    try:
        w, h, rgba = gwfmt.zbm_to_rgba(blob)
    except Exception as ex:  # unknown variants are reported, not fatal
        print("   ! png failed for %s: %s" % (os.path.basename(path), ex))
        return
    Image.frombytes("RGBA", (w, h), rgba).save(path[:-4] + ".png")


def extract(data, outdir, depth=0):
    pad = "  " * depth
    pos = gwfmt.find_cheese(data)
    if pos < 0:
        print(pad + "no file table")
        return
    print(pad + "code/prefix: 0x%x bytes, file table at 0x%x" % (pos, pos))
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "_prefix.bin"), "wb") as f:
        f.write(data[:pos])
    for name, blob in gwfmt.parse_cheese(data, pos):
        path = os.path.join(outdir, *name.split("\\"))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(blob)
        print(pad + "  %-44s %8d" % (name, len(blob)))
        if name.lower().endswith(".lzh"):
            raw = lzh.unpack(blob)
            with open(path[:-4], "wb") as f:
                f.write(raw)
            print(pad + "    -> unpacked %d bytes" % len(raw))
            if gwfmt.find_cheese(raw) >= 0:
                extract(raw, path[:-4] + ".d", depth + 2)
        elif name.lower().endswith(".zbm"):
            save_png(path, blob)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    with open(sys.argv[1], "rb") as f:
        data = f.read()
    extract(data, sys.argv[2])


if __name__ == "__main__":
    main()
