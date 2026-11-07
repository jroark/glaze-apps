#!/usr/bin/env python3
"""Draw a simple 44x44 home-screen icon straight to GZI (standard library only).

usage: mkicon.py OUT.gzi [RRGGBB]

A rounded tile in the given color with a lighter top half and a centered
light disc. Replace it with a real design later: draw a 44x44 PNG and run
python3 $GLAZE_SDK/tools/mkgzi.py icon.png packages/<id>/icon.gzi (needs
Pillow).
GZI: "GZI1", u16 w, u16 h, u16 flags (1 = alpha), u16 pad, w*h RGB565 LE, w*h A8.
"""
import struct
import sys

SIZE = 44


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def inside_rounded(x, y, radius=9):
    cx = min(max(x, radius), SIZE - 1 - radius)
    cy = min(max(y, radius), SIZE - 1 - radius)
    return (x - cx) ** 2 + (y - cy) ** 2 <= radius ** 2


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__.strip())
    color = sys.argv[2] if len(sys.argv) == 3 else "3C8F46"
    base = tuple(int(color[i:i + 2], 16) for i in (0, 2, 4))
    light = tuple(min(255, c + (255 - c) // 3) for c in base)
    rgb, alpha = bytearray(), bytearray()
    for y in range(SIZE):
        for x in range(SIZE):
            if not inside_rounded(x, y):
                rgb += struct.pack("<H", 0)
                alpha.append(0)
                continue
            r, g, b = light if y < SIZE // 2 else base
            if (x - 21.5) ** 2 + (y - 21.5) ** 2 <= 10 ** 2:
                r, g, b = 0xF5, 0xF0, 0xE0
            rgb += struct.pack("<H", rgb565(r, g, b))
            alpha.append(255)
    with open(sys.argv[1], "wb") as f:
        f.write(b"GZI1" + struct.pack("<HHHH", SIZE, SIZE, 1, 0) + rgb + alpha)


if __name__ == "__main__":
    main()
