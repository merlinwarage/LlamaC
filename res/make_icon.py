"""make_icon.py - draws the LlamaC app icon and writes res/llamac.ico (no third-party packages).

A front-facing llama head in white on an indigo-violet rounded tile (the app's accent colours).
Shapes are signed distance functions; every icon size is rendered directly with supersampling, so the
small sizes stay sharp instead of being scaled down from a big image.

    python res/make_icon.py              -> res/llamac.ico
    python res/make_icon.py --preview D  -> also D/icon-256.png and D/icon-sizes.png (small sizes enlarged)
"""
import math
import os
import struct
import sys
import zlib

SIZES = [16, 20, 24, 32, 40, 48, 64, 256]

BG0 = (129, 140, 248)  # top-left     #818CF8
BG1 = (109, 40, 217)   # bottom-right #6D28D9
WHITE = (255, 255, 255)
MUZZLE = (232, 234, 255)
INNER_EAR = (199, 210, 254)
EYE = (30, 27, 75)


# ---------- signed distance functions (unit square, y down; negative = inside) ----------
def sd_round_box(x, y, cx, cy, hx, hy, r):
    qx = abs(x - cx) - hx + r
    qy = abs(y - cy) - hy + r
    return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - r


def sd_ellipse(x, y, cx, cy, rx, ry, rot=0.0):
    dx, dy = x - cx, y - cy
    if rot:
        c, s = math.cos(rot), math.sin(rot)
        dx, dy = c * dx + s * dy, -s * dx + c * dy
    return (math.hypot(dx / rx, dy / ry) - 1.0) * min(rx, ry)


def sd_circle(x, y, cx, cy, r):
    return math.hypot(x - cx, y - cy) - r


def smin(a, b, k):
    h = max(k - abs(a - b), 0.0) / k
    return min(a, b) - h * h * k * 0.25


EAR_ROT = math.radians(9)


def sd_bg(x, y, size):
    half = 0.47 if size >= 32 else 0.49  # small icons use the whole cell
    return sd_round_box(x, y, 0.5, 0.5, half, half, 0.21)


def sd_llama(x, y):
    d = sd_ellipse(x, y, 0.5, 0.50, 0.205, 0.185)                      # head
    d = smin(d, sd_ellipse(x, y, 0.5, 0.645, 0.112, 0.112), 0.04)      # muzzle
    for cx, cy, r in ((0.395, 0.36, 0.06), (0.448, 0.318, 0.06), (0.5, 0.300, 0.062), (0.552, 0.318, 0.06), (0.605, 0.36, 0.06)):
        d = smin(d, sd_circle(x, y, cx, cy, r), 0.012)                 # woolly fringe between the ears
    d = smin(d, sd_ellipse(x, y, 0.365, 0.235, 0.052, 0.118, -EAR_ROT), 0.025)
    d = smin(d, sd_ellipse(x, y, 0.635, 0.235, 0.052, 0.118, EAR_ROT), 0.025)
    d = smin(d, sd_round_box(x, y, 0.5, 0.88, 0.15, 0.26, 0.06), 0.05)  # neck, cut by the tile
    return d


def shade(x, y, size):
    """colour (r, g, b, a) of one sample"""
    if sd_bg(x, y, size) > 0:
        return (0, 0, 0, 0)
    t = min(max((x + y) * 0.5, 0.0), 1.0)
    col = tuple(BG0[i] + (BG1[i] - BG0[i]) * t for i in range(3))
    if sd_llama(x, y) < 0:
        col = WHITE
        if size >= 24:
            if sd_ellipse(x, y, 0.5, 0.655, 0.098, 0.092) < 0:
                col = MUZZLE
            if (sd_ellipse(x, y, 0.368, 0.245, 0.024, 0.075, -EAR_ROT) < 0
                    or sd_ellipse(x, y, 0.632, 0.245, 0.024, 0.075, EAR_ROT) < 0):
                col = INNER_EAR
        er = 0.03 if size < 32 else 0.027
        for ex in (0.425, 0.575):
            if sd_circle(x, y, ex, 0.49, er) < 0:
                col = EYE
                if size >= 64 and sd_circle(x, y, ex + 0.009, 0.481, 0.009) < 0:
                    col = WHITE  # catch-light
        if size >= 32:
            for nx in (0.474, 0.526):
                if sd_ellipse(x, y, nx, 0.632, 0.013, 0.009, 0.5 if nx < 0.5 else -0.5) < 0:
                    col = EYE
        if size >= 64:  # the split upper lip: a short line down to a shallow smile
            w = 0.006
            if abs(x - 0.5) < w and 0.64 < y < 0.672:
                col = EYE
            if abs(math.hypot(x - 0.5, y - 0.632) - 0.042) < w and y > 0.664 and abs(x - 0.5) < 0.03:
                col = EYE
    return (col[0], col[1], col[2], 255)


