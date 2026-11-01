#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Quelllisten der Engine-Bibliotheken aus ihren CMakeLists lesen (tools/linuxtests/run.sh).

Damit gibt es fuer den Linux-Testlauf keine zweite Liste, die hinter der CMake-Fassung
zurueckbleiben koennte. Beruecksichtigt auch nachtraegliche target_sources()-Zeilen.

Seit Phase 7 der Angleichung stehen diese CMakeLists unter Code/
statt doppelt in beiden Baeumen. Der Spiegel legt beide Quellen uebereinander, die Pfade
unterhalb von tree/ bleiben deshalb dieselben -- gelesen wird weiter aus dem Spiegel.
"""
import os, re, sys

LIBS = ("Core/WWMath",
        "Core/WWLib",
        "Core/WWDebug",
        "Core/WWSaveLoad",
        # WWDownload seit 2026-09-26 entfernt (TODO 69 Runde A)
        "Core/Compression")


def sources(cmake_text):
    out = []
    # Die schliessende Klammer darf eingerueckt sein (frueher bei WWDownload, das add_library
    # stand dort eine Ebene tiefer). Ohne \s* fand die Suche das Ende der Liste nicht.
    m = re.search(r"add_library\([^\n]*\n(.*?)\n\s*\)", cmake_text, re.S)
    if m:
        out += m.group(1).split("\n")
    for extra in re.finditer(r"target_sources\([^)]*?PRIVATE(.*?)\)", cmake_text, re.S):
        out += extra.group(1).split("\n")
    return [f.strip() for f in out if f.strip() and not f.strip().startswith("#")]


def main():
    tree = sys.argv[1]
    for sub in LIBS:
        p = os.path.join(tree, sub, "CMakeLists.txt")
        if not os.path.isfile(p):
            sys.stderr.write("fehlt: %s\n" % p)
            return 1
        with open(p, errors="replace") as fh:
            for f in sources(fh.read()):
                full = os.path.join(tree, sub, f)
                if os.path.exists(full):
                    print(full)
    return 0


if __name__ == "__main__":
    sys.exit(main())
