#!/usr/bin/env python3
"""Draw Snake's art to res/*.gzi (Pillow; see tools/gzart.py).

    python3 packages/snake/art/draw_art.py [--sheet out.png]

A mown-lawn tile, a shaded tube body in two stripe tones with joints, a head
for each direction, and an apple. All drawn here, procedurally.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "tools"))
import gzart as A  # noqa: E402
from gzart import Image  # noqa: E402

GREEN_D, GREEN_L = (30, 110, 30), (200, 255, 120)
STRIPE_D, STRIPE_L = (70, 90, 10), (230, 220, 90)


def lawn():
    """20x20: two cells each way in a mown checker, with noise and blades."""
    n = A.fbm(20, 20, [10, 5, 2], 3)
    im = Image.new("RGB", (20, 20))
    px = im.load()
    for y in range(20):
        for x in range(20):
            light = ((x // 10) + (y // 10)) % 2 == 0
            base = (24, 52, 34) if light else (20, 44, 30)
            px[x, y] = A.mix(base, (40, 76, 46), (n[y][x] - 0.35) * 0.8)
    return im


def segment(r, dark, lit):
    im, _ = A.big(10, 10)
    A.sphere(im, 5, 5, r, dark, lit, shine=10, rim=0.45)
    return A.down(im)


def joint(horizontal):
    """A short tube piece that joins two neighbouring segments."""
    w, h = (6, 9) if horizontal else (9, 6)
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = im.load()
    for y in range(h):
        for x in range(w):
            t = (y + 0.5) / h if horizontal else (x + 0.5) / w     # across the tube
            shade = 1 - abs(t - 0.38) * 1.9
            px[x, y] = A.mix(GREEN_D, GREEN_L, shade) + (255,)
    return im


def head(direction):
    im, d = A.big(12, 12)
    A.sphere(im, 6, 6, 5.9, (30, 110, 30), (210, 255, 140), shine=12, rim=0.45)
    # eyes look the way the snake moves: (dx, dy) and the perpendicular
    dx, dy = [(0, -1), (1, 0), (0, 1), (-1, 0)][direction]
    px_, py_ = -dy, dx
    for side in (-1, 1):
        ex = 6 + dx * 2.2 + px_ * side * 2.4
        ey = 6 + dy * 2.2 + py_ * side * 2.4
        d.ellipse(A.S(ex - 1.6, ey - 1.6, ex + 1.6, ey + 1.6), fill=(250, 252, 240, 255))
        d.ellipse(A.S(ex - 0.8 + dx * 0.6, ey - 0.8 + dy * 0.6, ex + 0.8 + dx * 0.6, ey + 0.8 + dy * 0.6),
                  fill=(16, 20, 16, 255))
    # nostrils
    for side in (-1, 1):
        nx = 6 + dx * 4.6 + px_ * side * 1.0
        ny = 6 + dy * 4.6 + py_ * side * 1.0
        d.ellipse(A.S(nx - 0.35, ny - 0.35, nx + 0.35, ny + 0.35), fill=(10, 50, 20, 255))
    return A.down(im)


def apple():
    im, d = A.big(12, 12)
    A.sphere(im, 6, 6.8, 4.8, (110, 12, 20), (255, 90, 90), shine=16, rim=0.5)
    d.line(A.P([(6, 3.2), (6.6, 0.8)]), fill=(90, 56, 30, 255), width=A.SS)
    d.ellipse(A.S(6.6, 0.6, 10.4, 2.8), fill=(70, 170, 60, 255))
    return A.shadow(A.down(im), offset=(1, 1), blur=0.8, alpha=120)


def icon():
    import math
    im = A.icon_tile((44, 104, 58), (14, 44, 24))
    im.alpha_composite(apple().resize((16, 16), A.Image.LANCZOS), (25, 27))
    body, d = A.big(A.ICON, A.ICON)
    pts = [(5 + t * 0.30, 19 + math.sin(t / 100 * 2 * math.pi * 1.15 + 0.4) * 8) for t in range(0, 101, 4)]
    for k, (x, y) in enumerate(pts[:-1]):
        rad = 2.4 + 1.8 * k / len(pts)
        dark, lit = (STRIPE_D, STRIPE_L) if (k // 3) % 2 else (GREEN_D, GREEN_L)
        A.sphere(body, x, y, rad, dark, lit, shine=10, rim=0.4)
    hx, hy = pts[-1][0] + 2, pts[-1][1]
    d.line(A.P([(hx + 4, hy + 1), (hx + 9, hy + 1)]), fill=(220, 30, 50, 255), width=A.SS)
    d.line(A.P([(hx + 9, hy + 1), (hx + 11, hy - 0.6)]), fill=(220, 30, 50, 255), width=A.SS)
    d.line(A.P([(hx + 9, hy + 1), (hx + 11, hy + 2.6)]), fill=(220, 30, 50, 255), width=A.SS)
    A.sphere(body, hx, hy, 5.4, (30, 110, 30), (210, 255, 140), shine=12, rim=0.4)
    d.ellipse(A.S(hx - 0.6, hy - 4, hx + 2.6, hy - 0.8), fill=(255, 255, 240, 255))
    d.ellipse(A.S(hx + 0.6, hy - 3.2, hx + 2.2, hy - 1.6), fill=(10, 10, 10, 255))
    im.alpha_composite(A.shadow(A.down(body), offset=(1, 2), blur=1.0, alpha=140), (-3, -3))
    return A.icon_finish(im)


def main():
    art = A.Art(__file__)
    art.write("lawn.gzi", lawn(), alpha=False)
    art.write("body0.gzi", segment(5.3, GREEN_D, GREEN_L))
    art.write("body1.gzi", segment(5.3, STRIPE_D, STRIPE_L))
    art.write("tail.gzi", segment(3.8, GREEN_D, GREEN_L))
    art.write("joint_h.gzi", joint(True))
    art.write("joint_v.gzi", joint(False))
    for i, n in enumerate("urdl"):
        art.write("head_%s.gzi" % n, head(i))
    art.write("apple.gzi", apple())
    art.icon(icon())
    art.finish()


if __name__ == "__main__":
    main()
