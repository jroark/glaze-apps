#!/usr/bin/env python3
"""Draw Flappy's art to res/*.gzi and its icon (Pillow; see tools/gzart.py).

    python3 packages/flappy/art/draw_art.py [--sheet out.png]

A round yellow bird in three wing poses and five tilts, cylinder-shaded pipes
and caps, hazy far hills and near bushes (tileable, for parallax), soft
clouds and a grassy ground strip. All drawn here, procedurally.
"""
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "tools"))
import gzart as A  # noqa: E402
from gzart import Image  # noqa: E402

ANGLES = [-25, -10, 0, 25, 60]          # nose down is positive
PIPE_D, PIPE_M, PIPE_L = (20, 90, 40), (80, 190, 90), (190, 250, 170)


def bird(wing, angle):
    W = 24
    im, d = A.big(W, W)
    c = W / 2
    # tail
    d.polygon(A.P([(c - 6, c - 1), (c - 10, c - 3.5), (c - 9.5, c + 1.5)]), fill=(220, 130, 30, 255))
    A.sphere(im, c, c, 6.8, (200, 120, 10), (255, 236, 120), shine=10, rim=0.55)
    d.ellipse(A.S(c - 4.5, c + 0.5, c + 3.5, c + 6.2), fill=(255, 244, 200, 230))      # belly
    wy = {0: -4.0, 1: 0.0, 2: 3.0}[wing]
    wh = {0: 3.4, 1: 3.0, 2: 3.4}[wing]
    d.ellipse(A.S(c - 8.5, c + wy - wh / 2 - 0.5, c - 1, c + wy + wh / 2), fill=(170, 80, 10, 255))
    d.ellipse(A.S(c - 8, c + wy - wh / 2, c - 1.5, c + wy + wh / 2 - 0.6), fill=(255, 160, 50, 255))
    # beak
    d.polygon(A.P([(c + 4.5, c - 0.6), (c + 10, c + 0.6), (c + 4.5, c + 1.8)]), fill=(240, 100, 30, 255))
    d.polygon(A.P([(c + 4.5, c + 1.8), (c + 8.5, c + 1.6), (c + 4.5, c + 3.0)]), fill=(190, 60, 20, 255))
    # eye
    d.ellipse(A.S(c + 0.6, c - 5.4, c + 5.2, c - 0.8), fill=(255, 255, 255, 255))
    d.ellipse(A.S(c + 2.6, c - 4.2, c + 4.6, c - 2.0), fill=(20, 24, 36, 255))
    im = im.rotate(-angle, resample=Image.BICUBIC, center=(c * A.SS, c * A.SS))
    return A.down(im)


def pipe_shaft():
    w, h = 24, 4
    im = Image.new("RGB", (w, h))
    px = im.load()
    for x in range(w):
        t = (x + 0.5) / w
        k = 1 - abs(t - 0.3) / 0.7                     # light from the left of centre
        c = A.mix(PIPE_D, PIPE_M, min(1.0, k * 1.6))
        if 0.18 < t < 0.3:
            c = A.mix(c, PIPE_L, 0.6)
        if x in (0, w - 1):
            c = (16, 60, 28)
        for y in range(h):
            px[x, y] = c
    return im


