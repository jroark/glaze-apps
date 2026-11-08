"""Shared helpers for the games' art scripts (packages/<id>/art/draw_art.py).

Needs Pillow. Each script does:

    sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "tools"))
    import gzart
    art = gzart.Art(__file__)          # writes to packages/<id>/res
    art.write("ball.gzi", image)
    art.finish()                       # --sheet out.png writes a contact sheet

Everything is drawn procedurally: shapes at SS x then box-filtered down for
anti-aliased edges, spheres shaded per pixel, tileable value noise.

GZI: "GZI1", u16 w, u16 h, u16 flags (1 = alpha), u16 pad, w*h RGB565 LE,
then w*h A8 when flags & 1.
"""
import math
import os
import random
import struct
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter  # noqa: F401  (re-exported for scripts)

SS = 4


class Art:
    def __init__(self, script):
        self.res = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(script))), "res")
        os.makedirs(self.res, exist_ok=True)
        for f in os.listdir(self.res):
            if f.endswith(".gzi"):
                os.remove(os.path.join(self.res, f))
        self.out = {}
        print("writing %s" % self.res)

    def write(self, name, im, alpha=True):
        im = im.convert("RGBA")
        w, h = im.size
        px = im.load()
        rgb = bytearray()
        a8 = bytearray()
        for y in range(h):
            for x in range(w):
                r, g, b, a = px[x, y]
                rgb += struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
                a8.append(a)
        with open(os.path.join(self.res, name), "wb") as f:
            f.write(b"GZI1" + struct.pack("<HHHH", w, h, 1 if alpha else 0, 0) + rgb)
            if alpha:
                f.write(bytes(a8))
        self.out[name] = im
        print("  %-16s %dx%d" % (name, w, h))

    def finish(self):
        if "--sheet" in sys.argv:
            sheet(self.out, sys.argv[sys.argv.index("--sheet") + 1])


