#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Mehrere Laeufe derselben Szene exakt vergleichen.

compare_shots.py prueft gegen eine Referenz mit Schwelle und PSNR; fuer die Frage "ist die Fahrt
pixelgenau reproduzierbar?" zaehlt aber jede Abweichung um eine Stufe. Dieses Werkzeug vergleicht
beliebig viele Laufordner Bild fuer Bild ohne Schwelle, gruppiert die Laeufe je Bild nach identischem
Inhalt (so sieht man, ob es genau zwei Zustaende gibt -- typisch fuer eine Ja/Nein-Entscheidung irgendwo
im Programm -- oder lauter verschiedene) und nennt fuer jede abweichende Gruppe das umschliessende
Rechteck der Abweichung.

  python tools/render/compare_runs.py render-out/fahrt-a render-out/fahrt-b render-out/fahrt-c
  python tools/render/compare_runs.py --min 3 ...     # Abweichungen bis 2 Stufen je Kanal ignorieren

Rueckgabewert 0: alle Laeufe in allen Bildern gleich; 1: Abweichungen; 2: Lesefehler.
Braucht Pillow und NumPy (wie inspect_shots.py --crops).
"""
import argparse
import glob
import os
import sys

try:
    import numpy as np
    from PIL import Image
except ImportError:
    print("compare_runs.py braucht Pillow und NumPy (pip install pillow numpy)")
    sys.exit(2)


def load(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(np.int16)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("runs", nargs="+", help="Laufordner mit shot_*.bmp")
    ap.add_argument("--min", type=int, default=1,
                    help="kleinste Kanalabweichung, die zaehlt (Vorgabe 1 = jede Abweichung)")
    a = ap.parse_args()
    if len(a.runs) < 2:
        print("mindestens zwei Laufordner angeben")
        return 2

    names = sorted(os.path.basename(p) for p in glob.glob(os.path.join(a.runs[0], "shot_*.bmp")))
    if not names:
        print(f"keine shot_*.bmp in {a.runs[0]}")
        return 2
    differing = 0
    for name in names:
        images = []
        for run in a.runs:
            path = os.path.join(run, name)
            if not os.path.isfile(path):
                print(f"{name}: fehlt in {run}")
                return 2
            images.append(load(path))
        # Gruppen gleicher Bilder (Vertreter = erster Lauf der Gruppe)
        groups = []
        for i, img in enumerate(images):
            for g in groups:
                if np.abs(images[g[0]] - img).max() < a.min:
                    g.append(i)
                    break
            else:
                groups.append([i])
        if len(groups) == 1:
            continue
        differing += 1
        parts = []
        for g in groups[1:]:
            d = np.abs(images[groups[0][0]] - images[g[0]]).max(axis=2) >= a.min
            ys, xs = np.nonzero(d)
            parts.append(f"{int(d.sum())} Pixel x {xs.min()}-{xs.max()} y {ys.min()}-{ys.max()}")
        label = " | ".join("+".join(os.path.basename(a.runs[i].rstrip("/\\")) for i in g) for g in groups)
        print(f"{name}: {len(groups)} Zustaende [{label}]; gegen den ersten: " + "; ".join(parts))
    print(f"{differing} von {len(names)} Bildern nicht in allen Laeufen gleich ({len(a.runs)} Laeufe)")
    return 1 if differing else 0


if __name__ == "__main__":
    sys.exit(main())
