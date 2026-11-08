#!/usr/bin/env python3
"""Draw Chess's art to res/*.gzi and its icon (Pillow; see tools/gzart.py).

    python3 packages/chess/art/draw_art.py [--sheet out.png]

Staunton-style pieces drawn as polygons on a 22 px grid (4x supersampled),
filled with cylinder shading in ivory or ebony and outlined, plus a wooden
board (maple and walnut squares with grain). All drawn here.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "tools"))
import gzart as A  # noqa: E402
from gzart import Image, ImageDraw, ImageFilter  # noqa: E402

N = 22
IVORY = ((255, 252, 240), (176, 164, 140), (70, 54, 36))      # lit, shade, outline
EBONY = ((120, 122, 134), (18, 18, 24), (210, 214, 226))


def base(d):
    d.rounded_rectangle(A.S(4.5, 17.2, 17.5, 20.8), radius=1.6 * A.SS, fill=255)
    d.rounded_rectangle(A.S(6.2, 15.8, 15.8, 17.6), radius=0.8 * A.SS, fill=255)


def shape(kind):
    m = Image.new("L", (N * A.SS, N * A.SS), 0)
    d = ImageDraw.Draw(m)
    details = []
    if kind == "P":
        d.ellipse(A.S(7.8, 3.4, 14.2, 9.8), fill=255)
        d.polygon(A.P([(9.4, 9), (12.6, 9), (14.4, 16.2), (7.6, 16.2)]), fill=255)
        d.rounded_rectangle(A.S(7.6, 9.6, 14.4, 11.0), radius=0.6 * A.SS, fill=255)
        base(d)
    elif kind == "R":
        d.polygon(A.P([(7.4, 9), (14.6, 9), (15.2, 16.2), (6.8, 16.2)]), fill=255)
        d.rectangle(A.S(6, 3.6, 16, 9.4), fill=255)
        for x in (8.2, 11.9):
            d.rectangle(A.S(x, 3.4, x + 1.9, 5.6), fill=0)
        base(d)
        details.append(("line", [(6.6, 9.4), (15.4, 9.4)]))
    elif kind == "B":
        d.ellipse(A.S(9.9, 0.6, 12.1, 2.8), fill=255)
        # a tall pointed mitre
        d.polygon(A.P([(11, 2.0), (13.4, 4.6), (14.4, 7.6), (13.8, 10.6), (8.2, 10.6), (7.6, 7.6), (8.6, 4.6)]),
                  fill=255)
        d.polygon(A.P([(8.6, 10.4), (13.4, 10.4), (15.2, 16.2), (6.8, 16.2)]), fill=255)
        d.rounded_rectangle(A.S(7.2, 10.2, 14.8, 11.6), radius=0.6 * A.SS, fill=255)
        base(d)
        details.append(("cut", [(13.0, 5.0), (11.2, 7.8)]))
    elif kind == "N":
        d.polygon(A.P([(15.4, 16.4), (15.6, 11), (14.8, 7.2), (13, 4.4), (11.8, 2.2), (11, 4.0), (8.6, 5.0),
                       (5.6, 8.8), (4.8, 11.2), (6.2, 12.6), (8.6, 11.0), (10.0, 11.6), (7.4, 15.0), (7.2, 16.4)]),
                  fill=255)
        base(d)
        details.append(("eye", (10.4, 6.6)))
        details.append(("line", [(12.6, 5.2), (14.6, 10.6)]))
    elif kind == "Q":
        pts = [(5.2, 4.6), (7.6, 10.4), (8.8, 3.4), (11, 9.6), (13.2, 3.4), (14.4, 10.4), (16.8, 4.6),
               (15.4, 12.8), (6.6, 12.8)]
        d.polygon(A.P(pts), fill=255)
        for (x, y) in ((5.2, 4.6), (8.8, 3.4), (13.2, 3.4), (16.8, 4.6)):
            d.ellipse(A.S(x - 1.1, y - 1.1, x + 1.1, y + 1.1), fill=255)
        d.ellipse(A.S(10, 1.4, 12, 3.4), fill=255)
        d.polygon(A.P([(7, 12.4), (15, 12.4), (15.8, 16.2), (6.2, 16.2)]), fill=255)
        base(d)
        details.append(("line", [(6.8, 12.8), (15.2, 12.8)]))
    elif kind == "K":
        d.rectangle(A.S(10.2, 0.6, 11.8, 6.0), fill=255)
        d.rectangle(A.S(8.6, 2.0, 13.4, 3.6), fill=255)
        d.ellipse(A.S(6.4, 5.0, 15.6, 12.2), fill=255)
        d.polygon(A.P([(7.4, 10.4), (14.6, 10.4), (15.8, 16.2), (6.2, 16.2)]), fill=255)
        base(d)
        details.append(("line", [(7.0, 10.6), (15.0, 10.6)]))
    return m, details


def piece(kind, palette):
    lit, shade, outline = palette
    m, details = shape(kind)
    w = h = N * A.SS
    grow = m.filter(ImageFilter.MaxFilter(7))
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    img.paste(Image.new("RGBA", (w, h), outline + (255,)), (0, 0), grow)
    fill = Image.new("RGBA", (w, h))
    px = fill.load()
    for x in range(w):
        t = x / w
        c = A.mix(shade, lit, 1 - abs(t - 0.38) * 1.9)       # cylinder: light left of centre
        for y in range(h):
            px[x, y] = A.mix(c, shade, max(0.0, (y / h - 0.75) * 1.5)) + (255,)
    img.paste(fill, (0, 0), m)
    d = ImageDraw.Draw(img)
    for kind_, data in details:
        if kind_ == "line":
            d.line(A.P(data), fill=outline + (200,), width=A.SS)
        elif kind_ == "cut":
            d.line(A.P(data), fill=outline + (255,), width=int(0.9 * A.SS))
        elif kind_ == "eye":
            x, y = data
            d.ellipse(A.S(x - 0.8, y - 0.8, x + 0.8, y + 0.8), fill=outline + (255,))
    out = A.down(img)
    return A.shadow(out, offset=(1, 1), blur=0.8, alpha=110)


def board():
    C = 24
    im = Image.new("RGB", (8 * C, 8 * C))
    for r in range(8):
        for c in range(8):
            dark = (r + c) & 1
            sq = A.wood(C, C, 100 + r * 8 + c, light=(150, 104, 66) if dark else (236, 206, 160),
                        dark=(104, 66, 40) if dark else (204, 168, 118), grain=4)
            if (r + c) % 2:                         # alternate grain direction, like inlay
                sq = sq.transpose(Image.ROTATE_90)
            im.paste(sq, (c * C, r * C))
    return im


def icon():
    im = A.icon_tile((210, 176, 130), (140, 98, 60))
    for r in range(4):
        for c in range(4):
            if (r + c) & 1:
                d = ImageDraw.Draw(im)
                d.rectangle((c * 11, r * 11, c * 11 + 10, r * 11 + 10), fill=(132, 88, 54, 255))
    k = piece("N", EBONY).resize((40, 40), Image.LANCZOS)
    im.alpha_composite(k, (1, 0))
    return A.icon_finish(im)


def main():
    art = A.Art(__file__)
    for k in "PNBRQK":
        art.write("w%s.gzi" % k.lower(), piece(k, IVORY))
        art.write("b%s.gzi" % k.lower(), piece(k, EBONY))
    art.write("board.gzi", board(), alpha=False)
    art.icon(icon())
    art.finish()


if __name__ == "__main__":
    main()
