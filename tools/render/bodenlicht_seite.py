# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Vergleichsseite fuer das Licht je Pixel auf dem Boden (TODO 47 R-6): Kamerafahrten heute
(Licht je Ecke) gegen neu (Schalter terrainlight), Tag und Nacht, Generals und Zero Hour (nur Windows mit Spieldaten).

Aufruf:
  python tools/render/bodenlicht_seite.py lauf --fassung heute               Fahrten ohne terrainlight
  python tools/render/bodenlicht_seite.py lauf --fassung neu                 Fahrten mit terrainlight
        [--effekte LISTE] [--nur quer,rand] [--schalter-text "..."]         (weitere Schalter fuer beide Fassungen gleich)
  python tools/render/bodenlicht_seite.py vergleich heute main               pixelgleich? (Schalter aus gegen main)
  python tools/render/bodenlicht_seite.py seite [--alt heute] [--neu neu]    render-out/bodenlicht/index.html

Die Fahrten laufen ueber tools/render/alle_fahrten.py (je Fahrt zweimal, Warteschlange tools/abnahme/slot.py), Bilder unter
render-out/bodenlicht/<fassung>/<fahrt>/a/shot_*.bmp. Die Seite zeigt je Bild heute und neu (Leertaste wechselt, Lupe,
nebeneinander) und je Bild und Fahrt die mittlere Helligkeit (Luminanz Rec. 709) und den mittleren Farbton (a*, b* in
CIELAB) ohne die Befehlsleiste; dazu helligkeit.csv.
"""
import argparse
import json
import os
import subprocess
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HIER))
ZIEL = os.path.join(REPO, "render-out", "bodenlicht")

# Fahrten aus alle_fahrten.py: Tag (Generals Alpine Assault, Zero Hour Green Pastures), Nacht (Tundra in beiden Spielen,
# Lights Out), Berge, Gefecht mit Explosionen (dynamische Lichter).
FAHRTEN = [
    ("quer", "quer: Alpine Assault (Generals, Tag)"),
    ("rand", "rand: Alpine Assault (Generals, Tag)"),
    ("rundum", "rundum: Alpine Assault (Generals, Tag)"),
    ("rundum-zh", "rundum: Green Pastures (Zero Hour, Tag)"),
    ("berge_alpine", "Berge: Alpine Assault (Zero Hour, Tag)"),
    ("berge_nacht", "Berge: Lights Out (Zero Hour, Nacht)"),
    ("tundra_nacht-g", "Gefecht: Tournament Tundra (Generals, Nacht, Schnee)"),
    ("tundra_nacht-z", "Gefecht: Tournament Tundra (Zero Hour, Nacht, Schnee)"),
    ("see_kampf", "Gefecht: Tournament Lake (Zero Hour, Explosionen)"),
]
TITEL = dict(FAHRTEN)
# Effekte der Stufe Hoch ohne die Bild fuer Bild schwankenden Nachbearbeitungen; beide Fassungen gleich.
EFFEKTE = "fxaa,sharpen,softparticles,colorgrade,ssao,shadowmap=2048,terraindetail"
LEISTE = 0.78		# Anteil der Bildhoehe ueber der Befehlsleiste


def cmd_lauf(a):
    effekte = a.effekte + (",terrainlight" if a.fassung == "neu" else "")
    schalter = ("%s -effects %s" % (a.schalter_text, effekte)).strip()
    nur = a.nur or ",".join(n for n, _ in FAHRTEN)
    cmd = [sys.executable, os.path.join(HIER, "alle_fahrten.py"), "--nur", nur, "--ohne-bildpruefung",
           "--out", os.path.join(ZIEL, a.fassung), "--schalter-text", schalter]
    print(">>", " ".join(cmd), flush=True)
    return subprocess.call(cmd, cwd=REPO)


def bilder(fassung, fahrt):
    d = os.path.join(ZIEL, fassung, fahrt, "a")
    if not os.path.isdir(d):
        return d, []
    return d, sorted(x for x in os.listdir(d) if x.startswith("shot_") and x.endswith(".bmp"))


def cmd_vergleich(a):
    """Pixelvergleich zweier Fassungen je Fahrt (Schalter aus gegen main: muss 0 sein)."""
    import numpy as np
    from PIL import Image
    gesamt = 0
    paare = 0
    for fahrt, _ in FAHRTEN:
        d1, f1 = bilder(a.a, fahrt)
        d2, _ = bilder(a.b, fahrt)
        for f in f1:
            p2 = os.path.join(d2, f)
            if not os.path.exists(p2):
                print(fahrt, f, "fehlt")
                gesamt += 1
                continue
            x = np.asarray(Image.open(os.path.join(d1, f)).convert("RGB")).astype(int)
            y = np.asarray(Image.open(p2).convert("RGB")).astype(int)
            n = int((np.abs(x - y).max(axis=2) > 0).sum())
            gesamt += n
            paare += 1
            if n:
                print("%-15s %s  abweichende Pixel %d" % (fahrt, f, n))
    print("%d Bildpaare, abweichende Pixel gesamt %d" % (paare, gesamt))
    return 0 if gesamt == 0 and paare else 1


def lab(img):
    """Mittelwerte L*, a*, b* (D65) des Bildbereichs ohne Befehlsleiste."""
    import numpy as np
    x = img[:int(img.shape[0] * LEISTE)].astype(np.float64) / 255.0
    x = np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4)
    m = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = x.reshape(-1, 3) @ m.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 216 / 24389, np.cbrt(xyz), (24389 / 27 * xyz + 16) / 116)
    L = 116 * f[:, 1] - 16
    A = 500 * (f[:, 0] - f[:, 1])
    B = 200 * (f[:, 1] - f[:, 2])
    return float(L.mean()), float(A.mean()), float(B.mean())


def lum709(img):
    import numpy as np
    x = img[:int(img.shape[0] * LEISTE)].astype(np.float64)
    return float((0.2126 * x[..., 0] + 0.7152 * x[..., 1] + 0.0722 * x[..., 2]).mean())


def cmd_seite(a):
    import numpy as np
    from PIL import Image
    daten = {"szenen": [], "kopf": a.kopf or "", "fuss": a.fuss or "", "alt": a.alt, "neu": a.neu}
    zeilen = []
    for fahrt, titel in FAHRTEN:
        d_alt, liste = bilder(a.alt, fahrt)
        d_neu, _ = bilder(a.neu, fahrt)
        shots = []
        for f in liste:
            if not os.path.exists(os.path.join(d_neu, f)):
                continue
            os.makedirs(os.path.join(ZIEL, "bilder", fahrt), exist_ok=True)
            stem = f[:-4]
            img = {}
            werte = {}
            for key, d in (("alt", d_alt), ("neu", d_neu)):
                img[key] = np.asarray(Image.open(os.path.join(d, f)).convert("RGB"))
                L, A, B = lab(img[key])
                werte[key] = {"hell": round(lum709(img[key]), 3), "L": round(L, 3), "a": round(A, 3), "b": round(B, 3)}
                Image.fromarray(img[key]).save(os.path.join(ZIEL, "bilder", fahrt, "%s_%s.jpg" % (stem, key)), quality=94)
            h = img["alt"].shape[0]
            diff = np.abs(img["alt"].astype(int) - img["neu"].astype(int)).max(axis=2)[:int(h * LEISTE)]
            anteil = float((diff > 8).mean() * 100)
            abw = (werte["neu"]["hell"] / werte["alt"]["hell"] - 1) * 100 if werte["alt"]["hell"] else 0.0
            # mean colour shift: distance of the mean a*, b* (CIELAB units; about 1 is the threshold of visibility)
            farbe = float(np.hypot(werte["neu"]["a"] - werte["alt"]["a"], werte["neu"]["b"] - werte["alt"]["b"]))
            shots.append({"shot": stem, "werte": werte, "abw": abw, "farbe": farbe, "anteil": anteil})
            zeilen.append((fahrt, stem, werte["alt"]["hell"], werte["neu"]["hell"], abw, farbe, anteil))
        if shots:
            daten["szenen"].append({"name": fahrt, "titel": titel, "shots": shots,
                                    "abw": sum(s["abw"] for s in shots) / len(shots),
                                    "farbe": sum(s["farbe"] for s in shots) / len(shots),
                                    "anteil": sum(s["anteil"] for s in shots) / len(shots)})
    if not zeilen:
        print("keine Bildpaare unter", ZIEL)
        return 1
    tpl = open(os.path.join(HIER, "bodenlicht_seite.html"), encoding="utf-8").read()
    with open(os.path.join(ZIEL, "index.html"), "w", encoding="utf-8") as f:
        f.write(tpl.replace("/*DATEN*/null", json.dumps(daten, ensure_ascii=False)))
    with open(os.path.join(ZIEL, "helligkeit.csv"), "w", encoding="utf-8") as f:
        f.write("fahrt;bild;%s;%s;helligkeit_prozent;farbton_ab;pixel_ueber_8_prozent\n" % (a.alt, a.neu))
        for z in zeilen:
            f.write("%s;%s;%.3f;%.3f;%.3f;%.3f;%.2f\n" % z)
    print("Seite", os.path.join(ZIEL, "index.html"), len(zeilen), "Bildpaare")
    for s in daten["szenen"]:
        print("  %-15s Helligkeit %+6.2f %%  Farbton %.2f  Pixel>8 %5.1f %%" % (s["name"], s["abw"], s["farbe"], s["anteil"]))
    werte = [z[4] for z in zeilen]
    print("Helligkeit neu/heute je Bild: min %+.2f %%, max %+.2f %%, Mittel %+.2f %%; Farbton (a*b*) Mittel %.2f, max %.2f"
          % (min(werte), max(werte), sum(werte) / len(werte), sum(z[5] for z in zeilen) / len(zeilen), max(z[5] for z in zeilen)))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    l = sub.add_parser("lauf")
    l.add_argument("--fassung", required=True, help="heute (ohne terrainlight), neu (mit) oder ein anderer Name (ohne)")
    l.add_argument("--effekte", default=EFFEKTE)
    l.add_argument("--nur", help="Fahrten, kommagetrennt (Standard: alle der Seite)")
    l.add_argument("--schalter-text", default="", help="weitere Spielschalter fuer die Fassung")
    v = sub.add_parser("vergleich")
    v.add_argument("a")
    v.add_argument("b")
    s = sub.add_parser("seite")
    s.add_argument("--alt", default="heute")
    s.add_argument("--neu", default="neu")
    s.add_argument("--kopf")
    s.add_argument("--fuss")
    a = ap.parse_args()
    return {"lauf": cmd_lauf, "vergleich": cmd_vergleich, "seite": cmd_seite}[a.cmd](a) or 0


if __name__ == "__main__":
    sys.exit(main())