def sheet(images, path, sc=3, cols=6, pad=6):
    items = list(images.items())
    cw = min(max(im.size[0] for _, im in items) * sc, 240 * 2) + pad
    ch = min(max(im.size[1] for _, im in items) * sc, 320 * 2) + pad
    rows = (len(items) + cols - 1) // cols
    out = Image.new("RGB", (cols * cw, rows * ch), (40, 44, 52))
    for i, (_, im) in enumerate(items):
        k = sc if im.size[0] * sc <= cw and im.size[1] * sc <= ch else 1
        z = im.resize((im.size[0] * k, im.size[1] * k), Image.NEAREST)
        out.paste(z, ((i % cols) * cw, (i // cols) * ch), z)
    out.save(path)
    print("sheet: %s" % path)


# ---- drawing ----

def big(w, h):
    im = Image.new("RGBA", (int(w * SS), int(h * SS)), (0, 0, 0, 0))
    return im, ImageDraw.Draw(im)


def down(im):
    w, h = im.size
    return im.convert("RGBa").resize((w // SS, h // SS), Image.BOX).convert("RGBA")


def S(*v):
    return [c * SS for c in v]


def P(pts):
    return [(x * SS, y * SS) for x, y in pts]


def mix(a, b, t):
    t = max(0.0, min(1.0, t))
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def rgb(hex_):
    return ((hex_ >> 16) & 255, (hex_ >> 8) & 255, hex_ & 255)


def sphere(im, cx, cy, r, dark, lit, spec=(255, 255, 255), light=(-0.55, -0.65), rim=0.7, shine=14):
    """Per-pixel shaded ball on a supersampled canvas (coords in final px)."""
    px = im.load()
    lx, ly = light
    lz = math.sqrt(max(0.0, 1 - lx * lx - ly * ly))
    R = r * SS
    for y in range(int((cy - r) * SS), int((cy + r) * SS) + 1):
        for x in range(int((cx - r) * SS), int((cx + r) * SS) + 1):
            if not (0 <= x < im.size[0] and 0 <= y < im.size[1]):
                continue
            nx = (x + 0.5 - cx * SS) / R
            ny = (y + 0.5 - cy * SS) / R
            d = nx * nx + ny * ny
            if d > 1:
                continue
            nz = math.sqrt(1 - d)
            lam = max(0.0, nx * lx + ny * ly + nz * lz)
            c = mix(dark, lit, lam * 0.9 + 0.1)
            if lam > 0 and shine:
                c = mix(c, spec, max(0.0, nz * 2 * lam - lz) ** shine)
            edge = max(0.0, (math.sqrt(d) - 0.82) / 0.18)
            c = mix(c, (10, 12, 24), edge * rim)
            px[x, y] = c + (255,)


def glow(w, h, cx, cy, rx, ry, color, core=(255, 255, 255), power=2.0, core_frac=0.35, gain=1.6):
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = im.load()
    for y in range(h):
        for x in range(w):
            dx = (x + 0.5 - cx) / rx
            dy = (y + 0.5 - cy) / ry
            d = math.sqrt(dx * dx + dy * dy)
            if d >= 1:
                continue
            a = (1 - d) ** power
            c = mix(color, core, max(0.0, (core_frac - d) / core_frac)) if core_frac else color
            px[x, y] = c + (int(255 * min(1.0, a * gain)),)
    return im


def bevel_tile(w, h, base, radius=2, depth=2, light=0.45, shade=0.45, gloss=True):
    """A raised, rounded, lit-from-top-left tile (final px)."""
    im, d = big(w, h)
    hi = mix(base, (255, 255, 255), light)
    lo = mix(base, (0, 0, 0), shade)
    r = radius * SS
    d.rounded_rectangle(S(0, 0, w, h), radius=r, fill=lo + (255,))
    d.rounded_rectangle(S(0, 0, w - depth * 0.6, h - depth * 0.6), radius=r, fill=hi + (255,))
    d.rounded_rectangle(S(depth * 0.5, depth * 0.5, w - depth * 0.5, h - depth * 0.5), radius=max(1, r - SS),
                        fill=base + (255,))
    out = down(im)
    if gloss:
        g, gd = big(w, h)
        gd.rounded_rectangle(S(depth * 0.5 + 0.5, depth * 0.5 + 0.3, w - depth * 0.5 - 0.5, h * 0.45),
                             radius=max(1, r - SS), fill=(255, 255, 255, 52))
        out.alpha_composite(down(g))
    return out


def vgrad(w, h, top, bottom):
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        c = mix(top, bottom, y / max(1, h - 1))
        for x in range(w):
            px[x, y] = c
    return im


def value_noise(w, h, cell, seed, wrap_x=True, wrap_y=True):
    rnd = random.Random(seed)
    nx, ny = max(1, w // cell), max(1, h // cell)
    grid = [[rnd.random() for _ in range(nx + 2)] for _ in range(ny + 3)]

    def g(ix, iy):
        if wrap_x:
            ix %= nx
        if wrap_y:
            iy %= ny
        return grid[iy][ix]

    out = [[0.0] * w for _ in range(h)]
    for y in range(h):
        fy = y / cell
        iy = int(fy)
        ty = fy - iy
        ty = ty * ty * (3 - 2 * ty)
        for x in range(w):
            fx = x / cell
            ix = int(fx)
            tx = fx - ix
            tx = tx * tx * (3 - 2 * tx)
            a = g(ix, iy) + (g(ix + 1, iy) - g(ix, iy)) * tx
            b = g(ix, iy + 1) + (g(ix + 1, iy + 1) - g(ix, iy + 1)) * tx
            out[y][x] = a + (b - a) * ty
    return out


def fbm(w, h, cells, seed, **kw):
    out = [[0.0] * w for _ in range(h)]
    amp, total = 1.0, 0.0
    for i, c in enumerate(cells):
        layer = value_noise(w, h, c, seed + i, **kw)
        for y in range(h):
            row, src = out[y], layer[y]
            for x in range(w):
                row[x] += src[x] * amp
        total += amp
        amp *= 0.5
    for y in range(h):
        for x in range(w):
            out[y][x] /= total
    return out


def noise_image(w, h, cells, seed, lo, hi, contrast=1.4, **kw):
    """Colour noise texture between lo and hi (tileable by default)."""
    n = fbm(w, h, cells, seed, **kw)
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = mix(lo, hi, 0.5 + (n[y][x] - 0.5) * contrast)
    return im


def shadow(im, offset=(1, 1), blur=1.0, alpha=110):
    """Drop shadow under a sprite, same size plus the offset."""
    w, h = im.size
    ox, oy = offset
    pad = int(blur * 2) + 1
    out = Image.new("RGBA", (w + pad * 2 + ox, h + pad * 2 + oy), (0, 0, 0, 0))
    a = im.split()[3].point(lambda v: v * alpha // 255)
    sh = Image.new("RGBA", im.size, (0, 0, 0, 255))
    sh.putalpha(a)
    out.alpha_composite(sh, (pad + ox, pad + oy))
    out = out.filter(ImageFilter.GaussianBlur(blur))
    out.alpha_composite(im, (pad, pad))
    return out


# ---- home-screen icons (44x44, beside the sources as icon.gzi) ----

ICON = 44
ICON_R = 9


def icon_mask():
    m, d = big(ICON, ICON)
    d.rounded_rectangle(S(0, 0, ICON, ICON), radius=ICON_R * SS, fill=(255, 255, 255, 255))
    return down(m).split()[3]


def icon_tile(top, bottom):
    """The icon's background: a vertical gradient, full square (masked later)."""
    return vgrad(ICON, ICON, top, bottom).convert("RGBA")


def icon_finish(im, gloss=60, border=(0, 0, 0)):
    """Gloss band across the top, a soft inner edge, then the rounded mask."""
    im = im.convert("RGBA")
    g, gd = big(ICON, ICON)
    gd.ellipse(S(-14, -30, ICON + 14, ICON * 0.48), fill=(255, 255, 255, gloss))
    im.alpha_composite(down(g))
    e, ed = big(ICON, ICON)
    ed.rounded_rectangle(S(0.5, 0.5, ICON - 0.5, ICON - 0.5), radius=ICON_R * SS, outline=border + (90,), width=SS)
    im.alpha_composite(down(e))
    im.putalpha(icon_mask())
    return im


def _icon_write(self, im):
    """Write packages/<id>/icon.gzi (the packer takes it from beside the sources)."""
    path = os.path.join(os.path.dirname(self.res), "icon.gzi")
    saved = self.res
    self.res = os.path.dirname(path)
    try:
        self.write("icon.gzi", im)
    finally:
        self.res = saved
    png = os.path.join(os.path.dirname(path), "icon.png")
    if os.path.exists(png):                     # some packages keep the icon's source image too
        im.save(png)


Art.icon = _icon_write


def font(size, serif=False):
    """A bold TrueType font for icon lettering (system font, else Pillow's own)."""
    from PIL import ImageFont
    names = (["/System/Library/Fonts/Supplemental/Georgia Bold.ttf", "DejaVuSerif-Bold.ttf"] if serif else
             ["/System/Library/Fonts/Supplemental/Arial Bold.ttf", "DejaVuSans-Bold.ttf",
              "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"])
    for n in names:
        try:
            return ImageFont.truetype(n, size)
        except OSError:
            pass
    return ImageFont.load_default(size)


def wood(w, h, seed, light=(196, 150, 96), dark=(132, 88, 50), grain=6.0):
    """Wood texture: long stretched noise for grain plus fine streaks."""
    n = fbm(w, h, [max(4, w // 2), max(2, w // 6)], seed, wrap_x=False, wrap_y=False)
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            v = n[y][x]
            t = 0.5 + 0.5 * math.sin((y * 0.9 + v * grain * 3 + x * 0.05) * 1.3)
            px[x, y] = mix(dark, light, 0.35 + t * 0.4 + (v - 0.5) * 0.6)
    return im
