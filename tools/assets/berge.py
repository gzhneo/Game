# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Berge und Klippen: Felstexturen fuer die triplanare Felsebene im D3D11-Gelaendeshader.

Aus fotografischen CC0-Scans (Poly Haven) entstehen zwei Texturen, je 32 Bit RGBA mit voller Mip-Kette:

  Data/Textures/Berge/fels_grob.dds   grosse Formen (Bloecke, Kluefte), eine Kachel = PERIODE_GROB Welteinheiten
  Data/Textures/Berge/fels_fein.dds   Feinstruktur (Koernung, Risse),   eine Kachel = PERIODE_FEIN Welteinheiten

Kanaele: R = Helligkeitsfaktor (Byte b = Faktor b/128, Mittel jeder Mip-Stufe genau 128 -- wie die Boden-
Detailebene), G/B = Normale x/y (OpenGL-Konvention, 128 = 0; +y = Bild oben),
A = Hoehe (Verschiebungskarte, 0..255) fuer den Uebergang Fels/Boden.

Die Farbe des Scans wird nicht benutzt: der Shader nimmt die weichgezeichnete Farbe der EA-Gelaendetextur und
multipliziert sie mit R -- Farbe und mittlere Helligkeit der Karte bleiben, die gestreckte Struktur der EA-Klippe
wird durch die Scan-Struktur ersetzt.

Aufruf:
  python tools/assets/berge.py laden      Scans nach <GZH_WERKZEUGE>\\cc0\\download (2K-PNG)
  python tools/assets/berge.py bauen      DDS schreiben, Vorschau nach render-out/berge/arbeit
  python tools/assets/berge.py nachweis   Tabelle Herkunft/Lizenz fuer ASSET_CREDITS.md
