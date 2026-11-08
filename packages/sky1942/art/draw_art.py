#!/usr/bin/env python3
"""Draw sky1942's original sprites (the default art) straight to res/*.gzi.

    python3 packages/sky1942/art/draw_art.py

All art here was drawn for this app: the pixel maps below are hand authored
(one character = one pixel, '.' transparent), the explosions are plain
concentric colour rings. Needs only the Python standard library.
arcade_art.py replaces these with the ripped originals from
kavros/1942-Arcade-Game if you want that look; re-run this to restore.

GZI: "GZI1", u16 w, u16 h, u16 flags (1 = alpha), u16 pad,
w*h RGB565 LE, w*h A8.
"""
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(os.path.dirname(HERE), "res")


def write_gzi(name, w, h, rgba):
    """rgba: bytearray of w*h RGBA."""
    rgb = bytearray()
    alpha = bytearray()
    for i in range(w * h):
        r, g, b, a = rgba[i * 4:i * 4 + 4]
        rgb += struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
        alpha.append(a)
    with open(os.path.join(RES, name), "wb") as f:
        f.write(b"GZI1" + struct.pack("<HHHH", w, h, 1, 0) + rgb + bytes(alpha))
    print("  %-12s %dx%d" % (name, w, h))


def from_map(lines, palette):
    """Pixel map: strings of palette keys; '.' = transparent."""
    h = len(lines)
    w = max(len(l) for l in lines)
    rgba = bytearray(w * h * 4)
    for y, line in enumerate(lines):
        assert len(line) == w, "row %d of the map is %d chars, expected %d" % (y, len(line), w)
        for x, key in enumerate(line):
            c = palette.get(key)
            if not c:
                continue
            o = (y * w + x) * 4
            rgba[o:o + 4] = bytes(c) + b"\xff"
    return w, h, rgba


# player: high-wing fighter, nose up; white wings, blue tips and canopy,
# red wing bars, red rudder.
player = from_map([
    "..........d..........",
    ".........sss.........",
    ".........sws.........",
    ".........sws.........",
    ".........sws.........",
    ".........bbb.........",
    ".........bwb.........",
    ".bwwwwwwwwwwwwwwwwwb.",
    ".bwrrwwwwwwwwwwwrrwb.",
    ".ddddddddddddddddddd.",
    ".........sws.........",
    ".........sws.........",
    ".........sws.........",
    ".........sws.........",
    ".....bwwwwwwwb.......",
    ".....ddddddddd.......",
    ".........sws.........",
    ".........sws.........",
    ".........rrr.........",
    ".........sws.........",
    "..........d..........",
], {".": None, "w": (240, 244, 248), "s": (150, 160, 172),
    "b": (62, 111, 163), "d": (40, 48, 60), "r": (215, 60, 45)})

# fighter: stubby olive plane, nose down (propeller at the bottom),
# red fuselage marking.
fighter = from_map([
    "........p........",
    ".....ggggggg.....",
    ".....GGGGGGG.....",
    ".....GGGGGGG.....",
    ".......GGG.......",
    ".GGGGGGGGGGGGGGG.",
    ".GGGGGGRRGGGGGGG.",
    ".GGGGGGRRGGGGGGG.",
    ".ggggggggggggggg.",
    ".......GGG.......",
    ".......GGG.......",
    "......GGGGG......",
    "......ppppp......",
    ".......ppp.......",
], {".": None, "G": (150, 142, 92), "g": (100, 94, 62),
    "R": (210, 60, 45), "p": (58, 56, 50)})

# diver: fast red plane, nose down, swept wings with yellow tips.
diver = from_map([
    ".........p.........",
    "........pRp........",
    "......RRRRRRR......",
    "......rrrrrrr......",
    "........RRR........",
    "........RRR........",
    "......RRRRRRR......",
    ".....RRRRRRRRR.....",
    "...yyRRRRRRRRRyy...",
    "...yyRRRRRRRRRyy...",
    ".....RRRRRRRRR.....",
    "......rrrrrrr......",
    ".......RRRRR.......",
    "......ppppppp......",
    ".........p.........",
], {".": None, "R": (210, 80, 60), "r": (150, 40, 36),
    "y": (240, 200, 80), "p": (58, 56, 50)})

