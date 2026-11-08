#!/usr/bin/env python3
"""Draw Blocks' art to res/*.gzi and its icon (Pillow; see tools/gzart.py).

    python3 packages/tetris/art/draw_art.py [--sheet out.png]

Bevelled, glossy blocks in the seven piece colours, a ghost outline, and the
well's backdrop (a deep gradient with a faint cell grid and a vignette).
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "tools"))
import gzart as A  # noqa: E402
from gzart import Image  # noqa: E402

COLORS = [0x42D9E8, 0x507EF0, 0xF2A547, 0xF5D85B, 0x69CE8A, 0xB88BF0, 0xF07482]


def block(c, size=10):
    base = A.rgb(c)
    im = A.bevel_tile(size, size, base, radius=1.6, depth=2.2, light=0.55, shade=0.55)
    d = A.ImageDraw.Draw(im)
    d.point((2, 2), fill=(255, 255, 255, 200))                    # a glint
    return im


def ghost():
    im, d = A.big(10, 10)
    d.rounded_rectangle(A.S(0.5, 0.5, 9.5, 9.5), radius=1.5 * A.SS, outline=(220, 235, 255, 150), width=A.SS)
    d.rounded_rectangle(A.S(1.5, 1.5, 8.5, 8.5), radius=1 * A.SS, fill=(200, 220, 255, 30))
    return A.down(im)


def well():
    w, h = 100, 200
    im = A.vgrad(w, h, (16, 26, 48), (8, 12, 24))
    px = im.load()
    n = A.fbm(w, h, [25, 12], 4, wrap_x=False, wrap_y=False)
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            k = (n[y][x] - 0.5) * 10
            if x % 10 == 0 or y % 10 == 0:
                k += 9                                           # faint cell grid
            dx, dy = (x - w / 2) / (w / 2), (y - h / 2) / (h / 2)
            k -= (dx * dx * 0.6 + dy * dy * 0.3) * 14            # vignette
            px[x, y] = (max(0, min(255, int(r + k))), max(0, min(255, int(g + k))), max(0, min(255, int(b + k * 1.3))))
    return im


def icon():
    im = A.icon_tile((40, 52, 96), (14, 18, 40))
    cells = [(1, 4, 0), (2, 4, 0), (3, 4, 1), (4, 4, 1), (1, 3, 2), (4, 3, 1), (1, 2, 2), (2, 2, 3),
             (3, 3, 4), (2, 3, 4), (4, 2, 5), (2, 0, 6), (3, 0, 6), (3, 1, 6), (4, 0, 6)]
    for (cx, cy, p) in cells:
        bl = block(COLORS[p], 8)
        im.alpha_composite(A.shadow(bl, offset=(1, 1), blur=0.6, alpha=120), (int(1 + cx * 8), int(2 + cy * 8)))
    return A.icon_finish(im)


def main():
    art = A.Art(__file__)
    for i, c in enumerate(COLORS):
        art.write("block%d.gzi" % (i + 1), block(c))
    art.write("ghost.gzi", ghost())
    art.write("well.gzi", well(), alpha=False)
    art.icon(icon())
    art.finish()


if __name__ == "__main__":
    main()
