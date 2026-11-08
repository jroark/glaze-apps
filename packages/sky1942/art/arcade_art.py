#!/usr/bin/env python3
"""OPTIONAL: fetch the ripped 1942 arcade sprites from
github.com/kavros/1942-Arcade-Game and convert them to res/*.gzi, replacing
this app's own art with the arcade originals.

    python3 packages/sky1942/art/arcade_art.py

Needs only the Python standard library. The source PNGs land in art/src/.
Warning: res/ holds this app's own committed sprites (see draw_art.py), so
after this run git will show res/*.gzi as MODIFIED. Do not commit them:
restore the default art with `python3 packages/sky1942/art/draw_art.py`
(or `git checkout -- packages/sky1942/res`).

The upstream repo is Apache-2.0 for its code, but its README says the sheets
are the original Capcom sprites, which the students cannot license; keep the
swapped build for yourself only.

GZI: "GZI1", u16 w, u16 h, u16 flags (1 = alpha), u16 pad,
w*h RGB565 LE, w*h A8.
"""
import os
import struct
import sys
import urllib.request
import zlib

BASE = "https://raw.githubusercontent.com/kavros/1942-Arcade-Game/master/resources/images"
HERE = os.path.dirname(os.path.abspath(__file__))
PKG = os.path.dirname(HERE)                      # packages/sky1942
SRC = os.path.join(HERE, "src")
RES = os.path.join(PKG, "res")

SOURCES = [
    ("white_up.png", BASE + "/white_plane/white_up.png"),
    ("sideFighters.png", BASE + "/sideFighters.png"),
    ("red_plane.png", BASE + "/red_plane.png"),
    ("bigGrey.png", BASE + "/bigGrey.png"),
    ("bullets.png", BASE + "/bullets.png"),
    ("explosion.png", BASE + "/explosion.png"),
]

# ---------------------------------------------------------------- PNG decode


def png_decode(path):
    """Decode an 8-bit non-interlaced PNG to (w, h, RGBA bytearray)."""
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, plte, trns, ihdr = 8, b"", None, None, None
    while pos + 8 <= len(data):
        ln = int.from_bytes(data[pos:pos + 4], "big")
        typ, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            ihdr = body
        elif typ == b"IDAT":
            idat += body
        elif typ == b"PLTE":
            plte = body
        elif typ == b"tRNS":
            trns = body
        pos += 12 + ln
    w, h, bd, ct, comp, filt, inter = struct.unpack(">IIBBBBB", ihdr)
    assert bd == 8 and inter == 0 and ct in (0, 2, 3, 6), "need 8-bit flat PNG"
    ch = {0: 1, 2: 3, 3: 1, 6: 4}[ct]
    raw = zlib.decompress(idat)
    rgba = bytearray(w * h * 4)
    prev = bytearray(w * ch)
    p = 0
    for y in range(h):
        f = raw[p]
        p += 1
        line = bytearray(raw[p:p + w * ch])
        p += w * ch
        if f == 1:
            for i in range(ch, len(line)):
                line[i] = (line[i] + line[i - ch]) & 255
        elif f == 2:
            for i in range(len(line)):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(len(line)):
                a = line[i - ch] if i >= ch else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(len(line)):
                a = line[i - ch] if i >= ch else 0
                b, c = prev[i], prev[i - ch] if i >= ch else 0
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        for x in range(w):
            if ct == 6:
                r, g, b, a = line[x * 4:x * 4 + 4]
            elif ct == 2:
                r, g, b, a = line[x * 3], line[x * 3 + 1], line[x * 3 + 2], 255
            elif ct == 0:
                r = g = b = line[x]
                a = 255
            else:
                idx = line[x]
                r, g, b = plte[idx * 3], plte[idx * 3 + 1], plte[idx * 3 + 2]
                a = trns[idx] if trns and idx < len(trns) else 255
            o = y * w * 4 + x * 4
            rgba[o:o + 4] = bytes((r, g, b, a))
        prev = line
    return [w, h, rgba]


# ------------------------------------------------------------- image helpers


def at(img, x, y):
    o = (y * img[0] + x) * 4
    return img[2][o], img[2][o + 1], img[2][o + 2], img[2][o + 3]


def bbox(img, x0=0, y0=0, x1=None, y1=None):
    """Tight box of non-transparent pixels inside a region, or None."""
    w, h = img[0], img[1]
    x1, y1 = min(x1 or w, w), min(y1 or h, h)
    bx0, by0, bx1, by1 = x1, y1, x0, y0
    for y in range(y0, y1):
        for x in range(x0, x1):
            if img[2][(y * w + x) * 4 + 3]:
                bx0, by0 = min(bx0, x), min(by0, y)
                bx1, by1 = max(bx1, x + 1), max(by1, y + 1)
    return (bx0, by0, bx1, by1) if bx1 > bx0 else None


def crop(img, box):
    x0, y0, x1, y1 = box
    w = x1 - x0
    out = [w, y1 - y0, bytearray(w * (y1 - y0) * 4)]
    for y in range(y1 - y0):
        s = ((y0 + y) * img[0] + x0) * 4
        out[2][y * w * 4:(y + 1) * w * 4] = img[2][s:s + w * 4]
    return out


