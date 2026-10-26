#!/usr/bin/env python3
"""Render the Cartarchy board for the Amiga client as C data.

Inputs are the iOS app's own files, so every client plays on one map:
  ios/cartarchy/board_paths.json  the 42 territory outlines (SVG M/L/C/Z)
  ios/cartarchy/Models.swift      names, continents, bonuses, neighbours

The map is rasterised for a 640x200 hires screen, where a pixel is half as
wide as it is tall. Out come:
  - the territory and continent tables, in Models.swift order;
  - the base map as ByteRun1 bitplanes: ocean, coast shadow, graticule,
    borders and sea routes, with land in the unclaimed colour;
  - each territory's interior as scanline spans, which the client fills
    with the owner's colour and hit-tests clicks against;
  - where each territory's army badge goes.

Plain Python (zlib only). With --preview it also writes a PNG to look at.

Usage: mkmap.py <ios-dir> <out.c> [--preview out.png]
"""
import json
import math
import os
import re
import struct
import sys
import zlib

MAP_W, MAP_H = 496, 172
SRC_X, SRC_Y = 167.99651, 118.55507
SRC_W, SRC_H = 749.81909, 519.06781

# Pens shared with src/gfx.h
OCEAN, SHADOW, INK, WHITE, LAND, ROUTE = 0, 1, 2, 3, 4, 15

# Visible sea routes and the wrap from Alaska to Kamchatka, in board
# fractions (WorldMapGeometry.swift)
SEA_ROUTES = [
    ((0.333, 0.150), (0.423, 0.225)),
    ((0.386, 0.618), (0.468, 0.585)),
    ((0.500, 0.430), (0.480, 0.565)),
    ((0.585, 0.445), (0.585, 0.565)),
    ((0.655, 0.555), (0.625, 0.665)),
    ((0.785, 0.620), (0.805, 0.735)),
    ((0.0, 0.22), (0.055, 0.22)),
    ((0.94, 0.22), (1.0, 0.22)),
]


def parse_models(path):
    src = open(path).read()
    enum = re.search(r"enum Continent[^{]*\{(.*?)\n\}", src, re.S).group(1)
    cases = re.findall(r"(\w+) = \"([^\"]+)\"", enum)
    bonus = dict(re.findall(r"case \.(\w+): (\d+)", enum))
    continents = [(key, name, int(bonus[key])) for key, name in cases]
    order = [key for key, _, _ in continents]
    rows = re.findall(r't\("(\w+)","([^"]+)",\.(\w+),[\d.]+,[\d.]+,\[([^\]]*)\]\)', src)
    territories = []
    for tid, name, cont, neigh in rows:
        territories.append((tid, name, order.index(cont), re.findall(r'"(\w+)"', neigh)))
    if len(territories) != 42:
        sys.exit(f"{path}: found {len(territories)} territories, expected 42")
    return continents, territories


def to_px(x, y):
    return ((x - SRC_X) * MAP_W / SRC_W, (y - SRC_Y) * MAP_H / SRC_H)


def flatten(path):
    """SVG path -> list of closed polygons in map pixels."""
    tokens = re.findall(r"[MLCZz]|-?[\d.]+(?:e-?\d+)?", path)
    polys, poly, pos, i, cmd = [], [], (0.0, 0.0), 0, None
    nums = lambda n: [float(t) for t in tokens[i:i + n]]
    while i < len(tokens):
        t = tokens[i]
        if t in "MLCZz":
            cmd = t
            i += 1
            if cmd in "Zz":
                if poly:
                    polys.append(poly)
                poly = []
            continue
        if cmd == "M":
            if poly:
                polys.append(poly)
            pos = tuple(nums(2))
            poly = [to_px(*pos)]
            i += 2
            cmd = "L"
        elif cmd == "L":
            pos = tuple(nums(2))
            poly.append(to_px(*pos))
            i += 2
        elif cmd == "C":
            x1, y1, x2, y2, x3, y3 = nums(6)
            x0, y0 = pos
            for k in range(1, 9):
                s = k / 8
                a, b, c, d = (1 - s) ** 3, 3 * s * (1 - s) ** 2, 3 * s * s * (1 - s), s ** 3
                poly.append(to_px(a * x0 + b * x1 + c * x2 + d * x3, a * y0 + b * y1 + c * y2 + d * y3))
            pos = (x3, y3)
            i += 6
        else:
            sys.exit(f"unexpected token {t}")
    if poly:
        polys.append(poly)
    return polys


