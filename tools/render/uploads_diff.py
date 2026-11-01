#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Vergleicht die Upload-Protokolle zweier Laeufe derselben Szene (Spielschalter -logUploads).

Mit  -crcLog <datei> -logUploads <von> <bis>  schreibt das Spiel je gezeichnetem Bild einer Partie eine Zeile
"UPLOADS <Frame> SUMME uploads=.. bytes=.. upload=<Hash> draws=.. draw=<Hash>" und fuer die Logik-Frames
<von> bis <bis> zusaetzlich je Upload eine Zeile "UPLOADS <Frame> U ..." (Art, Bytes, Offset, Hash der Daten,
Inhalt des Ziels danach), je GPU-Kopie "C ..." und je Draw "D ..." (Art, Anzahl, Inhalt von Vertex-/Indexpuffer und
Texturen, Hash der Sampler- und Render-Zustaende, der VS-/PS-Konstanten, der Objekt- und Frame-Konstanten, der
Uploads seit dem vorigen Draw und von allem zusammen).

Ausgabe: die Frames mit abweichender Summe (erst die Upload-, dann die Draw-Hashes) und fuer jeden Frame mit
Einzelzeilen die erste abweichende Zeile beider Laeufe samt den Feldern, die sich unterscheiden, und ein paar Zeilen
davor. Die erste abweichende U-Zeile nennt den Schreiber (Art und Groesse); die erste abweichende D-Zeile, welche
Eingabe des Draws anders ist.

  python tools/render/uploads_diff.py lauf-a/runner.log lauf-b/runner.log
  python tools/render/uploads_diff.py --kontext 5 --alle a.log b.log    # alle abweichenden Frames im Detail

Rueckgabewert 0: gleich; 1: Abweichungen; 2: Lesefehler oder keine UPLOADS-Zeilen.
"""

import argparse
import signal
import sys


def lesen(pfad):
    summen, zeilen = {}, {}
    with open(pfad, encoding="latin-1", errors="replace") as f:
        for z in f:
            t = z.split()
            if len(t) < 3 or t[0] != "UPLOADS" or not t[1].isdigit():
                continue
            frame = int(t[1])
            if t[2] == "SUMME":
                # ein Frame kann mehrfach gezeichnet werden (reine Renderbilder); alle Summen zaehlen
                summen.setdefault(frame, []).append(" ".join(t[3:]))
            else:
                zeilen.setdefault(frame, []).append(t[2:])
    return summen, zeilen


def felder(t):
    """Feld name=wert -> dict; Positionsfelder (Art, Nummer, Kind) unter ihrem Index."""
    d = {}
    for i, w in enumerate(t):
        if "=" in w:
            k, v = w.split("=", 1)
            d[k] = v
        else:
            d["#%d" % i] = w
    return d


def ohne_nummer(t):
    # Die laufende Nummer (Feld 1 bei U und D) verschiebt sich hinter einer zusaetzlichen Zeile; verglichen wird
    # der Inhalt.
    return [w for i, w in enumerate(t) if not (i == 1 and t[0] in ("U", "D"))]


def main():
    if hasattr(signal, "SIGPIPE"):
        signal.signal(signal.SIGPIPE, signal.SIG_DFL)  # Ausgabe an head
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--kontext", type=int, default=3, help="Zeilen vor der ersten Abweichung (Vorgabe 3)")
    ap.add_argument("--alle", action="store_true", help="jeden abweichenden Frame mit Einzelzeilen zeigen, nicht nur den ersten")
    a = ap.parse_args()
    try:
        sa, za = lesen(a.a)
        sb, zb = lesen(a.b)
    except OSError as e:
        print("Lesefehler: %s" % e)
        return 2
    if not sa or not sb:
        print("keine UPLOADS-Zeilen (Spielschalter -logUploads und -crcLog gesetzt?)")
        return 2

    gemeinsam = sorted(set(sa) & set(sb))
    abweichend = [f for f in gemeinsam if sa[f] != sb[f]]
    print("Frames mit Summe: %d / %d, gemeinsam %d, abweichend %d" % (len(sa), len(sb), len(gemeinsam), len(abweichend)))
    for f in abweichend[:20]:
        print("  Frame %d" % f)
        for x, y in zip(sa[f], sb[f]):
            if x != y:
                print("    a: %s\n    b: %s" % (x, y))
    if len(abweichend) > 20:
        print("  ... %d weitere" % (len(abweichend) - 20))

    detail = sorted(set(za) & set(zb))
    gezeigt = 0
    for f in detail:
        la, lb = za[f], zb[f]
        n = next((i for i, (x, y) in enumerate(zip(la, lb)) if ohne_nummer(x) != ohne_nummer(y)), None)
        if n is None:
            if len(la) != len(lb):
                print("\nFrame %d: gleich bis Zeile %d, dann endet ein Lauf (a %d, b %d Zeilen)" % (f, min(len(la), len(lb)), len(la), len(lb)))
                gezeigt += 1
            continue
        print("\nFrame %d: erste abweichende Zeile %d von %d/%d" % (f, n + 1, len(la), len(lb)))
        for i in range(max(0, n - a.kontext), n):
            print("    = %s" % " ".join(la[i]))
        print("  a: %s" % " ".join(la[n]))
        print("  b: %s" % " ".join(lb[n]))
        fa, fb = felder(la[n]), felder(lb[n])
        anders = [k for k in fa if k in fb and fa[k] != fb[k] and not (k == "#1" and la[n][0] in ("U", "D"))]
        if anders:
            print("  verschieden: %s" % ", ".join(k for k in anders))
        gezeigt += 1
        if not a.alle:
            break
    if not detail:
        print("\nkeine Einzelzeilen (Frame-Bereich von -logUploads ausserhalb der Partie?)")
    return 1 if abweichend or gezeigt else 0


if __name__ == "__main__":
    sys.exit(main())
