#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Spiegel abgleichen: neu gebauten Spiegel (Quelle) in den bestehenden (Ziel) uebernehmen.

tools/syntaxcheck/setup.sh baut den Spiegel jedes Mal neu; die gepatchten Kopien bekaemen damit
neue Zeitstempel und make uebersetzte alles. Hier bleibt jede unveraenderte Datei (gleiches
Symlink-Ziel oder gleicher Inhalt) samt Zeitstempel stehen; nur Geaendertes wird ersetzt."""
import os, shutil, sys

def same(a, b):
    if os.path.islink(a) != os.path.islink(b):
        return False
    if os.path.islink(a):
        return os.readlink(a) == os.readlink(b)
    if not os.path.isfile(b):
        return False
    if os.path.getsize(a) != os.path.getsize(b):
        return False
    with open(a, 'rb') as fa, open(b, 'rb') as fb:
        return fa.read() == fb.read()

def sync(src, dst):
    changed = 0
    wanted = set()
    for dp, dns, fns in os.walk(src, followlinks=False):
        rel = os.path.relpath(dp, src)
        tdir = os.path.join(dst, rel)
        if os.path.islink(tdir) or (os.path.exists(tdir) and not os.path.isdir(tdir)):
            os.unlink(tdir)
        os.makedirs(tdir, exist_ok=True)
        wanted.add(os.path.normpath(tdir))
        for name in dns + fns:
            s = os.path.join(dp, name)
            t = os.path.join(tdir, name)
            wanted.add(os.path.normpath(t))
            if os.path.isdir(s) and not os.path.islink(s):
                continue
            if os.path.lexists(t) and same(s, t):
                continue
            if os.path.isdir(t) and not os.path.islink(t):
                shutil.rmtree(t)
            elif os.path.lexists(t):
                os.unlink(t)
            if os.path.islink(s):
                os.symlink(os.readlink(s), t)
            else:
                shutil.copy2(s, t)
            changed += 1
    for dp, dns, fns in os.walk(dst, topdown=False, followlinks=False):
        for name in fns + dns:
            p = os.path.normpath(os.path.join(dp, name))
            if p not in wanted:
                if os.path.isdir(p) and not os.path.islink(p):
                    shutil.rmtree(p)
                else:
                    os.unlink(p)
                changed += 1
    return changed

if __name__ == '__main__':
    print("Spiegel: %d Eintraege geaendert" % sync(sys.argv[1], sys.argv[2]))
