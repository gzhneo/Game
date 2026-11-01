#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Stetigkeit der Darstellungszeit pruefen.

Liest die CSV aus `-logRenderTime <Datei>` (GameEngine.cpp, logRenderTime) und prueft, ob die
dargestellte Zeit ueber Tick- und Zwischenbilder stetig mit der Echtzeit laeuft.

Spalten der CSV: ms, art, tick, frame, alpha, anzeige, ticklen
  art T = Tick-Bild nach der Logik (interpoliert), R = Zwischenbild (interpoliert),
      F = Tick-Bild vor der Logik ohne Interpolation (-fixedTimeStep, Zeitraffer, eingefrorene
          Kamerafahrt). In F-Bildern steht die Kamera schon auf anzeige + 1.
  anzeige = tick - 1 + alpha in Ticks

Ausgewertet werden aufeinanderfolgende Bilder, die beide T oder R sind und hoechstens --gap ms
auseinanderliegen (Laden, Alt-Tab und Aufholen trennen die Abschnitte). Gemeldet werden:
  rueckwaerts  anzeige ist gefallen -- das Bild zeigt einen frueheren Stand als das vorige.
               Genau das war der Doppelbild-Fehler; soll 0 sein.
  schritt      Zuwachs von anzeige je Bild in Ticks (bei 60 Hz und 30 Ticks/s um 0,5)
  gleichlauf   Zuwachs von anzeige * ticklen minus Zuwachs von ms, also wie weit die gezeigte Zeit
               von einem Bild zum naechsten gegen die Echtzeit verrutscht (ms). Die Spalte ms wird
               nach dem Zeichnen genommen, alpha schon davor; die Streuung enthaelt deshalb die
               schwankende Zeichen- und Present-Dauer. Mit VSync sind wenige ms normal.

Aufruf:
    python tools/render/rendertime_check.py rendertime.csv
    python tools/render/rendertime_check.py rendertime.csv --skip 300 --gap 100

Rueckgabe 0 = keine Rueckwaertsschritte, 1 = Rueckwaertsschritte gefunden, 2 = Datei unbrauchbar.
"""

import argparse
import csv
import sys


def quantile(values, q):
    if not values:
        return float('nan')
    s = sorted(values)
    return s[min(len(s) - 1, int(q * (len(s) - 1) + 0.5))]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('csv', help='Datei aus -logRenderTime')
    ap.add_argument('--skip', type=int, default=0, help='die ersten N Bilder auslassen (Start, Einblenden)')
    ap.add_argument('--gap', type=float, default=100.0, help='groesserer Abstand in ms trennt Abschnitte (Standard 100)')
    ap.add_argument('--eps', type=float, default=1e-3, help='Toleranz fuer Rueckwaertsschritte in Ticks')
    ap.add_argument('--list', type=int, default=10, help='so viele Rueckwaertsschritte einzeln zeigen')
    a = ap.parse_args()

    rows = []
    with open(a.csv, newline='') as f:
        for r in csv.DictReader(f):
            try:
                rows.append((float(r['ms']), r['art'], int(r['tick']), int(r['frame']),
                             float(r['alpha']), float(r['anzeige']), float(r['ticklen'])))
            except (KeyError, ValueError):
                print('unlesbare Zeile:', r)
                return 2
    rows = rows[a.skip:]
    if len(rows) < 2:
        print('zu wenige Bilder')
        return 2

    arten = {}
    for r in rows:
        arten[r[1]] = arten.get(r[1], 0) + 1
    dauer = rows[-1][0] - rows[0][0]
    print('Bilder: %d in %.1f s (%s), %.1f Bilder/s' % (
        len(rows), dauer / 1000.0, ', '.join('%s %d' % kv for kv in sorted(arten.items())),
        (len(rows) - 1) * 1000.0 / dauer if dauer > 0 else 0.0))

    schritte, gleichlauf, rueck = [], [], []
    paare = 0
    for prev, cur in zip(rows, rows[1:]):
        if prev[1] not in 'TR' or cur[1] not in 'TR':
            continue
        dms = cur[0] - prev[0]
        if dms > a.gap:
            continue
        paare += 1
        d = cur[5] - prev[5]
        schritte.append(d)
        gleichlauf.append(d * cur[6] - dms)
        if d < -a.eps:
            rueck.append((prev, cur))

    print('ausgewertete Bildpaare (T/R, Abstand <= %.0f ms): %d' % (a.gap, paare))
    if paare == 0:
        print('keine interpolierten Bilder -- lief das Spiel im entkoppelten Modus (ohne -fixedTimeStep/Zeitraffer)?')
        return 2
    print('rueckwaerts: %d' % len(rueck))
    for prev, cur in rueck[:a.list]:
        print('  %.3f ms %s tick %d alpha %.4f anzeige %.4f  ->  %.3f ms %s tick %d alpha %.4f anzeige %.4f' % (
            prev[0], prev[1], prev[2], prev[4], prev[5], cur[0], cur[1], cur[2], cur[4], cur[5]))
    print('schritt in Ticks     p1 %.3f  p50 %.3f  p99 %.3f  min %.3f  max %.3f' % (
        quantile(schritte, 0.01), quantile(schritte, 0.5), quantile(schritte, 0.99), min(schritte), max(schritte)))
    abw = [abs(g) for g in gleichlauf]
    print('gleichlauf in ms     p50 %.2f  p95 %.2f  p99 %.2f  max %.2f  (Betrag)' % (
        quantile(abw, 0.5), quantile(abw, 0.95), quantile(abw, 0.99), max(abw)))
    return 1 if rueck else 0


if __name__ == '__main__':
    sys.exit(main())
