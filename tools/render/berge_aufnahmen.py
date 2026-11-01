# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Aufnahmen fuer die Felsebene: Kamerafahrten zu steilen Haengen und Laeufe ueber die
Warteschlange (tools/abnahme/slot.py).

Je Karte zwei Orte (aus tools/render/map_hang.py), je Ort drei Bilder: Standardansicht, nah (Zoom 0,35) und nah mit
flacher Kamera (Neigung +25). Alle 120 Logik-Frames ein Bild.

Aufruf:
  python tools/render/berge_aufnahmen.py pfade                      Pfaddateien tests/render/paths/berge_*.txt schreiben
  python tools/render/berge_aufnahmen.py lauf --fassung alt --zh <GZHNeoZH.exe> --g <GZHNeo.exe> [--effekte LISTE]
        [--nur alpine,fortress] [--extra ...]
Bilder: render-out/berge/raw/<szene>/<fassung>/shot_*.bmp
"""
import argparse
import os
import subprocess
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HIER))
sys.path.insert(0, os.path.join(REPO, "tools", "abnahme"))

# Szene: (Spiel, Karte, Orte [(x, y, Drehung)])
SZENEN = {
    "alpine":   ("zh", "Alpine Assault",     [(0.25, 0.50, 0), (0.78, 0.80, 180)]),
    "fortress": ("zh", "Fortress Avalanche", [(0.50, 0.60, 0), (0.58, 0.50, 90)]),
    "winter":   ("zh", "Winter Wolf",        [(0.46, 0.29, 0), (0.82, 0.22, 180)]),
    "lake":     ("zh", "Tournament Lake",    [(0.50, 0.62, 0), (0.36, 0.50, 90)]),
    "dust":     ("zh", "Dust Devil",         [(0.35, 0.55, 0), (0.70, 0.30, 180)]),
    "nacht":    ("zh", "Lights Out",         [(0.35, 0.52, 0), (0.68, 0.55, 180)]),
    "g_alpine": ("g",  "Alpine Assault",     [(0.25, 0.50, 0), (0.78, 0.80, 180)]),
}
POSEN = [("standard", "standard"), ("0.35", "standard"), ("0.60", "15")]
TAKT = 120
EFFEKTE = "fxaa,sharpen,softparticles,colorgrade,ssao,shadowmap=4096,terraindetail"


def pfad_datei(name):
    return os.path.join(REPO, "tests", "render", "paths", "berge_%s.txt" % name.replace("g_", ""))


def cmd_pfade(a):
    geschrieben = set()
    for name, (_, karte, orte) in SZENEN.items():
        datei = pfad_datei(name)
        if datei in geschrieben:
            continue
        geschrieben.add(datei)
        zeilen = ["# Berge: %s -- je Ort Standardansicht, nah, nah mit flacher Kamera." % karte,
                  "# Erzeugt von tools/render/berge_aufnahmen.py pfade; Orte aus tools/render/map_hang.py.",
                  "# frame   x      y      zoom       drehung   neigung"]
        k = 0
        for x, y, dreh in orte:
            for zoom, neig in POSEN:
                a0 = 0 if k == 0 else k * TAKT + 1
                for f in (a0, (k + 1) * TAKT):
                    zeilen.append("%-9d %-6.2f %-6.2f %-10s %-9d %s" % (f, x, y, zoom, dreh, neig))
                k += 1
        with open(datei, "w", newline="\r\n", encoding="ascii") as f:
            f.write("\n".join(zeilen) + "\n")
        print("geschrieben", os.path.relpath(datei, REPO))


def cmd_lauf(a):
    from slot import wait_free
    nur = a.nur.split(",") if a.nur else list(SZENEN)
    fehler = 0
    for name in nur:
        spiel, karte, _ = SZENEN[name]
        exe = a.zh if spiel == "zh" else a.g
        out = os.path.join(REPO, "render-out", "berge", "raw", name, a.fassung)
        wait_free(parallel=3)
        cmd = [sys.executable, os.path.join(HIER, "run_render_test.py"), "run", "--exe", exe, "--scene", "skirmish",
               "--map", karte, "--ai", "2", "--seed", "1", "--path", pfad_datei(name), "--every", str(TAKT),
               "--xres", "1920", "--yres", "1080", "--mehrfach", "--out", out,
               "--extra", "-ignoreAsserts", "-d3d11Effects", a.effekte] + (a.extra or [])
        r = subprocess.call(cmd)
        print("==", name, a.fassung, "Exit", r)
        fehler += r != 0
    return 1 if fehler else 0


LEISTE = 0.78		# Anteil der Bildhoehe ueber der Befehlsleiste (1080p)
TITEL = {"alpine": "Alpine Assault (ZH)", "fortress": "Fortress Avalanche (ZH)", "winter": "Winter Wolf (ZH, Schnee)",
         "lake": "Tournament Lake (ZH)", "dust": "Dust Devil (ZH, Wueste)", "nacht": "Lights Out (ZH, Nacht)",
         "g_alpine": "Alpine Assault (Generals)"}
POSE_NAMEN = ["Standard", "nah", "nah flach"]


def lum709(img):
    import numpy as np
    x = img.astype(np.float64)
    return 0.2126 * x[..., 0] + 0.7152 * x[..., 1] + 0.0722 * x[..., 2]


def cmd_vergleich(a):
    """Pixelvergleich zweier Fassungen je Szene (etwa alt gegen neu_aus: muss 0 sein)."""
    import numpy as np
    from PIL import Image
    gesamt = 0
    for name in SZENEN:
        d1 = os.path.join(REPO, "render-out", "berge", "raw", name, a.a)
        d2 = os.path.join(REPO, "render-out", "berge", "raw", name, a.b)
        if not (os.path.isdir(d1) and os.path.isdir(d2)):
            continue
        for f in sorted(x for x in os.listdir(d1) if x.startswith("shot_")):
            if not os.path.exists(os.path.join(d2, f)):
                print(name, f, "fehlt"); gesamt += 1; continue
            x = np.asarray(Image.open(os.path.join(d1, f)).convert("RGB")).astype(int)
            y = np.asarray(Image.open(os.path.join(d2, f)).convert("RGB")).astype(int)
            n = int((np.abs(x - y).max(axis=2) > 0).sum())
            gesamt += n
            print("%-9s %s  abweichende Pixel %d" % (name, f, n))
    print("GESAMT abweichende Pixel", gesamt)
    return 0 if gesamt == 0 else 1


def cmd_seite(a):
    import json
    import numpy as np
    from PIL import Image
    ziel = os.path.join(REPO, "render-out", "berge")
    daten = {"szenen": [], "kopf": a.kopf or "", "fuss": a.fuss or ""}
    zeilen = []
    for name in SZENEN:
        d_alt = os.path.join(ziel, "raw", name, a.alt)
        d_neu = os.path.join(ziel, "raw", name, a.neu)
        if not (os.path.isdir(d_alt) and os.path.isdir(d_neu)):
            continue
        os.makedirs(os.path.join(ziel, "bilder", name), exist_ok=True)
        shots = []
        for i, f in enumerate(sorted(x for x in os.listdir(d_alt) if x.startswith("shot_") and x.endswith(".bmp"))):
            if not os.path.exists(os.path.join(d_neu, f)):
                continue
            stem = f[:-4]
            hell = {}
            bilder = {}
            for key, d in (("alt", d_alt), ("neu", d_neu)):
                img = np.asarray(Image.open(os.path.join(d, f)).convert("RGB"))
                bilder[key] = img
                hell[key] = round(float(lum709(img)[:int(img.shape[0] * LEISTE)].mean()), 3)
                Image.fromarray(img).save(os.path.join(ziel, "bilder", name, "%s_%s.jpg" % (stem, key)), quality=94)
            diff = np.abs(bilder["alt"].astype(int) - bilder["neu"].astype(int)).max(axis=2)[:int(1080 * LEISTE)]
            anteil = float((diff > 8).mean() * 100)
            abw = (hell["neu"] / hell["alt"] - 1) * 100 if hell["alt"] else 0.0
            shots.append({"shot": stem, "pose": POSE_NAMEN[i % 3], "hell": hell, "abw": abw, "anteil": anteil})
            zeilen.append((name, stem, POSE_NAMEN[i % 3], hell["alt"], hell["neu"], abw, anteil))
        daten["szenen"].append({"name": name, "titel": TITEL[name], "beschreibung": TITEL[name], "shots": shots})
    tpl = open(os.path.join(HIER, "berge_seite.html"), encoding="utf-8").read()
    open(os.path.join(ziel, "index.html"), "w", encoding="utf-8").write(
        tpl.replace("/*DATEN*/null", json.dumps(daten, ensure_ascii=False)))
    with open(os.path.join(ziel, "helligkeit.csv"), "w", encoding="utf-8") as f:
        f.write("szene;bild;pose;alt;neu;abweichung_prozent;pixel_ueber_8_prozent\n")
        for z in zeilen:
            f.write("%s;%s;%s;%.3f;%.3f;%.3f;%.2f\n" % z)
    einzeln = os.path.join(ziel, "einzelbilder")
    os.makedirs(einzeln, exist_ok=True)
    for eintrag in (a.einzeln or "").split(","):
        if not eintrag:
            continue
        name, nr = eintrag.split(":")
        for key, d in (("alt", a.alt), ("neu", a.neu)):
            pfad = os.path.join(ziel, "raw", name, d, "shot_%06d.bmp" % int(nr))
            if os.path.exists(pfad):
                Image.open(pfad).convert("RGB").save(os.path.join(einzeln, "%s_%06d_%s.png" % (name, int(nr), key)))
    werte = [z[5] for z in zeilen]
    print("Seite", os.path.join(ziel, "index.html"), len(zeilen), "Paare; Helligkeit neu/alt min %.2f %% max %.2f %% Mittel %.2f %%"
          % (min(werte), max(werte), sum(werte) / len(werte)))
    for z in zeilen:
        print("  %-9s %s %-10s %7.2f %7.2f %+6.2f %%  Pixel>8 %5.1f %%" % z)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("pfade")
    l = sub.add_parser("lauf")
    l.add_argument("--fassung", required=True)
    l.add_argument("--zh", default=os.path.join(REPO, "build-x64", "GeneralsMD", "Code", "Main", "Release", "GZHNeoZH.exe"))
    l.add_argument("--g", default=os.path.join(REPO, "build-x64", "Generals", "Code", "Main", "Release", "GZHNeo.exe"))
    l.add_argument("--effekte", default=EFFEKTE)
    l.add_argument("--nur")
    l.add_argument("--extra", nargs=argparse.REMAINDER)
    v = sub.add_parser("vergleich")
    v.add_argument("a")
    v.add_argument("b")
    s = sub.add_parser("seite")
    s.add_argument("--alt", default="alt")
    s.add_argument("--neu", default="neu")
    s.add_argument("--kopf")
    s.add_argument("--fuss")
    s.add_argument("--einzeln", help="szene:bild,... als verlustfreie PNG nach render-out/berge/einzelbilder")
    a = ap.parse_args()
    return {"pfade": cmd_pfade, "lauf": cmd_lauf, "vergleich": cmd_vergleich, "seite": cmd_seite}[a.cmd](a) or 0


if __name__ == "__main__":
    sys.exit(main())
