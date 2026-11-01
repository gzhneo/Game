#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Aliase fuer Systemheader, die der Quelltext anders schreibt als MinGW sie ablegt.

Windows-Includes sind case-insensitiv, Linux nicht: der Bestand bindet <EXCPT.H> und
<TCHAR.H> gross ein, MinGW legt excpt.h und tchar.h ab. Das Gegenstueck fuer die
projekteigenen Header macht tools/syntaxcheck/mkalias.py.

Aufruf:  python tools/linuxtests/sysalias.py <Quellbaum> [<Quellbaum> ...] <Zielordner>

Seit Phase 7 der Angleichung liegt ein Teil der Quellen unter Code/ statt im Baum; run.sh gibt
deshalb beide Quellen an (Code/ und <Baum>/Code).
"""
import os, re, sys

MINGW = ("/usr/share/mingw-w64/include", "/usr/x86_64-w64-mingw32/include")
INC = re.compile(r'#\s*include\s*<([^>]+)>')


def main():
    srcs, dst = sys.argv[1:-1], sys.argv[-1]
    have = {}
    for d in MINGW:
        for root, _, files in os.walk(d):
            for f in files:
                have.setdefault(f.lower(), os.path.join(root, f))
    spellings = set()
    for root, _, files in (w for src in srcs for w in os.walk(src)):
        for f in files:
            if not f.lower().endswith((".h", ".hpp", ".cpp", ".inl")):
                continue
            try:
                text = open(os.path.join(root, f), errors="replace").read()
            except OSError:
                continue
            spellings.update(m.group(1).replace("\\", "/") for m in INC.finditer(text))
    os.makedirs(dst, exist_ok=True)
    made = 0
    for sp in sorted(spellings):
        if "/" in sp:
            continue
        real = have.get(sp.lower())
        if real and os.path.basename(real) != sp:
            link = os.path.join(dst, sp)
            if not os.path.lexists(link):
                os.symlink(real, link)
                made += 1
    print("Systemheader-Aliase: %d" % made, file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
