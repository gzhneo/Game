#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Check all .big archives against the rules of the archives_in reader without starting the game.

The reader (Engine::readBigArchiveDirectory, Code/Core/EngineCore/Engine/BigArchive.cpp) rejects an archives_in when
the header is not "BIGF", the entry count does not fit into the file, a name is longer than 259 characters or ends
without a terminator, or an entry lies behind the end of the file. A rejected archives_in is missing for the game
altogether. This tool reads every archives_in by the same rules and reports
  REJECTED  the game would not load the archives_in
Expected for EA archives, texture tiers and released mods: none of these.

Besides, every .tga in the archives is checked against the header check of the TGA reader (Check_Header in
Code/Core/WWLib/TARGA.CPP) and the RLE packets against the picture size:
  TGA REJECTED  the game would not load the texture (replacement texture)

Usage:
  python tools/abnahme/big_check.py                both asset folders (as in tools/render/map_props.py), recursively
  python tools/abnahme/big_check.py <folder or file.big> ...
Returns 0 when no archives_in is rejected, otherwise 1.
"""
import os
import struct
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "render"))
import map_props  # noqa: E402  (paths of the asset folders)

MAX_NAME = 259
MIN_ENTRY = 9


def check_archive(path):
    """Error text or None; plus the number of entries."""
    file_size = os.path.getsize(path)
    with open(path, "rb") as f:
        header = f.read(16)
        if len(header) < 16:
            return "header truncated", 0
        if header[:4] != b"BIGF":
            return "no BIGF header (%r)" % header[:4], 0
        count, = struct.unpack(">I", header[8:12])
        if count > (file_size - 16) // MIN_ENTRY:
            return "%d entries do not fit into %d bytes" % (count, file_size), 0
        data = f.read()
    p = 0
    for i in range(count):
        if p + 8 > len(data):
            return "directory truncated at entry %d" % i, i
        off, size = struct.unpack(">II", data[p:p + 8]); p += 8
        end = data.find(b"\0", p, p + MAX_NAME + 1)
        if end < 0:
            if len(data) - p <= MAX_NAME:
                return "directory truncated in the name of entry %d" % i, i
            return "name of entry %d longer than %d characters" % (i, MAX_NAME), i
        name = data[p:end].decode("latin-1"); p = end + 1
        if off + size > file_size:
            return "entry %d (%s) lies behind the end of the file: %d + %d > %d" % (i, name, off, size, file_size), i
        if name.lower().endswith(".tga"):
            TGAS.append((path, name, off, size))
    return None, count


TGAS = []


def check_tga(data):
    """Error text or None, as TARGA.CPP Check_Header and DecodeImage."""
    if len(data) < 26:
        return "shorter than the TGA 2 footer (26 bytes)"
    idlen, cmaptype, itype, cstart, clen, cdepth, _, _, w, h, pdepth, _ = struct.unpack("<BBBhhBhhhhBB", data[:18])
    if not (0 < w <= 16384 and 0 < h <= 16384):
        return "size %dx%d" % (w, h)
    if pdepth not in (8, 15, 16, 24, 32):
        return "pixel depth %d" % pdepth
    if cmaptype == 1:
        if cdepth not in (15, 16, 24, 32):
            return "palette depth %d" % cdepth
        if cstart < 0 or clen < 0 or cstart + clen > 256:
            return "palette %d+%d entries" % (cstart, clen)
    elif cmaptype != 0:
        return "palette type %d" % cmaptype
    if itype in (9, 10):
        p = 18 + idlen + (clen * (cdepth >> 3) if cmaptype == 1 else 0)
        depth = (pdepth + 7) >> 3
        rest = w * h
        while rest > 0:
            if p >= len(data):
                return "RLE data truncated"
            n = (data[p] & 0x7F) + 1
            if n > rest:
                return "RLE packet beyond the end of the picture"
            p += 1 + (depth if data[p] & 0x80 else n * depth)
            rest -= n
    return None


def archives_in(targets):
    for target in targets:
        if os.path.isfile(target):
            yield target
            continue
        for root, _, files in os.walk(target, followlinks=True):
            for d in sorted(files):
                if d.lower().endswith(".big"):
                    yield os.path.join(root, d)


def main():
    targets = sys.argv[1:] or sorted({os.path.dirname(p) for p in map_props.ARCHIVES.values() if p})
    if not targets:
        sys.exit("no asset folders: give them as arguments or set GZH_ASSETS_GENERALS/GZH_ASSETS_ZH")
    count = rejected = entries = 0
    for z in targets:
        if not os.path.exists(z):
            print("MISSING %s" % z)
    for path in archives_in([z for z in targets if os.path.exists(z)]):
        count += 1
        error, n = check_archive(path)
        entries += n
        if error:
            rejected += 1
            print("REJECTED %s: %s" % (path, error))
    tga_rejected = 0
    for path, name, off, size in TGAS:
        with open(path, "rb") as f:
            f.seek(off)
            error = check_tga(f.read(size))
        if error:
            tga_rejected += 1
            print("TGA REJECTED %s: %s: %s" % (path, name, error))
    print("%d archives, %d entries, %d rejected; %d TGA, %d rejected" % (count, entries, rejected, len(TGAS), tga_rejected))
    return 1 if rejected or tga_rejected else 0


if __name__ == "__main__":
    sys.exit(main())
