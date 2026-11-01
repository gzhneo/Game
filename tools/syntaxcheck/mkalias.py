#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Ergaenzt im Spiegel fuer jede in den Quellen benutzte Include-Schreibweise
einen Symlink (Windows-Includes sind case-insensitiv, Linux nicht)."""
import os, re, sys
T = os.path.abspath(sys.argv[1])
spellings = set()
inc = re.compile(r'#\s*include\s*[<"]([^">]+)[">]')
for dirpath, dirnames, filenames in os.walk(T):
    dirnames[:] = [d for d in dirnames if not os.path.islink(os.path.join(dirpath, d))]
    for fn in filenames:
        p = os.path.join(dirpath, fn)
        if not fn.lower().endswith(('.h', '.hpp', '.cpp', '.inl')): continue
        try: text = open(p, errors='replace').read()
        except OSError: continue
        for m in inc.finditer(text):
            spellings.add(m.group(1).replace('\\', '/'))
made = 0
# Index aller Dateien pro Verzeichnis nach Kleinschreibung
index = {}
for dirpath, dirnames, filenames in os.walk(T):
    dirnames[:] = [d for d in dirnames if not os.path.islink(os.path.join(dirpath, d))]
    index[dirpath] = {f.lower(): f for f in filenames}
for dirpath, files in list(index.items()):
    for sp in spellings:
        if '/' in sp: continue
        real = files.get(sp.lower())
        if real and real != sp:
            dst = os.path.join(dirpath, sp)
            if not os.path.lexists(dst):
                os.symlink(os.path.join(dirpath, real), dst); made += 1
print("aliases", made)

# Schreibweisen mit Unterverzeichnis: je Include-Wurzel aufloesen.
#
# Die Aufloesung muss zurueckspringen koennen. Fuer "WWLIB/ffactory.h" legt die Schleife unten ein
# echtes Verzeichnis "WWLIB" an (makedirs); steht daneben das richtige "WWLib", gibt es zwei
# Kandidaten, die sich nur in der Schreibweise unterscheiden. Ein einfaches next() nahm den
# erstbesten aus os.listdir -- und traf damit fuer "WWLib/RefCount.h" mal das echte Verzeichnis,
# mal die halbleere Alias-Huelle. Da ausserdem ueber ein set iteriert wurde, hing das Ergebnis an
# der Hash-Reihenfolge und damit am Inhalt des Baums: derselbe Quellstand konnte beim einen Lauf
# 25 Dateien uebersetzen und beim naechsten nicht. Deshalb: sortiert iterieren (reproduzierbar),
# die genaue Schreibweise zuerst probieren und bei einer Sackgasse den naechsten Kandidaten nehmen.
ROOTS = [T, os.path.join(T, 'Core'), os.path.join(T, 'GameEngine'),
         os.path.join(T, 'Core/EngineCore'),
         os.path.join(T, 'Platform'), os.path.join(T, 'Core'), os.path.join(T, 'Renderer')]


def resolve(cur, parts):
    """Pfad im Spiegel case-insensitiv aufloesen, mit Rueckspruengen. None, wenn es ihn nicht gibt."""
    if not parts:
        return cur
    part, rest = parts[0], parts[1:]
    try:
        entries = os.listdir(cur)
    except OSError:
        return None
    # genaue Schreibweise zuerst, danach die uebrigen Kandidaten in fester Reihenfolge
    cands = [e for e in entries if e == part] + sorted(e for e in entries if e != part and e.lower() == part.lower())
    for e in cands:
        hit = resolve(os.path.join(cur, e), rest)
        if hit is not None:
            return hit
    return None


made2 = 0
for root in ROOTS:
    if not os.path.isdir(root): continue
    for sp in sorted(spellings):
        if '/' not in sp: continue
        dst = os.path.join(root, sp)
        if os.path.lexists(dst): continue
        cur = resolve(root, sp.split('/'))
        if cur is not None:
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if not os.path.lexists(dst):
                os.symlink(cur, dst); made2 += 1
print("path aliases", made2)
