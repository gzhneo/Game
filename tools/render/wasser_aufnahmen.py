#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Bildaufnahmen fuer das Wasser: dieselben Szenen fuer einen Stand
aufnehmen, damit sich alt und neu Bild fuer Bild vergleichen lassen.

  python tools/render/wasser_aufnahmen.py --label neu [--build build-x64] [--config Debug] [--nur g-see,z-see]
      [--extra -waterQuality 2 ...] [--parallel 3]

Jeder Lauf wartet auf die Spielstart-Warteschlange (tools/abnahme/slot.py, parallel wie --parallel) und startet
run_render_test.py mit --mehrfach. Ergebnis: <out>/<label>/<szene>/shot_*.bmp samt run.log.
Szenen: Tournament Lake (Ufer und offene Flaeche, beide Spiele), Silent River bei Nacht (beide Spiele),
Heartland Shield (Fluss, Generals), Shell-Maps beider Spiele und eine feste Pose am Kratersee fuer
Bewegungsbilder (alle 2 Logik-Frames ein Bild).
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

# name: (spiel, szene, argumente)
SZENEN = {
    "g-see":      ("g", ["--scene", "skirmish", "--map", "Tournament Lake", "--ai", "1", "--path", os.path.join(PFADE, "wasser_see.txt"), "--every", "60"]),
    "z-see":      ("z", ["--scene", "skirmish", "--map", "Tournament Lake", "--ai", "1", "--path", os.path.join(PFADE, "wasser_see.txt"), "--every", "60"]),
    "g-nacht":    ("g", ["--scene", "skirmish", "--map", "Silent River", "--ai", "1", "--path", os.path.join(PFADE, "wasser_nacht.txt"), "--every", "60"]),
    "z-nacht":    ("z", ["--scene", "skirmish", "--map", "Silent River", "--ai", "1", "--path", os.path.join(PFADE, "wasser_nacht.txt"), "--every", "60"]),
    "g-fluss":    ("g", ["--scene", "skirmish", "--map", "Heartland Shield", "--ai", "1", "--path", os.path.join(PFADE, "wasser_fluss.txt"), "--every", "60"]),
    "g-shell":    ("g", ["--scene", "shellmap", "--frames", "300,900"]),
    "z-shell":    ("z", ["--scene", "shellmap", "--frames", "300,900"]),
    "g-bewegung": ("g", ["--scene", "skirmish", "--map", "Tournament Lake", "--ai", "1", "--path", os.path.join(PFADE, "wasser_bewegung.txt"), "--every", "2"]),
}


def lauf(name, a, ergebnisse):
    spiel, argumente = SZENEN[name]
    baum, exe = EXE[spiel]
    exe = os.path.join(a.build, baum, "Code", "Main", a.config, exe)
    out = os.path.join(a.out, a.label, name)
    cmd = [sys.executable, os.path.join(WURZEL, "tools", "render", "run_render_test.py"), "run", "--exe", exe,
           "--out", out, "--xres", str(a.xres), "--yres", str(a.yres), "--mehrfach", "--timeout", "1200"] + argumente
    cmd += ["--extra", "-ignoreAsserts"] + list(a.extra or [])
    wait_free(log=lambda s: None, parallel=a.parallel)
    with open(os.path.join(a.out, a.label, name + ".log"), "w", encoding="utf-8", errors="replace") as log:
        r = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, cwd=WURZEL)
    ergebnisse[name] = r.returncode
    print(f"{name}: Rueckgabe {r.returncode}", flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--label", required=True)
    ap.add_argument("--build", default=os.path.join(WURZEL, "build-x64"))
    ap.add_argument("--config", default="Debug")
    ap.add_argument("--out", default=os.path.join(WURZEL, "render-out", "wasser2", "raw"))
    ap.add_argument("--nur", help="kommagetrennte Szenen (Vorgabe: alle)")
    ap.add_argument("--parallel", type=int, default=3)
    ap.add_argument("--xres", type=int, default=1920)
    ap.add_argument("--yres", type=int, default=1080)
    ap.add_argument("--extra", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    namen = a.nur.split(",") if a.nur else list(SZENEN)
    os.makedirs(os.path.join(a.out, a.label), exist_ok=True)
    ergebnisse = {}
    offen = list(namen)
    faeden = []
    while offen or faeden:
        faeden = [t for t in faeden if t.is_alive()]
        while offen and len(faeden) < a.parallel:
            t = threading.Thread(target=lauf, args=(offen.pop(0), a, ergebnisse))
            t.start()
            faeden.append(t)
            # der naechste Start wartet ohnehin in der Schlange (Startsperre); kurz versetzt anmelden
            t.join(timeout=20)
        for t in faeden:
            t.join(timeout=5)
        faeden = [t for t in faeden if t.is_alive()]
    fehler = {n: r for n, r in ergebnisse.items() if r != 0}
    print("fertig:", ", ".join(f"{n}={r}" for n, r in sorted(ergebnisse.items())))
    return 1 if fehler else 0


if __name__ == "__main__":
    sys.exit(main())
