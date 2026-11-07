#!/usr/bin/env python3
"""Draw the original Maps package icon (a street map with a position dot);
standard-library only. Writes packages/maps/icon.png and icon.gzi."""
from pathlib import Path
import math
import struct
import zlib


def rounded(x, y, rect, radius):
    left, top, right, bottom = rect
    if not left <= x <= right or not top <= y <= bottom:
        return False
    cx = min(max(x, left + radius), right - radius)
    cy = min(max(y, top + radius), bottom - radius)
    return (x-cx)**2 + (y-cy)**2 <= radius**2


def pixel(x, y):
    if not rounded(x, y, (0, 0, 44, 44), 8):
        return (0, 0, 0, 0)
    color = (236, 231, 219, 255)                       # land
    if 26 < x < 40 and 5 < y < 17:
        color = (196, 226, 180, 255)                   # park
    if abs((y - 44) + x * 0.9 - 6) < 4.2:
        color = (150, 196, 226, 255)                   # river
    for road in (12, 31):
        if abs(x - road) < 1.6 or abs(y - road + 6) < 1.6:
            color = (255, 255, 255, 255)
    if abs((x - 4) - (y - 2) * 0.55 - 14) < 1.9 and y < 24:
        color = (247, 205, 96, 255)                    # main road
    d = math.hypot(x - 22, y - 24)
    if d < 7.5:
        color = (210, 228, 250, 255)                   # accuracy ring
    if d < 5:
        color = (255, 255, 255, 255)
    if d < 3.6:
        color = (30, 120, 230, 255)
    return color


def main():
    target = Path(__file__).resolve().parents[1] / 'packages/maps'
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
