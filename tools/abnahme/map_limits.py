#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Check all EA maps against the limits of the map readers without starting the game.

The readers reject maps whose values would burst fixed arrays and reset single invalid entries. This tool reads
every map from maps.big / MapsZH.big like the C++ code and reports per map:
  REJECTED   the reader throws ERROR_CORRUPT_FILE_FORMAT (the game quits while loading)
  CLAMPED    tile count above 1024, limited to 1024 (loads on)
  DISCARDED  texture class outside the tile table, without tiles (missing texture)
  PATCHED    tile/blend/cliff index outside, set to 0
Expected for the EA maps: none of these.

Modelled on: WorldHeightMap::ParseHeightMapData/ParseBlendTileData and the check loop in the constructor,
MapUtil ParseSizeOnly, PolygonTrigger::ParsePolygonTriggersDataChunk, condition/script-action parameter count.

Usage:
  python tools/abnahme/map_limits.py                    both games (paths as in tools/render/map_props.py)
  python tools/abnahme/map_limits.py --big <file.big>   only this archive
Returns 0 when no map is rejected, otherwise 1.
"""
import argparse
import os
import struct
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "render"))
sys.path.insert(0, os.path.join(ROOT, "tools", "determinism"))
import map_props  # noqa: E402  (RefPack, BIG-Archiv, Chunks)
import check_map_scripts  # noqa: E402  (script lines with parameters)

# limits as in the code (WorldHeightMap.h, WorldHeightMap.cpp, Scripts.h)
NUM_SOURCE_TILES = 1024
NUM_BLEND_TILES = 16192
NUM_CLIFF_INFO = 32384
NUM_TEXTURE_CLASSES = 256
MAX_MAP_EXTENT = 16384
MAX_MAP_BOUNDARIES = 1024
MAX_PARMS = 12


class Rejected(Exception):
    pass


class R:
    def __init__(self, body):
        self.d, self.p = body, 0

    def i32(self):
        v, = struct.unpack_from("<i", self.d, self.p); self.p += 4; return v

    def i16s(self, n):
        v = struct.unpack_from("<%dh" % n, self.d, self.p); self.p += 2 * n; return v

    def u8(self):
        v = self.d[self.p]; self.p += 1; return v

    def skip(self, n):
        self.p += n

    def astr(self):
        ln, = struct.unpack_from("<H", self.d, self.p); self.p += 2 + ln

    def left(self):
        return len(self.d) - self.p


def tile_ok(ndx):
    return ndx >= 0 and (ndx >> 2) < NUM_SOURCE_TILES


def height_map(ver, body, findings):
    r = R(body)
    w, h = r.i32(), r.i32()
    border = r.i32() if ver >= 3 else 0
    if w <= 0 or h <= 0 or w > MAX_MAP_EXTENT or h > MAX_MAP_EXTENT or border < 0:
        raise Rejected(f"HeightMapData: size {w}x{h}, border {border}")
    if ver >= 4:
        n = r.i32()
        if n < 0 or n > MAX_MAP_BOUNDARIES:
            raise Rejected(f"HeightMapData: {n} limits")
        r.skip(8 * n)
    size = r.i32()
    if size <= 0 or size != w * h or size > r.left():
        raise Rejected(f"HeightMapData: data size {size} at {w}x{h}, rest {r.left()}")
    return w, h, size


def blend_tiles(ver, body, w, h, size, findings):
    r = R(body)
    if r.i32() != size:
        raise Rejected("BlendTileData: length does not match the height map")
    tiles = r.i16s(size)
    blends = r.i16s(size)
    extra = r.i16s(size) if ver >= 6 else ()
    cliffs = r.i16s(size) if ver >= 5 else ()
    if ver >= 7:
        r.skip(h * ((w + 1) // 8) if ver == 7 else h * ((w + 7) // 8))
    n_bitmap, n_blended = r.i32(), r.i32()
    n_cliff = r.i32() if ver >= 5 else 1
    if not (0 <= n_blended <= NUM_BLEND_TILES) or not (0 <= n_cliff <= NUM_CLIFF_INFO):
        raise Rejected(f"BlendTileData: {n_blended} blend tiles, {n_cliff} cliff infos")
    if not 0 <= n_bitmap <= NUM_SOURCE_TILES:
        findings.append(f"CLAMPED numBitmapTiles {n_bitmap}")

    def klassen(art, edge):
        n = r.i32()
        if not 0 <= n <= NUM_TEXTURE_CLASSES:
            raise Rejected(f"BlendTileData: {n} {art}")
        for i in range(n):
            first, num, width = r.i32(), r.i32(), r.i32()
            if not edge:
                r.i32()                                   # legacy GDF
            r.astr()
            ok = (first >= 0 and num >= 0 and 0 <= width <= NUM_SOURCE_TILES
                  and first + num <= NUM_SOURCE_TILES and first + width * width <= NUM_SOURCE_TILES)
            if not ok:
                findings.append(f"DISCARDED {art} {i}: firstTile {first}, numTiles {num}, width {width}")
        return n

    klassen("Texturklassen", False)
    n_edge_classes = 0
    if ver >= 4:
        n_edge_tiles = r.i32()
        if not 0 <= n_edge_tiles <= NUM_SOURCE_TILES:
            findings.append(f"CLAMPED numEdgeTiles {n_edge_tiles}")
        # the edge classes follow without a legacy field; their count stands before them
        n_edge_classes = klassen("Randklassen", True)
    bad_blend = bad_edge = 0
    for _ in range(1, n_blended):
        ndx = r.i32()
        r.skip(5)
        if ver >= 3:
            r.skip(1)
        edge = r.i32() if ver >= 4 else -1
        if r.i32() != 0x7ADA0000:
            raise Rejected("BlendTileData: blend tile without an identifier")
        bad_blend += not tile_ok(ndx)
        bad_edge += edge >= n_edge_classes
    bad_cliff = 0
    if ver >= 5:
        for _ in range(1, n_cliff):
            ndx = r.i32()
            r.skip(8 * 4 + 2)
            bad_cliff += not tile_ok(ndx)
    cells = sum(not tile_ok(t) for t in tiles)
    cells_blend = sum(not (0 <= b < n_blended) for b in blends) + sum(not (0 <= b < n_blended) for b in extra)
    cells_cliff = sum(not (0 <= c < n_cliff) for c in cliffs)
    for n, text in ((cells, "cells with a tile index outside"), (bad_blend, "blend tiles with a tile index outside"),
                    (bad_edge, "blend tiles with an edge class outside"), (bad_cliff, "cliff infos with a tile index outside"),
                    (cells_blend, "cells with a blend index outside (already patched by EA)"),
                    (cells_cliff, "cells with a cliff index outside (already patched by EA)")):
        if n:
            findings.append(f"PATCHED {n} {text}")


def polygon_triggers(ver, body):
    r = R(body)
    count = r.i32()
    if count < 0 or count > r.left():
        raise Rejected(f"PolygonTriggers: {count} triggers, rest {r.left()} bytes")
    for _ in range(count):
        r.astr()
        if ver >= 4:
            r.astr()
        r.i32()
        if ver >= 2:
            r.u8()
        if ver >= 3:
            r.u8(); r.i32()
        n = r.i32()
        if n < 0 or n > r.left() // 12:
            raise Rejected(f"PolygonTriggers: {n} points, rest {r.left()} bytes")
        r.skip(12 * n)


def check_map(name, data, ver_ok):
    findings = []
    names, top = map_props.parse_chunks(data)
    w = h = size = None
    if "HeightMapData" in top:
        w, h, size = height_map(*top["HeightMapData"], findings)
    else:
        raise Rejected("without HeightMapData")
    if "BlendTileData" not in top:
        raise Rejected("without BlendTileData")
    blend_tiles(*top["BlendTileData"], w, h, size, findings)
    if "PolygonTriggers" in top:
        polygon_triggers(*top["PolygonTriggers"])
    for ln in check_map_scripts.parse_script_file(data, name):
        if len(ln.params) > MAX_PARMS:
            raise Rejected(f"script '{ln.script}': {len(ln.params)} parameters")
    return findings


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--big", action="append", help="BIG archive with maps (may be given several times); default: both games")
    a = ap.parse_args()
    archive = a.big or [p for p in map_props.ARCHIVES.values() if p]
    if not archive:
        sys.exit("no map archives: give --big or set GZH_ASSETS_GENERALS/GZH_ASSETS_ZH")
    # parameter type COORD3D from Scripts.h, as in check_map_scripts
    check_map_scripts.COORD3D = check_map_scripts.Tree("GeneralsMD", None).ptypes.index("COORD3D")
    maps = rejected = conspicuous = 0
    for big in archive:
        if not os.path.exists(big):
            print(f"MISSING {big}")
            continue
        f, ents = map_props.big_entries(big)
        for nm, off, size in ents:
            if not nm.endswith(".map"):
                continue
            f.seek(off)
            map_name = nm.split("/")[-2] if nm.count("/") >= 2 else nm
            maps += 1
            try:
                findings = check_map(map_name, check_map_scripts.unpack(f.read(size)), True)
            except Rejected as e:
                rejected += 1
                print(f"REJECTED {os.path.basename(big)} {map_name}: {e}")
                continue
            except Exception as e:                         # reader of this tool, not of the game
                print(f"NOT READ {os.path.basename(big)} {map_name}: {type(e).__name__} {e}")
                continue
            if findings:
                conspicuous += 1
                for b in findings:
                    print(f"{b.split()[0]} {os.path.basename(big)} {map_name}: {' '.join(b.split()[1:])}")
    print(f"{maps} maps, {rejected} rejected, {conspicuous} with clamped/discarded/patched values")
    return 1 if rejected else 0


if __name__ == "__main__":
    sys.exit(main())
