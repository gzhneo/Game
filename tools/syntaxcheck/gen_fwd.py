#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Harness: MSVC macht "friend class X;" fuer die normale Namenssuche sichtbar, clang nicht.
Erzeugt eine Sammel-Vorwaertsdeklaration, die per -include vorangestellt wird. Ausserdem
Gross-/Kleinschreib-Aliase fuer Systemheader (TCHAR.H, EXCPT.H, ...)."""
import os, re, sys
T = os.path.abspath(sys.argv[1]); SHIM = os.path.abspath(sys.argv[2])
names = set()
templates = set()
fr = re.compile(r'friend\s+class\s+([A-Za-z_]\w*)\s*;')
sysinc = re.compile(r'#\s*include\s*<([A-Za-z0-9_.]+)>')
sysnames = set()
templates = set()
for dp, dn, fns in os.walk(T):
    dn[:] = [d for d in dn if not os.path.islink(os.path.join(dp, d))]
    for fn in fns:
        if not fn.lower().endswith(('.h', '.cpp')): continue
        try: s = open(os.path.join(dp, fn), errors='replace').read()
        except OSError: continue
        names.update(fr.findall(s))
        for m in re.finditer(r'template\s*<[^>]*>\s*(?:class|struct)\s+([A-Za-z_]\w*)', s):
            templates.add(m.group(1))
        for m in sysinc.finditer(s):
            if m.group(1) != m.group(1).lower(): sysnames.add(m.group(1))
os.makedirs(SHIM, exist_ok=True)
with open(os.path.join(SHIM, 'fwd_friends.h'), 'w') as f:
    f.write('// erzeugt von gen_fwd.py -- nur fuer die Syntaxpruefung\n')
    for n in sorted(names - templates): f.write('class %s;\n' % n)
MINGW = '/usr/x86_64-w64-mingw32/include'
made = 0
for n in sorted(sysnames):
    src = os.path.join(MINGW, n.lower())
    dst = os.path.join(SHIM, n)
    if os.path.exists(src) and not os.path.lexists(dst):
        os.symlink(src, dst); made += 1
print("friends", len(names), "sysalias", made)
