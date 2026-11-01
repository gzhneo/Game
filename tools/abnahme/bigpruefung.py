#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Alle .big-Archive gegen die Regeln des Archivlesers pruefen, ohne das Spiel zu starten.

Der Leser (Engine::readBigArchiveDirectory, Code/Core/EngineCore/Engine/BigArchive.cpp) lehnt ein
Archiv ab, wenn der Kopf nicht "BIGF" ist, die Eintragszahl nicht in die Datei passt, ein Name laenger als
259 Zeichen ist oder ohne Ende abbricht, oder ein Eintrag hinter dem Dateiende liegt. Ein abgelehntes
Archiv fehlt dem Spiel ganz. Dieses Werkzeug liest jedes Archiv nach denselben Regeln und meldet
  ABGELEHNT  das Spiel wuerde das Archiv nicht laden
Erwartet fuer EA-Archive, Texturstufen und freigegebene Mods: nichts davon.

Dazu jede .tga in den Archiven gegen die Kopfpruefung des TGA-Lesers (Check_Header in
Code/Core/WWLib/TARGA.CPP) und die RLE-Pakete gegen die Bildgroesse:
  TGA ABGELEHNT  das Spiel wuerde die Textur nicht laden (Ersatztextur)

Aufruf:
  python tools/abnahme/bigpruefung.py                beide Asset-Ordner (wie tools/render/map_props.py), rekursiv
  python tools/abnahme/bigpruefung.py <Ordner|Datei> ...
Rueckgabe 0, wenn kein Archiv abgelehnt wird; sonst 1.
"""
import os
import struct
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "render"))
import map_props  # noqa: E402  (Pfade der Asset-Ordner)

MAX_NAME = 259
MIN_ENTRY = 9


def pruefe(path):
    """Fehlertext oder None; dazu die Zahl der Eintraege."""
    groesse = os.path.getsize(path)
    with open(path, "rb") as f:
        kopf = f.read(16)
        if len(kopf) < 16:
            return "Kopf abgeschnitten", 0
        if kopf[:4] != b"BIGF":
            return "kein BIGF-Kopf (%r)" % kopf[:4], 0
        anzahl, = struct.unpack(">I", kopf[8:12])
        if anzahl > (groesse - 16) // MIN_ENTRY:
            return "%d Eintraege passen nicht in %d Byte" % (anzahl, groesse), 0
        daten = f.read()
    p = 0
    for i in range(anzahl):
        if p + 8 > len(daten):
            return "Verzeichnis abgeschnitten bei Eintrag %d" % i, i
        off, size = struct.unpack(">II", daten[p:p + 8]); p += 8
        ende = daten.find(b"\0", p, p + MAX_NAME + 1)
        if ende < 0:
            if len(daten) - p <= MAX_NAME:
                return "Verzeichnis abgeschnitten im Namen von Eintrag %d" % i, i
            return "Name von Eintrag %d laenger als %d Zeichen" % (i, MAX_NAME), i
        name = daten[p:ende].decode("latin-1"); p = ende + 1
        if off + size > groesse:
            return "Eintrag %d (%s) liegt hinter dem Dateiende: %d + %d > %d" % (i, name, off, size, groesse), i
        if name.lower().endswith(".tga"):
            TGAS.append((path, name, off, size))
    return None, anzahl


TGAS = []


def pruefe_tga(daten):
    """Fehlertext oder None, wie TARGA.CPP Check_Header und DecodeImage."""
    if len(daten) < 26:
        return "kuerzer als der TGA-2-Fuss (26 Byte)"
    idlen, cmaptype, itype, cstart, clen, cdepth, _, _, w, h, pdepth, _ = struct.unpack("<BBBhhBhhhhBB", daten[:18])
    if not (0 < w <= 16384 and 0 < h <= 16384):
        return "Groesse %dx%d" % (w, h)
    if pdepth not in (8, 15, 16, 24, 32):
        return "Pixeltiefe %d" % pdepth
    if cmaptype == 1:
        if cdepth not in (15, 16, 24, 32):
            return "Palettentiefe %d" % cdepth
        if cstart < 0 or clen < 0 or cstart + clen > 256:
            return "Palette %d+%d Eintraege" % (cstart, clen)
    elif cmaptype != 0:
        return "Palettentyp %d" % cmaptype
    if itype in (9, 10):
        p = 18 + idlen + (clen * (cdepth >> 3) if cmaptype == 1 else 0)
        tiefe = (pdepth + 7) >> 3
        rest = w * h
        while rest > 0:
            if p >= len(daten):
                return "RLE-Daten abgeschnitten"
            n = (daten[p] & 0x7F) + 1
            if n > rest:
                return "RLE-Paket ueber das Bildende"
            p += 1 + (tiefe if daten[p] & 0x80 else n * tiefe)
            rest -= n
    return None


def archive(ziele):
    for ziel in ziele:
        if os.path.isfile(ziel):
            yield ziel
            continue
        for wurzel, _, dateien in os.walk(ziel, followlinks=True):
            for d in sorted(dateien):
                if d.lower().endswith(".big"):
                    yield os.path.join(wurzel, d)


def main():
    ziele = sys.argv[1:] or sorted({os.path.dirname(p) for p in map_props.ARCHIVES.values() if p})
    if not ziele:
        sys.exit("keine Asset-Ordner: als Argumente angeben oder GZH_ASSETS_GENERALS/GZH_ASSETS_ZH setzen")
    anzahl = abgelehnt = eintraege = 0
    for z in ziele:
        if not os.path.exists(z):
            print("FEHLT %s" % z)
    for pfad in archive([z for z in ziele if os.path.exists(z)]):
        anzahl += 1
        fehler, n = pruefe(pfad)
        eintraege += n
        if fehler:
            abgelehnt += 1
            print("ABGELEHNT %s: %s" % (pfad, fehler))
    tga_abgelehnt = 0
    for pfad, name, off, size in TGAS:
        with open(pfad, "rb") as f:
            f.seek(off)
            fehler = pruefe_tga(f.read(size))
        if fehler:
            tga_abgelehnt += 1
            print("TGA ABGELEHNT %s: %s: %s" % (pfad, name, fehler))
    print("%d Archive, %d Eintraege, %d abgelehnt; %d TGA, %d abgelehnt" % (anzahl, eintraege, abgelehnt, len(TGAS), tga_abgelehnt))
    return 1 if abgelehnt or tga_abgelehnt else 0


if __name__ == "__main__":
    sys.exit(main())
