#!/usr/bin/env python3
"""Draw TwixT's art to res/*.gzi and its icon (Pillow; see tools/gzart.py).

    python3 packages/twixt/art/draw_art.py [--sheet out.png]

A wooden pegboard (208 px, 12x12 holes on a 16 px pitch, corners left
solid, inlaid border lines), shaded red and black pegs, and a felt tile.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "tools"))
import gzart as A  # noqa: E402

DIM, PITCH, PAD = 12, 16, 8
RED_D, RED_L = (120, 20, 16), (255, 120, 100)
BLK_D, BLK_L = (14, 16, 22), (130, 140, 160)


def board():
    size = PAD * 2 + (DIM - 1) * PITCH          # 192 + 16 = 208
    im = A.wood(size, size, 31, light=(214, 176, 120), dark=(160, 118, 72), grain=5).convert("RGBA")
    m, d = A.big(size, size)
    d.rounded_rectangle(A.S(0, 0, size, size), radius=6 * A.SS, fill=(255, 255, 255, 255))
    mask = A.down(m).split()[3]
    o, od = A.big(size, size)
    # border lines between the edge rows and the playing field
    a, b = PAD + PITCH * 0.5, PAD + PITCH * (DIM - 1.5)
    od.line(A.P([(a, a), (b, a)]), fill=(180, 40, 34, 220), width=2 * A.SS)
    od.line(A.P([(a, b), (b, b)]), fill=(180, 40, 34, 220), width=2 * A.SS)
    od.line(A.P([(a, a), (a, b)]), fill=(30, 30, 36, 220), width=2 * A.SS)
    od.line(A.P([(b, a), (b, b)]), fill=(30, 30, 36, 220), width=2 * A.SS)
    for r in range(DIM):
        for c in range(DIM):
            if (r in (0, DIM - 1)) and (c in (0, DIM - 1)):
                continue
            x, y = PAD + c * PITCH, PAD + r * PITCH
            od.ellipse(A.S(x - 2.8, y - 2.4, x + 2.8, y + 3.2), fill=(255, 236, 200, 110))   # lit lower lip
            od.ellipse(A.S(x - 2.6, y - 2.6, x + 2.6, y + 2.6), fill=(46, 28, 14, 255))
            od.ellipse(A.S(x - 1.6, y - 1.6, x + 1.8, y + 1.8), fill=(22, 12, 6, 255))
    od.rounded_rectangle(A.S(0.5, 0.5, size - 0.5, size - 0.5), radius=6 * A.SS, outline=(255, 230, 190, 120), width=A.SS)
    im.alpha_composite(A.down(o))
    im.putalpha(mask)
    return im


def peg(dark, lit, size=14):
    im, _ = A.big(size, size)
    A.sphere(im, size / 2, size / 2, size / 2 - 1.2, dark, lit, shine=16, rim=0.5)
    return A.shadow(A.down(im), offset=(1, 1), blur=0.7, alpha=130)


def felt():
    return A.noise_image(32, 32, [8, 4, 2], 5, (14, 40, 34), (24, 58, 48), contrast=1.0)


def icon():
    im = A.icon_tile((206, 166, 112), (150, 108, 64))
    o, od = A.big(A.ICON, A.ICON)
    for r in range(5):
        for c in range(5):
            x, y = 6 + c * 8, 6 + r * 8
            od.ellipse(A.S(x - 1.3, y - 1.3, x + 1.3, y + 1.3), fill=(46, 28, 14, 255))
    # two links: red down the middle, black across
    for (x0, y0, x1, y1, col) in ((14, 6, 22, 22, (200, 40, 34)), (22, 22, 30, 38, (200, 40, 34)),
                                   (6, 30, 22, 38, (30, 32, 40))):
        od.line(A.P([(x0, y0), (x1, y1)]), fill=col + (255,), width=int(2.6 * A.SS))
    im.alpha_composite(A.down(o))
    for (x, y, d_, l_) in ((14, 6, RED_D, RED_L), (22, 22, RED_D, RED_L), (30, 38, RED_D, RED_L),
                           (6, 30, BLK_D, BLK_L), (22, 38, BLK_D, BLK_L)):
        im.alpha_composite(peg(d_, l_, 10), (x - 6, y - 6))
    return A.icon_finish(im)


def main():
    art = A.Art(__file__)
    art.write("board.gzi", board())
    art.write("peg_red.gzi", peg(RED_D, RED_L))
    art.write("peg_black.gzi", peg(BLK_D, BLK_L))
    art.write("felt.gzi", felt(), alpha=False)
    art.icon(icon())
    art.finish()


if __name__ == "__main__":
    main()
