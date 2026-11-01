#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Alle EA-Karten gegen die Grenzen der Kartenleser pruefen, ohne das Spiel zu starten.

Die Leser lehnen seit R-303 Karten ab, deren Werte feste Arrays sprengen wuerden, und setzen einzelne
ungueltige Eintraege zurueck. Dieses Werkzeug liest jede Karte aus maps.big / MapsZH.big wie der C++-Code
und meldet je Karte:
  ABGELEHNT  der Leser wirft ERROR_CORRUPT_FILE_FORMAT (Spiel beendet sich beim Laden)
  GEKAPPT    Kachelzahl ueber 1024, auf 1024 begrenzt (laedt weiter)
  VERWORFEN  Texturklasse ausserhalb der Kacheltabelle, ohne Kacheln (fehlende Textur)
  GEPATCHT   Kachel-/Blend-/Klippenindex ausserhalb, auf 0 gesetzt
Erwartet fuer die EA-Karten: nichts davon.

Nachgebildet: WorldHeightMap::ParseHeightMapData/ParseBlendTileData und die Pruefschleife im Konstruktor,
MapUtil ParseSizeOnly, PolygonTrigger::ParsePolygonTriggersDataChunk, Condition/ScriptAction-Parameterzahl.

Aufruf:
  python tools/abnahme/kartengrenzen.py                    beide Spiele (Pfade wie tools/render/map_props.py)
  python tools/abnahme/kartengrenzen.py --big <datei.big>  nur dieses Archiv
