#!/usr/bin/env python3
"""Draw 1942's original art to res/*.gzi and its icon (Pillow; tools/gzart.py).

    python3 packages/sky1942/art/draw_art.py [--sheet out.png]

Everything here was drawn for this app, procedurally: top-down planes from
shaded polygons (4x supersampled), fireball explosion frames, tracer and
bomb glows, a tileable sea, two islands with surf, and soft clouds. The
sprite names and sizes match arcade_art.py's optional swap; re-run this to
restore our own art after it.
"""
import math
import os
import random
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "tools"))
import gzart as A  # noqa: E402
from gzart import Image, ImageDraw, ImageFilter  # noqa: E402


def plane(w, h, body, wing, mark, nose_up, engines=0, wing_y=0.42, span=1.0, tail=0.16):
    """A top-down propeller plane in a w x h box. nose_up: player (nose at top)."""
    im, d = A.big(w, h)
    cx = w / 2
    lit = A.mix(wing, (255, 255, 255), 0.35)
    dark = A.mix(wing, (0, 0, 0), 0.35)
    # all shapes drawn nose-up, flipped at the end for enemies
    wy = h * wing_y
    half = w / 2 * span
    chord = h * 0.2
    d.polygon(A.P([(cx - half, wy + chord * 0.35), (cx - half * 0.9, wy - chord * 0.1), (cx, wy - chord * 0.55),
                   (cx + half * 0.9, wy - chord * 0.1), (cx + half, wy + chord * 0.35), (cx, wy + chord * 0.6)]),
              fill=dark + (255,))
    d.polygon(A.P([(cx - half * 0.95, wy + chord * 0.15), (cx - half * 0.85, wy - chord * 0.12), (cx, wy - chord * 0.5),
                   (cx + half * 0.85, wy - chord * 0.12), (cx + half * 0.95, wy + chord * 0.15), (cx, wy + chord * 0.25)]),
              fill=wing + (255,))
    d.polygon(A.P([(cx - half * 0.85, wy - chord * 0.1), (cx, wy - chord * 0.5), (cx + half * 0.85, wy - chord * 0.1),
                   (cx, wy - chord * 0.2)]), fill=lit + (255,))
    # tailplane
    ty = h * (1 - tail)
    d.polygon(A.P([(cx - w * 0.22, ty + h * 0.05), (cx, ty - h * 0.05), (cx + w * 0.22, ty + h * 0.05),
                   (cx, ty + h * 0.09)]), fill=wing + (255,))
    # engines on the wings
    for k in range(engines):
        ex = cx + (k * 2 - 1) * half * 0.48
        d.rounded_rectangle(A.S(ex - w * 0.055, wy - h * 0.2, ex + w * 0.055, wy + h * 0.16), radius=A.SS,
                            fill=A.mix(body, (0, 0, 0), 0.2) + (255,))
        d.ellipse(A.S(ex - w * 0.11, wy - h * 0.26, ex + w * 0.11, wy - h * 0.2), fill=(220, 220, 230, 110))
    out = A.down(im)
    # fuselage: a shaded capsule
    fus, fd = A.big(w, h)
    fw = max(2.4, w * 0.13)
    fd.rounded_rectangle(A.S(cx - fw / 2, h * 0.08, cx + fw / 2, h * 0.97), radius=fw / 2 * A.SS, fill=(255, 255, 255, 255))
    fm = A.down(fus).split()[3]
    shade = Image.new("RGBA", (w, h))
    px = shade.load()
    for x in range(w):
        t = (x + 0.5 - (cx - fw / 2)) / fw
        c = A.mix(A.mix(body, (0, 0, 0), 0.45), A.mix(body, (255, 255, 255), 0.4), 1 - abs(t - 0.35) * 1.6)
        for y in range(h):
            px[x, y] = c + (255,)
    shade.putalpha(fm)
    out.alpha_composite(shade)
    dd = ImageDraw.Draw(out)
    # canopy and markings (in final px)
    dd.ellipse((cx - fw * 0.32, h * 0.3, cx + fw * 0.32, h * 0.44), fill=(90, 180, 230, 255))
    dd.point((int(cx - fw * 0.1), int(h * 0.32)), fill=(230, 250, 255, 255))
    for side in (-1, 1):
        mx = cx + side * half * 0.62
        r = max(1.0, w * 0.06)
        dd.ellipse((mx - r, wy - r * 0.6, mx + r, wy + r * 1.2), fill=mark + (255,))
    # propeller blur at the nose
    pr = max(2.0, fw * 1.1)
    prop = A.glow(w, h, cx, h * 0.08, pr * 1.6, pr * 0.45, (230, 230, 236), (255, 255, 255), power=1.0, gain=0.9)
    out.alpha_composite(prop)
    return out if nose_up else out.transpose(Image.FLIP_TOP_BOTTOM)


_BOOM = {}


def explosion(i, n=6):
    sizes = [13, 15, 19, 21, 19, 17]
    W = sizes[i]
    t = i / (n - 1)
    if W not in _BOOM:
        _BOOM[W] = A.fbm(W, W, [6, 3, 2], 70 + W)
    nz = _BOOM[W]
    R = W / 2 - 0.5
    ramp = [(0.0, (60, 50, 50)), (0.3, (150, 50, 20)), (0.5, (235, 100, 20)), (0.7, (255, 180, 50)),
            (0.88, (255, 245, 180)), (1.0, (255, 255, 255))]

    def col(hh):
        for k in range(1, len(ramp)):
            if hh <= ramp[k][0]:
                a0, c0 = ramp[k - 1]
                a1, c1 = ramp[k]
                return A.mix(c0, c1, (hh - a0) / (a1 - a0))
        return ramp[-1][1]
    out = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    px = out.load()
    for y in range(W):
        for x in range(W):
            v = nz[y][x]
            dx, dy = x + 0.5 - W / 2, y + 0.5 - W / 2
            dist = math.sqrt(dx * dx + dy * dy) / (R * (0.75 + v * 0.45))
            if dist >= 1:
                continue
            heat = (1 - dist) * (1.3 - t * 1.15) + (v - 0.5) * 0.5
            a = min(1.0, (1 - dist) * 4) * max(0.0, 1 - t ** 1.7)
            px[x, y] = col(max(0.0, min(1.0, heat))) + (int(255 * a),)
    return out