def pipe_cap():
    w, h = 32, 8
    im = Image.new("RGBA", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            t = (x + 0.5) / w
            k = 1 - abs(t - 0.3) / 0.7
            c = A.mix(PIPE_D, PIPE_M, min(1.0, k * 1.6))
            if 0.2 < t < 0.3:
                c = A.mix(c, PIPE_L, 0.7)
            if y == 0:
                c = A.mix(c, (255, 255, 255), 0.35)
            if y == h - 1:
                c = A.mix(c, (0, 0, 0), 0.4)
            if x in (0, w - 1) or y in (0, h - 1):
                c = A.mix(c, (16, 60, 28), 0.6)
            px[x, y] = c + (255,)
    return im


def ridge(w, h, seed, base, top, cells, amp, haze=None):
    """A tileable silhouette: noisy height, shaded toward its top."""
    n = A.fbm(w, 1, cells, seed)[0]
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = im.load()
    for x in range(w):
        hx = int(h * (1 - amp) + n[x] * h * amp * 1.4)
        hx = max(2, min(h - 1, hx))
        for y in range(h - hx, h):
            t = (y - (h - hx)) / max(1, hx)
            c = A.mix(top, base, min(1.0, t * 1.6))
            px[x, y] = c + (255,)
        if haze:
            px[x, h - hx] = haze + (255,)
    return im


def bushes():
    w, h = 240, 26
    im, d = A.big(w, h)
    import random
    rnd = random.Random(8)
    x = -6
    while x < w + 10:
        r = 6 + rnd.random() * 6
        for dx in (0, w):                                  # wrap so the strip tiles
            d.ellipse(A.S(x + dx - r, h - r * 1.5, x + dx + r, h + r), fill=(60, 150, 70, 255))
        x += r * 1.3
    im = A.down(im)
    hi, hd = A.big(w, h)
    rnd = random.Random(8)
    x = -6
    while x < w + 10:
        r = 6 + rnd.random() * 6
        for dx in (0, w):
            hd.ellipse(A.S(x + dx - r * 0.7, h - r * 1.45, x + dx + r * 0.3, h - r * 0.6), fill=(140, 220, 120, 120))
        x += r * 1.3
    im.alpha_composite(A.down(hi))
    return im


def cloud(seed):
    w, h = 44, 20
    im, d = A.big(w, h)
    import random
    rnd = random.Random(seed)
    puffs = [(10, 13, 7), (20, 9, 9), (31, 12, 7), (25, 14, 7), (15, 15, 6)]
    for (x, y, r) in puffs:
        r += rnd.random() * 1.5
        d.ellipse(A.S(x - r, y - r, x + r, y + r), fill=(200, 222, 240, 255))
    for (x, y, r) in puffs:
        d.ellipse(A.S(x - r + 0.5, y - r, x + r - 1, y + r - 2.2), fill=(255, 255, 255, 255))
    return A.down(im)


def ground():
    w, h = 16, 6
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            if y == 0:
                c = (170, 240, 120)
            elif y == 1:
                c = (70, 170, 60) if (x // 4) % 2 else (90, 190, 70)
            else:
                c = (200, 160, 90) if ((x + y * 2) // 4) % 2 else (180, 140, 76)
            px[x, y] = c
    return im


def icon():
    im = A.icon_tile((98, 200, 236), (200, 236, 248))
    im.alpha_composite(cloud(3).resize((30, 14), Image.LANCZOS), (2, 4))
    im.alpha_composite(ridge(44, 14, 5, (110, 190, 140), (150, 210, 170), [11, 5], 0.6), (0, 26))
    for x, top in ((26, 22), ):
        shaft = pipe_shaft().resize((10, 4))
        for y in range(top + 8, 44, 4):
            im.paste(shaft, (x + 3, y))
        for y in range(0, top - 30, 4):
            im.paste(shaft, (x + 3, y))
        cap = pipe_cap().resize((14, 5), Image.LANCZOS)
        im.alpha_composite(cap, (x + 1, top + 4))
    g = ground().resize((44, 5))
    im.paste(g, (0, 39))
    b = bird(0, -15).resize((26, 26), Image.LANCZOS)
    im.alpha_composite(A.shadow(b, offset=(1, 1), blur=0.8, alpha=90), (0, 7))
    return A.icon_finish(im)


def main():
    art = A.Art(__file__)
    for f in range(3):
        for a, ang in enumerate(ANGLES):
            art.write("bird%d%d.gzi" % (f, a), bird(f, ang))
    art.write("shaft.gzi", pipe_shaft(), alpha=False)
    art.write("cap.gzi", pipe_cap())
    art.write("hills.gzi", ridge(240, 56, 21, (120, 196, 196), (170, 220, 220), [60, 30, 12], 0.55, haze=(200, 236, 236)))
    art.write("bushes.gzi", bushes())
    art.write("cloud0.gzi", cloud(1))
    art.write("cloud1.gzi", cloud(2))
    art.write("ground.gzi", ground(), alpha=False)
    art.icon(icon())
    art.finish()


if __name__ == "__main__":
    main()