Rueckgabe 0, wenn keine Karte abgelehnt wird; sonst 1.
"""
import argparse
import os
import struct
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "render"))
sys.path.insert(0, os.path.join(ROOT, "tools", "determinism"))
import map_props  # noqa: E402  (RefPack, BIG-Archiv, Chunks)
import check_map_scripts  # noqa: E402  (Skriptzeilen mit Parametern)

# Grenzen wie im Code (WorldHeightMap.h, WorldHeightMap.cpp, Scripts.h)
NUM_SOURCE_TILES = 1024
NUM_BLEND_TILES = 16192
NUM_CLIFF_INFO = 32384
NUM_TEXTURE_CLASSES = 256
MAX_MAP_EXTENT = 16384
MAX_MAP_BOUNDARIES = 1024
MAX_PARMS = 12


class Abgelehnt(Exception):
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


def height_map(ver, body, befunde):
    r = R(body)
    w, h = r.i32(), r.i32()
    border = r.i32() if ver >= 3 else 0
    if w <= 0 or h <= 0 or w > MAX_MAP_EXTENT or h > MAX_MAP_EXTENT or border < 0:
        raise Abgelehnt(f"HeightMapData: Groesse {w}x{h}, Rand {border}")
    if ver >= 4:
        n = r.i32()
        if n < 0 or n > MAX_MAP_BOUNDARIES:
            raise Abgelehnt(f"HeightMapData: {n} Grenzen")
        r.skip(8 * n)
    size = r.i32()
    if size <= 0 or size != w * h or size > r.left():
        raise Abgelehnt(f"HeightMapData: Datengroesse {size} bei {w}x{h}, Rest {r.left()}")
    return w, h, size


def blend_tiles(ver, body, w, h, size, befunde):
    r = R(body)
    if r.i32() != size:
        raise Abgelehnt("BlendTileData: Laenge passt nicht zur Hoehenkarte")
    tiles = r.i16s(size)
    blends = r.i16s(size)
    extra = r.i16s(size) if ver >= 6 else ()
    cliffs = r.i16s(size) if ver >= 5 else ()
    if ver >= 7:
        r.skip(h * ((w + 1) // 8) if ver == 7 else h * ((w + 7) // 8))
    n_bitmap, n_blended = r.i32(), r.i32()
    n_cliff = r.i32() if ver >= 5 else 1
    if not (0 <= n_blended <= NUM_BLEND_TILES) or not (0 <= n_cliff <= NUM_CLIFF_INFO):
        raise Abgelehnt(f"BlendTileData: {n_blended} Blend-Tiles, {n_cliff} Klippen-Infos")
    if not 0 <= n_bitmap <= NUM_SOURCE_TILES:
        befunde.append(f"GEKAPPT numBitmapTiles {n_bitmap}")

    def klassen(art, edge):
        n = r.i32()
        if not 0 <= n <= NUM_TEXTURE_CLASSES:
            raise Abgelehnt(f"BlendTileData: {n} {art}")
        for i in range(n):
            first, num, width = r.i32(), r.i32(), r.i32()
            if not edge:
                r.i32()                                   # legacy GDF
            r.astr()
            ok = (first >= 0 and num >= 0 and 0 <= width <= NUM_SOURCE_TILES
                  and first + num <= NUM_SOURCE_TILES and first + width * width <= NUM_SOURCE_TILES)
            if not ok:
                befunde.append(f"VERWORFEN {art} {i}: firstTile {first}, numTiles {num}, width {width}")
        return n

    klassen("Texturklassen", False)
    n_edge_classes = 0
    if ver >= 4:
        n_edge_tiles = r.i32()
        if not 0 <= n_edge_tiles <= NUM_SOURCE_TILES:
            befunde.append(f"GEKAPPT numEdgeTiles {n_edge_tiles}")
        # die Randklassen folgen ohne legacy-Feld; die Anzahl steht vor ihnen
        n_edge_classes = klassen("Randklassen", True)
    bad_blend = bad_edge = 0
    for _ in range(1, n_blended):
        ndx = r.i32()
        r.skip(5)
        if ver >= 3:
            r.skip(1)
        edge = r.i32() if ver >= 4 else -1
        if r.i32() != 0x7ADA0000:
            raise Abgelehnt("BlendTileData: Blend-Tile ohne Kennung")
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
    for n, text in ((cells, "Zellen mit Kachelindex ausserhalb"), (bad_blend, "Blend-Tiles mit Kachelindex ausserhalb"),
                    (bad_edge, "Blend-Tiles mit Randklasse ausserhalb"), (bad_cliff, "Klippen-Infos mit Kachelindex ausserhalb"),
                    (cells_blend, "Zellen mit Blend-Index ausserhalb (schon von EA gepatcht)"),
                    (cells_cliff, "Zellen mit Klippenindex ausserhalb (schon von EA gepatcht)")):
        if n:
            befunde.append(f"GEPATCHT {n} {text}")


def polygon_triggers(ver, body):
    r = R(body)
    count = r.i32()
    if count < 0 or count > r.left():
        raise Abgelehnt(f"PolygonTriggers: {count} Trigger, Rest {r.left()} Byte")
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
            raise Abgelehnt(f"PolygonTriggers: {n} Punkte, Rest {r.left()} Byte")
        r.skip(12 * n)


def pruefe_karte(name, data, ver_ok):
    befunde = []
    names, top = map_props.parse_chunks(data)
    w = h = size = None
    if "HeightMapData" in top:
        w, h, size = height_map(*top["HeightMapData"], befunde)
    else:
        raise Abgelehnt("ohne HeightMapData")
    if "BlendTileData" not in top:
        raise Abgelehnt("ohne BlendTileData")
    blend_tiles(*top["BlendTileData"], w, h, size, befunde)
    if "PolygonTriggers" in top:
        polygon_triggers(*top["PolygonTriggers"])
    for ln in check_map_scripts.parse_script_file(data, name):
        if len(ln.params) > MAX_PARMS:
            raise Abgelehnt(f"Skript '{ln.script}': {len(ln.params)} Parameter")
    return befunde


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--big", action="append", help="BIG-Archiv mit Karten (mehrfach); Standard: beide Spiele")
    a = ap.parse_args()
    archive = a.big or [p for p in map_props.ARCHIVES.values() if p]
    if not archive:
        sys.exit("keine Kartenarchive: --big angeben oder GZH_ASSETS_GENERALS/GZH_ASSETS_ZH setzen")
    # Parametertyp COORD3D aus Scripts.h, wie check_map_scripts
    check_map_scripts.COORD3D = check_map_scripts.Tree("GeneralsMD", None).ptypes.index("COORD3D")
    karten = abgelehnt = auffaellig = 0
    for big in archive:
        if not os.path.exists(big):
            print(f"FEHLT {big}")
            continue
        f, ents = map_props.big_entries(big)
        for nm, off, size in ents:
            if not nm.endswith(".map"):
                continue
            f.seek(off)
            karte = nm.split("/")[-2] if nm.count("/") >= 2 else nm
            karten += 1
            try:
                befunde = pruefe_karte(karte, check_map_scripts.unpack(f.read(size)), True)
            except Abgelehnt as e:
                abgelehnt += 1
                print(f"ABGELEHNT {os.path.basename(big)} {karte}: {e}")
                continue
            except Exception as e:                         # Leser dieses Werkzeugs, nicht des Spiels
                print(f"NICHT GELESEN {os.path.basename(big)} {karte}: {type(e).__name__} {e}")
                continue
            if befunde:
                auffaellig += 1
                for b in befunde:
                    print(f"{b.split()[0]} {os.path.basename(big)} {karte}: {' '.join(b.split()[1:])}")
    print(f"{karten} Karten, {abgelehnt} abgelehnt, {auffaellig} mit gekappten/verworfenen/gepatchten Werten")
    return 1 if abgelehnt else 0


if __name__ == "__main__":
    sys.exit(main())
