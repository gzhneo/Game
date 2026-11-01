#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Schwarze Flaechen in Kamerafahrten den Laeufen und der Kamera zuordnen.

Eingabe: mehrere Laeufe derselben Fahrt, je als NAME=ORDNER (der Fahrt-Ordner von alle_fahrten.py mit Unterordner a,
oder direkt ein Ordner mit shot_*.bmp und run.log). Die Laeufe unterscheiden sich in den Effekten, etwa ohne
Tessellation, mit terraintess=2, mit terraintess=2,rocktess=2, jeweils mit und ohne shadowmap; alle mit -logCamera.

Je Bild (Logik-Frame aus dem Dateinamen): Befund der Bildpruefung (inspect_shots.py, Pruefungen schwarz und leer) je
Lauf, dazu die Kamerawerte des Frames aus dem Runner-Protokoll (CAMERA-Zeilen: clearance = Auge ueber dem Gelaende,
nearplane = kleinste Hoehe der Nahebene ueber dem Gelaende). Am Ende eine Einordnung je Bild mit Befund:

  kamera-im-gelaende   nearplane < 0: auch ohne Tessellation schwarz zu erwarten (Kameraführung, R-470)
  nahebene-tess        nur mit Tessellation schwarz, nearplane zwischen 0 und --verschiebung (die tessierte Flaeche
                       schneidet die Nahebene)
  kachelrand           nur in Laeufen ohne shadowmap (Name enthaelt "ohne-sm" oder "nosm"), Kachelausschluss ohne
                       Rand (R-473)
  nur-tess             nur mit Tessellation, Kamera frei: weder Nahebene noch Kachelrand -- neu untersuchen
  immer                in allen Laeufen, Kamera frei: nicht die Tessellation (etwa Horizont, Gelaendefenster)
  uneinheitlich        sonst

Ob ein Lauf Tessellation hat, steht im Namen ("tess" oder "rock"; "ohne-tess" heisst ohne).

  python tools/render/tess_zuordnung.py ohne=render-out/t0/tess_steil tess=render-out/t2/tess_steil \\
      rock=render-out/t3/tess_steil ohne-sm=render-out/t0n/tess_steil tess-ohne-sm=render-out/t2n/tess_steil

