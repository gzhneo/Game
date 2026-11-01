#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Wo unterscheiden sich zwei Laeufe derselben Fahrt?

Fuer jedes Bildpaar gleichen Namens, das nicht pixelgleich ist, die Stellen mit Abweichung: das Bild wird in Zellen von
--zelle Pixeln geteilt, zusammenhaengende Zellen mit mindestens einem abweichenden Pixel bilden eine Stelle. Je Stelle:
Rechteck (Pixel und Prozent des Bildes), Zahl der abweichenden Pixel, groesste Kanalabweichung, mittlere Farbe in Lauf a
dort (hilft beim Erkennen: Himmel, Wasser, Gelaende, Baumkrone, Leiste) und ob die Abweichung nur am Rand von Flaechen
liegt (Kanten, etwa Antialiasing) oder flaechig ist. Mit --ausschnitte ORDNER zusaetzlich je Stelle ein Bild mit a, b und
der vergroesserten Differenz nebeneinander (klein genug zum Verschicken).

  python tools/render/diff_orte.py render-out/r170_alle/zoom             (Ordner mit a und b)
  python tools/render/diff_orte.py lauf1/a lauf2/a --ausschnitte C:/tmp/r170_orte

Rueckgabewert 0 = alle Paare pixelgleich, 1 = Abweichungen, 2 = Eingabefehler.
"""
import argparse
import os
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HIER)
from compare_shots import read_bmp, write_bmp, Image  # noqa: E402


def paare(a, b):
    namen = sorted(n for n in os.listdir(a) if n.lower().endswith(".bmp") and os.path.isfile(os.path.join(b, n)))
    return [(n, os.path.join(a, n), os.path.join(b, n)) for n in namen]


def stellen(ia, ib, zelle):
    w, h = ia.width, ia.height
    pa, pb = ia.bgr, ib.bgr
    cw, ch = (w + zelle - 1) // zelle, (h + zelle - 1) // zelle
    anzahl = [0] * (cw * ch)
    groesste = [0] * (cw * ch)
    summe = [[0, 0, 0] for _ in range(cw * ch)]
    kante = [0] * (cw * ch)
    for y in range(h):
        zeile = y * w * 3
        cy = (y // zelle) * cw
        for x in range(w):
            i = zeile + x * 3
            d = max(abs(pa[i] - pb[i]), abs(pa[i + 1] - pb[i + 1]), abs(pa[i + 2] - pb[i + 2]))
            if not d:
                continue
            c = cy + x // zelle
            anzahl[c] += 1
            groesste[c] = max(groesste[c], d)
            s = summe[c]
            s[0] += pa[i + 2]
            s[1] += pa[i + 1]
            s[2] += pa[i]
            # edge: the neighbour to the right or below differs strongly in run a
            if x + 1 < w and max(abs(pa[i] - pa[i + 3]), abs(pa[i + 1] - pa[i + 4]), abs(pa[i + 2] - pa[i + 5])) > 32:
                kante[c] += 1
            elif y + 1 < h and max(abs(pa[i] - pa[i + w * 3]), abs(pa[i + 1] - pa[i + w * 3 + 1]),
                                   abs(pa[i + 2] - pa[i + w * 3 + 2])) > 32:
                kante[c] += 1
    gesehen = [False] * (cw * ch)
    out = []
    for start in range(cw * ch):
        if not anzahl[start] or gesehen[start]:
            continue
        stapel = [start]
        gesehen[start] = True
        zellen = []
        while stapel:
            c = stapel.pop()
            zellen.append(c)
            cx, cy = c % cw, c // cw
            for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                if 0 <= nx < cw and 0 <= ny < ch:
                    n = ny * cw + nx
                    if anzahl[n] and not gesehen[n]:
                        gesehen[n] = True
                        stapel.append(n)
        xs = [c % cw for c in zellen]
        ys = [c // cw for c in zellen]
        x0, y0 = min(xs) * zelle, min(ys) * zelle
        x1, y1 = min(w, (max(xs) + 1) * zelle), min(h, (max(ys) + 1) * zelle)
        n = sum(anzahl[c] for c in zellen)
        farbe = [sum(summe[c][k] for c in zellen) // n for k in range(3)]
        out.append({"x": x0, "y": y0, "b": x1 - x0, "h": y1 - y0, "pixel": n, "max": max(groesste[c] for c in zellen),
                    "farbe": farbe, "kantenanteil": sum(kante[c] for c in zellen) / n})
    out.sort(key=lambda s: -s["pixel"])
    return out


def ausschnitt(ia, ib, s, pfad, rand=8, mal=3):
    """a, b und die verstaerkte Differenz nebeneinander, vergroessert."""
    x0, y0 = max(0, s["x"] - rand), max(0, s["y"] - rand)
    x1, y1 = min(ia.width, s["x"] + s["b"] + rand), min(ia.height, s["y"] + s["h"] + rand)
    bw, bh = (x1 - x0) * mal, (y1 - y0) * mal
    breite = bw * 3 + 8
    px = bytearray(breite * bh * 3)
    for teil in range(3):
        for y in range(bh):
            sy = y0 + y // mal
            for x in range(bw):
                sx = x0 + x // mal
                i = (sy * ia.width + sx) * 3
                if teil == 0:
                    bgr = ia.bgr[i:i + 3]
                elif teil == 1:
                    bgr = ib.bgr[i:i + 3]
                else:
                    d = max(abs(ia.bgr[i + k] - ib.bgr[i + k]) for k in range(3))
                    v = min(255, d * 16)
                    bgr = bytes((0, 0, v))
                o = (y * breite + teil * (bw + 4) + x) * 3
                px[o:o + 3] = bgr
    write_bmp(pfad, Image(breite, bh, px))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a", help="Ordner von Lauf a, oder ein Fahrt-Ordner mit Unterordnern a und b")
    ap.add_argument("b", nargs="?", help="Ordner von Lauf b")
    ap.add_argument("--zelle", type=int, default=16, help="Zellengroesse in Pixeln (Vorgabe 16)")
    ap.add_argument("--ausschnitte", help="Ordner fuer Ausschnittbilder (a | b | Differenz x16)")
    ap.add_argument("--max-stellen", type=int, default=6, help="hoechstens so viele Stellen je Bild ausgeben")
    args = ap.parse_args()
    a, b = args.a, args.b
    if b is None:
        a, b = os.path.join(args.a, "a"), os.path.join(args.a, "b")
    if not (os.path.isdir(a) and os.path.isdir(b)):
        print("Ordner fehlt: %s oder %s" % (a, b))
        return 2
    if args.ausschnitte:
        os.makedirs(args.ausschnitte, exist_ok=True)
    abweichend = 0
    liste = paare(a, b)
    for name, pa, pb in liste:
        ia, ib = read_bmp(pa), read_bmp(pb)
        if (ia.width, ia.height) != (ib.width, ib.height):
            print("%s: Groessen verschieden" % name)
            abweichend += 1
            continue
        if ia.bgr == ib.bgr:
            continue
        abweichend += 1
        st = stellen(ia, ib, args.zelle)
        print("%s: %d Stellen, %d Pixel" % (name, len(st), sum(s["pixel"] for s in st)))
        for k, s in enumerate(st[:args.max_stellen]):
            print("  x %4d y %4d %4dx%-4d (%3.0f%% %3.0f%% %3.0f%%x%3.0f%%)  %6d Pixel, bis %3d, Farbe a rgb(%d,%d,%d), "
                  "%s" % (s["x"], s["y"], s["b"], s["h"], 100.0 * s["x"] / ia.width, 100.0 * s["y"] / ia.height,
                          100.0 * s["b"] / ia.width, 100.0 * s["h"] / ia.height, s["pixel"], s["max"], *s["farbe"],
                          "an Kanten" if s["kantenanteil"] > 0.5 else "flaechig"))
            if args.ausschnitte:
                ausschnitt(ia, ib, s, os.path.join(args.ausschnitte, "%s_%d.bmp" % (os.path.splitext(name)[0], k)))
        if len(st) > args.max_stellen:
            print("  ... %d weitere Stellen" % (len(st) - args.max_stellen))
    print("%d von %d Paaren nicht pixelgleich" % (abweichend, len(liste)))
    return 1 if abweichend else 0


if __name__ == "__main__":
    sys.exit(main())