def rasterise(polys):
    """Even-odd fill sampled at pixel centres -> set of (x, y)."""
    edges = []
    for poly in polys:
        for (ax, ay), (bx, by) in zip(poly, poly[1:] + poly[:1]):
            if ay != by:
                edges.append((ax, ay, bx, by))
    pixels = set()
    for y in range(MAP_H):
        cy = y + 0.5
        xs = []
        for ax, ay, bx, by in edges:
            if (ay <= cy < by) or (by <= cy < ay):
                xs.append(ax + (cy - ay) * (bx - ax) / (by - ay))
        xs.sort()
        for a, b in zip(xs[0::2], xs[1::2]):
            for x in range(max(0, math.ceil(a - 0.5)), min(MAP_W, math.floor(b - 0.5) + 1)):
                pixels.add((x, y))
    return pixels


def dashed_line(p, q):
    """Pixels of a dashed two-pixel-wide line, dashes measured in screen units."""
    (x0, y0), (x1, y1) = (p[0] * MAP_W, p[1] * MAP_H), (q[0] * MAP_W, q[1] * MAP_H)
    # A hires pixel is half a line wide on screen
    length = math.hypot((x1 - x0) / 2, y1 - y0)
    steps = max(1, int(length * 4))
    out = set()
    for k in range(steps + 1):
        s = k / steps
        if (s * length) % 7 >= 4:
            continue
        x, y = x0 + (x1 - x0) * s, y0 + (y1 - y0) * s
        for dx in (0, 1):
            out.add((int(x) + dx, int(y)))
    return out


def build(ios_dir):
    continents, territories = parse_models(os.path.join(ios_dir, "Models.swift"))
    paths = json.load(open(os.path.join(ios_dir, "board_paths.json")))["territories"]
    ids = [t[0] for t in territories]

    grid = [[-1] * MAP_W for _ in range(MAP_H)]
    for index, tid in enumerate(ids):
        for x, y in rasterise(flatten(paths[tid])):
            if grid[y][x] < 0:
                grid[y][x] = index

    def at(x, y):
        return grid[y][x] if 0 <= x < MAP_W and 0 <= y < MAP_H else -1

    # Borders: two pixels wide across, one line down, so both look alike on
    # screen; coasts are two pixels wide on the land side
    border = [[False] * MAP_W for _ in range(MAP_H)]
    for y in range(MAP_H):
        for x in range(MAP_W):
            t = grid[y][x]
            if t < 0:
                continue
            if at(x - 1, y) != t or at(x + 1, y) != t or at(x, y + 1) != t:
                border[y][x] = True
            elif at(x, y - 1) < 0 or at(x - 2, y) < 0 or at(x + 2, y) < 0:
                border[y][x] = True

    image = [[OCEAN] * MAP_W for _ in range(MAP_H)]
    for y in range(MAP_H):
        for x in range(MAP_W):
            if grid[y][x] >= 0:
                image[y][x] = INK if border[y][x] else LAND
                continue
            # A drop shadow down and to the right lifts the land off the sea
            if any(at(x - dx, y - dy) >= 0 for dx in (2, 3, 4) for dy in (1, 2)):
                image[y][x] = SHADOW
            # Faint graticule: meridians and parallels as dotted lines
            elif (x % 62 == 30 and y % 2 == 0) or (y % 24 == 11 and x % 4 == 0):
                image[y][x] = SHADOW
    for p, q in SEA_ROUTES:
        for x, y in dashed_line(p, q):
            if 0 <= x < MAP_W and 0 <= y < MAP_H and grid[y][x] < 0:
                image[y][x] = ROUTE

    # Interior spans and badge spots
    spans, badges = [], []
    for index in range(len(ids)):
        mine = []
        for y in range(MAP_H):
            x = 0
            while x < MAP_W:
                if grid[y][x] == index and not border[y][x]:
                    x0 = x
                    while x < MAP_W and grid[y][x] == index and not border[y][x]:
                        x += 1
                    mine.append((y, x0, x - 1))
                else:
                    x += 1
        if not mine:
            sys.exit(f"{ids[index]} has no interior at this size")
        spans.append(mine)
        badges.append(badge_spot(grid, border, index))
    return continents, territories, image, spans, badges


def badge_spot(grid, border, index):
    """The interior point farthest from the edge, nearest the middle on ties."""
    cells = [(x, y) for y in range(MAP_H) for x in range(MAP_W)
             if grid[y][x] == index and not border[y][x]]
    inside = set(cells)
    cx = sum(x for x, _ in cells) / len(cells)
    cy = sum(y for _, y in cells) / len(cells)
    best, best_key = None, None
    for x, y in cells:
        # Clearance in screen units: a hires pixel is half a line wide
        r = 0
        while r < 12 and all((x + dx, y + dy) in inside
                             for dx, dy in ((-2 * r, 0), (2 * r, 0), (0, -r), (0, r),
                                            (-2 * r, -r), (2 * r, r), (-2 * r, r), (2 * r, -r))):
            r += 1
        key = (min(r, 6), -math.hypot((x - cx) / 2, y - cy))
        if best_key is None or key > best_key:
            best, best_key = (x, y), key
    return best


