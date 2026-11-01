#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Abstaende zwischen gezeigten Bildern auswerten (Ruckler).

Liest die CSV aus `-logRenderTime <Datei>` (GameEngine.cpp, logRenderTime; Spalten ms, art, tick, frame,
alpha, anzeige, ticklen) und wertet die Abstaende zwischen aufeinanderfolgenden Bildern aus:

  - Verteilung p50/p95/p99/p99.9, laengster Abstand, Bilder/s
  - Zahl der Abstaende ueber 2 x Median und ueber feste Grenzen (Standard 16,7 / 33,3 / 50 ms)
  - getrennt nach der Art des Bildes, das den Abstand beendet: T = Tick-Bild (davor lief die Logik),
    R = Zwischenbild, F = Tick-Bild ohne Interpolation. Ruckler durch lange Ticks landen bei T.
  - getrennt nach Dritteln der Laufzeit (spaetes Gefecht = letztes Drittel)
  - die laengsten Abstaende einzeln mit Logik-Frame, zum Abgleich mit LOGIKSPITZE aus -crcLog

Abstaende ueber --pause ms (Laden, Alt-Tab, Haltepunkt) zaehlen nicht mit und werden nur genannt.
Mit --vergleich <CSV> steht ein zweiter Lauf daneben (vorher/nachher).

Aufruf:
    python tools/render/bildabstand.py rendertime.csv
    python tools/render/bildabstand.py nachher.csv --vergleich vorher.csv --skip 300

