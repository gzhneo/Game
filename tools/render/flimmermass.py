#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Flimmermass einer Bildfolge (TODO 91 Punkt 6).

Eingabe: ein Ordner mit aufeinanderfolgenden Bildern einer langsamen Kamerafahrt (shot_NNNNNN.bmp, etwa mit
-shotEvery 1). Gemessen wird die zweite zeitliche Differenz je Pixel, |I(t+1) - 2 I(t) + I(t-1)|, gemittelt ueber
alle Pixel und Bildtripel, in Graustufen 0..255. Gleichmaessige Bewegung (Bildlauf) faellt dabei fast heraus; was
bleibt, ist Kantenkriechen und Flimmern von Bild zu Bild. Kleiner ist ruhiger.

Aufruf:  flimmermass.py <Ordner> [<Ordner> ...] [--zeilen OBEN:UNTEN]
  --zeilen  nur diese Bildzeilen (etwa 0:470 ohne Befehlsleiste)
"""
import argparse
import glob
import os
import sys

from PIL import Image


def grau(pfad, zeilen):
    bild = Image.open(pfad).convert('L')
    if zeilen:
        bild = bild.crop((0, zeilen[0], bild.width, zeilen[1]))
    return bild.tobytes()


def flimmern(ordner, zeilen):
    bilder = sorted(glob.glob(os.path.join(ordner, '*.bmp')))
    if len(bilder) < 3:
        return None, len(bilder)
    summe = 0
    anzahl = 0
    a = grau(bilder[0], zeilen)
    b = grau(bilder[1], zeilen)
    for pfad in bilder[2:]:
        c = grau(pfad, zeilen)
        summe += sum(abs(x - 2 * y + z) for x, y, z in zip(a, b, c))
        anzahl += len(a)
        a, b = b, c
    return summe / anzahl, len(bilder)


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('ordner', nargs='+')
    p.add_argument('--zeilen', help='OBEN:UNTEN')
    args = p.parse_args()
    zeilen = tuple(int(v) for v in args.zeilen.split(':')) if args.zeilen else None
    for ordner in args.ordner:
        wert, n = flimmern(ordner, zeilen)
        if wert is None:
            print('%s: zu wenige Bilder (%d)' % (ordner, n))
            continue
        print('%-40s Flimmermass %.3f (%d Bilder)' % (os.path.basename(os.path.normpath(ordner)), wert, n))
    return 0


if __name__ == '__main__':
    sys.exit(main())
