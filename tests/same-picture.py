#!/usr/bin/env python3
"""Says whether two pictures, each a .tga (the test runners') or a binary .ppm
(XaviXEmu's own program's), show the same pixels. The colours are compared, and
the order of the rows is read from the file: the tests' TGA from the runner
and from Chimera's screenshots start at different corners.

  same-picture.py <a> <b>     exit 0 when they are the same, 1 when not
"""
import sys


def load(path):
    d = open(path, "rb").read()
    if d[:2] == b"P6":
        parts = d.split(None, 4)
        w, h = int(parts[1]), int(parts[2])
        px = parts[4]
        return w, h, [tuple(px[i:i + 3]) for i in range(0, w * h * 3, 3)]
    w, h = d[12] | d[13] << 8, d[14] | d[15] << 8
    bpp = d[16] // 8
    top_first = bool(d[17] & 0x20)
    rows = [d[18 + y * w * bpp:18 + (y + 1) * w * bpp] for y in range(h)]
    if not top_first:
        rows.reverse()
    px = []
    for row in rows:
        for x in range(w):
            b, g, r = row[x * bpp], row[x * bpp + 1], row[x * bpp + 2]
            px.append((r, g, b))
    return w, h, px


a, b = load(sys.argv[1]), load(sys.argv[2])
if a == b:
    sys.exit(0)
print("%dx%d against %dx%d, %d pixels differ" % (a[0], a[1], b[0], b[1],
      sum(1 for p, q in zip(a[2], b[2]) if p != q)) if a[:2] == b[:2] else "sizes differ")
sys.exit(1)