# bomber: big green twin engine, nose down, red wing marks, prop blur discs.
bomber = from_map([
    "..............GG..............",
    "..............GG..............",
    ".........GGGGGGGGGGGG.........",
    ".........GGGGGGGGGGGG.........",
    ".........gggggggggggg.........",
    "..............GG..............",
    "..............GG..............",
    "..............GG..............",
    ".....pppp.....GG.....pppp.....",
    "..GGGppppGGGGGGGGGGGGppppGGGG.",
    "..GGGGGGGRRGGGGGGGGRRGGGGGGGG.",
    "..GGGppppGGGGGGGGGGGGppppGGGG.",
    "..ggggggggggggggggggggggggggg.",
    "..............GG..............",
    "..............GG..............",
    "..............GG..............",
    "..............GG..............",
    "..............GG..............",
    ".............GGGG.............",
    ".............RRRR.............",
    "..............GG..............",
], {".": None, "G": (110, 140, 74), "g": (74, 98, 48),
    "p": (45, 48, 45), "R": (210, 60, 45)})

# player tracer (yellow with a hot core) and the falling grey bomb.
shot = from_map([
    "YYY",
    "YYY",
    "YYY",
    "YOY",
    "YOY",
    "YOY",
    "OrO",
    "OrO",
    "OrO",
], {".": None, "Y": (255, 230, 130), "O": (245, 175, 70), "r": (230, 110, 50)})

bomb = from_map([
    ".......",
    "..www..",
    ".wdddw.",
    ".wdldw.",
    ".wdddw.",
    "..www..",
    ".......",
], {".": None, "w": (215, 220, 228), "d": (55, 60, 70), "l": (115, 120, 132)})

# explosions: a growing fireball that fades to smoke, six frames.
BOOM_R = [5, 6, 8, 9, 8, 7]
BOOM_CORE = [(255, 245, 170), (255, 245, 170), (255, 220, 110), (250, 170, 70),
             (150, 150, 150), (110, 110, 110)]
BOOM_RING = [(255, 200, 80), (250, 150, 60), (220, 70, 40), (190, 55, 35),
             (95, 95, 95), (70, 70, 70)]


def boom_frame(i):
    w = h = BOOM_R[i] * 2 + 3
    rgba = bytearray(w * h * 4)
    cx = cy = w // 2
    rng = (i * 2654435761 + 1) & 0xFFFFFFFF
    for y in range(h):
        for x in range(w):
            d2 = (x - cx) ** 2 + (y - cy) ** 2
            r = BOOM_R[i]
            rng = (rng * 1103515245 + 12345) & 0x7FFFFFFF
            if i >= 4 and rng % 5 == 0 and (r + 1) ** 2 < d2 < (r + 3) ** 2:
                c = BOOM_RING[i]        # smoke flecks around spent bursts
            elif d2 <= (r * 0.45) ** 2:
                c = BOOM_CORE[i]
            elif d2 <= r * r:
                c = BOOM_RING[i]
            else:
                continue
            o = (y * w + x) * 4
            rgba[o:o + 4] = bytes(c) + b"\xff"
    return w, h, rgba


def main():
    os.makedirs(RES, exist_ok=True)
    for name, img in [("player.gzi", player), ("fighter.gzi", fighter),
                      ("diver.gzi", diver), ("bomber.gzi", bomber),
                      ("shot.gzi", shot), ("bomb.gzi", bomb)]:
        write_gzi(name, *img)
    for i in range(6):
        write_gzi("boom%d.gzi" % i, *boom_frame(i))
    print("wrote 12 original sprites to %s" % RES)


if __name__ == "__main__":
    main()