def byterun1(row):
    out, i = bytearray(), 0
    while i < len(row):
        run = 1
        while i + run < len(row) and row[i + run] == row[i] and run < 128:
            run += 1
        if run > 1:
            out += bytes([(257 - run) & 255, row[i]])
            i += run
            continue
        start = i
        while i < len(row) and i - start < 128 and not (i + 1 < len(row) and row[i + 1] == row[i]):
            i += 1
        if i == start:
            i += 1
        out += bytes([i - start - 1]) + bytes(row[start:i])
    return out


def planes(image, depth=4):
    body = bytearray()
    for row in image:
        for plane in range(depth):
            bits = bytearray(MAP_W // 8)
            for x, pen in enumerate(row):
                if pen >> plane & 1:
                    bits[x // 8] |= 0x80 >> (x % 8)
            body += byterun1(bits)
    return body


def c_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def write_c(path, continents, territories, image, spans, badges):
    ids = [t[0] for t in territories]
    body = planes(image)
    with open(path, "w") as f:
        f.write("/* Generated by amiga/support/mkmap.py from the iOS board; do not edit. */\n\n")
        f.write('#include "mapdata.h"\n\n')
        f.write("const Continent continents[NCONTINENTS] = {\n")
        for _, name, bonus in continents:
            f.write(f"  {{ {c_string(name)}, {bonus} }},\n")
        f.write("};\n\n")
        f.write("const Territory territories[NTERR] = {\n")
        first = 0
        for i, (tid, name, cont, neigh) in enumerate(territories):
            n = ", ".join(str(ids.index(x)) for x in neigh)
            bx, by = badges[i]
            f.write(f"  {{ {c_string(tid)}, {c_string(name)}, {cont}, {len(neigh)}, {{ {n} }}, "
                    f"{first}, {len(spans[i])}, {bx}, {by} }},\n")
            first += len(spans[i])
        f.write("};\n\n")
        f.write(f"const Span spans[{first}] = {{\n")
        for mine in spans:
            f.write("  " + ", ".join(f"{{{y},{a},{b}}}" for y, a, b in mine) + ",\n")
        f.write("};\n\n")
        f.write(f"const unsigned long mapPlanesSize = {len(body)};\n")
        f.write(f"const unsigned char mapPlanes[{len(body)}] = {{\n")
        for i in range(0, len(body), 24):
            f.write("  " + ",".join(str(v) for v in body[i:i + 24]) + ",\n")
        f.write("};\n")
    return first, len(body)


# --- preview --------------------------------------------------------------

PREVIEW_PALETTE = {OCEAN: 0x247, SHADOW: 0x124, INK: 0x000, WHITE: 0xFFF, LAND: 0xDCA, ROUTE: 0x9BF}
PLAYERS = [0xE33, 0x3B3, 0xEC2, 0xA5D, 0x2CC, 0xF82]


def write_png(path, rows):
    raw = b"".join(b"\0" + bytes(c for px in row for c in px) for row in rows)
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    w, h = len(rows[0]), len(rows)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def preview(path, image, spans, badges):
    rgb = lambda c: ((c >> 8) * 17, (c >> 4 & 15) * 17, (c & 15) * 17)
    pix = [[rgb(PREVIEW_PALETTE[p]) for p in row] for row in image]
    for i, mine in enumerate(spans):
        colour = rgb(PLAYERS[i % 6])
        for y, a, b in mine:
            for x in range(a, b + 1):
                pix[y][x] = colour
    for bx, by in badges:
        for y in range(by - 5, by + 6):
            for x in range(bx - 9, bx + 10):
                if 0 <= x < MAP_W and 0 <= y < MAP_H:
                    edge = y in (by - 5, by + 5) or x in (bx - 9, bx - 8, bx + 8, bx + 9)
                    pix[y][x] = (0, 0, 0) if edge else (255, 255, 255)
    # Double the lines so the preview has the screen's proportions
    write_png(path, [row for row in pix for _ in (0, 1)])


def main():
    args = sys.argv[1:]
    png = None
    if "--preview" in args:
        k = args.index("--preview")
        png = args[k + 1]
        del args[k:k + 2]
    if len(args) != 2:
        sys.exit(__doc__)
    continents, territories, image, spans, badges = build(args[0])
    count, size = write_c(args[1], continents, territories, image, spans, badges)
    print(f"mkmap: {count} spans, {size} bytes of map planes", file=sys.stderr)
    if png:
        preview(png, image, spans, badges)


if __name__ == "__main__":
    main()
