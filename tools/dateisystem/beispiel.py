#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Beispielordner mit .big- und .zip-Archiven für den Vergleich der Dateisysteme.

Jedes Archiv prüft eine Eigenheit der alten Vorrangregel oder der Mod-Ränge (lose Dateien über den
Archiven des Mod-Ordners); die Soll-Listen stehen in pruefen.sh. Die Pfade liegen unter gzhtest/ und
art/textures/gzhtest_*.dds, damit das Spiel sie nicht als Spieldaten liest.

    beispiel.py <Zielordner>      legt <Ziel>/spiel, <Ziel>/mod und <Ziel>/einzel.zip an
"""
import os
import struct
import sys
import zipfile


def big(path, files):
    """files: Liste (Name im Archiv, Inhalt als bytes)."""
    header = 16 + sum(8 + len(name) + 1 for name, _ in files)
    table, data, offset = b"", b"", header
    for name, content in files:
        table += struct.pack(">II", offset, len(content)) + name.encode("ascii") + b"\0"
        data += content
        offset += len(content)
    blob = b"BIGF" + struct.pack("<I", header + len(data)) + struct.pack(">II", len(files), header) + table + data
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(blob)


def main():
    ziel = sys.argv[1]
    spiel = os.path.join(ziel, "spiel")
    mod = os.path.join(ziel, "mod")
    t = b"x"
    # Grundarchive: das alphabetisch erste gewinnt; "Data\INI\INIZH.big" kommt vor "INIZH.big",
    # löscht Zero Hour aber vor dem Suchlauf
    big(os.path.join(spiel, "INIZH.big"), [("gzhtest\\a.txt", t), ("gzhtest\\b.txt", t)])
    big(os.path.join(spiel, "Data", "INI", "INIZH.big"), [("gzhtest\\a.txt", t)])
    # "A1.big" vor "a\b.big": '1' sortiert vor '\'
    big(os.path.join(spiel, "A1.big"), [("gzhtest\\n1.txt", t)])
    big(os.path.join(spiel, "a", "b.big"), [("gzhtest\\n1.txt", t), ("gzhtest\\n2.txt", t)])
    # Ordner mit Punkt im Namen durchsucht der alte Suchlauf nicht
    big(os.path.join(spiel, "Mod.v2", "x.big"), [("gzhtest\\versteckt.txt", t)])
    # Generals-Archive unter Zero Hour: nach allen anderen
    big(os.path.join(spiel, "ZH_Generals", "INI.big"), [("gzhtest\\b.txt", t), ("gzhtest\\g.txt", t)])
    # beschädigtes Archiv: wird nicht benutzt
    with open(os.path.join(spiel, "Kaputt.big"), "wb") as f:
        f.write(b"XXXX0000")
    # Texturpaket (ab HD): das alphabetisch spätere Archiv gewinnt; ein Archiv mit anderem Inhalt wird ganz
    # abgewiesen, eines ohne Manifest bleibt draußen; ein alter Stufenordner wird nicht gelesen
    paket = os.path.join(spiel, "Texturstufen", "Paket")
    manifest = ("texturpaket.txt", b"TEXTURPAKET 1 UHD\r\nart\\textures\\gzhtest_t.dds 4 4\r\nart\\textures\\gzhtest_h.dds 4 4\r\n")
    big(os.path.join(paket, "gzh_a.big"), [manifest, ("art\\textures\\gzhtest_t.dds", t), ("Art\\Textures\\gzhtest_h.dds", t)])
    big(os.path.join(paket, "gzh_b.big"), [manifest, ("art\\textures\\gzhtest_t.dds", t)])
    big(os.path.join(paket, "schlecht.big"), [manifest, ("art\\textures\\gzhtest_s.dds", t), ("gzhtest\\schlecht.txt", t)])
    big(os.path.join(paket, "ohne.big"), [("art\\textures\\gzhtest_o.dds", t)])
    big(os.path.join(spiel, "Texturstufen", "HD", "alt.big"), [("art\\textures\\gzhtest_alt.dds", t)])
    # Mod-Ordner: über dem Paket; sein eigenes Paket liegt über seinen Archiven und losen Dateien
    big(os.path.join(mod, "m.big"), [("gzhtest\\b.txt", t), ("art\\textures\\gzhtest_t.dds", t)])
    big(os.path.join(mod, "Texturstufen", "Paket", "mt.big"), [manifest, ("art\\textures\\gzhtest_t.dds", t)])
    # ZIP im Mod-Ordner (gespeichert und Deflate); lose Dateien des Mods liegen über allen seinen Archiven
    with zipfile.ZipFile(os.path.join(mod, "z.zip"), "w") as z:
        z.writestr("gzhtest/z.txt", t, compress_type=zipfile.ZIP_STORED)
        z.writestr("gzhtest/l.txt", t * 100, compress_type=zipfile.ZIP_DEFLATED)
    os.makedirs(os.path.join(mod, "GzhTest"), exist_ok=True)
    for name in ("l.txt", "nur.txt"):
        with open(os.path.join(mod, "GzhTest", name), "wb") as f:
            f.write(t)
    # beschädigtes ZIP im Mod-Ordner: wird nicht benutzt
    with open(os.path.join(mod, "Kaputt.zip"), "wb") as f:
        f.write(b"PK\x03\x04kaputt")
    # Mod als einzelnes ZIP (-mod <Datei.zip>)
    with zipfile.ZipFile(os.path.join(ziel, "einzel.zip"), "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("gzhtest/b.txt", t)
        z.writestr("gzhtest/e.txt", t)


if __name__ == "__main__":
    main()
