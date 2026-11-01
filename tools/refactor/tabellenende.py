#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Prueft, dass INI-Tabellen mit ihrem Endeintrag schliessen (Pruefgang, tools/gate.sh).

Der INI-Leser sucht in FieldParse-Tabellen und Namenslisten bis zum Eintrag mit nullptr; fehlt
er, liest ein unbekanntes Feld oder ein unbekannter Name aus einer INI-Datei (auch aus einer
map.ini fremder Karten) hinter dem Array weiter. Geprueft werden
  - jede Tabelle "FieldParse name[] = { ... }": letzter Eintrag { nullptr, ... },
  - jede Namensliste bzw. LookupListRec-Tabelle, die in einer FieldParse-Tabelle als userData
    eines Listen-Parsers (parseIndexList, parseBitString*, parseLookupList, ...) steht.
Rueckgabewert 1, wenn eine Tabelle ohne Endeintrag gefunden wurde."""
import os, re, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Code')


def body_of(s, start):
    i, depth = start, 1
    while depth and i < len(s):
        if s[i] == '{':
            depth += 1
        elif s[i] == '}':
            depth -= 1
        i += 1
    return s[start:i - 1]


def main():
    files = {}
    for dp, dn, fns in os.walk(ROOT):
        for fn in fns:
            if fn.lower().endswith(('.cpp', '.h', '.inl')):
                p = os.path.join(dp, fn)
                s = open(p, 'rb').read().decode('latin-1')
                files[os.path.relpath(p, os.path.join(ROOT, '..'))] = re.sub(r'//[^\n]*', '', s)

    bad = []
    tables = 0
    for p, s in files.items():
        for m in re.finditer(r'FieldParse\s+[\w:]+\s*\[\s*\]\s*=\s*\{', s):
            tables += 1
            entries = re.findall(r'\{([^{}]*)\}', body_of(s, m.end()))
            if not entries or not re.match(r'\s*(nullptr|NULL|0)\s*,', entries[-1]):
                bad.append('%s:%d FieldParse-Tabelle ohne { nullptr, ... }' % (p, s[:m.start()].count('\n') + 1))

    used = set()
    lister = r'parse(?:IndexList|BitString8|BitString32|LookupList|ByteSizedIndexList)\w*'
    for s in files.values():
        for m in re.finditer(r'\{\s*"[^"]*"\s*,\s*[\w:]*' + lister + r'\s*,\s*([\w:]+)\s*,', s):
            used.add(m.group(1).split('::')[-1])
    used -= {'nullptr', 'NULL'}
    lists = 0
    for p, s in files.items():
        for m in re.finditer(r'(?:char\s*\*\s*(?:const\s*)?|LookupListRec\s+)([\w:]+)\s*\[\s*\w*\s*\]\s*=\s*\{', s):
            if m.group(1).split('::')[-1] not in used:
                continue
            lists += 1
            body = body_of(s, m.end()).strip().rstrip(',').strip()
            if body.endswith('}'):
                ok = re.match(r'\{\s*(nullptr|NULL|0)\s*,', body[body.rfind('{'):])
            else:
                ok = body.split(',')[-1].strip() in ('nullptr', 'NULL', '0')
            if not ok:
                bad.append('%s:%d Namensliste %s ohne nullptr am Ende' % (p, s[:m.start()].count('\n') + 1, m.group(1)))

    for b in bad:
        print(b)
    print('%d FieldParse-Tabellen, %d Namenslisten geprueft, %d ohne Endeintrag' % (tables, lists, len(bad)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
