#!/usr/bin/env python3
"""Draw Breakout's art to res/*.gzi and its icon (Pillow; see tools/gzart.py).

    python3 packages/breakout/art/draw_art.py [--sheet out.png]

Glossy bricks (one colour per row), gold two-hit bricks, riveted steel, a
chrome paddle with glowing ends, a shaded ball and a dark tiled backdrop.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "tools"))
import gzart as A  # noqa: E402
from gzart import Image  # noqa: E402

ROWS = [0xE8505A, 0xF08A3C, 0xF2C94C, 0x5CC46A, 0x4C8FD6, 0x9A6CE0]
BW, BH = 18, 13


def brick(c, w=BW, h=BH):
    return A.bevel_tile(w, h, A.rgb(c), radius=2, depth=2.4, light=0.5, shade=0.55)


def tough(w=BW, h=BH):
    im = A.bevel_tile(w, h, (214, 160, 50), radius=2, depth=2.4, light=0.55, shade=0.6)
    d = A.ImageDraw.Draw(im)
    for x, y in ((3, 3), (w - 4, 3), (3, h - 4), (w - 4, h - 4)):
        d.point((x, y), fill=(255, 240, 180, 255))
        d.point((x + 1, y + 1), fill=(120, 80, 20, 255))
    return im


def steel(w=BW, h=BH):
    im = A.bevel_tile(w, h, (120, 132, 150), radius=1.5, depth=2.4, light=0.6, shade=0.6, gloss=False)
    px = im.load()
    for y in range(2, h - 2):                       # brushed streaks
        for x in range(2, w - 2):
            r, g, b, a = px[x, y]
            k = ((x * 7 + y * 13) % 5) - 2
            px[x, y] = (r + k * 3, g + k * 3, b + k * 3, a)
    m, d = A.big(w, h)
    for x, y in ((3.5, 3.5), (w - 3.5, 3.5), (3.5, h - 3.5), (w - 3.5, h - 3.5)):
        d.ellipse(A.S(x - 1.2, y - 1.2, x + 1.2, y + 1.2), fill=(220, 228, 240, 255))
        d.ellipse(A.S(x - 0.5, y - 0.5, x + 1.2, y + 1.2), fill=(70, 78, 92, 255))
    im.alpha_composite(A.down(m))
    return im


def paddle():
    w, h = 44, 8
    im = Image.new("RGBA", (w, h))
    px = im.load()
    for y in range(h):
        t = (y + 0.5) / h
        c = A.mix((250, 252, 255), (90, 100, 120), abs(t - 0.3) * 1.8)
        for x in range(w):
            px[x, y] = c + (255,)
    m, d = A.big(w, h)
    d.rounded_rectangle(A.S(0, 0, w, h), radius=4 * A.SS, fill=(255, 255, 255, 255))
    im.putalpha(A.down(m).split()[3])
    for cx in (3.5, w - 3.5):                       # glowing caps
        im.alpha_composite(A.glow(w, h, cx, h / 2, 5, 5, (60, 200, 255), (220, 250, 255), power=1.0))
    return im


def ball():
    im, _ = A.big(8, 8)
    A.sphere(im, 4, 4, 3.4, (120, 140, 170), (255, 255, 255), shine=8, rim=0.3)
    return A.down(im)


def backdrop():
    w = h = 32
    im = A.noise_image(w, h, [16, 8], 9, (10, 16, 30), (18, 28, 50), contrast=1.2)
    px = im.load()
    for y in range(h):
        for x in range(w):
            if x % 16 == 0 or y % 16 == 0:
                r, g, b = px[x, y]
                px[x, y] = (r + 8, g + 12, b + 20)
    return im


def icon():
    im = A.icon_tile((30, 40, 76), (8, 12, 26))
    for r in range(4):
        for c in range(4):
            b = brick(ROWS[r], 9, 6)
            im.alpha_composite(b, (2 + c * 10, 5 + r * 7))
    p = paddle().resize((20, 5), Image.LANCZOS)
    im.alpha_composite(p, (14, 37))
    bl = ball().resize((7, 7), Image.LANCZOS)
    im.alpha_composite(A.glow(14, 14, 7, 7, 7, 7, (120, 200, 255), power=2.0), (18, 25))
    im.alpha_composite(bl, (21, 28))
    return A.icon_finish(im)


def main():
    art = A.Art(__file__)
    for i, c in enumerate(ROWS):
        art.write("brick%d.gzi" % i, brick(c))
    art.write("tough.gzi", tough())
    art.write("steel.gzi", steel())
    art.write("paddle.gzi", paddle())
    art.write("ball.gzi", ball())
    art.write("back.gzi", backdrop(), alpha=False)
    art.icon(icon())
    art.finish()


if __name__ == "__main__":
    main()