"""
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image

HIER = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HIER))
sys.path.insert(0, HIER)
import boden_detail as bd  # noqa: E402  (Laden, Mip-Kette, Quantisierung, DDS)

ZIEL_TEX = os.path.join(REPO, "Data", "Textures", "Berge")

# Name, Quelle, Id, Kantenlaenge, Parameter:
#   streu   Streuung des Helligkeitsfaktors, gemessen in Mip-Stufe 1
#   ao      Exponent der Umgebungsverdeckung in der Helligkeit
#   grenze  Hochpass: Strukturen groesser als Kachel/grenze fallen weg (die Farbe kommt von EA)
#   normal  Faktor auf die Neigung der Normalen (1 = wie gescannt)
TEXTUREN = [
    ("fels_grob", "polyhaven", "marble_cliff_03", 1024, {"streu": 0.26, "ao": 0.5, "grenze": 2.0, "normal": 1.0}),
    ("fels_fein", "polyhaven", "rock_face_03",    512,  {"streu": 0.12, "ao": 0.5, "grenze": 3.0, "normal": 1.0}),
]


def box_auf(x, n):
    """Auf n x n verkleinern (Flaechenmittel)."""
    if x.shape[0] == n:
        return x
    if x.shape[0] % n == 0:
        return bd.box(x, x.shape[0] // n)
    im = Image.fromarray(x.astype(np.float32), mode="F")
    return np.asarray(im.resize((n, n), Image.BOX), dtype=np.float64)


def helligkeit(d, n, p):
    farbe = bd.lies(os.path.join(d, "farbe.png"))
    lum = 0.299 * farbe[..., 0] + 0.587 * farbe[..., 1] + 0.114 * farbe[..., 2]
    roh = np.maximum(lum, 1e-3)
    if p["ao"] > 0 and os.path.exists(os.path.join(d, "ao.png")):
        ao = bd.lies(os.path.join(d, "ao.png"), grau=True)
        roh = roh * np.power(np.clip(ao, 0.05, 1.0), p["ao"])
    klein = box_auf(roh, n)
    lg = np.log(klein)
    hp = lg - bd.gauss_periodisch(lg, n / (p["grenze"] * 2.0))
    fak = np.exp(hp)
    fak = fak / fak.mean()
    gemessen = bd.box(fak, 2)
    fak = 1.0 + (fak - 1.0) * (p["streu"] / max(gemessen.std(), 1e-6))
    fak = np.clip(fak, 0.05, 1.95)
    return fak / fak.mean()


def normale(d, n, p):
    nm = bd.lies(os.path.join(d, "normal.png")) * 2.0 - 1.0
    xy = np.stack([box_auf(nm[..., 0], n), box_auf(nm[..., 1], n)], axis=-1) * p["normal"]
    z = np.sqrt(np.clip(1.0 - (xy ** 2).sum(axis=-1), 0.0, 1.0))
    laenge = np.sqrt((xy ** 2).sum(axis=-1) + z ** 2)
    return xy / laenge[..., None]


def hoehe(d, n):
    h = bd.lies(os.path.join(d, "hoehe.png"), grau=True)
    if h.ndim == 3:
        h = h[..., 0]
    h = box_auf(h, n)
    lo, hi = np.percentile(h, 1), np.percentile(h, 99)
    return np.clip((h - lo) / max(hi - lo, 1e-6), 0.0, 1.0)


def cmd_laden(a):
    for _, quelle, aid, _, _ in TEXTUREN:
        ziel = os.path.join(a.download, quelle, aid)
        os.makedirs(ziel, exist_ok=True)
        meta = bd.lade_polyhaven(aid, ziel)
        with open(os.path.join(ziel, "meta.json"), "w", encoding="utf-8") as f:
            json.dump(meta, f, indent=1, ensure_ascii=False)
        print("%-10s %-18s %s  %s" % (quelle, aid, meta["masse_mm"], ", ".join(meta["autoren"])))


def cmd_bauen(a):
    os.makedirs(ZIEL_TEX, exist_ok=True)
    os.makedirs(a.arbeit, exist_ok=True)
    for name, quelle, aid, n, p in TEXTUREN:
        d = os.path.join(a.download, quelle, aid)
        fak = helligkeit(d, n, p)
        nxy = normale(d, n, p)
        h = hoehe(d, n)
        stufen = []
        f_mips = bd.mipkette(fak)
        x, y, hh = nxy[..., 0], nxy[..., 1], h
        for lvl, f in enumerate(f_mips):
            r = bd.quantisiere(f)
            g = np.clip(np.rint(x * 127.5 + 127.5), 0, 255).astype(np.uint8)
            b = np.clip(np.rint(y * 127.5 + 127.5), 0, 255).astype(np.uint8)
            al = np.clip(np.rint(hh * 255.0), 0, 255).astype(np.uint8)
            stufen.append(np.stack([r, g, b, al], axis=-1))
            if f.shape[0] > 1:
                x, y, hh = bd.box(x, 2), bd.box(y, 2), bd.box(hh, 2)
        bd.schreibe_dds(os.path.join(ZIEL_TEX, name + ".dds"), stufen)
        vor = np.clip(fak * 128.0, 0, 255).astype(np.uint8)
        Image.fromarray(np.tile(vor, (2, 2))).save(os.path.join(a.arbeit, name + "_hell.png"))
        nv = np.stack([stufen[0][..., 1], stufen[0][..., 2], np.full_like(stufen[0][..., 1], 255)], axis=-1)
        Image.fromarray(nv).save(os.path.join(a.arbeit, name + "_normal.png"))
        print("%-10s %-16s %4d  Streuung %.3f  Min %.2f Max %.2f  Normale |xy| %.3f  Hoehe %.2f" % (
            name, aid, n, fak.std(), fak.min(), fak.max(), np.sqrt((nxy ** 2).sum(axis=-1)).mean(), h.mean()))


def cmd_nachweis(a):
    for name, quelle, aid, n, p in TEXTUREN:
        d = os.path.join(a.download, quelle, aid)
        with open(os.path.join(d, "meta.json"), encoding="utf-8") as f:
            m = json.load(f)
        masse = "x".join("%.1f m" % (v / 1000.0) for v in (m.get("masse_mm") or [])[:1])
        print("| `%s` | %s `%s` | %s | %s | %s | %s |" % (name, m["quelle"], aid, m["name"], ", ".join(m["autoren"]),
              masse, m["seite"]))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--download", default=bd.DOWNLOAD, required=bd.DOWNLOAD is None)
    ap.add_argument("--arbeit", default=os.path.join(REPO, "render-out", "berge", "arbeit"))
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("laden")
    sub.add_parser("bauen")
    sub.add_parser("nachweis")
    a = ap.parse_args()
    {"laden": cmd_laden, "bauen": cmd_bauen, "nachweis": cmd_nachweis}[a.cmd](a)


if __name__ == "__main__":
    main()
