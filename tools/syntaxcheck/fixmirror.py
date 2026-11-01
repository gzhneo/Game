#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Patcht Kopien der Mirror-Header, bis clang keine 'undeclared identifier'-Fehler
mehr meldet (MSVC-Zweiphasenlookup in Templates; nur fuer die Syntaxpruefung)."""
import os, re, subprocess, sys, shutil
HERE = os.path.dirname(os.path.abspath(__file__))
SCR = os.environ.get('SYNTAXCHECK_DIR', os.path.join(os.environ.get('TMPDIR','/tmp'), 'cnc-syntaxcheck'))
target = sys.argv[1]
for it in range(40):
    p = subprocess.run([os.path.join(HERE, 'check.sh'), target], capture_output=True, text=True)
    errs = re.findall(r'^(\S+):(\d+):(\d+): error: use of undeclared identifier', p.stderr, re.M)
    enums = re.findall(r'^(\S+):(\d+):(\d+): error: ISO C\+\+ forbids forward references', p.stderr, re.M)
    for f, l, c in set(enums):
        if not os.path.isabs(f):
            f = os.path.join(SCR, 'tree', f)
        if os.path.islink(f):
            real = os.path.realpath(f); os.unlink(f); shutil.copy(real, f)
        lines = open(f, errors='replace').read().split('\n')
        if ';' in lines[int(l)-1] and ':' not in lines[int(l)-1]:
            lines[int(l)-1] = lines[int(l)-1].replace(';', ' : int;', 1)
            open(f, 'w').write('\n'.join(lines))
    if enums:
        print("enum", len(enums)); continue
    if not errs:
        print(p.stderr[-4000:] if p.returncode else "clean")
        break
    # pro Datei nur den ersten Fehler pro Zeile, von hinten nach vorne einfuegen
    byfile = {}
    for f, l, c in errs:
        byfile.setdefault(f, []).append((int(l), int(c)))
    for f, poss in byfile.items():
        if not os.path.isabs(f):
            f = os.path.join(SCR, 'tree', f)   # Pfade sind relativ zum Spiegel, nie zum Repo
        if os.path.islink(f):
            real = os.path.realpath(f); os.unlink(f); shutil.copy(real, f)
        lines = open(f, errors='replace').read().split('\n')
        for l, c in sorted(set(poss), reverse=True):
            s = lines[l-1]
            lines[l-1] = s[:c-1] + 'this->' + s[c-1:]
        open(f, 'w').write('\n'.join(lines))
    print("iter", it, {k: len(v) for k, v in byfile.items()})