def sea():
    w, h = 220, 110                     # the field's width; cells divide both sides so it tiles
    n = A.fbm(w, h, [55, 22, 11], 3)
    m = A.fbm(w, h, [11, 5, 2], 9)
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            c = A.mix((14, 52, 96), (28, 84, 138), (n[y][x] - 0.5) * 1.8 + 0.5)
            if m[y][x] > 0.7:                                   # sparse foam flecks
                c = A.mix(c, (130, 180, 214), (m[y][x] - 0.7) * 4)
            px[x, y] = c
    return im


def island(seed, w, h):
    rnd = random.Random(seed)
    n = A.fbm(w, h, [12, 6, 3], seed, wrap_x=False, wrap_y=False)
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = im.load()
    for y in range(h):
        for x in range(w):
            dx, dy = (x + 0.5 - w / 2) / (w / 2), (y + 0.5 - h / 2) / (h / 2)
            dist = math.sqrt(dx * dx + dy * dy) + (n[y][x] - 0.5) * 0.7
            if dist > 1.0:
                continue
            if dist > 0.88:
                c, a = (210, 236, 244), int(255 * (1 - (dist - 0.88) / 0.12) * 0.8)   # surf
            elif dist > 0.78:
                c, a = (60, 150, 170), 200                                            # shallows
            elif dist > 0.66:
                c, a = (232, 214, 160), 255                                           # sand
            else:
                k = n[y][x] + (0.66 - dist) * 0.4
                c, a = A.mix((30, 90, 40), (90, 160, 70), k), 255                    # jungle
            px[x, y] = c + (a,)
    d = ImageDraw.Draw(im)
    for _ in range(8):                                                               # palm crowns
        x, y = w / 2 + (rnd.random() - 0.5) * w * 0.45, h / 2 + (rnd.random() - 0.5) * h * 0.45
        d.ellipse((x - 2, y - 2, x + 2, y + 2), fill=(40, 120, 50, 255))
        d.point((int(x - 1), int(y - 1)), fill=(120, 190, 90, 255))
    return im


def cloud(seed, w=64, h=34):
    rnd = random.Random(seed)
    im, d = A.big(w, h)
    for _ in range(9):
        r = 5 + rnd.random() * 5
        x, y = r + 4 + rnd.random() * (w - 2 * r - 8), r * 0.8 + 4 + rnd.random() * (h - 1.6 * r - 8)
        d.ellipse(A.S(x - r, y - r * 0.8, x + r, y + r * 0.8), fill=(255, 255, 255, 255))
    out = A.down(im).filter(ImageFilter.GaussianBlur(1.6))
    out.putalpha(out.split()[3].point(lambda v: v * 150 // 255))
    return out


def icon():
    im = sea().resize((44, 44)).convert("RGBA")
    isl = island(4, 30, 22)
    im.alpha_composite(isl, (-6, 24))
    p = plane(30, 30, (196, 204, 216), (216, 224, 236), (50, 100, 200), True)
    sh = Image.new("RGBA", p.size, (0, 0, 0, 0))
    sh.paste((0, 0, 0, 90), (0, 0), p.split()[3])
    im.alpha_composite(sh, (12, 11))
    im.alpha_composite(p, (8, 4))
    e = plane(16, 13, (110, 120, 60), (120, 130, 70), (200, 40, 40), False)
    im.alpha_composite(e, (27, 1))
    im.alpha_composite(A.glow(4, 9, 2, 4.5, 2, 4.5, (255, 220, 90), power=1.0), (21, 1))
    return A.icon_finish(im, gloss=50)


def main():
    art = A.Art(__file__)
    art.write("player.gzi", plane(21, 21, (196, 204, 216), (216, 224, 236), (50, 100, 200), True))
    art.write("fighter.gzi", plane(17, 14, (110, 120, 60), (126, 136, 72), (210, 40, 40), False, wing_y=0.45))
    art.write("diver.gzi", plane(19, 15, (190, 60, 50), (214, 80, 64), (250, 230, 230), False, wing_y=0.48, span=0.95))
    art.write("bomber.gzi", plane(30, 21, (70, 100, 60), (84, 116, 70), (210, 40, 40), False, engines=2, wing_y=0.46))
    art.write("shot.gzi", A.glow(4, 10, 2, 5, 2, 5, (255, 210, 80), (255, 255, 220), power=0.9, core_frac=0.6))
    art.write("bomb.gzi", A.glow(8, 8, 4, 4, 4, 4, (255, 120, 40), (255, 240, 200), power=1.2, core_frac=0.5))
    for i in range(6):
        art.write("boom%d.gzi" % i, explosion(i))
    art.write("sea.gzi", sea(), alpha=False)
    art.write("island0.gzi", island(11, 64, 46))
    art.write("island1.gzi", island(23, 48, 40))
    art.write("cloud0.gzi", cloud(1))
    art.write("cloud1.gzi", cloud(2, 52, 30))
    art.icon(icon())
    art.finish()


if __name__ == "__main__":
    main()
