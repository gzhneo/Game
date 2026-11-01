#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Schichtpruefung unter Code/.

  python3 tools/schichten/pruefen.py            pruefen (Rueckgabe 0: keine neue Abweichung)
  python3 tools/schichten/pruefen.py --liste    alle Abweichungen ausgeben, auch die erlaubten

Schichten von unten nach oben; eine Include-Zeile darf nur in dieselbe oder eine tiefere Schicht zeigen:
  0 Core (Code/Core: EngineCore, WWLib, WWMath, WWDebug, WWSaveLoad, Lib, Compression, Monocypher)
  1 Renderer (Code/Renderer)
  2 GameEngine (Code/GameEngine; innerhalb dieser Schicht wird nicht geprueft)
  3 Platform (Code/Platform)
  4 App, Games, Tools, Tests (oben, binden alles ein)

Jede Zeile `#include "..."` in .cpp/.c/.h/.hpp/.inl unter Code/ (Latin-1, ohne Kommentare) wird auf eine Datei im
Baum aufgeloest: relativ zur Datei, sonst ueber die Suchpfade der Schichten (Bibliotheksordner, wie in den
CMake-Listen). Was nicht aufloest (Systemkoepfe), zaehlt nicht.

Bekannte Abweichungen stehen mit Grund in tools/schichten/ausnahmen.txt (Datei|Ziel|Grund, Pfade relativ zu
Code/). Neue Abweichungen lassen die Pruefung scheitern; eine Ausnahme, die nicht mehr vorkommt, ebenfalls
(dann gehoert sie aus der Liste gestrichen).
"""
import os
import re
import subprocess
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HIER, "..", ".."))

SCHICHTEN = (("Core/", 0, "Core"), ("Renderer/", 1, "Renderer"), ("GameEngine/", 2, "GameEngine"),
             ("Platform/", 3, "Platform"), ("App/", 4, "App"), ("Games/", 4, "Games"), ("Tools/", 4, "Tools"),
             ("Tests/", 4, "Tests"))
# Suchpfade in der Reihenfolge der Build-Listen (relativ zu Code/); dazu der Ordner der Datei.
SUCHPFADE = ("GameEngine", "Core/EngineCore", "Platform", "Core", "Renderer", "Renderer/WW3D2", "Renderer/WW3D2/Animation", "Renderer/WW3D2/Buffers", "Renderer/WW3D2/Effects", "Renderer/WW3D2/Material", "Renderer/WW3D2/Mesh", "Renderer/WW3D2/Render2D", "Renderer/WW3D2/Scene", "Renderer/WW3D2/System", "Core/WWLib",
             "Core/WWMath", "Core/WWDebug", "Core/WWSaveLoad", "Core/Compression", "App")
ENDUNGEN = (".cpp", ".c", ".h", ".hpp", ".inl", ".cc")
INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"')


def schicht(rel):
    for praefix, stufe, name in SCHICHTEN:
        if rel.startswith(praefix):
            return stufe, name
    return 4, rel.split("/")[0]


def ohne_kommentare(text):
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def dateien():
    aus = subprocess.run(["git", "ls-files", "Code"], cwd=ROOT, capture_output=True, text=True).stdout.split("\n")
    return [f[len("Code/"):] for f in aus if f.startswith("Code/")]


def main():
    liste = "--liste" in sys.argv[1:]
    alle = dateien()
    klein = {}
    for f in alle:
        klein.setdefault(f.lower(), f)

    def finde(rel):
        rel = os.path.normpath(rel).replace(os.sep, "/")
        return klein.get(rel.lower())

    abweichungen = []
    for f in alle:
        if not f.lower().endswith(ENDUNGEN):
            continue
        with open(os.path.join(ROOT, "Code", f), encoding="latin-1") as h:
            text = ohne_kommentare(h.read())
        stufe_von, name_von = schicht(f)
        for zeile in text.split("\n"):
            m = INCLUDE.match(zeile)
            if not m:
                continue
            inc = m.group(1).replace("\\", "/")
            ziel = finde(os.path.join(os.path.dirname(f), inc))
            if ziel is None:
                for sp in SUCHPFADE:
                    ziel = finde(os.path.join(sp, inc))
                    if ziel:
                        break
            if ziel is None:
                continue
            stufe_nach, name_nach = schicht(ziel)
            if stufe_nach > stufe_von:
                abweichungen.append((f, ziel, "%s -> %s" % (name_von, name_nach)))

    erlaubt = {}
    with open(os.path.join(HIER, "ausnahmen.txt"), encoding="utf-8") as h:
        for zeile in h:
            zeile = zeile.strip()
            if not zeile or zeile.startswith("#"):
                continue
            datei, ziel, grund = zeile.split("|", 2)
            erlaubt[(datei.lower(), ziel.lower())] = grund
    gesehen, neu = set(), []
    for f, ziel, richtung in abweichungen:
        schluessel = (f.lower(), ziel.lower())
        if schluessel in erlaubt:
            gesehen.add(schluessel)
            if liste:
                print("erlaubt  %s -> %s (%s; %s)" % (f, ziel, richtung, erlaubt[schluessel]))
        else:
            neu.append((f, ziel, richtung))
    veraltet = [k for k in erlaubt if k not in gesehen]
    for f, ziel, richtung in neu:
        print("NEU      %s -> %s (%s)" % (f, ziel, richtung))
    for f, ziel in veraltet:
        print("VERALTET %s -> %s (kommt nicht mehr vor, aus ausnahmen.txt streichen)" % (f, ziel))
    print("Schichten: %d Abweichungen, davon %d erlaubt, %d neu, %d Ausnahmen veraltet"
          % (len(abweichungen), len(abweichungen) - len(neu), len(neu), len(veraltet)))
    return 1 if (neu or veraltet) else 0


if __name__ == "__main__":
    sys.exit(main())
