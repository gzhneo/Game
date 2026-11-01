# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Steile Haenge einer Karte finden: Hoehenfeld aus den Kartendaten lesen, Neigung je Zelle rechnen, die
groessten zusammenhaengenden Steilflaechen mit ihrer Lage (kartenrelativ wie in den
Kamerafahrten) ausgeben -- Grundlage fuer die Aufnahmen der Berge.

Aufruf:
  python tools/render/map_hang.py --map "alpine assault"                 (Zero Hour, sonst --spiel generals)
  python tools/render/map_hang.py --map "fortress avalanche" --bild out.png   Neigungskarte als Bild
  python tools/render/map_hang.py --alle                                  Anteil steiler Zellen je Karte

Neigung wie im Spiel: Hoehe * MAP_HEIGHT_SCALE (0,625), Zellen zu 10 Welteinheiten, zentrale Differenzen.
Die Lage x, y ist (Index - Rand) / (Breite - 1 - 2 * Rand), also ueber TheTerrainLogic->getExtent().
"""
import argparse
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import map_props  # noqa: E402

HEIGHT_SCALE = 0.625
CELL = 10.0


def height_map(data):
    names, top = map_props.parse_chunks(map_props.unpack_map(data))
    ver, body = top["HeightMapData"]
    p = 0
    w, h = struct.unpack("<ii", body[p:p + 8]); p += 8
    border = 0
    if ver >= 3:
        border, = struct.unpack("<i", body[p:p + 4]); p += 4
    if ver >= 4:
        n, = struct.unpack("<i", body[p:p + 4]); p += 4 + 8 * n
    size, = struct.unpack("<i", body[p:p + 4]); p += 4
    hts = body[p:p + size]
    return w, h, border, hts


def slopes(w, h, hts):
    """Neigung in Grad je Gitterpunkt (zentrale Differenzen)."""
    out = [0.0] * (w * h)
    for y in range(h):
        for x in range(w):
            xa, xb = max(x - 1, 0), min(x + 1, w - 1)
            ya, yb = max(y - 1, 0), min(y + 1, h - 1)
            dx = (hts[y * w + xb] - hts[y * w + xa]) * HEIGHT_SCALE / ((xb - xa) * CELL)
            dy = (hts[yb * w + x] - hts[ya * w + x]) * HEIGHT_SCALE / ((yb - ya) * CELL)
            out[y * w + x] = math.degrees(math.atan(math.hypot(dx, dy)))
    return out


def load(game, name):
    path = map_props.ARCHIVES["ZeroHour" if game == "zh" else "Generals"]
    f, ents = map_props.big_entries(path)
    for nm, off, size in ents:
        if nm.endswith(".map") and nm.split("/")[-2] == name.lower():
            f.seek(off)
            return f.read(size)
    raise SystemExit(f"Karte {name} nicht in {path}")


def clusters(w, h, border, sl, limit, grid=12):
    """Steile Punkte in ein grobes Raster zaehlen; Felder mit den meisten steilen Punkten = Kandidaten."""
    cols = rows = grid
    count = [[0] * cols for _ in range(rows)]
    hmax = [[0.0] * cols for _ in range(rows)]
    ex, ey = w - 1 - 2 * border, h - 1 - 2 * border
    for y in range(border, h - border):
        for x in range(border, w - border):
            if sl[y * w + x] >= limit:
                cx = min(int((x - border) / ex * cols), cols - 1)
                cy = min(int((y - border) / ey * rows), rows - 1)
                count[cy][cx] += 1
                hmax[cy][cx] = max(hmax[cy][cx], sl[y * w + x])
    cand = []
    for cy in range(rows):
        for cx in range(cols):
            if count[cy][cx]:
                cand.append((count[cy][cx], (cx + 0.5) / cols, (cy + 0.5) / rows, hmax[cy][cx]))
    cand.sort(reverse=True)
    return cand


def write_png(path, w, h, sl, hts):
    import zlib
    rows = []
    for y in range(h - 1, -1, -1):			# Norden oben
        row = bytearray([0])
        for x in range(w):
            s = min(sl[y * w + x] / 60.0, 1.0)
            g = hts[y * w + x]
            row += bytes((int(255 * s), g, int(g * (1 - s))))
        rows.append(bytes(row))
    raw = b"".join(rows)

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--map")
    ap.add_argument("--spiel", choices=("zh", "generals"), default="zh")
    ap.add_argument("--grenze", type=float, default=35.0, help="ab dieser Neigung (Grad) steil")
    ap.add_argument("--bild", help="Neigungskarte als PNG (rot = steil, gruen = Hoehe)")
    ap.add_argument("--alle", action="store_true")
    a = ap.parse_args()
    if a.alle:
        for game, key in (("zh", "ZeroHour"), ("generals", "Generals")):
            f, ents = map_props.big_entries(map_props.ARCHIVES[key])
            for nm, off, size in ents:
                if not nm.endswith(".map") or nm.count("/") < 2:
                    continue
                f.seek(off)
                try:
                    w, h, b, hts = height_map(f.read(size))
                except Exception:
                    continue
                sl = slopes(w, h, hts)
                steep = sum(1 for s in sl if s >= a.grenze) / len(sl)
                print(f"{game:9s} {nm.split('/')[-2]:40s} {w:4d}x{h:<4d} steil {100 * steep:5.1f} %  max {max(sl):4.1f} Grad")
        return
    w, h, b, hts = height_map(load(a.spiel, a.map))
    sl = slopes(w, h, hts)
    print(f"{a.map}: {w} x {h}, Rand {b}, Hoehe {min(hts)}..{max(hts)}, max Neigung {max(sl):.1f} Grad")
    for n, x, y, m in clusters(w, h, b, sl, a.grenze)[:10]:
        print(f"  x {x:.3f}  y {y:.3f}  steile Punkte {n:4d}  max {m:4.1f} Grad")
    if a.bild:
        write_png(a.bild, w, h, sl, hts)


if __name__ == "__main__":
    main()
