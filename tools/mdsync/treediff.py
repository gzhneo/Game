#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Pruefung der Spiel-Ordner (Phase 8 Schritt 7, Entscheidung P8-8).

Seit Phase 8 Schritt 7 (2026-09-23) liegt der gesamte Code einmal unter Code/. Je Spiel bleibt nur ein
kleiner Ordner (Code/Games/Generals, Code/Games/ZeroHour; bis 2026-10-13 Generals/Spiel und
GeneralsMD/Spiel im Hauptverzeichnis, TODO Nr. 102) mit genau drei Dateien:

    CMakeLists.txt    Parameter des Spiels (GAME_SUFFIX, GAME_EXE_NAME, ...)
    GameEdition.cpp   legt das Spiel fest (getGameEdition())
    Generals.ico      Programmsymbol (RTS.RC)

(Bis 2026-10-02 lag dort auch WorldBuilder.bmp, das Startbild des alten WorldBuilder; der ist entfernt.)

Die Pruefung haelt das so: Jede weitere Datei in einem Spiel-Ordner ist ein Fehler, ebenso ein
wiederbelebter Ordner Generals/ oder GeneralsMD/ im Hauptverzeichnis (alter Baum <Baum>/Code oder
alter Spiel-Ordner <Baum>/Spiel). Der Spiel-Ordner ist Suchpfad des Spiels
(Symbol); ein dort abgelegter Header wuerde also womoeglich einen unter Code/ verdecken, und
neuer Code gehoert ohnehin unter Code/.

Frueher (bis Phase 8 Schritt 6) verglich das Werkzeug Generals/Code gegen GeneralsMD/Code und lieferte die
Kennzahl der Angleichung; beides hat keinen Gegenstand mehr (Fassung davor in der Git-Historie).

Aufruf:  python tools/mdsync/treediff.py              pruefen (Rueckgabe 0 gut, 1 Befund)
         python tools/mdsync/treediff.py --overlap    dasselbe (Name aus der Zeit der Baeume; tools/gate.sh)
Liest nur, schreibt nichts.
"""
import argparse, os, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SPIELE = ("Code/Games/Generals", "Code/Games/ZeroHour")
ALTE_BAEUME = ("Generals", "GeneralsMD")
ERLAUBT = {"cmakelists.txt", "gameedition.cpp", "generals.ico"}


def befunde():
    out = []
    for name in SPIELE:
        spiel = os.path.join(ROOT, *name.split("/"))
        if not os.path.isdir(spiel):
            out.append(f"{name} fehlt")
            continue
        gefunden = set()
        for dp, dn, fn in os.walk(spiel):
            for d in dn:
                out.append(f"Unterordner {os.path.relpath(os.path.join(dp, d), ROOT)} (Spiel-Ordner ist flach)")
            for f in fn:
                if f.lower() in ERLAUBT and dp == spiel:
                    gefunden.add(f.lower())
                else:
                    out.append(f"unerwartete Datei {os.path.relpath(os.path.join(dp, f), ROOT)}")
            dn[:] = []
        for f in sorted(ERLAUBT - gefunden):
            out.append(f"{name}: {f} fehlt")
    for baum in ALTE_BAEUME:
        if os.path.exists(os.path.join(ROOT, baum)):
            out.append(f"{baum}/ gibt es wieder im Hauptverzeichnis (Code gehoert unter Code/, die Spiel-Ordner "
                       "liegen unter Code/Games/)")
    return [b.replace("\\", "/") for b in out]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--overlap", action="store_true", help="wie ohne Schalter (fuer tools/gate.sh)")
    ap.parse_args()
    b = befunde()
    for z in b:
        print(z)
    if b:
        print(f"{len(b)} Befunde in den Spiel-Ordnern")
        return 1
    print(f"Spiel-Ordner sauber: {', '.join(SPIELE)} mit je {len(ERLAUBT)} Dateien, "
          "kein Generals/ oder GeneralsMD/ im Hauptverzeichnis")
    return 0


if __name__ == "__main__":
    sys.exit(main())
