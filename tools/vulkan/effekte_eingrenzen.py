# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Abweichung Vulkan gegen Direct3D 11 je Effektgruppe eingrenzen (nur Windows).

Je Gruppe: die Kurzfahrten (alle_fahrten.py --gruppe kurz) erst mit Direct3D 11, dann mit -vulkan und denselben
Schaltern gegen diesen Lauf (--referenz, Standardtoleranz von compare_shots.py). Am Ende eine Tabelle: Bilder
ausserhalb der Toleranz je Gruppe und Fahrt.

Aufruf:
  python tools/vulkan/effekte_eingrenzen.py                      die vier Gruppen der Stufe "Sehr hoch"
  python tools/vulkan/effekte_eingrenzen.py --einzeln            jeder Effekt der Stufe "Sehr hoch" allein
  python tools/vulkan/effekte_eingrenzen.py --nur ssao,shadowmap=4096 --einzeln
  python tools/vulkan/effekte_eingrenzen.py --ohne-je-einen     alle Effekte, und je ein Lauf ohne einen von ihnen
  python tools/vulkan/effekte_eingrenzen.py --kumulativ          Effekte nacheinander dazunehmen (FXAA und Schaerfen zuletzt)
  python tools/vulkan/effekte_eingrenzen.py --basis ""           ohne -depthD32 (Standard: mit, siehe unten)

Liegt die Abweichung nur im Zusammenspiel mehrerer Effekte (jeder allein in der Toleranz), zeigt --ohne-je-einen die
beteiligten: der Lauf ohne einen beteiligten Effekt faellt auf 0. --kumulativ zeigt, ab welchem Effekt sie auftritt.

--basis (Standard "-depthD32") gilt fuer beide Laeufe jeder Gruppe: so bleibt nur, was nicht am Tiefenformat liegt
(AMD hat unter Vulkan kein D24S8). Ausgabe unter render-out/effekte/<Gruppe>/{d3d11,vulkan}, Tabelle in
render-out/effekte/ergebnis.txt.
"""
import argparse
import os
import re
import subprocess
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
WURZEL = os.path.dirname(os.path.dirname(HIER))
FAHRTEN = os.path.join(WURZEL, "tools", "render", "alle_fahrten.py")

# The effects of the detail level "Sehr hoch" (Data/INI/GameLODOverride.ini), grouped by what they touch.
GRUPPEN = {
    "nachbearbeitung": "fxaa,sharpen,colorgrade,bloom,heat,haze",
    "tiefe": "ssao,softparticles,shadowmap=4096",
    "gelaende": "terraindetail,terrainrock,terrainnormal=2,terraintess=2,rocktess=1",
    "licht": "particlelight,particleground,lights",
}
# All of them in the order --kumulativ adds them: scene and terrain first, then lights and depth effects, the post
# effects last (FXAA and sharpening decide per edge and so turn small input differences into visible ones).
ALLE = ["terraindetail", "terrainrock", "terrainnormal=2", "terraintess=2", "rocktess=1", "shadowmap=4096",
        "particlelight", "particleground", "lights", "softparticles", "ssao", "haze", "heat", "bloom", "colorgrade",
        "sharpen", "fxaa"]
# Effects that need others to show anything.
VORAUSSETZUNG = {
    "rocktess=1": "terrainrock,terraintess=2,rocktess=1",
    "terrainnormal=2": "terrainrock,terrainnormal=2",
}


def lauf(name, schalter, out, referenz=None):
    cmd = [sys.executable, FAHRTEN, "--gruppe", "kurz", "--schalter-text", schalter, "--out", out]
    if referenz:
        cmd += ["--referenz", referenz]
    print(">>", name, schalter, flush=True)
    p = subprocess.run(cmd, cwd=WURZEL, capture_output=True, text=True, errors="replace")
    print(p.stdout, flush=True)
    return p.stdout


def ausserhalb(text):
    """Fahrt -> Bilder ausserhalb der Toleranz (0, wenn in der Toleranz)."""
    werte = {}
    for z in text.splitlines():
        m = re.match(r"(\S+)\s+\d+ Bilder", z)
        if not m:
            continue
        a = re.search(r"AUSSERHALB \((\d+) Bilder\)", z)
        werte[m.group(1)] = int(a.group(1)) if a else (0 if "in der Toleranz" in z else -1)
    return werte


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--einzeln", action="store_true", help="jeden Effekt allein statt der Gruppen")
    ap.add_argument("--ohne-je-einen", action="store_true", help="alle Effekte, dann je einen weglassen")
    ap.add_argument("--kumulativ", action="store_true", help="Effekte nacheinander dazunehmen")
    ap.add_argument("--nur", help="Gruppen bzw. Effekte, kommagetrennt (bei --ohne-je-einen: die wegzulassenden)")
    ap.add_argument("--basis", default="-depthD32", help="Schalter fuer beide Laeufe jeder Gruppe")
    ap.add_argument("--out", default=os.path.join(WURZEL, "render-out", "effekte"))
    ap.add_argument("--trocken", action="store_true", help="nur die geplanten Laeufe zeigen")
    a = ap.parse_args()

    if a.ohne_je_einen:
        namen = a.nur.split(",") if a.nur else ALLE
        reihe = [("alle", ",".join(ALLE))]
        reihe += [("ohne_" + n.split("=")[0], ",".join(e for e in ALLE if e != n)) for n in namen]
    elif a.kumulativ:
        reihe = [("bis_" + ALLE[i].split("=")[0], ",".join(ALLE[:i + 1])) for i in range(len(ALLE))]
    elif a.einzeln:
        alle = [e for g in GRUPPEN.values() for e in g.split(",")]
        namen = a.nur.split(",") if a.nur else alle
        reihe = [(n.split("=")[0], VORAUSSETZUNG.get(n, n)) for n in namen]
    else:
        namen = a.nur.split(",") if a.nur else list(GRUPPEN)
        reihe = [(n, GRUPPEN[n]) for n in namen]

    if a.trocken:
        for name, effekte in reihe:
            print("%-20s %s -effects %s" % (name, a.basis, effekte))
        return 0

    tabelle = []
    for name, effekte in reihe:
        basis = os.path.join(a.out, name)
        schalter = ("%s -effects %s" % (a.basis, effekte)).strip()
        lauf(name + " d3d11", schalter, os.path.join(basis, "d3d11"))
        text = lauf(name + " vulkan", "-vulkan " + schalter, os.path.join(basis, "vulkan"), os.path.join(basis, "d3d11"))
        tabelle.append((name, effekte, ausserhalb(text)))

    fahrten = sorted({f for _, _, w in tabelle for f in w})
    zeilen = ["Basis: %s" % (a.basis or "(keine)"), "%-20s %s  Summe  Effekte" % ("Lauf", " ".join("%10s" % f for f in fahrten))]
    for name, effekte, w in tabelle:
        zeilen.append("%-20s %s  %5d  %s" % (name, " ".join("%10d" % w.get(f, -1) for f in fahrten),
                                             sum(v for v in w.values() if v > 0), effekte))
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, "ergebnis.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(zeilen) + "\n")
    print("\n".join(zeilen))
    return 0


if __name__ == "__main__":
    sys.exit(main())
