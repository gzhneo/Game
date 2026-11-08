#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Prueft die projekteigenen Spieltexte (Data/Text/<Sprache>.str, CP1252).

- Jede Sprachdatei hat dieselben Kennungen wie english.str, mit denselben Formatplatzhaltern.
- Jede Kennung "GZH:..." im Code steht in english.str; jede Kennung in english.str wird benutzt (im Code oder,
  laut IN_FENSTERN bzw. IN_TASTENBELEGUNG, in einer Fensterdatei oder der Tastenbelegung von EA).

Rueckgabewert 0 heisst bestanden."""
import os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TEXTE = os.path.join(ROOT, 'Data', 'Text')
# Texte, die EAs Fensterdateien (.wnd) nennen statt des Codes: für Generals ergänzt, weil es Zero Hours Fenster
# übernimmt (OptionsMenu.wnd aus WindowZH.big, tools/assets/setup_assets.py).
IN_FENSTERN = {'GUI:DoubleClickAttackMove', 'GUI:RetaliationTitle', 'GUI:HeatEffects', 'TOOLTIP:HeatEffects',
               'TOOLTIP:DoubleClickAttackMove', 'GUI:Retaliation'}
# Texte, die EAs Tastenbelegung (CommandMap.ini) nennt: Generals uebernimmt die Belegung von Zero Hour
# (tools/assets/setup_assets.py); die englische hat SELECT_ALL_AIRCRAFT, dessen Text nur in Zero Hour steht.
IN_TASTENBELEGUNG = {'GUI:SelectAllAircraft', 'GUI:SelectAllAircraftDescription'}
PLATZHALTER = re.compile(r'%[-+ #0-9.]*(?:l|h|ll)?[a-zA-Z]')


def lesen(pfad):
    eintraege = {}
    zeilen = open(pfad, encoding='cp1252').read().splitlines()
    i = 0
    while i < len(zeilen):
        z = zeilen[i].strip()
        i += 1
        if not z or z.startswith('//'):
            continue
        kennung, text = z, None
        while i < len(zeilen):
            t = zeilen[i].strip()
            i += 1
            if t.lower() == 'end':
                break
            if t.startswith('"') and text is None:
                text = t[1:t.rfind('"')] if t.count('"') >= 2 else t[1:]
        if kennung in eintraege:
            raise SystemExit('%s: %s doppelt' % (pfad, kennung))
        eintraege[kennung] = text or ''
    return eintraege


def main():
    fehler = []
    en = lesen(os.path.join(TEXTE, 'english.str'))
    for datei in sorted(os.listdir(TEXTE)):
        if not datei.endswith('.str') or datei == 'english.str':
            continue
        andere = lesen(os.path.join(TEXTE, datei))
        for k in sorted(set(en) - set(andere)):
            fehler.append('%s: %s fehlt' % (datei, k))
        for k in sorted(set(andere) - set(en)):
            fehler.append('%s: %s steht nicht in english.str' % (datei, k))
        for k in sorted(set(en) & set(andere)):
            if PLATZHALTER.findall(en[k]) != PLATZHALTER.findall(andere[k]):
                fehler.append('%s: %s hat andere Platzhalter' % (datei, k))
    benutzt = set()
    for wurzel, _, dateien in os.walk(os.path.join(ROOT, 'Code')):
        for d in dateien:
            if d.endswith(('.cpp', '.h')):
                benutzt |= set(re.findall(r'"(GZH:[A-Za-z0-9_]+)"', open(os.path.join(wurzel, d), encoding='latin-1').read()))
    for k in sorted(benutzt - set(en)):
        fehler.append('Code benutzt %s, english.str kennt es nicht' % k)
    for k in sorted(set(en) - benutzt - IN_FENSTERN - IN_TASTENBELEGUNG):
        fehler.append('english.str: %s wird im Code nicht benutzt' % k)
    for f in fehler:
        print(f)
    print('%d Texte, %d Sprachdateien, %d Fehler' % (len(en), len([d for d in os.listdir(TEXTE) if d.endswith('.str')]), len(fehler)))
    return 1 if fehler else 0


if __name__ == '__main__':
    sys.exit(main())
