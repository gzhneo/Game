#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Picture inspection for render tests.

Checks every picture of a folder on its own for typical rendering errors -- without a reference picture. This
finds errors that already sit in a reference and errors for which there is none yet.

Checks (identifier in brackets):
  black     large black area in the scene with a hard edge (missing terrain supply). The shrouded area outside
            the map is black too, but fades out softly -- it is recognised at the edge and not reported.
  seam      sharp vertical or horizontal jumps over large parts of the picture, single and very long or repeated
            at a fixed distance (texture coordinates, half-pixel offset; the lines in the main menu were 10
            pixels apart)
  magenta   area in the replacement colour for missing textures
  checker   small two-colour checkerboard (replacement texture)
  empty     single-coloured or almost empty picture (drawing aborted, wrong render state)
  jump      a picture differs strongly from both neighbours that resemble each other (flicker, lost objects) --
            only over the picture sequence of a folder, from three pictures on

The 2D bar at the bottom is not scene -- only the bar itself, not the whole picture width: left and right of it
is 3D scene and is checked too. The bar is as wide as a 4:3 picture of the picture height and stands in the
middle (1280x720: x 160 to 1117, 1024x768: full width); the default is two rectangles, the radar part on the left
with the higher top and the rest (DEFAULT_HUD). --hud X,Y,W,H replaces the default, may be given several times,
in pixels or percent of the picture like --ignore in compare_shots.py. In the bar only magenta, checkerboard and
an empty picture are checked.

Every finding names the picture section (x, y, width, height in pixels). With --crops FOLDER it is also saved as
an enlarged section (never write into the checked folder).

Usage:
    python tools/render/inspect_shots.py render-out/cam-zoom-g-x86
    python tools/render/inspect_shots.py folder1 folder2 --json
    python tools/render/inspect_shots.py picture.bmp --crops C:/tmp/sections
    python tools/render/inspect_shots.py folder --only black,seam --stats
    python tools/render/inspect_shots.py folder --hud 12%,68%,76%,32%       # own bar mask
    python tools/render/inspect_shots.py --selftest

Exit code: 0 = no findings, 1 = at least one finding, 2 = error (file not readable etc.).

Standard library only; BMP reading from compare_shots.py. It computes on a grid of 8x8 pixels so a 1280x720
picture takes about one second.

