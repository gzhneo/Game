# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Kartenobjekte nach Vorlage zählen und ihre Lage ausgeben -- für Kamerafahrten zu Requisiten (Felsen, Bäume, Fässer auf Tournament Lake und der Shell-Map).

  python tools/render/map_objekte.py "Tournament Lake"                     Vorlagen mit Anzahl
  python tools/render/map_objekte.py "Tournament Lake" Rocks1 TreePine     Lage (Welt und kartenrelativ) je Objekt
  python tools/render/map_objekte.py ShellMapMD 55GallonDrum --cluster 60  Häufungen (Mittel naher Objekte)

Kartenrelativ heißt wie bei map_wegpunkte.py: bezogen auf die spielbare Fläche, wie -cameraPath sie erwartet.
"""
import argparse
import collections
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from map_props import parse_chunks  # noqa: E402
from map_wegpunkte import karte_lesen, unterchunks  # noqa: E402


def objekte(name):
    archiv, d = karte_lesen(name)
    names, top = parse_chunks(d)
    ver, hm = top["HeightMapData"]
    w, h = struct.unpack("<ii", hm[:8])
    rand = struct.unpack("<i", hm[8:12])[0] if ver >= 3 else 0
    breite, hoehe = (w - 2 * rand) * 10.0, (h - 2 * rand) * 10.0
    out = []
    for nm, ver, body in unterchunks(top["ObjectsList"][1], names):
        if nm != "Object":
            continue
        x, y, z, winkel = struct.unpack("<ffff", body[:16])
        p = 20
        ln, = struct.unpack("<H", body[p:p + 2]); p += 2
        out.append((body[p:p + ln].decode("latin-1"), x, y, z, winkel))
    return archiv, breite, hoehe, out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("karte")
    ap.add_argument("vorlagen", nargs="*")
    ap.add_argument("--cluster", type=float, default=0, help="Objekte näher als dieser Abstand zusammenfassen")
    a = ap.parse_args()
    archiv, breite, hoehe, objs = objekte(a.karte)
    print("Karte %s (%s), spielbar %.0f x %.0f, %d Objekte" % (a.karte, archiv, breite, hoehe, len(objs)))
    if not a.vorlagen:
        for k, v in collections.Counter(o[0] for o in objs).most_common():
            print("  %5d %s" % (v, k))
        return
    wahl = [o for o in objs if o[0] in a.vorlagen]
    if a.cluster > 0:
        gruppen = []
        for o in wahl:
            for g in gruppen:
                if abs(g[0][1] - o[1]) < a.cluster and abs(g[0][2] - o[2]) < a.cluster:
                    g.append(o)
                    break
            else:
                gruppen.append([o])
        gruppen.sort(key=len, reverse=True)
        for g in gruppen:
            mx = sum(o[1] for o in g) / len(g)
            my = sum(o[2] for o in g) / len(g)
            print("  %3d Objekte %-40s Welt %8.1f %8.1f  relativ %.3f %.3f" % (
                len(g), ",".join(sorted({o[0] for o in g}))[:40], mx, my, mx / breite, my / hoehe))
        return
    for o in wahl:
        print("  %-24s Welt %8.1f %8.1f %6.1f  Winkel %6.1f  relativ %.3f %.3f" % (
            o[0], o[1], o[2], o[3], o[4] * 57.2958, o[1] / breite, o[2] / hoehe))


if __name__ == "__main__":
    main()
