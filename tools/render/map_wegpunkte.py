# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Wegpunkte einer Karte (Startpositionen u. a.) in Weltkoordinaten und kartenrelativ (0..1) ausgeben --
für Kamerafahrten zu einem Ort (TODO 62: Humvees an einer KI-Basis).

  python tools/render/map_wegpunkte.py "Tournament Lake" [--alle]

Ohne --alle nur die Startpunkte (Player_N_Start). Kartenrelativ heißt: bezogen auf die spielbare Fläche
(Höhenfeld ohne Rand, 10 Welteinheiten je Zelle), wie -cameraPath sie erwartet.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from map_props import big_entries, parse_chunks, read_dict, unpack_map  # noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import pfade  # noqa: E402
ARCHIVE = [os.path.join(w, n) for w, n in ((pfade.assets("zh", False), "MapsZH.big"),
                                          (pfade.assets("zh", False), os.path.join("ZH_Generals", "maps.big")),
                                          (pfade.assets("generals", False), "maps.big")) if w]


def karte_lesen(name):
    ziel = name.lower()
    for a in ARCHIVE:
        if not os.path.exists(a):
            continue
        f, ents = big_entries(a)
        for nm, off, size in ents:
            if nm.endswith(".map") and nm.split("/")[-2] == ziel:
                f.seek(off)
                return a, unpack_map(f.read(size))
    raise SystemExit("Karte nicht gefunden: " + name)


def unterchunks(body, names):
    p = 0
    while p + 10 <= len(body):
        cid, ver, ln = struct.unpack("<IHI", body[p:p + 10])
        yield names.get(cid, str(cid)), ver, body[p + 10:p + 10 + ln]
        p += 10 + ln


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("karte")
    ap.add_argument("--alle", action="store_true")
    a = ap.parse_args()
    archiv, d = karte_lesen(a.karte)
    names, top = parse_chunks(d)
    ver, hm = top["HeightMapData"]
    w, h = struct.unpack("<ii", hm[:8])
    rand = struct.unpack("<i", hm[8:12])[0] if ver >= 3 else 0
    breite, hoehe = (w - 2 * rand) * 10.0, (h - 2 * rand) * 10.0
    print("Karte %s (%s): Hoehenfeld %dx%d, Rand %d, spielbar %.0f x %.0f" % (a.karte, archiv, w, h, rand, breite, hoehe))
    for nm, ver, body in unterchunks(top["ObjectsList"][1], names):
        if nm != "Object":
            continue
        x, y, z, winkel = struct.unpack("<ffff", body[:16])
        p = 16 + 4
        ln, = struct.unpack("<H", body[p:p + 2]); p += 2
        vorlage = body[p:p + ln].decode("latin-1"); p += ln
        dic = read_dict(body[p:], names)
        wp = dic.get("waypointName")
        if wp and (a.alle or wp.startswith("Player_")):
            print("  %-24s Welt %8.1f %8.1f  relativ %.3f %.3f" % (wp, x, y, x / breite, y / hoehe))
        elif a.alle and not wp:
            pass


if __name__ == "__main__":
    main()
