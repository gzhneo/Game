#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour (Electronic Arts).
# Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Bildaufnahmen fuer die freie Wassertextur: dieselben Szenen mit der
EA-Wasserfarbe und mit -waterTexture free, je Wasserstufe.

  python tools/render/wassertextur_aufnahmen.py [--build build-x64] [--config Release] [--nur z-see,z-insel]
      [--stufen 2,0] [--varianten ea,frei] [--parallel 3]

Jeder Lauf wartet auf die Spielstart-Warteschlange (tools/abnahme/slot.py) und startet run_render_test.py mit
--mehrfach im Fenster. Ergebnis: <out>/<variante>_q<stufe>/<szene>/shot_*.bmp samt Protokoll.
Szenen: Meer (Tournament Island, Shell-Map Zero Hour), See (Tournament Lake), Fluss (Winding River, Heartland
Shield), Winter (Bitter Winter mit truebem Wasser, Whiteout), Wueste (Golden Oasis, Barren Badlands mit truebem
Wasser), Nacht (Silent River).
"""
import argparse
import os
import subprocess
import sys
import threading

WURZEL = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(WURZEL, "tools", "abnahme"))
from slot import wait_free  # noqa: E402

PFADE = os.path.join(WURZEL, "tests", "render", "paths")
EXE = {"g": ("Generals", "GZHNeo.exe"), "z": ("GeneralsMD", "GZHNeoZH.exe")}


def karte(spiel, name, pfad):
    return (spiel, ["--scene", "skirmish", "--map", name, "--ai", "1", "--path", os.path.join(PFADE, pfad), "--every", "60"])


SZENEN = {
    "z-insel":    karte("z", "Tournament Island", "wassertextur_insel.txt"),
    "z-see":      karte("z", "Tournament Lake", "wasser_see.txt"),
    "z-fluss":    karte("z", "Winding River", "wassertextur_fluss.txt"),
    "g-fluss":    karte("g", "Heartland Shield", "wasser_fluss.txt"),
    "z-winter":   karte("z", "Bitter Winter", "wassertextur_winter.txt"),
    "z-whiteout": karte("z", "Whiteout", "wassertextur_whiteout.txt"),
    "z-oase":     karte("z", "Golden Oasis", "wassertextur_oase.txt"),
    "z-badlands": karte("z", "BarrenBadlands", "wassertextur_badlands.txt"),
    "g-nacht":    karte("g", "Silent River", "wasser_nacht.txt"),
    "z-shell":    ("z", ["--scene", "shellmap", "--frames", "300,900"]),
}
# "ohne": kein Schalter (Vorgabe aus Options.ini); mit einer Exe ohne den Schalter fuer den Nachweis "pixelgleich zur Basis".
VARIANTEN = {"ea": ["-waterTexture", "ea"], "frei": ["-waterTexture", "free"], "ohne": []}


def lauf(name, variante, stufe, a, ergebnisse):
    spiel, argumente = SZENEN[name]
    baum, exe = EXE[spiel]
    exe = os.path.join(a.build, baum, "Code", "Main", a.config, exe)
    marke = "%s_q%d" % (a.marke or variante, stufe)
    out = os.path.join(a.out, marke, name)
    cmd = [sys.executable, os.path.join(WURZEL, "tools", "render", "run_render_test.py"), "run", "--exe", exe,
           "--out", out, "--xres", str(a.xres), "--yres", str(a.yres), "--texture-quality", a.texture_quality,
           "--mehrfach", "--timeout", "1200"] + argumente
    # -ignoreAsserts: the debug build stops at the known load-screen assertion for observer games otherwise
    cmd += ["--extra", "-ignoreAsserts", "-waterQuality", str(stufe)] + VARIANTEN[variante]
    wait_free(log=lambda s: None, parallel=a.parallel)
    os.makedirs(os.path.join(a.out, marke), exist_ok=True)
    with open(os.path.join(a.out, marke, name + ".log"), "w", encoding="utf-8", errors="replace") as log:
        r = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, cwd=WURZEL)
    ergebnisse[(name, marke)] = r.returncode
    print(f"{marke}/{name}: Rueckgabe {r.returncode}", flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default=os.path.join(WURZEL, "build-x64"))
    ap.add_argument("--config", default="Release")
    ap.add_argument("--out", default=os.path.join(WURZEL, "render-out", "wassertextur", "raw"))
    ap.add_argument("--nur", help="kommagetrennte Szenen (Vorgabe: alle)")
    ap.add_argument("--stufen", default="2", help="Wasserstufen (-waterQuality), kommagetrennt")
    ap.add_argument("--varianten", default="ea,frei", help="ea, frei, ohne (Marke des Ordners mit --marke aendern)")
    ap.add_argument("--marke", help="Ordnername statt der Variante, etwa basis fuer die Exe des Ausgangsstands")
    ap.add_argument("--parallel", type=int, default=3)
    ap.add_argument("--xres", type=int, default=1920)
    ap.add_argument("--yres", type=int, default=1080)
    ap.add_argument("--texture-quality", default="hd")
    a = ap.parse_args()
    namen = a.nur.split(",") if a.nur else list(SZENEN)
    auftraege = [(n, v, int(s)) for s in a.stufen.split(",") for n in namen for v in a.varianten.split(",")]
    ergebnisse = {}
    faeden = []
    while auftraege or faeden:
        faeden = [t for t in faeden if t.is_alive()]
        while auftraege and len(faeden) < a.parallel:
            t = threading.Thread(target=lauf, args=(*auftraege.pop(0), a, ergebnisse))
            t.start()
            faeden.append(t)
            t.join(timeout=20)		# der naechste Start wartet ohnehin in der Schlange; kurz versetzt anmelden
        for t in faeden:
            t.join(timeout=5)
    fehler = {k: v for k, v in ergebnisse.items() if v != 0}
    print("fertig, %d Laeufe, Fehler: %s" % (len(ergebnisse), fehler or "keine"))
    return 1 if fehler else 0


if __name__ == "__main__":
    sys.exit(main())
