#!/usr/bin/env python3
"""Draw Solitaire's art to res/*.gzi and its icon (Pillow; see tools/gzart.py).

    python3 packages/solitaire/art/draw_art.py [--sheet out.png]

Cards are composed at run time from: a shaded 32x44 card blank, anti-aliased
suit symbols (8 px corner, 15 px centre), J/Q/K court panels in red and black
ink, an ornate back, and a felt tile. All drawn here, procedurally.
"""
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "tools"))
import gzart as A  # noqa: E402
from gzart import Image, ImageDraw  # noqa: E402

CW, CH = 32, 44
RED, BLACK = (208, 24, 30), (22, 22, 28)


def suit_shape(d, suit, x0, y0, s, fill):
    def p(u, v):
        return ((x0 + u * s) * A.SS, (y0 + v * s) * A.SS)

    def circ(u, v, r):
        (cx, cy) = p(u, v)
        d.ellipse((cx - r * s * A.SS, cy - r * s * A.SS, cx + r * s * A.SS, cy + r * s * A.SS), fill=fill)
    if suit == 1:      # hearts
        circ(0.29, 0.33, 0.25)
        circ(0.71, 0.33, 0.25)
        d.polygon([p(0.05, 0.42), p(0.95, 0.42), p(0.5, 0.96)], fill=fill)
    elif suit == 2:    # diamonds
        d.polygon([p(0.5, 0.0), p(0.88, 0.5), p(0.5, 1.0), p(0.12, 0.5)], fill=fill)
    elif suit == 0:    # spades
        circ(0.29, 0.58, 0.23)
        circ(0.71, 0.58, 0.23)
        d.polygon([p(0.07, 0.56), p(0.93, 0.56), p(0.5, 0.0)], fill=fill)
        d.polygon([p(0.5, 0.55), p(0.3, 1.0), p(0.7, 1.0)], fill=fill)
    else:              # clubs
        circ(0.5, 0.27, 0.22)
        circ(0.26, 0.6, 0.22)
        circ(0.74, 0.6, 0.22)
        d.polygon([p(0.5, 0.5), p(0.3, 1.0), p(0.7, 1.0)], fill=fill)


def suit(s_, n):
    im, d = A.big(n, n)
    ink = RED if s_ in (1, 2) else BLACK
    suit_shape(d, s_, 0, 0, n, ink + (255,))
    out = A.down(im)
    if n >= 12:                                   # a soft highlight on the big pips
        g, gd = A.big(n, n)
        suit_shape(gd, s_, n * 0.06, n * 0.04, n * 0.7, (255, 255, 255, 50))
        hl = A.down(g)
        hl.putalpha(A.ImageChops.multiply(hl.split()[3], out.split()[3]))
        out.alpha_composite(hl)
    return out


def blank():
    im = A.vgrad(CW, CH, (255, 255, 255), (232, 234, 238)).convert("RGBA")
    m, d = A.big(CW, CH)
    d.rounded_rectangle(A.S(0, 0, CW, CH), radius=3 * A.SS, fill=(255, 255, 255, 255))
    e, ed = A.big(CW, CH)
    ed.rounded_rectangle(A.S(0.4, 0.4, CW - 0.4, CH - 0.4), radius=3 * A.SS, outline=(110, 114, 124, 255), width=A.SS)
    im.alpha_composite(A.down(e))
    im.putalpha(A.down(m).split()[3])
    return im


