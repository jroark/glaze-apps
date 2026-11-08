#!/usr/bin/env python3
"""Draw Minesweeper's art to res/*.gzi and its icon (Pillow; see tools/gzart.py).

    python3 packages/minesweeper/art/draw_art.py [--sheet out.png]

For both cell sizes (26 px Beginner, 15 px Intermediate): glossy raised
tiles in two checker tones, a recessed open cell and its red "boom" twin, a
spiked shaded mine and a waving flag. Plus the four faces of the reset button.
"""
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "tools"))
import gzart as A  # noqa: E402
from gzart import Image  # noqa: E402

SIZES = (26, 15)
TILE_A, TILE_B = (92, 140, 210), (80, 126, 196)


def covered(n, base):
    return A.bevel_tile(n, n, base, radius=max(1.5, n / 8), depth=max(1.6, n / 9), light=0.45, shade=0.5)


def opened(n, base=(222, 226, 232)):
    im = Image.new("RGBA", (n, n), base + (255,))
    px = im.load()
    for y in range(n):
        for x in range(n):
            k = 0
            if x == 0 or y == 0:
                k = -40                                  # recessed: dark top/left lip
            elif x == 1 or y == 1:
                k = -16
            elif x == n - 1 or y == n - 1:
                k = 10
            r, g, b, a = px[x, y]
            px[x, y] = (max(0, min(255, r + k)), max(0, min(255, g + k)), max(0, min(255, b + k)), a)
    return im


def mine(n):
    im, d = A.big(n, n)
    c, r = n / 2, n * 0.27
    for k in range(8):
        a = k * math.pi / 4
        d.line(A.P([(c, c), (c + math.cos(a) * r * 1.55, c + math.sin(a) * r * 1.55)]), fill=(20, 22, 28, 255),
               width=max(1, int(n / 12 * A.SS)))
    A.sphere(im, c, c, r, (10, 10, 14), (110, 116, 130), shine=12, rim=0.3)
    return A.shadow(A.down(im), offset=(1, 1), blur=0.6, alpha=90)


def flag(n):
    im, d = A.big(n, n)
    px0 = n * 0.42
    top, h = n * 0.16, n * 0.7
    d.rectangle(A.S(px0, top, px0 + max(1.2, n / 16), top + h), fill=(40, 40, 46, 255))
    d.rounded_rectangle(A.S(n * 0.26, top + h - n * 0.07, n * 0.7, top + h + n * 0.04), radius=A.SS,
                        fill=(40, 40, 46, 255))
    pts = []
    fw, fh = n * 0.4, n * 0.34
    for i in range(9):
        t = i / 8
        pts.append((px0 + t * fw, top + math.sin(t * math.pi) * n * 0.04))
    for i in range(8, -1, -1):
        t = i / 8
        pts.append((px0 + t * fw, top + fh + math.sin(t * math.pi) * n * 0.04))
    d.polygon(A.P(pts), fill=(230, 40, 40, 255))
    d.polygon(A.P(pts[:9] + [(px0 + fw, top + fh * 0.45), (px0, top + fh * 0.45)]), fill=(255, 110, 100, 255))
    return A.shadow(A.down(im), offset=(1, 1), blur=0.6, alpha=90)


def face(kind):
    n = 24
    im, d = A.big(n, n)
    A.sphere(im, 12, 12, 10.6, (210, 140, 10), (255, 240, 120), shine=14, rim=0.6)
    k = (20, 16, 10, 255)
    w = int(1.3 * A.SS)
    if kind == "dead":
        for ex in (8.2, 15.8):
            d.line(A.P([(ex - 1.8, 7.2), (ex + 1.8, 10.8)]), fill=k, width=w)
            d.line(A.P([(ex - 1.8, 10.8), (ex + 1.8, 7.2)]), fill=k, width=w)
        d.arc(A.S(7.5, 14.5, 16.5, 21), 200, 340, fill=k, width=w)
    elif kind == "cool":
        d.rounded_rectangle(A.S(4.6, 7.2, 11.2, 11.4), radius=A.SS, fill=k)
        d.rounded_rectangle(A.S(12.8, 7.2, 19.4, 11.4), radius=A.SS, fill=k)
        d.line(A.P([(11, 8), (13, 8)]), fill=k, width=w)
        d.arc(A.S(7, 10, 17, 18.5), 20, 160, fill=k, width=w)
    elif kind == "wow":
        d.ellipse(A.S(7, 7, 10, 10.4), fill=k)
        d.ellipse(A.S(14, 7, 17, 10.4), fill=k)
        d.ellipse(A.S(9.8, 14, 14.2, 18.4), fill=k)
    else:
        d.ellipse(A.S(7.2, 7.2, 9.8, 10.6), fill=k)
        d.ellipse(A.S(14.2, 7.2, 16.8, 10.6), fill=k)
        d.arc(A.S(7, 10, 17, 18.5), 20, 160, fill=k, width=w)
    return A.down(im)


def icon():
    im = A.icon_tile((96, 140, 206), (40, 70, 130))
    for r in range(3):
        for c in range(3):
            x, y = 1 + c * 14, 1 + r * 14
            if (r, c) in ((1, 1), (2, 0), (2, 1)):
                im.alpha_composite(opened(14), (x, y))
            else:
                im.alpha_composite(covered(14, TILE_A if (r + c) & 1 else TILE_B), (x, y))
    d = A.ImageDraw.Draw(im)
    f = A.font(11)
    d.text((8, 36.5), "1", font=f, fill=(30, 60, 220, 255), anchor="mm")
    d.text((22, 36.5), "2", font=f, fill=(20, 130, 40, 255), anchor="mm")
    im.alpha_composite(mine(20), (13, 13))
    im.alpha_composite(flag(16), (28, 0))
    return A.icon_finish(im)


def main():
    art = A.Art(__file__)
    for n in SIZES:
        art.write("tile_a%d.gzi" % n, covered(n, TILE_A))
        art.write("tile_b%d.gzi" % n, covered(n, TILE_B))
        art.write("open%d.gzi" % n, opened(n))
        art.write("boom%d.gzi" % n, opened(n, (236, 70, 60)))
        art.write("mine%d.gzi" % n, mine(n))
        art.write("flag%d.gzi" % n, flag(n))
    for k in ("smile", "wow", "dead", "cool"):
        art.write("face_%s.gzi" % k, face(k))
    art.icon(icon())
    art.finish()


if __name__ == "__main__":
    main()