Rueckgabe 0 = ausgewertet, 2 = Datei unbrauchbar.
"""

import argparse
import csv
import sys


def quantil(werte, q):
    if not werte:
        return float('nan')
    s = sorted(werte)
    return s[min(len(s) - 1, int(q * (len(s) - 1) + 0.5))]


def lesen(pfad, skip):
    zeilen = []
    with open(pfad, newline='') as f:
        for r in csv.DictReader(f):
            try:
                zeilen.append((float(r['ms']), r['art'], int(r['frame'])))
            except (KeyError, ValueError):
                print('%s: unlesbare Zeile: %s' % (pfad, r))
                return None
    return zeilen[skip:]


def auswerten(zeilen, pause):
    """Liefert (abstaende, pausen): abstaende als Liste (ms, art, frame, zeitpunkt)."""
    abstaende, pausen = [], []
    for prev, cur in zip(zeilen, zeilen[1:]):
        d = cur[0] - prev[0]
        if d > pause:
            pausen.append((d, cur[2], cur[0]))
        else:
            abstaende.append((d, cur[1], cur[2], cur[0]))
    return abstaende, pausen


def kennzahlen(werte, grenzen, median_bezug=None):
    if not werte:
        return None
    med = quantil(werte, 0.5)
    bezug = med if median_bezug is None else median_bezug
    k = {
        'n': len(werte),
        'p50': med,
        'p95': quantil(werte, 0.95),
        'p99': quantil(werte, 0.99),
        'p999': quantil(werte, 0.999),
        'max': max(werte),
        'mittel': sum(werte) / len(werte),
        'ueber2med': sum(1 for w in werte if w > 2.0 * bezug),
    }
    for g in grenzen:
        k['>%g' % g] = sum(1 for w in werte if w > g)
    return k


def zeile(name, k, grenzen):
    if k is None:
        return '%-16s (keine Bilder)' % name
    teile = ['%-16s n %7d' % (name, k['n']),
             'p50 %6.2f' % k['p50'], 'p95 %6.2f' % k['p95'], 'p99 %6.2f' % k['p99'],
             'p99.9 %6.2f' % k['p999'], 'max %7.2f' % k['max'],
             '>2xMed %5d (%5.2f %%)' % (k['ueber2med'], 100.0 * k['ueber2med'] / k['n'])]
    teile += ['>%gms %5d' % (g, k['>%g' % g]) for g in grenzen]
    return '  '.join(teile)


def bericht(titel, zeilen, a, grenzen):
    abstaende, pausen = auswerten(zeilen, a.pause)
    print('== %s' % titel)
    if not abstaende:
        print('zu wenige Bilder')
        return None
    dauer = sum(x[0] for x in abstaende)
    arten = {}
    for x in abstaende:
        arten[x[1]] = arten.get(x[1], 0) + 1
    print('Bilder: %d in %.1f s ohne Pausen (%s), %.1f Bilder/s, Logik-Frames %d bis %d' % (
        len(abstaende) + 1, dauer / 1000.0, ', '.join('%s %d' % kv for kv in sorted(arten.items())),
        len(abstaende) * 1000.0 / dauer if dauer > 0 else 0.0, min(z[2] for z in zeilen), max(z[2] for z in zeilen)))
    if pausen:
        print('Pausen ueber %.0f ms (nicht gezaehlt): %d, laengste %.0f ms bei Frame %d' % (
            a.pause, len(pausen), max(p[0] for p in pausen), max(pausen)[1]))

    alle = [x[0] for x in abstaende]
    gesamt = kennzahlen(alle, grenzen)
    print('Abstand in ms; >2xMed bezieht sich auf den Median aller Bilder (%.2f ms)' % gesamt['p50'])
    print(zeile('alle', gesamt, grenzen))
    for art in sorted(arten):
        print(zeile('endet mit ' + art, kennzahlen([x[0] for x in abstaende if x[1] == art], grenzen, gesamt['p50']),
                    grenzen))
    t0, t1 = zeilen[0][0], zeilen[-1][0]
    for i, name in enumerate(('erstes Drittel', 'zweites Drittel', 'letztes Drittel')):
        von, bis = t0 + (t1 - t0) * i / 3.0, t0 + (t1 - t0) * (i + 1) / 3.0
        werte = [x[0] for x in abstaende if von < x[3] <= bis or (i == 0 and x[3] == von)]
        print(zeile(name, kennzahlen(werte, grenzen, gesamt['p50']), grenzen))

    if a.list > 0:
        print('laengste Abstaende:')
        for d, art, frame, ms in sorted(abstaende, reverse=True)[:a.list]:
            print('  %8.2f ms  bis Bild %s bei %9.1f ms, Logik-Frame %d' % (d, art, ms, frame))
    return gesamt


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('csv', help='Datei aus -logRenderTime')
    ap.add_argument('--vergleich', help='zweite Datei (etwa der Lauf vor einer Aenderung)')
    ap.add_argument('--skip', type=int, default=0, help='die ersten N Bilder auslassen (Start, Einblenden)')
    ap.add_argument('--pause', type=float, default=1000.0,
                    help='Abstaende darueber sind Pausen und zaehlen nicht (Standard 1000 ms)')
    ap.add_argument('--grenzen', default='16.7,33.3,50', help='feste Grenzen in ms, durch Komma getrennt')
    ap.add_argument('--list', type=int, default=10, help='so viele laengste Abstaende einzeln zeigen')
    a = ap.parse_args()
    grenzen = [float(g) for g in a.grenzen.split(',') if g.strip()]

    zeilen = lesen(a.csv, a.skip)
    if zeilen is None or len(zeilen) < 2:
        print('%s: unbrauchbar' % a.csv)
        return 2
    neu = bericht(a.csv, zeilen, a, grenzen)
    if neu is None:
        return 2
    if a.vergleich:
        alt_zeilen = lesen(a.vergleich, a.skip)
        if alt_zeilen is None or len(alt_zeilen) < 2:
            print('%s: unbrauchbar' % a.vergleich)
            return 2
        print()
        alt = bericht(a.vergleich, alt_zeilen, a, grenzen)
        if alt is None:
            return 2
        print()
        print('== Vergleich (%s gegen %s)' % (a.csv, a.vergleich))
        for key in ('p50', 'p95', 'p99', 'p999', 'max'):
            print('  %-5s %8.2f -> %8.2f ms (%+.1f %%)' % (
                key, alt[key], neu[key], 100.0 * (neu[key] - alt[key]) / alt[key] if alt[key] else 0.0))
        print('  >2xMed je 1000 Bilder %.2f -> %.2f' % (
            1000.0 * alt['ueber2med'] / alt['n'], 1000.0 * neu['ueber2med'] / neu['n']))
    return 0


if __name__ == '__main__':
    sys.exit(main())