Calibration (Generals x86, 1280x720; repeated with the bar mask): in render-out/cam-zoom-g-x86 the pictures 720
to 960 report "black", the other pictures of that path, cam-quer-g-x86 (black corners outside the map) and all
ref-* folders report nothing. New, because the corners next to the bar are scene now: cam-rundum-g-x86 picture
360 (black triangle at the bottom right, missing near terrain before the terrain supply was fixed). Night maps
may need their own thresholds (--black).
"""
import argparse
import glob
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_shots  # noqa: E402

CHECKS = ("black", "seam", "magenta", "checker", "empty", "jump")

BLOCK = 8                   # grid width in pixels
# 2D bar at the bottom (measured on pixels that stay the same over all 30 pictures of a path). The bar is a 4:3
# picture of the picture height, centred: 1280x720 x 160 to 1117, 1024x768 x 0 to 1023. Top edge of the radar
# part with its top piece 494 of 720 (68.6 %) or 528 of 768 (68.8 %), up to 22.1 % of the bar width; right of
# it at most 519 of 720 (72.1 %) or 553 of 768 (72.0 %), the buttons at the top right.
# Per rectangle: x and width in percent of the bar width from its left edge, y and height in percent of the
# picture height; about half a percentage point of margin each so nothing of the bar reaches into the scene.
DEFAULT_HUD = ((-0.5, 68.0, 23.0, 32.0),    # radar with top piece
               (22.5, 71.5, 78.0, 28.5))    # middle and right part up to the buttons
HUD_ASPECT = 4.0 / 3.0      # aspect ratio of the bar
BLACK_LEVEL = 12            # pixels with max(R,G,B) <= BLACK_LEVEL count as black
BLACK_BLOCK_SHARE = 0.90    # a block is black when this share of its pixels is black
BLACK_MIN_AREA = 0.01       # minimum size of a black area (share of the scene)
DIM_LEVEL = 40              # "dimmed": above BLACK_LEVEL, at most DIM_LEVEL (max channel)
SOFT_EDGE_MAX = 0.20        # share of dimmed pixels at the edge (two block rows wide) up to which
                            # the edge counts as hard. Calibrated: missing terrain 0.02 to 0.13 per
                            # picture (at least one area), area outside the map from 0.25
SEAM_JUMP = 28              # brightness jump between two neighbouring pixels that counts as a jump
SEAM_SINGLE = 0.85          # a single seam must cover this much of the scene height/width
SEAM_REPEAT = 0.30          # repeated seams: coverage per line
SEAM_REPEAT_MIN = 4         # ... and at least this many lines at a fixed distance
MAGENTA_MIN_BLOCKS = 4      # connected magenta blocks for a finding
CHECKER_MIN_BLOCKS = 4
EMPTY_SHARE = 0.97          # share of blocks with almost the same colour for "empty"
JUMP_RATIO = 3.0            # difference to the neighbours / difference of the neighbours among themselves
JUMP_MIN = 20.0             # ... and at least this mean difference (0..255)


# --- Vorverarbeitung --------------------------------------------------------------------------------
def hud_boxes(hud, w, h):
    """Rectangles of the 2D bar in pixels, (x0, y0, x1, y1) with x1/y1 exclusive.
    hud: None for the default (DEFAULT_HUD, aligned to the bar width) or a list of
    compare_shots.Rect (pixels or percent of the picture)."""
    boxes = []
    if hud is None:
        bar_w = min(w, h * HUD_ASPECT)
        left = (w - bar_w) / 2.0
        for x, y, bw_, bh_ in DEFAULT_HUD:
            x0 = int(left + x / 100.0 * bar_w)
            x1 = int(round(left + (x + bw_) / 100.0 * bar_w + 0.4999))
            y0 = int(y / 100.0 * h)
            y1 = int(round((y + bh_) / 100.0 * h + 0.4999))
            x0, y0, x1, y1 = max(0, x0), max(0, y0), min(w, x1), min(h, y1)
            if x1 > x0 and y1 > y0:
                boxes.append((x0, y0, x1, y1))
    else:
        for r in hud:
            box = r.resolve(w, h)
            if box is not None:
                boxes.append(box)
    return boxes


def _free_runs(cuts, n):
    """Sections (a, b) of 0..n that none of the intervals in cuts covers."""
    out = []
    pos = 0
    for a, b in sorted(cuts):
        if a > pos:
            out.append((pos, a))
        pos = max(pos, b)
    if pos < n:
        out.append((pos, n))
    return out


class Prepared:
    """Brightness (max channel and luma) per pixel, block statistics and mask of the 2D bar."""

    def __init__(self, img, hud, black_level):
        self.img = img
        w, h = img.width, img.height
        self.w, self.h = w, h
        self.bw, self.bh = w // BLOCK, h // BLOCK
        # 2D bar: pixel-exact for the seam check, block-wise for the others. A block belongs to the bar as
        # soon as it touches it -- otherwise a half-covered block with the dark bar edge would count as scene.
        self.hud = hud_boxes(hud, w, h)
        self.bmask = [[False] * self.bw for _ in range(self.bh)]
        for x0, y0, x1, y1 in self.hud:
            for by in range(y0 // BLOCK, min(self.bh, (y1 - 1) // BLOCK + 1)):
                for bx in range(x0 // BLOCK, min(self.bw, (x1 - 1) // BLOCK + 1)):
                    self.bmask[by][bx] = True
        self.scene_blocks = [(bx, by) for by in range(self.bh) for bx in range(self.bw)
                             if not self.bmask[by][bx]]
        black_table = bytes(1 if v <= black_level else 0 for v in range(256))
        dim_table = bytes(1 if black_level < v <= DIM_LEVEL else 0 for v in range(256))
        self.mx = []        # per row: bytes with max(R,G,B)
        self.luma = []      # per row: bytes with luma
        bgr = img.bgr
        for y in range(h):
            s = y * w * 3
            b, g, r = bgr[s:s + w * 3:3], bgr[s + 1:s + w * 3:3], bgr[s + 2:s + w * 3:3]
            self.mx.append(bytes(map(max, b, g, r)))
            self.luma.append(bytes(map(_luma, r, g, b)))
        # block statistics: sum of the max channel, number of black and dimmed pixels
        bw, bh = self.bw, self.bh
        self.bsum = [[0] * bw for _ in range(bh)]
        self.bblack = [[0] * bw for _ in range(bh)]
        self.bdim = [[0] * bw for _ in range(bh)]
        for y in range(bh * BLOCK):
            by = y // BLOCK
            row = self.mx[y]
            mask = row.translate(black_table)
            dmask = row.translate(dim_table)
            srow, krow, drow = self.bsum[by], self.bblack[by], self.bdim[by]
            for bx in range(bw):
                a = bx * BLOCK
                srow[bx] += sum(row[a:a + BLOCK])
                krow[bx] += sum(mask[a:a + BLOCK])
                drow[bx] += sum(dmask[a:a + BLOCK])

    def block_pixels(self):
        return BLOCK * BLOCK

    def row_runs(self, y):
        """Scene sections (x0, x1) of row y, x1 exclusive."""
        return _free_runs([(x0, x1) for x0, y0, x1, y1 in self.hud if y0 <= y < y1], self.w)

    def col_runs(self, x):
        """Scene sections (y0, y1) of column x, y1 exclusive."""
        return _free_runs([(y0, y1) for x0, y0, x1, y1 in self.hud if x0 <= x < x1], self.h)


def _luma(r, g, b):
    return (r * 77 + g * 150 + b * 29) >> 8


def rect_of_blocks(bx0, by0, bx1, by1):
    """Blockrechteck (inklusive) -> Pixelrechteck (x, y, w, h)."""
    return (bx0 * BLOCK, by0 * BLOCK, (bx1 - bx0 + 1) * BLOCK, (by1 - by0 + 1) * BLOCK)


def components(cells, bw, bh):
    """4-connected components of a set of (bx, by)."""
    seen = set()
    out = []
    for start in cells:
        if start in seen:
            continue
        stack = [start]
        seen.add(start)
        comp = []
        while stack:
            c = stack.pop()
            comp.append(c)
            x, y = c
            for n in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if n in cells and n not in seen:
                    seen.add(n)
                    stack.append(n)
        out.append(comp)
    return out


# --- Checks -------------------------------------------------------------------------------------
def check_black(p, stats):
    npix = p.block_pixels()
    black = set()
    for bx, by in p.scene_blocks:              # blocks of the bar are never black scene
        if p.bblack[by][bx] >= BLACK_BLOCK_SHARE * npix:
            black.add((bx, by))
    scene_blocks = len(p.scene_blocks) or 1
    findings = []
    for comp in components(black, p.bw, p.bh):
        area = len(comp) / scene_blocks
        if area < BLACK_MIN_AREA:
            continue
        # Edge: two block rows around the area. Outside the map the picture fades into black -- there the
        # edge is full of dimmed pixels. When terrain is missing, black borders directly on grass, road or
        # house; dimmed pixels are rare. (Silhouettes in front of the map edge are hard as well; the
        # gradient next to them outweighs.) Blocks of the bar do not count as edge: their edge is always hard.
        cs = set(comp)
        ring = set()
        frontier = cs
        for _ in range(2):
            nxt = set()
            for x, y in frontier:
                for n in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                    if (0 <= n[0] < p.bw and 0 <= n[1] < p.bh and not p.bmask[n[1]][n[0]]
                            and n not in cs and n not in ring):
                        nxt.add(n)
            ring |= nxt
            frontier = nxt
        dim = nonblack = 0
        for x, y in ring:
            dim += p.bdim[y][x]
            nonblack += npix - p.bblack[y][x]
        soft = dim / nonblack if nonblack else 1.0
        xs = [c[0] for c in comp]
        ys = [c[1] for c in comp]
        rect = rect_of_blocks(min(xs), min(ys), max(xs), max(ys))
        info = {"area": round(area, 4), "soft_edge": round(soft, 3), "crop": rect}
        if stats is not None:
            stats.append(("black", info))
        if soft <= SOFT_EDGE_MAX:
            findings.append({
                "check": "black",
                "text": f"black area in the picture, {area * 100:.1f} % of the scene, hard edge "
                        f"({soft * 100:.0f} % softened edge pixels)",
                "crop": list(rect),
                "values": info,
            })
    return findings


def _seam_profile(segments, n):
    """Seam profiles along one axis. Returns (up, down, line), per position i the share of the lines with
      up/down: an isolated step between i and i+1 (neighbouring steps small), by direction;
      line:    a thin line exactly on pixel i (two opposite jumps, calm outside).
    segments: list of (start, run) -- scene sections of the rows (vertical seams) or columns (horizontal
    seams); the 2D bar splits a line into several sections.
    The share refers to the lines that see position i as scene, but at least to half of the most -- otherwise a
    few lines next to the bar would be enough for a seam."""
    up = [0] * n
    down = [0] * n
    spike = [0] * n
    count = [0] * n
    j, half = SEAM_JUMP, SEAM_JUMP / 2
    for off, line in segments:
        m = len(line)
        if m < 4:
            continue
        d = [line[i + 1] - line[i] for i in range(m - 1)]
        a = [abs(v) for v in d]
        for i in range(1, m - 2):
            count[off + i] += 1
            ai = a[i]
            if ai >= j and a[i - 1] < ai / 2 and a[i + 1] < ai / 2:
                if d[i] > 0:
                    up[off + i] += 1
                else:
                    down[off + i] += 1
            elif (ai >= j and a[i - 1] >= j and (d[i] > 0) != (d[i - 1] > 0)
                  and (i < 2 or a[i - 2] < half) and a[i + 1] < half):
                spike[off + i] += 1
    floor = max(1, max(count) / 2)
    norm = [max(c, floor) for c in count]
    return ([c / k for c, k in zip(up, norm)], [c / k for c, k in zip(down, norm)],
            [c / k for c, k in zip(spike, norm)])


def _single_seams(up, down):
    """Long single seam: a step over almost the whole scene without an opposite edge within 6 pixels. With an
    opposite edge it is a narrow bar -- the frame of a menu or the like."""
    out = []
    n = len(up)
    for prof, other in ((up, down), (down, up)):
        for i, v in enumerate(prof):
            if v < SEAM_SINGLE:
                continue
            near = max(other[max(0, i - 6):min(n, i + 7)])
            if near >= 0.4 * v:
                continue
            out.append((i, v))
    return out


def _is_thin_bar(p, axis, i, samples=64):
    """True when the step at i is only the edge of a narrow light or dark bar (frame line of a menu, scaled
    softly: several steps instead of an opposite edge): the brightness eight to four pixels before the step
    equals that four to eight pixels after it. At a real seam the two sides are at least one jump apart."""
    n = p.w if axis == "vertical" else p.h
    lines = p.h if axis == "vertical" else p.w
    if i - 8 < 0 or i + 9 >= n:
        return False
    step = max(1, lines // samples)
    same = tested = 0
    for k in range(0, lines, step):
        if axis == "vertical":
            row = p.luma[k]
            before, after = row[i - 8:i - 3], row[i + 5:i + 10]
        else:
            before = bytes(p.luma[y][k] for y in range(i - 8, i - 3))
            after = bytes(p.luma[y][k] for y in range(i + 5, i + 10))
        tested += 1
        if abs(sum(before) / len(before) - sum(after) / len(after)) < SEAM_JUMP * 0.75:
            same += 1
    return tested > 0 and same >= 0.6 * tested


def check_seams(p, stats):
    findings = []
    # vertical seams: over the rows of the scene (every second row is enough), without the bar
    rows = []
    for y in range(0, p.h, 2):
        row = p.luma[y]
        rows += [(a, row[a:b]) for a, b in p.row_runs(y)]
    prof_v = _seam_profile(rows, p.w)
    # horizontal seams: over the columns of the scene (every second column)
    cols = []
    for x in range(0, p.w, 2):
        for a, b in p.col_runs(x):
            cols.append((a, bytes(p.luma[y][x] for y in range(a, b))))
    prof_h = _seam_profile(cols, p.h)

    def col_extent(x0, x1):
        """Lowest scene row over the columns x0..x1 -- for the reported section."""
        return max((b for x in range(x0, min(p.w, x1 + 1)) for _, b in p.col_runs(x)), default=p.h)
    for axis, (up, down, spike) in (("vertical", prof_v), ("horizontal", prof_h)):
        combined = [u + d_ + s for u, d_, s in zip(up, down, spike)]
        peaks = [i for i, v in enumerate(combined) if v >= SEAM_REPEAT]
        single = [(i, v) for i, v in _single_seams(up, down) if not _is_thin_bar(p, axis, i)]
        if stats is not None:
            stats.append(("seam", {"achse": axis,
                                   "max": [round(v, 3) for v in sorted(combined, reverse=True)[:3]],
                                   "linien_ueber_wiederholschwelle": len(peaks),
                                   "einzelnaehte": len(single)}))
        for i, v in single:
            rect = (i, 0, 2, col_extent(i, i + 1)) if axis == "vertical" else (0, i, p.w, 2)
            findings.append({"check": "seam",
                             "text": f"{axis} seam at {i + 1}, over {v * 100:.0f} % of the scene",
                             "crop": list(rect), "values": {"coverage": round(v, 3)}})
        run = _periodic_run(peaks)
        if len(run) >= SEAM_REPEAT_MIN:
            step = run[1] - run[0]
            if axis == "vertical":
                rect = (run[0], 0, run[-1] - run[0] + 2, col_extent(run[0], run[-1] + 1))
            else:
                rect = (0, run[0], p.w, run[-1] - run[0] + 2)
            findings.append({"check": "seam",
                             "text": f"{len(run)} {axis} seams at a distance of {step} pixels",
                             "crop": list(rect),
                             "values": {"linien": len(run), "abstand": step}})
    return findings


def _periodic_run(positions):
    """Longest run of positions with the same distance (distance >= 3, tolerance 1 pixel)."""
    best = []
    pos = sorted(set(positions))
    for a in range(len(pos)):
        for b in range(a + 1, len(pos)):
            step = pos[b] - pos[a]
            if step < 3:
                continue
            run = [pos[a], pos[b]]
            nxt = pos[b] + step
            while True:
                hit = [q for q in pos if abs(q - nxt) <= 1]
                if not hit:
                    break
                run.append(hit[0])
                nxt = hit[0] + step
            if len(run) > len(best):
                best = run
        if len(best) >= SEAM_REPEAT_MIN and len(best) > len(pos) // 2:
            break
    return best


def _block_colors(p):
    """Mean colour per block (B, G, R)."""
    w = p.w
    bgr = p.img.bgr
    out = [[None] * p.bw for _ in range(p.bh)]
    for by in range(p.bh):
        for bx in range(p.bw):
            sb = sg = sr = 0
            for y in range(by * BLOCK, by * BLOCK + BLOCK, 2):
                s = (y * w + bx * BLOCK) * 3
                seg = bgr[s:s + BLOCK * 3]
                sb += sum(seg[0::3])
                sg += sum(seg[1::3])
                sr += sum(seg[2::3])
            n = BLOCK * BLOCK // 2
            out[by][bx] = (sb / n, sg / n, sr / n)
    return out


def _is_magenta(c):
    b, g, r = c
    return r >= 180 and b >= 180 and g <= 80


def check_magenta(p, colors, stats):
    cells = {(bx, by) for by in range(p.bh) for bx in range(p.bw) if _is_magenta(colors[by][bx])}
    findings = []
    for comp in components(cells, p.bw, p.bh):
        if len(comp) < MAGENTA_MIN_BLOCKS:
            continue
        xs = [c[0] for c in comp]
        ys = [c[1] for c in comp]
        rect = rect_of_blocks(min(xs), min(ys), max(xs), max(ys))
        findings.append({"check": "magenta",
                         "text": f"replacement colour magenta, {len(comp)} blocks",
                         "crop": list(rect), "values": {"blocks": len(comp)}})
    if stats is not None:
        stats.append(("magenta", {"blocks": len(cells)}))
    return findings


def _checker_block(p, bx, by):
    """The block shows a two-colour checkerboard with squares of 1 to 4 pixels."""
    x0, y0 = bx * BLOCK, by * BLOCK
    vals = [p.luma[y][x0:x0 + BLOCK] for y in range(y0, y0 + BLOCK)]
    flat = [v for row in vals for v in row]
    lo, hi = min(flat), max(flat)
    if hi - lo < 60:
        return False
    mid = (lo + hi) / 2
    if any(abs(v - mid) < (hi - lo) * 0.3 for v in flat):
        return False                      # Zwischenwerte: kein reines Zweifarbenmuster
    bits = [[1 if v > mid else 0 for v in row] for row in vals]
    for cell in (1, 2, 4):
        ok = True
        for y in range(BLOCK):
            for x in range(BLOCK):
                if bits[y][x] != (bits[0][0] ^ (((x // cell) + (y // cell)) & 1)):
                    ok = False
                    break
            if not ok:
                break
        if ok:
            return True
    return False


def check_checker(p, stats):
    cells = {(bx, by) for by in range(p.bh) for bx in range(p.bw) if _checker_block(p, bx, by)}
    findings = []
    for comp in components(cells, p.bw, p.bh):
        if len(comp) < CHECKER_MIN_BLOCKS:
            continue
        xs = [c[0] for c in comp]
        ys = [c[1] for c in comp]
        rect = rect_of_blocks(min(xs), min(ys), max(xs), max(ys))
        findings.append({"check": "checker", "text": f"checkerboard pattern, {len(comp)} blocks",
                         "crop": list(rect), "values": {"blocks": len(comp)}})
    if stats is not None:
        stats.append(("checker", {"blocks": len(cells)}))
    return findings


def check_empty(p, colors, stats):
    blocks = [c for row in colors for c in row]
    buckets = {}
    for b, g, r in blocks:
        key = (int(b) // 16, int(g) // 16, int(r) // 16)
        buckets[key] = buckets.get(key, 0) + 1
    share = max(buckets.values()) / len(blocks)
    if stats is not None:
        stats.append(("empty", {"groesste_farbe": round(share, 3)}))
    if share >= EMPTY_SHARE:
        return [{"check": "empty", "text": f"almost single-coloured picture ({share * 100:.0f} % one colour)",
                 "crop": [0, 0, p.w, p.h], "values": {"share": round(share, 3)}}]
    return []


def thumb(p):
    """Reduced brightness of the scene (one value per block, without the bar) for the sequence comparison."""
    n = p.block_pixels()
    return [p.bsum[by][bx] / n for bx, by in p.scene_blocks]


def mean_diff(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


# --- Ablauf -----------------------------------------------------------------------------------------
def inspect_image(path, args, stats=None):
    img = compare_shots.read_bmp(path)
    p = Prepared(img, args.hud, args.black)
    only = args.only
    findings = []
    colors = None
    if "black" in only:
        findings += check_black(p, stats)
    if "seam" in only:
        findings += check_seams(p, stats)
    if "magenta" in only or "empty" in only:
        colors = _block_colors(p)
    if "magenta" in only:
        findings += check_magenta(p, colors, stats)
    if "checker" in only:
        findings += check_checker(p, stats)
    if "empty" in only:
        findings += check_empty(p, colors, stats)
    return p, findings


def check_sequence(entries):
    """entries: Liste (pfad, thumb) in Bildfolge. Liefert {pfad: [befund]}."""
    out = {}
    for i in range(1, len(entries) - 1):
        (pa, ta), (pb, tb), (pc, tc) = entries[i - 1], entries[i], entries[i + 1]
        if len(ta) != len(tb) or len(tb) != len(tc):
            continue
        d_ab, d_bc, d_ac = mean_diff(ta, tb), mean_diff(tb, tc), mean_diff(ta, tc)
        near = min(d_ab, d_bc)
        if near >= JUMP_MIN and near >= JUMP_RATIO * max(d_ac, 1.0):
            out.setdefault(pb, []).append({
                "check": "jump",
                "text": f"picture differs from both neighbours ({d_ab:.1f}/{d_bc:.1f}), "
                        f"the neighbours among themselves only {d_ac:.1f}",
                "crop": None,
                "values": {"zum_vorigen": round(d_ab, 2), "zum_naechsten": round(d_bc, 2),
                          "nachbarn": round(d_ac, 2)}})
    return out


def save_crop(img, rect, path, scale=2):
    """Save a section (with a little margin) enlarged as a BMP."""
    x, y, w, h = rect
    pad = 16
    x0, y0 = max(0, x - pad), max(0, y - pad)
    x1, y1 = min(img.width, x + w + pad), min(img.height, y + h + pad)
    # do not enlarge large sections
    if (x1 - x0) * scale > 1600 or (y1 - y0) * scale > 1200:
        scale = 1
    cw, ch = (x1 - x0) * scale, (y1 - y0) * scale
    out = bytearray(cw * ch * 3)
    for yy in range(ch):
        sy = y0 + yy // scale
        for xx in range(cw):
            sx = x0 + xx // scale
            s = (sy * img.width + sx) * 3
            d = (yy * cw + xx) * 3
            out[d:d + 3] = img.bgr[s:s + 3]
    compare_shots.write_bmp(path, compare_shots.Image(cw, ch, out))


def collect(inputs):
    """Returns a list of (group, [paths]) -- one group per folder in picture order."""
    groups = []
    for inp in inputs:
        if os.path.isdir(inp):
            files = sorted(glob.glob(os.path.join(inp, "shot_*.bmp")))
            groups.append((inp, files))
        elif os.path.isfile(inp):
            groups.append((os.path.dirname(inp) or ".", [inp]))
        else:
            raise FileNotFoundError(f"{inp}: weder Ordner noch Bilddatei")
    return groups


def run(args):
    groups = collect(args.inputs)
    report = []
    total = 0
    errors = 0
    crop_root = os.path.abspath(args.crops) if args.crops else None
    for group, files in groups:
        if crop_root and os.path.abspath(group) == crop_root:
            raise ValueError("--crops must not be the checked folder")
        seq = []
        results = []
        for path in files:
            stats = [] if args.stats else None
            try:
                p, findings = inspect_image(path, args, stats)
            except (OSError, ValueError) as e:
                errors += 1
                results.append({"image": path, "error": str(e), "findings": []})
                continue
            seq.append((path, thumb(p)))
            results.append({"image": path, "width": p.w, "height": p.h, "findings": findings,
                            "_img": p.img, "_stats": stats})
        if "jump" in args.only and len(seq) >= 3:
            jumps = check_sequence(seq)
            for r in results:
                r["findings"] += jumps.get(r["image"], [])
        for r in results:
            if crop_root and r["findings"]:
                base = os.path.splitext(os.path.basename(r["image"]))[0]
                sub = os.path.join(crop_root, os.path.basename(os.path.normpath(group)))
                for k, f in enumerate(r["findings"]):
                    if f.get("crop"):
                        cp = os.path.join(sub, f"{base}_{f['check']}_{k}.bmp")
                        save_crop(r["_img"], f["crop"], cp)
                        f["crop_file"] = cp
            total += len(r["findings"])
            report.append(r)
    if args.json:
        clean = []
        for r in report:
            c = {k: v for k, v in r.items() if not k.startswith("_")}
            if args.stats and r.get("_stats") is not None:
                c["statistics"] = [{"check": n, **v} for n, v in r["_stats"]]
            clean.append(c)
        json.dump({"findings": total, "error": errors, "images": clean}, sys.stdout, indent=1,
                  ensure_ascii=False)
        print()
    else:
        for r in report:
            if "error" in r:
                print(f"ERROR  {r['image']}: {r['error']}")
                continue
            if r["findings"]:
                print(f"FINDING  {r['image']}")
                for f in r["findings"]:
                    rect = f.get("crop")
                    where = f"  [x={rect[0]} y={rect[1]} {rect[2]}x{rect[3]}]" if rect else ""
                    print(f"        {f['check']:8s} {f['text']}{where}")
                    if f.get("crop_file"):
                        print(f"                 Crop: {f['crop_file']}")
            elif not args.quiet:
                print(f"ok      {r['image']}")
            if args.stats and r.get("_stats"):
                for name, v in r["_stats"]:
                    print(f"        [{name}] {v}")
        n_img = len(report)
        n_bad = sum(1 for r in report if r["findings"])
        print(f"{n_img} pictures, {n_bad} with a finding, {total} findings, {errors} errors")
    if errors:
        return 2
    return 1 if total else 0


# --- Selbsttest -------------------------------------------------------------------------------------
def _in_bar(width, height, x, y):
    """Pixels of the artificial 2D bar: 4:3 wide and centred as in the game, the higher radar part on
    the left. Lies just inside the default mask DEFAULT_HUD."""
    bar_w = min(width, height * HUD_ASPECT)
    left = (width - bar_w) / 2.0
    if not left + 2 <= x < left + bar_w - 2:
        return False
    top = 0.69 if x < left + 0.22 * bar_w else 0.725
    return y >= height * top


def _synthetic(width=640, height=360, seed=1):
    """Terrain-like picture: soft value noise in green/brown plus fine noise, a dark 2D bar with a
    black field at the bottom centre (the bar is no scene), terrain left and right of it."""
    import random
    rnd = random.Random(seed)
    cell = 40
    gw, gh = width // cell + 2, height // cell + 2
    grid = [[rnd.random() for _ in range(gw)] for _ in range(gh)]
    out = bytearray(width * height * 3)
    hud_top = int(height * 0.725)
    for y in range(height):
        fy = y / cell
        iy = int(fy)
        ty = fy - iy
        for x in range(width):
            fx = x / cell
            ix = int(fx)
            tx = fx - ix
            v = (grid[iy][ix] * (1 - tx) * (1 - ty) + grid[iy][ix + 1] * tx * (1 - ty)
                 + grid[iy + 1][ix] * (1 - tx) * ty + grid[iy + 1][ix + 1] * tx * ty)
            n = rnd.randint(-6, 6)
            g = int(70 + 80 * v) + n
            r = int(50 + 60 * v) + n
            b = int(30 + 40 * v) + n
            if _in_bar(width, height, x, y):
                b, g, r = 60, 45, 20
                if width * 0.3 < x < width * 0.45 and y > hud_top + 10:
                    b = g = r = 0      # black field in the bar (minimap or the like)
            d = (y * width + x) * 3
            out[d], out[d + 1], out[d + 2] = max(0, min(255, b)), max(0, min(255, g)), max(0, min(255, r))
    return compare_shots.Image(width, height, out)


def _copy(img):
    return compare_shots.Image(img.width, img.height, bytearray(img.bgr))


def _fill(img, x0, y0, x1, y1, bgr):
    for y in range(y0, y1):
        for x in range(x0, x1):
            d = (y * img.width + x) * 3
            img.bgr[d:d + 3] = bytes(bgr)


def _soft_corner(img, radius, cx=0, cy=0):
    """Soft black gradient around the corner (cx, cy) (as outside the map). The bar stays."""
    for y in range(img.height):
        for x in range(img.width):
            if _in_bar(img.width, img.height, x, y):
                continue
            dist = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
            if dist < radius * 1.8:
                f = 0.0 if dist < radius else (dist - radius) / (radius * 0.8)
                d = (y * img.width + x) * 3
                for k in range(3):
                    img.bgr[d + k] = int(img.bgr[d + k] * f)


def selftest():
    ok = True
    base = _synthetic()
    cases = []
    cases.append(("unversehrt", base, set()))
    img = _copy(base)
    _fill(img, 200, 60, 360, 180, (0, 0, 0))
    _fill(img, 260, 100, 280, 150, (90, 140, 170))   # "house" in the black
    cases.append(("schwarzes Rechteck", img, {"black"}))
    img = _copy(base)
    _soft_corner(img, 140)
    cases.append(("soft edge outside the map", img, set()))
    img = _copy(base)
    for y in range(img.height):                          # the whole bar black
        for x in range(img.width):
            if _in_bar(img.width, img.height, x, y):
                _fill(img, x, y, x + 1, y + 1, (0, 0, 0))
    cases.append(("black only in the 2D bar", img, set()))
    img = _copy(base)
    _fill(img, 0, 230, 72, 360, (0, 0, 0))              # corner left of the bar, hard edge
    cases.append(("black corner next to the bar, hard edge", img, {"black"}))
    img = _copy(base)
    _soft_corner(img, 90, 0, 359)                        # Ecke links unten, weicher Verlauf
    cases.append(("soft edge in the corner next to the bar", img, set()))
    img = _copy(base)
    for x in range(40, 600, 10):                         # menu lines every 10 pixels
        for y in range(0, 280):
            d = (y * img.width + x) * 3
            img.bgr[d:d + 3] = bytes(max(0, v - 60) for v in img.bgr[d:d + 3])
    cases.append(("seams every 10 pixels", img, {"seam"}))
    img = _copy(base)                                    # Versatz: rechte Haelfte heller
    for y in range(img.height):
        for x in range(320, 640):
            d = (y * img.width + x) * 3
            img.bgr[d:d + 3] = bytes(min(255, v + 50) for v in img.bgr[d:d + 3])
    cases.append(("single vertical seam", img, {"seam"}))
    img = _copy(base)                                    # softly scaled frame line: not a seam
    for y in range(img.height):
        for x, add in ((300, 50), (301, 130), (302, 100), (303, 20)):
            d = (y * img.width + x) * 3
            img.bgr[d:d + 3] = bytes(min(255, v + add) for v in img.bgr[d:d + 3])
    cases.append(("weich skalierte Rahmenlinie", img, set()))
    img = _copy(base)
    _fill(img, 400, 40, 440, 80, (255, 0, 255))
    cases.append(("Magenta-Flaeche", img, {"magenta"}))
    img = _copy(base)
    for y in range(40, 88):
        for x in range(100, 164):
            v = 255 if ((x // 2 + y // 2) & 1) else 0
            d = (y * img.width + x) * 3
            img.bgr[d:d + 3] = bytes((v, v, v))
    cases.append(("Schachbrett", img, {"checker"}))
    img = compare_shots.Image(640, 360, bytearray(b"\x20\x20\x20" * 640 * 360))
    cases.append(("single-coloured", img, {"empty"}))

    args = argparse.Namespace(hud=None, black=BLACK_LEVEL, only=set(CHECKS))
    with tempfile.TemporaryDirectory() as tmp:
        for name, img, expected in cases:
            path = os.path.join(tmp, "shot.bmp")
            compare_shots.write_bmp(path, img)
            _, findings = inspect_image(path, args)
            got = {f["check"] for f in findings}
            good = got == expected
            ok &= good
            print(f"{'ok    ' if good else 'ERROR'} {name}: expected {sorted(expected) or '-'}, "
                  f"found {sorted(got) or '-'}")
        # sequence: the middle picture differs
        seq_dir = os.path.join(tmp, "folge")
        os.makedirs(seq_dir)
        flick = _copy(base)
        _fill(flick, 100, 20, 540, 260, (200, 200, 200))
        for i, im in enumerate((base, base, flick, base)):
            compare_shots.write_bmp(os.path.join(seq_dir, f"shot_{i:06d}.bmp"), im)
        entries = []
        for f in sorted(glob.glob(os.path.join(seq_dir, "shot_*.bmp"))):
            p, _ = inspect_image(f, argparse.Namespace(hud=None, black=BLACK_LEVEL, only=set()))
            entries.append((f, thumb(p)))
        jumps = check_sequence(entries)
        good = list(jumps) == [entries[2][0]]
        ok &= good
        print(f"{'ok    ' if good else 'ERROR'} jump in the picture sequence: "
              f"{'picture 2 reported' if good else sorted(os.path.basename(k) for k in jumps)}")
    print("Selbsttest", "bestanden" if ok else "FEHLGESCHLAGEN")
    return 0 if ok else 1


def _hud_rect(spec):
    try:
        return compare_shots.Rect(spec)
    except ValueError as e:
        raise argparse.ArgumentTypeError(str(e).replace("--ignore", "--hud"))


def main():
    ap = argparse.ArgumentParser(description="Picture inspection for render tests (without a reference)")
    ap.add_argument("inputs", nargs="*", help="folders with shot_*.bmp or single pictures")
    ap.add_argument("--json", action="store_true", help="result as JSON on stdout")
    ap.add_argument("--crops", help="folder for enlarged sections of the findings")
    ap.add_argument("--only", help="only these checks, comma-separated: " + ",".join(CHECKS))
    ap.add_argument("--hud", action="append", type=_hud_rect, metavar="X,Y,B,H", default=None,
                    help="rectangle of the 2D bar in pixels or percent of the picture (e.g. "
                         "12%%,68%%,76%%,32%%), may be given several times; replaces the default (4:3-wide "
                         "bar in the middle, radar part and rest)")
    ap.add_argument("--black", type=int, default=BLACK_LEVEL,
                    help="pixels with max(R,G,B) up to this value count as black (default 12)")
    ap.add_argument("--stats", action="store_true", help="print measured values per check (calibration)")
    ap.add_argument("--quiet", action="store_true", help="do not list unremarkable pictures")
    ap.add_argument("--selftest", action="store_true", help="self test with artificially damaged pictures")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not args.inputs:
        ap.error("give a folder or a picture (or --selftest)")
    if args.only:
        wanted = {s.strip() for s in args.only.split(",") if s.strip()}
        unknown = wanted - set(CHECKS)
        if unknown:
            ap.error("unknown check: " + ",".join(sorted(unknown)))
        args.only = wanted
    else:
        args.only = set(CHECKS)
    try:
        return run(args)
    except (OSError, ValueError) as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
