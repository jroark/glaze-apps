#!/usr/bin/env python3
"""Draw the TwixT package icon (board corner, red rod accepted, blue
rod refused at the crossing); standard-library only. Writes
packages/twixt/icon.png and icon.gzi."""
from pathlib import Path
import math
import struct
import zlib


def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px-ax)*dx + (py-ay)*dy) / (dx*dx + dy*dy)))
    return math.hypot(px - ax - t*dx, py - ay - t*dy)


def rounded(x, y, rect, radius):
    left, top, right, bottom = rect
    if not left <= x <= right or not top <= y <= bottom:
        return False
    cx = min(max(x, left + radius), right - radius)
    cy = min(max(y, top + radius), bottom - radius)
    return (x-cx)**2 + (y-cy)**2 <= radius**2


BOARD = (58, 81, 112)      # 0x3A5170
NAVY = (16, 27, 43)        # 0x101B2B
RED_EDGE = (184, 74, 64)   # 0xB84A40
BLUE_EDGE = (34, 50, 74)   # 0x22324A
RED_ROD = (210, 84, 70)    # 0xD25446
BLUE_ROD = (147, 166, 192) # 0x93A6C0
DOT = (157, 180, 210)      # 0x9DB4D2

# 4x4 grid of holes; rods are knight-move shaped (8.5 x 17)
GRID = [(11 + 8.5*i, 11 + 8.5*j) for j in range(4) for i in range(4)]
RED_A, RED_B = (28, 11), (19.5, 28)     # red plays first and takes the spot
BLUE_A, BLUE_B = (11, 19.5), (28, 28)   # its rod is refused at the crossing


def pixel(x, y):
    if not rounded(x, y, (0, 0, 44, 44), 8):
        return (0, 0, 0, 0)
    if (x < 8 or x > 36) and (y < 8 or y > 36):
        color = NAVY                                   # corner squares
    else:
        color = BOARD
        if 3 <= y < 6 or 38 <= y < 41:
            color = RED_EDGE                           # red home bands
        if 3 <= x < 6 or 38 <= x < 41:
            color = BLUE_EDGE                          # blue home bands
    dr = seg_dist(x, y, *RED_A, *RED_B)
    db = seg_dist(x, y, *BLUE_A, *BLUE_B)
    if db < 1.6 and dr > 2.4:                          # blue, broken
        color = BLUE_ROD
    if dr < 1.6:                                       # red rod intact
        color = RED_ROD
    for gx, gy in GRID:
        d = math.hypot(x-gx, y-gy)
        if d < 1.7:
            if math.hypot(gx-RED_A[0], gy-RED_A[1]) < .1 or \
               math.hypot(gx-RED_B[0], gy-RED_B[1]) < .1:
                color = RED_ROD
            elif math.hypot(gx-BLUE_A[0], gy-BLUE_A[1]) < .1 or \
                 math.hypot(gx-BLUE_B[0], gy-BLUE_B[1]) < .1:
                color = BLUE_ROD
            else:
                color = DOT
    return color + (255,)


def main():
    target = Path(__file__).resolve().parents[1] / 'packages/twixt'
    pixels = []
    for y in range(44):
        for x in range(44):
            samples = [pixel(x+(i+.5)/4, y+(j+.5)/4) for j in range(4) for i in range(4)]
            alpha = sum(p[3] for p in samples)
            pixels.append(tuple(round(sum(p[c]*p[3] for p in samples)/alpha) if alpha else 0
                                for c in range(3)) + (round(alpha/16),))
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind+data))
    raw = b''.join(b'\0' + bytes(v for p in pixels[y*44:(y+1)*44] for v in p) for y in range(44))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 44, 44, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
    (target/'icon.png').write_bytes(png)
    rgb = b''.join(struct.pack('<H', (r >> 3) << 11 | (g >> 2) << 5 | b >> 3) for r, g, b, a in pixels)
    (target/'icon.gzi').write_bytes(b'GZI1' + struct.pack('<HHHH', 44, 44, 1, 0) + rgb + bytes(p[3] for p in pixels))


if __name__ == '__main__':
    main()