Rueckgabewert 0 = kein Bild mit Befund, 1 = Befunde (Tabelle), 2 = Eingabefehler.
"""
import argparse
import json
import os
import re
import subprocess
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
SHOT = re.compile(r"shot_(\d+)\.bmp$", re.I)
CAMERA = re.compile(r"CAMERA (\d+) eye \S+ \S+ \S+ ground \S+ clearance (\S+) nearplane (\S+) near \S+ zoom (\S+) pitch (\S+)")


def ordner_des_laufs(pfad):
    a = os.path.join(pfad, "a")
    return a if os.path.isdir(a) else pfad


def kamera(ordner):
    """Logik-Frame -> (clearance, nearplane, zoom, pitch); je Frame der kleinste nearplane-Wert der gezeichneten Bilder."""
    werte = {}
    log = os.path.join(ordner, "run.log")
    if not os.path.isfile(log):
        return werte
    with open(log, encoding="latin-1", errors="replace") as f:
        for z in f:
            m = CAMERA.search(z)
            if not m:
                continue
            frame = int(m.group(1))
            w = tuple(float(m.group(i)) for i in range(2, 6))
            if frame not in werte or w[1] < werte[frame][1]:
                werte[frame] = w
    return werte


def befunde(ordner):
    """Logik-Frame -> Liste der Befunde (Pruefung, Ausschnitt)."""
    p = subprocess.run([sys.executable, os.path.join(HIER, "inspect_shots.py"), ordner, "--json", "--only", "schwarz,leer"],
                       capture_output=True, text=True, errors="replace")
    if p.returncode not in (0, 1):
        raise SystemExit("inspect_shots.py scheitert an %s: %s" % (ordner, p.stderr.strip()[-300:]))
    daten = json.loads(p.stdout)
    out = {}
    for b in daten["bilder"]:
        m = SHOT.search(b["bild"].replace("\\", "/"))
        if not m:
            continue
        out[int(m.group(1))] = [(f["pruefung"], f.get("ausschnitt")) for f in b.get("befunde", [])]
    return out


def kamera_zum_bild(werte, frame):
    """Kamerawerte des Bildes: das Bild entsteht im Frame selbst, sonst der naechste fruehere Frame."""
    if frame in werte:
        return werte[frame]
    frueher = [f for f in werte if f <= frame]
    return werte[max(frueher)] if frueher else None


def hat_tess(name):
    n = name.lower()
    return ("tess" in n or "rock" in n) and "ohne-tess" not in n


def ohne_shadowmap(name):
    n = name.lower()
    return "ohne-sm" in n or "nosm" in n


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("laeufe", nargs="+", help="NAME=ORDNER je Lauf")
    ap.add_argument("--verschiebung", type=float, default=4.0,
                    help="groesste Verschiebung der tessierten Flaeche in Welteinheiten (Stärke 2 mit Fels rund 3,4; Vorgabe 4)")
    a = ap.parse_args()
    laeufe = []
    for arg in a.laeufe:
        if "=" not in arg:
            print("Lauf ohne NAME=: %s" % arg)
            return 2
        name, pfad = arg.split("=", 1)
        ordner = ordner_des_laufs(pfad)
        if not os.path.isdir(ordner):
            print("Ordner fehlt: %s" % ordner)
            return 2
        laeufe.append((name, ordner, befunde(ordner), kamera(ordner)))

    frames = sorted(set().union(*(set(b) for _, _, b, _ in laeufe)))
    print("Bild    " + " ".join("%-14s" % n[:14] for n, _, _, _ in laeufe) + "  clearance nearplane  zoom  pitch  Einordnung")
    mit_befund = 0
    zaehler = {}
    for frame in frames:
        schwarz = {n: bool(b.get(frame)) for n, _, b, _ in laeufe}
        if not any(schwarz.values()):
            continue
        mit_befund += 1
        # camera values from the first run that logged them (the path is the same in every run)
        cam = next((kamera_zum_bild(k, frame) for _, _, _, k in laeufe if kamera_zum_bild(k, frame)), None)
        zellen = []
        for n, _, b, _ in laeufe:
            f = b.get(frame)
            zellen.append("%-14s" % (",".join(sorted({p for p, _ in f}))[:14] if f else "-"))
        tess_l = [n for n in schwarz if hat_tess(n)]
        ohne_l = [n for n in schwarz if not hat_tess(n)]
        if cam and cam[1] < 0:
            art = "kamera-im-gelaende"
        elif all(schwarz.values()):
            art = "immer"
        elif any(schwarz[n] for n in tess_l) and not any(schwarz[n] for n in ohne_l):
            nur_ohne_sm = all(ohne_shadowmap(n) for n in tess_l if schwarz[n]) and any(not ohne_shadowmap(n) for n in tess_l)
            if nur_ohne_sm:
                art = "kachelrand"
            elif cam and cam[1] < a.verschiebung:
                art = "nahebene-tess"
            else:
                art = "nur-tess"
        else:
            art = "uneinheitlich"
        zaehler[art] = zaehler.get(art, 0) + 1
        kam = "%9.2f %9.2f %5.3f %6.1f" % cam if cam else "    (keine CAMERA-Zeile)       "
        print("%06d  %s  %s  %s" % (frame, " ".join(zellen), kam, art))
    if not mit_befund:
        print("kein Bild mit schwarzer Flaeche oder leerem Bild in %d Bildern" % len(frames))
        return 0
    print("\n%d Bilder mit Befund: %s" % (mit_befund, ", ".join("%s %d" % kv for kv in sorted(zaehler.items()))))
    ohne_cam = [n for n, _, _, k in laeufe if not k]
    if ohne_cam:
        print("ohne CAMERA-Zeilen (Schalter -logCamera fehlte?): " + ", ".join(ohne_cam))
    return 1


if __name__ == "__main__":
    sys.exit(main())