def back():
    im = A.vgrad(CW, CH, (44, 92, 190), (22, 54, 130)).convert("RGBA")
    o, d = A.big(CW, CH)
    for k in range(-CH, CW + CH, 4):            # fine lattice
        d.line(A.P([(k, 2), (k + CH, CH + 2)]), fill=(120, 170, 255, 70), width=A.SS // 2)
        d.line(A.P([(k + CH, 2), (k, CH + 2)]), fill=(120, 170, 255, 70), width=A.SS // 2)
    d.ellipse(A.S(CW / 2 - 7, CH / 2 - 9, CW / 2 + 7, CH / 2 + 9), fill=(250, 220, 120, 255))
    d.ellipse(A.S(CW / 2 - 5.4, CH / 2 - 7.4, CW / 2 + 5.4, CH / 2 + 7.4), fill=(30, 70, 160, 255))
    suit_shape(d, 0, CW / 2 - 3.6, CH / 2 - 4, 7.2, (250, 220, 120, 255))
    im.alpha_composite(A.down(o))
    e, ed = A.big(CW, CH)
    ed.rounded_rectangle(A.S(0.4, 0.4, CW - 0.4, CH - 0.4), radius=3 * A.SS, outline=(255, 255, 255, 255), width=int(1.6 * A.SS))
    ed.rounded_rectangle(A.S(2.6, 2.6, CW - 2.6, CH - 2.6), radius=2 * A.SS, outline=(250, 220, 120, 200), width=A.SS // 2)
    im.alpha_composite(A.down(e))
    m, md = A.big(CW, CH)
    md.rounded_rectangle(A.S(0, 0, CW, CH), radius=3 * A.SS, fill=(255, 255, 255, 255))
    im.putalpha(A.down(m).split()[3])
    return im


def court(rank, ink):
    w, h = 22, 25
    im = A.vgrad(w, h, (252, 240, 196), (232, 206, 140)).convert("RGBA")
    o, d = A.big(w, h)
    gold, deep = (220, 170, 40, 255), ink + (255,)
    if rank == "K":                                   # a crown with three points
        d.polygon(A.P([(4, 10), (5, 3), (8, 7), (11, 2), (14, 7), (17, 3), (18, 10)]), fill=gold)
        for x, y in ((5, 3), (11, 2), (17, 3)):
            d.ellipse(A.S(x - 1.2, y - 1.2, x + 1.2, y + 1.2), fill=deep)
        d.rectangle(A.S(4, 9, 18, 11), fill=deep)
    elif rank == "Q":                                 # a tiara with a jewel
        d.chord(A.S(3, 2, 19, 18), 180, 360, fill=gold)
        d.ellipse(A.S(9, 3, 13, 7), fill=deep)
        d.rectangle(A.S(3, 9, 19, 11), fill=deep)
    else:                                             # a cap with a plume
        d.chord(A.S(4, 4, 18, 16), 180, 360, fill=deep)
        d.rectangle(A.S(3, 9.4, 19, 11), fill=gold)
        d.line(A.P([(14, 6), (19, 1)]), fill=gold, width=int(1.6 * A.SS))
    im.alpha_composite(A.down(o))
    e, ed = A.big(w, h)
    ed.rectangle(A.S(0.4, 0.4, w - 0.4, h - 0.4), outline=ink + (255,), width=A.SS)
    im.alpha_composite(A.down(e))
    return im


def felt():
    n = A.fbm(64, 64, [16, 8, 4, 2], 7)
    im = Image.new("RGB", (64, 64))
    px = im.load()
    for y in range(64):
        for x in range(64):
            px[x, y] = A.mix((30, 114, 56), (40, 132, 66), n[y][x] + ((x * 7 + y * 3) % 3 - 1) * 0.06)
    return im


def icon():
    im = A.icon_tile((52, 150, 76), (20, 92, 42))
    f = A.font(11)
    for i, (s_, r, ang, dx) in enumerate(((0, "K", 14, 18), (1, "A", -8, 6))):
        card = blank().resize((CW, CH))
        d = ImageDraw.Draw(card)
        ink = RED if s_ in (1, 2) else BLACK
        d.text((3, 1), r, font=f, fill=ink + (255,))
        big = suit(s_, 15)
        card.alpha_composite(big, (CW // 2 - 7, CH // 2 - 4))
        card = card.resize((24, 33), Image.LANCZOS)
        card = A.shadow(card, offset=(1, 1), blur=1.0, alpha=120).rotate(ang, resample=Image.BICUBIC, expand=True)
        im.alpha_composite(card, (dx - 4, 2 + i * 2))
    return A.icon_finish(im)


def main():
    art = A.Art(__file__)
    art.write("card.gzi", blank())
    art.write("back.gzi", back())
    for s_ in range(4):
        art.write("pip%d.gzi" % s_, suit(s_, 8))
        art.write("suit%d.gzi" % s_, suit(s_, 15))
    for r in "JQK":
        art.write("court_%s_r.gzi" % r.lower(), court(r, RED))
        art.write("court_%s_b.gzi" % r.lower(), court(r, BLACK))
    art.write("felt.gzi", felt(), alpha=False)
    art.icon(icon())
    art.finish()


if __name__ == "__main__":
    main()
