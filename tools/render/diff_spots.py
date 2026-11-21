#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Where do two runs of the same path differ?

For every pair of pictures of the same name that is not pixel-identical, the places with a difference: the picture is
divided into cells of --cell pixels, connected cells with at least one differing pixel form a place. Per place:
rectangle (pixels and percent of the picture), number of differing pixels, largest channel difference, mean colour in
run a there (helps to recognise sky, water, terrain, tree crown, bar) and whether the difference lies only at the edge
of areas (edges, e.g. antialiasing) or is areal. With --crops FOLDER additionally one picture per place with a, b and
the enlarged difference side by side (small enough to send around).

  python tools/render/diff_spots.py render-out/r170_all/zoom             (folder with a and b)
  python tools/render/diff_spots.py run1/a run2/a --crops C:/tmp/r170_places

Exit code 0 = all pairs pixel-identical, 1 = differences, 2 = input error.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from compare_shots import read_bmp, write_bmp, Image  # noqa: E402


def pairs(a, b):
    names = sorted(n for n in os.listdir(a) if n.lower().endswith(".bmp") and os.path.isfile(os.path.join(b, n)))
    return [(n, os.path.join(a, n), os.path.join(b, n)) for n in names]


def places(ia, ib, cell):
    w, h = ia.width, ia.height
    pa, pb = ia.bgr, ib.bgr
    cw, ch = (w + cell - 1) // cell, (h + cell - 1) // cell
    count = [0] * (cw * ch)
    largest = [0] * (cw * ch)
    total = [[0, 0, 0] for _ in range(cw * ch)]
    edge = [0] * (cw * ch)
    for y in range(h):
        row = y * w * 3
        cy = (y // cell) * cw
        for x in range(w):
            i = row + x * 3
            d = max(abs(pa[i] - pb[i]), abs(pa[i + 1] - pb[i + 1]), abs(pa[i + 2] - pb[i + 2]))
            if not d:
                continue
            c = cy + x // cell
            count[c] += 1
            largest[c] = max(largest[c], d)
            s = total[c]
            s[0] += pa[i + 2]
            s[1] += pa[i + 1]
            s[2] += pa[i]
            # edge: the neighbour to the right or below differs strongly in run a
            if x + 1 < w and max(abs(pa[i] - pa[i + 3]), abs(pa[i + 1] - pa[i + 4]), abs(pa[i + 2] - pa[i + 5])) > 32:
                edge[c] += 1
            elif y + 1 < h and max(abs(pa[i] - pa[i + w * 3]), abs(pa[i + 1] - pa[i + w * 3 + 1]),
                                   abs(pa[i + 2] - pa[i + w * 3 + 2])) > 32:
                edge[c] += 1
    seen = [False] * (cw * ch)
    out = []
    for start in range(cw * ch):
        if not count[start] or seen[start]:
            continue
        stack = [start]
        seen[start] = True
        cells = []
        while stack:
            c = stack.pop()
            cells.append(c)
            cx, cy = c % cw, c // cw
            for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                if 0 <= nx < cw and 0 <= ny < ch:
                    n = ny * cw + nx
                    if count[n] and not seen[n]:
                        seen[n] = True
                        stack.append(n)
        xs = [c % cw for c in cells]
        ys = [c // cw for c in cells]
        x0, y0 = min(xs) * cell, min(ys) * cell
        x1, y1 = min(w, (max(xs) + 1) * cell), min(h, (max(ys) + 1) * cell)
        n = sum(count[c] for c in cells)
        color = [sum(total[c][k] for c in cells) // n for k in range(3)]
        out.append({"x": x0, "y": y0, "w": x1 - x0, "h": y1 - y0, "pixels": n, "max": max(largest[c] for c in cells),
                    "color": color, "edge_share": sum(edge[c] for c in cells) / n})
    out.sort(key=lambda s: -s["pixels"])
    return out


def crop(ia, ib, s, pfad, margin=8, scale=3):
    """a, b and the amplified difference side by side, enlarged."""
    x0, y0 = max(0, s["x"] - margin), max(0, s["y"] - margin)
    x1, y1 = min(ia.width, s["x"] + s["w"] + margin), min(ia.height, s["y"] + s["h"] + margin)
    bw, bh = (x1 - x0) * scale, (y1 - y0) * scale
    width = bw * 3 + 8
    px = bytearray(width * bh * 3)
    for part in range(3):
        for y in range(bh):
            sy = y0 + y // scale
            for x in range(bw):
                sx = x0 + x // scale
                i = (sy * ia.width + sx) * 3
                if part == 0:
                    bgr = ia.bgr[i:i + 3]
                elif part == 1:
                    bgr = ib.bgr[i:i + 3]
                else:
                    d = max(abs(ia.bgr[i + k] - ib.bgr[i + k]) for k in range(3))
                    v = min(255, d * 16)
                    bgr = bytes((0, 0, v))
                o = (y * width + part * (bw + 4) + x) * 3
                px[o:o + 3] = bgr
    write_bmp(pfad, Image(width, bh, px))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a", help="folder of run a, or a path folder with subfolders a and b")
    ap.add_argument("b", nargs="?", help="folder of run b")
    ap.add_argument("--cell", type=int, default=16, help="cell size in pixels (default 16)")
    ap.add_argument("--crops", help="folder for crop pictures (a | b | difference x16)")
    ap.add_argument("--max-places", type=int, default=6, help="print at most this many places per picture")
    args = ap.parse_args()
    a, b = args.a, args.b
    if b is None:
        a, b = os.path.join(args.a, "a"), os.path.join(args.a, "b")
    if not (os.path.isdir(a) and os.path.isdir(b)):
        print("folder missing: %s or %s" % (a, b))
        return 2
    if args.crops:
        os.makedirs(args.crops, exist_ok=True)
    differing = 0
    pair_list = pairs(a, b)
    for name, pa, pb in pair_list:
        ia, ib = read_bmp(pa), read_bmp(pb)
        if (ia.width, ia.height) != (ib.width, ib.height):
            print("%s: sizes differ" % name)
            differing += 1
            continue
        if ia.bgr == ib.bgr:
            continue
        differing += 1
        found = places(ia, ib, args.cell)
        print("%s: %d places, %d pixels" % (name, len(found), sum(s["pixels"] for s in found)))
        for k, s in enumerate(found[:args.max_places]):
            print("  x %4d y %4d %4dx%-4d (%3.0f%% %3.0f%% %3.0f%%x%3.0f%%)  %6d pixels, up to %3d, colour a rgb(%d,%d,%d), "
                  "%s" % (s["x"], s["y"], s["w"], s["h"], 100.0 * s["x"] / ia.width, 100.0 * s["y"] / ia.height,
                          100.0 * s["w"] / ia.width, 100.0 * s["h"] / ia.height, s["pixels"], s["max"], *s["color"],
                          "at edges" if s["edge_share"] > 0.5 else "areal"))
            if args.crops:
                crop(ia, ib, s, os.path.join(args.crops, "%s_%d.bmp" % (os.path.splitext(name)[0], k)))
        if len(found) > args.max_places:
            print("  ... %d more places" % (len(found) - args.max_places))
    print("%d of %d pairs not pixel-identical" % (differing, len(pair_list)))
    return 1 if differing else 0


if __name__ == "__main__":
    sys.exit(main())
