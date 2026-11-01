#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Baut einen Symlink-Spiegel eines Quellbaums fuer die Syntaxpruefung:
jede Datei als Symlink unter Original- und Kleinschreibnamen; Verzeichnisse
zusaetzlich unter Kleinschreibnamen. Bereits gepatchte echte Dateien im Spiegel
(kein Symlink) bleiben erhalten.

Seit Phase 7 der Angleichung liegen die in beiden Spielen
bytegleichen Bibliotheken unter Code/, der Rest unter <Baum>/Code. Deshalb wird das
Skript zweimal in denselben Spiegel aufgerufen (siehe setup.sh): erst Code/, dann
<Baum>/Code. Ein schon vorhandener Eintrag wird nie ueberschrieben -- der erste
Aufruf gewinnt. Die beiden Quellen sind ueberschneidungsfrei (was unter Code/ liegt, gibt es
in keinem Baum mehr; tools/mdsync/treediff.py --overlap prueft das), die Reihenfolge
entscheidet also nichts; sie ist nur festgelegt, damit der Spiegel reproduzierbar bleibt (R-66).
Seit Phase 7 Teil 3 fuellt Code/ auch Teile von GameEngine und GameEngineDevice: im Spiegel
stehen unter tree/GameEngine die gemeinsamen und die baumeigenen Ordner wieder nebeneinander.
Code/Tools faellt wie <Baum>/Code/Tools unter SKIP."""
import os, sys
SRC = os.path.abspath(sys.argv[1])
DST = os.path.abspath(sys.argv[2])
SKIP = {'.git', 'build', 'build-x64', 'Tools'}
for dirpath, dirnames, filenames in os.walk(SRC):
    dirnames[:] = [d for d in dirnames if d not in SKIP]
    rel = os.path.relpath(dirpath, SRC)
    dst_dir = DST if rel == '.' else os.path.join(DST, rel)
    os.makedirs(dst_dir, exist_ok=True)
    base = os.path.basename(dst_dir)
    low = os.path.join(os.path.dirname(dst_dir), base.lower())
    if base.lower() != base and not os.path.lexists(low):
        try: os.symlink(base, low)
        except OSError: pass
    for fn in filenames:
        src = os.path.join(dirpath, fn)
        for name in {fn, fn.lower()}:
            dst = os.path.join(dst_dir, name)
            if not os.path.lexists(dst):
                os.symlink(src, dst)
print("tree ok")
