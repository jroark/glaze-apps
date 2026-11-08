#!/usr/bin/env python3
"""Draw Fifteen's art to res/*.gzi and its icon (Pillow; see tools/gzart.py).

    python3 packages/fifteen/art/draw_art.py [--sheet out.png]

Maple tiles with grain and a bevel (four grain variants), the walnut tray
they slide in, and a linen backdrop tile. main.c engraves the numbers.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "tools"))
import gzart as A  # noqa: E402
from gzart import Image  # noqa: E402

T = 49


def tile(seed, size=T):
    small = size < 30
    r, bw = (3, 1) if small else (5, 2)
    im = A.wood(size, size, seed, light=(240, 206, 150), dark=(184, 138, 86)).convert("RGBA")
    m, d = A.big(size, size)
    d.rounded_rectangle(A.S(0, 0, size, size), radius=r * A.SS, fill=(255, 255, 255, 255))
    mask = A.down(m).split()[3]
    # bevel: light top-left edge, dark bottom-right edge
    b, bd = A.big(size, size)
    bd.rounded_rectangle(A.S(bw, bw, size, size), radius=r * A.SS, fill=(96, 60, 28, 170))
    bd.rounded_rectangle(A.S(0, 0, size - bw, size - bw), radius=r * A.SS, fill=(255, 250, 230, 150))
    hole, hd = A.big(size, size)
    hd.rounded_rectangle(A.S(bw, bw, size - bw, size - bw), radius=max(1, r - 1) * A.SS, fill=(255, 255, 255, 255))
    ring = A.down(b)
    inside = A.Image.eval(A.down(hole).split()[3], lambda v: 255 - v)
    ring.putalpha(A.ImageChops.multiply(ring.split()[3], inside))      # keep only the edge band
    im.alpha_composite(ring)
    im.putalpha(mask)
    return im


def tray():
    w = h = 212
    im = A.wood(w, h, 77, light=(110, 70, 44), dark=(60, 36, 22), grain=4).convert("RGBA")
    m, d = A.big(w, h)
    d.rounded_rectangle(A.S(0, 0, w, h), radius=10 * A.SS, fill=(255, 255, 255, 255))
    mask = A.down(m).split()[3]
    e, ed = A.big(w, h)
    ed.rounded_rectangle(A.S(0.5, 0.5, w - 0.5, h - 0.5), radius=10 * A.SS, outline=(200, 150, 110, 120), width=A.SS)
    ed.rounded_rectangle(A.S(4, 4, w - 4, h - 4), radius=6 * A.SS, fill=(30, 18, 12, 255))
    ed.rounded_rectangle(A.S(4, 4, w - 4, h - 4), radius=6 * A.SS, outline=(0, 0, 0, 200), width=2 * A.SS)
    im.alpha_composite(A.down(e))
    im.putalpha(mask)
    return im


def linen():
    n = A.fbm(32, 32, [8, 4, 2], 12)
    im = Image.new("RGB", (32, 32))
    px = im.load()
    for y in range(32):
        for x in range(32):
            weave = ((x % 2) ^ (y % 2)) * 6
            px[x, y] = A.mix((206, 212, 222), (232, 236, 242), n[y][x] + weave / 40)
    return im


def icon():
    im = A.icon_tile((92, 60, 38), (52, 32, 20))
    f = A.font(15)
    for i, num in enumerate(["1", "2", "3", ""]):
        x, y = 4 + (i % 2) * 19, 4 + (i // 2) * 19
        if not num:
            continue
        t = tile(i + 3, 17)
        d = A.ImageDraw.Draw(t)
        d.text((8.5, 9.5), num, font=f, fill=(255, 240, 210, 160), anchor="mm")
        d.text((8.5, 8.5), num, font=f, fill=(76, 48, 26, 255), anchor="mm")
        im.alpha_composite(A.shadow(t, offset=(1, 1), blur=0.6, alpha=140), (x - 2, y - 2))
    return A.icon_finish(im)


def main():
    art = A.Art(__file__)
    for i in range(4):
        art.write("tile%d.gzi" % i, tile(i + 1))
    art.write("tray.gzi", tray())
    art.write("linen.gzi", linen(), alpha=False)
    art.icon(icon())
    art.finish()


if __name__ == "__main__":
    main()
