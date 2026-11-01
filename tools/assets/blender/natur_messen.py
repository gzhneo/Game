# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Messwerkzeug: stellt die Gerüstwerte der eigenen Naturmodelle denen der
# EA-Originale gegenüber. Nur zum Vergleichen -- erzeugt und verändert keine Datei; die Bauwerkzeuge (natur_bauen.py,
# objekt_ersetzen.py, baum_ersetzen.py, w3d_geruest.py) lesen keine EA-Datei.
#
#   python natur_messen.py <ea-ordner> [<eigen-ordner>] [<modell>...]
#
# <ea-ordner>: aus den EA-Archiven entpackte Modelle (big_werkzeug.py entpacke ... pmrocks01.w3d ...), außerhalb des
# Repos; <eigen-ordner>: Standard Data/Natur/Art/W3D. Ausgabe je Modell: Zeile für Zeile EA | eigen, '*' = abweichend.
import os
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HIER)
import w3d_geruest  # noqa: E402

MODELLE = ['PMRocks01', 'PMRocks03', 'PMmcluster', 'PMgaldrum', 'PMgaldrum_D', 'PTPine01', 'PTPine01_hi', 'PTPine01_B',
           'PTPine01_hi_B']


def main():
    if len(sys.argv) < 2:
        sys.exit('Aufruf: python natur_messen.py <ea-ordner> [<eigen-ordner>] [<modell>...]')
    ea_ordner = sys.argv[1]
    rest = sys.argv[2:]
    eigen = os.path.normpath(os.path.join(HIER, '..', '..', '..', 'Data', 'Natur', 'Art', 'W3D'))
    if rest and os.path.isdir(rest[0]):
        eigen = rest.pop(0)
    for m in rest or MODELLE:
        ea_pfad = os.path.join(ea_ordner, m.lower() + '.w3d')
        if not os.path.exists(ea_pfad):
            print('== %s: EA-Datei fehlt (%s)' % (m, ea_pfad))
            continue
        a = w3d_geruest.zeilen(open(ea_pfad, 'rb').read())
        b = w3d_geruest.zeilen(open(os.path.join(eigen, m + '.W3D'), 'rb').read())
        breite = max(len(x) for x in a) + 1
        print('== %s' % m)
        for i in range(max(len(a), len(b))):
            x = a[i] if i < len(a) else ''
            y = b[i] if i < len(b) else ''
            print('%s %-*s| %s' % (' ' if x == y else '*', breite, x, y))


if __name__ == '__main__':
    main()