def render(size):
    ss = 4 if size < 256 else 3
    n = ss * ss
    out = bytearray(size * size * 4)
    for py in range(size):
        for px in range(size):
            r = g = b = a = 0.0
            for sy in range(ss):
                for sx in range(ss):
                    c = shade((px + (sx + 0.5) / ss) / size, (py + (sy + 0.5) / ss) / size, size)
                    al = c[3] / 255.0
                    r += c[0] * al
                    g += c[1] * al
                    b += c[2] * al
                    a += al
            o = (py * size + px) * 4
            if a > 0:
                out[o:o + 4] = bytes((round(r / a), round(g / a), round(b / a), round(a / n * 255)))
    return bytes(out)


# ---------- file formats ----------
def png(size, rgba, scale=1):
    w = size * scale
    rows = bytearray()
    for y in range(w):
        rows.append(0)
        sy = y // scale
        for x in range(w):
            o = (sy * size + x // scale) * 4
            rows += rgba[o:o + 4]

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, w, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b""))


def dib(size, rgba):
    """32-bit BGRA bitmap for an ICO entry: bottom-up rows, then an all-zero AND mask"""
    mask_row = ((size + 31) // 32) * 4
    px = bytearray()
    for y in range(size - 1, -1, -1):
        for x in range(size):
            o = (y * size + x) * 4
            px += bytes((rgba[o + 2], rgba[o + 1], rgba[o], rgba[o + 3]))
    hdr = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, len(px) + mask_row * size, 0, 0, 0, 0)
    return hdr + bytes(px) + bytes(mask_row * size)


def ico(images):
    data = [png(s, p) if s >= 256 else dib(s, p) for s, p in images]
    out = struct.pack("<HHH", 0, 1, len(images))
    off = 6 + 16 * len(images)
    for (s, _), d in zip(images, data):
        out += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(d), off)
        off += len(d)
    return out + b"".join(data)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    images = [(s, render(s)) for s in SIZES]
    with open(os.path.join(here, "llamac.ico"), "wb") as f:
        f.write(ico(images))
    if "--preview" in sys.argv:
        d = sys.argv[sys.argv.index("--preview") + 1]
        big = dict(images)[256]
        with open(os.path.join(d, "icon-256.png"), "wb") as f:
            f.write(png(256, big))
        # small sizes enlarged 4x side by side, on a mid-grey strip
        small = [(s, p) for s, p in images if s <= 64]
        W = sum(s * 4 + 16 for s, _ in small) + 16
        H = 64 * 4 + 32
        sheet = bytearray(bytes((90, 90, 100, 255)) * (W * H))
        x0 = 16
        for s, p in small:
            for y in range(s * 4):
                for x in range(s * 4):
                    o = ((y // 4) * s + x // 4) * 4
                    a = p[o + 3] / 255.0
                    t = ((16 + y) * W + x0 + x) * 4
                    for i in range(3):
                        sheet[t + i] = round(p[o + i] * a + sheet[t + i] * (1 - a))
            x0 += s * 4 + 16
        rows = bytearray()
        for y in range(H):
            rows.append(0)
            rows += sheet[y * W * 4:(y + 1) * W * 4]

        def chunk(tag, data):
            return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

        with open(os.path.join(d, "icon-sizes.png"), "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0))
                    + chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b""))
    print("wrote", os.path.join(here, "llamac.ico"), "sizes", SIZES)


if __name__ == "__main__":
    main()