def rot180(img):
    w, h, d = img
    o = bytearray(len(d))
    for y in range(h):
        for x in range(w):
            s = (y * w + x) * 4
            t = ((h - 1 - y) * w + (w - 1 - x)) * 4
            o[t:t + 4] = d[s:s + 4]
    return [w, h, o]


def rot90cw(img):
    w, h, d = img
    o = bytearray(len(d))
    for y in range(h):
        for x in range(w):
            s = (y * w + x) * 4
            t = (x * h + (h - 1 - y)) * 4
            o[t:t + 4] = d[s:s + 4]
    return [h, w, o]


def resize(img, tw, th):
    w, h, d = img
    o = bytearray(tw * th * 4)
    for y in range(th):
        sy = y * h // th
        for x in range(tw):
            sx = x * w // tw
            s = (sy * w + sx) * 4
            t = (y * tw + x) * 4
            o[t:t + 4] = d[s:s + 4]
    return [tw, th, o]


def col_runs(img, y0, y1):
    """x ranges of components separated by empty columns, within rows y0..y1."""
    w = img[0]
    solid = [any(img[2][(y * w + x) * 4 + 3] for y in range(y0, y1))
             for x in range(w)]
    runs, x = [], 0
    while x < w:
        if solid[x]:
            x0 = x
            while x < w and solid[x]:
                x += 1
            runs.append((x0, x))
        else:
            x += 1
    return runs


def write_gzi(name, img):
    w, h, d = img
    rgb = bytearray()
    alpha = bytearray()
    for i in range(w * h):
        r, g, b, a = d[i * 4:i * 4 + 4]
        rgb += struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
        alpha.append(a)
    with open(os.path.join(RES, name), "wb") as f:
        f.write(b"GZI1" + struct.pack("<HHHH", w, h, 1, 0) + rgb + bytes(alpha))
    print("  %-12s %dx%d" % (name, w, h))


# ------------------------------------------------------------------- assemble


def main():
    os.makedirs(SRC, exist_ok=True)
    os.makedirs(RES, exist_ok=True)
    for name, url in SOURCES:
        dst = os.path.join(SRC, name)
        if not os.path.exists(dst):
            print("get %s" % name)
            req = urllib.request.Request(url, headers={"User-Agent": "sky1942-art"})
            open(dst, "wb").write(urllib.request.urlopen(req).read())

    dec = {n: png_decode(os.path.join(SRC, n)) for n, _ in SOURCES}

    # Player: the white plane, nose up.
    p = dec["white_up.png"]
    write_gzi("player.gzi", crop(p, bbox(p)))

    # Fighter: first olive frontal fighter of the sheet's top row, nose down.
    sf = dec["sideFighters.png"]
    runs = col_runs(sf, 0, sf[1] // 2)
    x0, x1 = runs[0]
    f = crop(sf, bbox(sf, x0, 0, x1, sf[1] // 2))
    write_gzi("fighter.gzi", rot180(f))

    # Diver: red plane strip of 14 fixed cells; pick the most horizontal frame
    # (right-pointing nose) and turn the nose down.
    rp = dec["red_plane.png"]
    n = 14
    cw = rp[0] // n
    cells = [(i * cw, (i + 1) * cw) for i in range(n)]
    def cell_w(c):
        b = bbox(rp, c[0], 0, c[1], rp[1])
        return b[2] - b[0]
    best = max(cells, key=cell_w)
    d = crop(rp, bbox(rp, best[0], 0, best[1], rp[1]))
    write_gzi("diver.gzi", rot90cw(d))

    # Bomber: big grey twin engine, nose down, sized to its collision box.
    bg = dec["bigGrey.png"]
    b = rot180(crop(bg, bbox(bg)))
    w = 30
    write_gzi("bomber.gzi", resize(b, w, max(1, b[1] * w // b[0])))

    # Bullets sheet: tallest component is the player tracer, the reddest blob
    # becomes the enemy bomb.
    bl = dec["bullets.png"]
    comps = col_runs(bl, 0, bl[1])
    tallest = max(comps, key=lambda c: bbox(bl, c[0], 0, c[1], bl[1])[3]
                  - bbox(bl, c[0], 0, c[1], bl[1])[1])
    write_gzi("shot.gzi", crop(bl, bbox(bl, tallest[0], 0, tallest[1], bl[1])))
    reddest = max(comps, key=lambda c: sum(at(bl, x, y)[0] - at(bl, x, y)[1]
                                           for x in range(c[0], c[1])
                                           for y in range(bl[1])
                                           if at(bl, x, y)[3]) // (c[1] - c[0] + 1))
    box = bbox(bl, reddest[0], 0, reddest[1], bl[1])
    write_gzi("bomb.gzi", resize(crop(bl, box), 7, 7))

    # Explosion: six frames in a row, kept at native size.
    ex = dec["explosion.png"]
    runs = col_runs(ex, 0, ex[1])
    assert len(runs) == 6, "expected 6 explosion frames, got %d" % len(runs)
    for i, (x0, x1) in enumerate(runs):
        write_gzi("boom%d.gzi" % i, crop(ex, bbox(ex, x0, 0, x1, ex[1])))

    print("wrote %d files to %s" % (12, RES))


if __name__ == "__main__":
    sys.exit(main())
