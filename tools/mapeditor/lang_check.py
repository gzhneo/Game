#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Checks the German texts of the editors: every T("...") in a program's App folder and in
Code/Tools/Editors/EditorCommon needs a line in the program's App/Resources/de.lang, and every line there should
still be used. Programs: the map editor (Code/Tools/Editors/MapEditor), the GUI editor (Code/Tools/Editors/GuiEditor) and the translation
tool Babylon (Code/Tools/Editors/Babylon).

Aufruf: python tools/mapeditor/lang_check.py   (Rueckgabewert 1 bei fehlenden Uebersetzungen)
"""
import glob
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TOOLS = os.path.join(ROOT, 'Code', 'Tools', 'Editors')
PROGRAMS = ('MapEditor', 'GuiEditor', 'Babylon')
LITERAL = re.compile(r'\bT\("((?:[^"\\]|\\.)*)"\)')


def decode(text):
    # C escapes used in the sources: \uXXXX, \", \\, \n.
    return re.sub(r'\\u([0-9a-fA-F]{4})|\\(.)',
                  lambda m: chr(int(m.group(1), 16)) if m.group(1) else {'n': '\n'}.get(m.group(2), m.group(2)), text)


def sources(folder):
    return glob.glob(os.path.join(folder, '**', '*.cpp'), recursive=True) + glob.glob(os.path.join(folder, '**', '*.h'), recursive=True)


def check(program):
    app = os.path.join(TOOLS, program, 'App')
    used = set()
    for path in sources(app) + sources(os.path.join(TOOLS, 'EditorCommon')):
        with open(path, encoding='ascii') as f:
            for m in LITERAL.finditer(f.read()):
                used.add(decode(m.group(1)))
    known = {}
    with open(os.path.join(app, 'Resources', 'de.lang'), encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\n')
            if not line.strip() or line.startswith('#') or '\t' not in line:
                continue
            key, value = line.split('\t', 1)
            known[key] = value
    missing = sorted(used - set(known))
    unused = sorted(set(known) - used)
    for m in missing:
        print(program + ': fehlt in de.lang:', m)
    for u in unused:
        print(program + ': unbenutzt in de.lang:', u)
    print(f'{program}: {len(used)} Texte, {len(known)} Uebersetzungen, {len(missing)} fehlen, {len(unused)} unbenutzt')
    return not missing


def main():
    results = [check(p) for p in PROGRAMS]
    return 0 if all(results) else 1


if __name__ == '__main__':
    sys.exit(main())
