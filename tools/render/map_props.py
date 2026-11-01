# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Eigenschaften der Spielkarten aus den echten Kartendaten lesen: Tageszeit, Wetter, Wasserflaechen,
verschneiter Boden, Kartengroesse. Grundlage fuer die Auswahl der Kamerafahrten
.

Die Kartendateien liegen in maps.big bzw. MapsZH.big und sind RefPack-gepackt (Kopf 'EAR\\0',
Signatur 0x10FB). Entpackt beginnen sie mit 'CkMp', einer Namenstabelle und den Chunks:
  GlobalLighting   erstes Int32 = Tageszeit (1 Morgen, 2 Nachmittag, 3 Abend, 4 Nacht)
  WorldInfo        Woerterbuch, Schluessel 'weather' (0 normal, 1 Schneefall)
  PolygonTriggers  Polygone, darunter die Wasserflaechen (Zero Hour ab Version 4 mit Ebenenname)
  BlendTileData    Gelaendetexturen -- 'snow' im Namen heisst verschneiter Boden

Aufruf:
  python tools/render/map_props.py                       alle Karten beider Spiele
  python tools/render/map_props.py --only night          nur Nachtkarten
  python tools/render/map_props.py --map "tournament tundra"
"""
import argparse
import os
import re
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import pfade  # noqa: E402


def _archiv(spiel, name):
    wurzel = pfade.assets(spiel, pflicht=False)
    return os.path.join(wurzel, name) if wurzel else ""


# Kartenarchive der Asset-Kopien; leer, wenn GZH_ASSETS_GENERALS bzw. GZH_ASSETS_ZH fehlt
ARCHIVES = {
    "Generals": _archiv("generals", "maps.big"),
    "ZeroHour": _archiv("zh", "MapsZH.big"),
}
TIME_OF_DAY = {1: "Morgen", 2: "Nachmittag", 3: "Abend", 4: "Nacht"}


# --- RefPack ------------------------------------------------------------------------------------

def unrefpack(data):
    """EA-RefPack (QFS) entpacken."""
    p = 0
    flags = data[p] << 8 | data[p + 1]; p += 2
    if flags & 0x00FF != 0x00FB:
        raise ValueError("keine RefPack-Signatur")
    big = flags & 0x8000
    if flags & 0x0100:
        p += 4 if big else 3
    n = 4 if big else 3
    size = int.from_bytes(data[p:p + n], "big"); p += n
    out = bytearray()
    while p < len(data):
        b0 = data[p]
        if b0 < 0x80:
            b1 = data[p + 1]; p += 2
            plain, ln, off = b0 & 3, ((b0 >> 2) & 7) + 3, ((b0 & 0x60) << 3) + b1 + 1
        elif b0 < 0xC0:
            b1, b2 = data[p + 1], data[p + 2]; p += 3
            plain, ln, off = (b1 >> 6) & 3, (b0 & 0x3F) + 4, ((b1 & 0x3F) << 8) + b2 + 1
        elif b0 < 0xE0:
            b1, b2, b3 = data[p + 1], data[p + 2], data[p + 3]; p += 4
            plain = b0 & 3
            ln = ((b0 & 0x0C) << 6) + b3 + 5
            off = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1
        elif b0 < 0xFC:
            p += 1
            plain, ln, off = ((b0 & 0x1F) << 2) + 4, 0, 0
        else:
            p += 1
            plain = b0 & 3
            out += data[p:p + plain]
            break
        out += data[p:p + plain]; p += plain
        for _ in range(ln):
            out.append(out[-off])
    if len(out) != size:
        raise ValueError(f"entpackt {len(out)} statt {size} Byte")
    return bytes(out)


def unpack_map(raw):
    return unrefpack(raw[8:]) if raw[:4] == b"EAR\0" else raw


# --- Archiv und Kartenformat --------------------------------------------------------------------

def big_entries(path):
    f = open(path, "rb")
    if f.read(4) != b"BIGF":
        raise ValueError(f"{path}: kein BIG-Archiv")
    f.read(4)
    n, = struct.unpack(">I", f.read(4)); f.read(4)
    out = []
    for _ in range(n):
        off, size = struct.unpack(">II", f.read(8)); nm = b""
        while True:
            c = f.read(1)
            if c == b"\0":
                break
            nm += c
        out.append((nm.decode("latin-1").lower().replace("\\", "/"), off, size))
    return f, out


def parse_chunks(d):
    if d[:4] != b"CkMp":
        raise ValueError("kein CkMp-Kopf")
    p = 4
    cnt, = struct.unpack("<I", d[p:p + 4]); p += 4
    names = {}
    for _ in range(cnt):
        ln = d[p]; p += 1
        nm = d[p:p + ln].decode("latin-1"); p += ln
        cid, = struct.unpack("<I", d[p:p + 4]); p += 4
        names[cid] = nm
    top = {}
    while p + 10 <= len(d):
        cid, ver, ln = struct.unpack("<IHI", d[p:p + 10])
        top.setdefault(names.get(cid, str(cid)), (ver, d[p + 10:p + 10 + ln]))
        p += 10 + ln
    return names, top


def read_dict(body, names):
    p = 0
    n, = struct.unpack("<H", body[p:p + 2]); p += 2
    out = {}
    for _ in range(n):
        kt, = struct.unpack("<I", body[p:p + 4]); p += 4
        typ, key = kt & 0xFF, names.get(kt >> 8, str(kt >> 8))
        if typ == 0:
            val = body[p]; p += 1
        elif typ == 1:
            val, = struct.unpack("<i", body[p:p + 4]); p += 4
        elif typ == 2:
            val, = struct.unpack("<f", body[p:p + 4]); p += 4
        elif typ == 3:
            l, = struct.unpack("<H", body[p:p + 2]); p += 2
            val = body[p:p + l].decode("latin-1"); p += l
        elif typ == 4:
            l, = struct.unpack("<H", body[p:p + 2]); p += 2
            val = body[p:p + 2 * l].decode("utf-16-le"); p += 2 * l
        else:
            break
        out[key] = val
    return out


def water_areas(ver, body):
    p = 0
    n, = struct.unpack("<i", body[p:p + 4]); p += 4
    water = 0
    for _ in range(n):
        l, = struct.unpack("<H", body[p:p + 2]); p += 2 + l
        if ver >= 4:                                   # Zero Hour: Ebenenname
            l, = struct.unpack("<H", body[p:p + 2]); p += 2 + l
        p += 4
        is_water = body[p]; p += 1
        if ver >= 3:
            p += 1 + 4                                 # isRiver, riverStart
        npts, = struct.unpack("<i", body[p:p + 4]); p += 4 + npts * 12
        water += 1 if is_water else 0
    return water


def map_properties(data):
    names, top = parse_chunks(unpack_map(data))
    tod = struct.unpack("<i", top["GlobalLighting"][1][:4])[0] if "GlobalLighting" in top else 0
    world = read_dict(top["WorldInfo"][1], names) if "WorldInfo" in top else {}
    water = water_areas(*top["PolygonTriggers"]) if "PolygonTriggers" in top else 0
    snow_ground = bool("BlendTileData" in top and re.search(rb"[Ss][Nn][Oo][Ww]", top["BlendTileData"][1]))
    size = struct.unpack("<ii", top["HeightMapData"][1][:8]) if "HeightMapData" in top else (0, 0)
    return {
        "tageszeit": TIME_OF_DAY.get(tod, str(tod)),
        "schneefall": world.get("weather", 0) == 1,
        "schneeboden": snow_ground,
        "wasser": water,
        "groesse": size,
    }


def scan():
    for game, path in ARCHIVES.items():
        if not os.path.exists(path):
            continue
        f, ents = big_entries(path)
        for nm, off, size in ents:
            if not nm.endswith(".map") or nm.count("/") < 2:
                continue
            f.seek(off)
            try:
                props = map_properties(f.read(size))
            except Exception:
                continue
            yield game, nm.split("/")[-2], props


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", choices=("night", "snow", "water"), help="nur Karten mit dieser Eigenschaft")
    ap.add_argument("--map", help="nur diese Karte (Name wie im Archiv, klein geschrieben)")
    a = ap.parse_args()
    rows = list(scan())
    if a.map:
        rows = [r for r in rows if r[1] == a.map.lower()]
    if a.only == "night":
        rows = [r for r in rows if r[2]["tageszeit"] == "Nacht"]
    elif a.only == "snow":
        rows = [r for r in rows if r[2]["schneefall"] or r[2]["schneeboden"]]
    elif a.only == "water":
        rows = [r for r in rows if r[2]["wasser"] > 0]
    print(f"{'Spiel':9} {'Karte':30} {'Tageszeit':11} {'Schneefall':10} {'Schneeboden':11} {'Wasser':6} Groesse")
    for game, name, p in sorted(rows, key=lambda r: (r[0], r[1])):
        print(f"{game:9} {name:30} {p['tageszeit']:11} {'ja' if p['schneefall'] else '-':10} "
              f"{'ja' if p['schneeboden'] else '-':11} {p['wasser']:6} {p['groesse'][0]}x{p['groesse'][1]}")
    print(f"\n{len(rows)} Karten")


if __name__ == "__main__":
    main()
