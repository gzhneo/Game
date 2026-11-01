#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Bestand der Texturen in den Spielarchiven.

Liest die .big-Archive eines Asset-Verzeichnisses nur lesend und gibt die Tabellen aus Abschnitt 2
des Plans aus: Texturen je Archiv (Anzahl, Groesse, Formate, Kantenlaengen), nach Verwendung
(Modell, Oberflaeche, Gelaende, Partikel, Kartenvorschau) und die Zero-Hour-Texturen, die nur in
halber Aufloesung vorliegen, obwohl die Generals-Basisarchive die volle Fassung haben (2.3).

Nur Python-Standardbibliothek. Das Modul wird auch von build_texture_tiers.py benutzt (BIG-Leser,
wirksamer Bestand, Kopfdaten von DDS und TGA).

Aufruf:
    python tools/assets/texture_inventory.py                    # Generals, GZH_ASSETS_GENERALS
    python tools/assets/texture_inventory.py --game zh          # Zero Hour samt ZH_Generals
    python tools/assets/texture_inventory.py --game zh --json bestand.json

Ladereihenfolge wie im Spiel (Win32BIGFileSystem::init): alle *.big unterhalb des Verzeichnisses,
rekursiv, ohne Ruecksicht auf Gross-/Kleinschreibung sortiert, der erste Treffer einer Datei gewinnt.
Der Ordner Texturstufen (HD-12) wird wie im Spiel ausgelassen.
"""
import argparse
import json
import os
import re
import struct
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import pfade  # noqa: E402


def default_dir(game):
    """Asset-Kopie des Spiels aus GZH_ASSETS_GENERALS bzw. GZH_ASSETS_ZH."""
    return pfade.assets(game, hinweis="oder --dir")
TIER_DIR = "texturstufen"	# HD-12: wird von der rekursiven Suche ausgenommen (Phase 2)
MB = 1024.0 * 1024.0


# --- BIG-Archiv -----------------------------------------------------------------------------------
# "BIGF", u32LE Gesamtgroesse, u32BE Anzahl, u32BE Ende des Verzeichnisses, dann je Eintrag
# u32BE Offset, u32BE Groesse, NUL-terminierter Name (Pfadtrenner Backslash).
class BigArchive:
    def __init__(self, path):
        self.path = path
        self.entries = {}	# kleingeschriebener Name -> (Offset, Groesse, Originalname)
        with open(path, "rb") as f:
            head = f.read(16)
            if head[:4] not in (b"BIGF", b"BIG4"):
                raise ValueError(f"{path}: kein BIG-Archiv")
            num, dir_end = struct.unpack(">II", head[8:16])
            directory = f.read(max(dir_end, 16) + 4096)
        pos = 0
        for _ in range(num):
            off, size = struct.unpack(">II", directory[pos:pos + 8])
            pos += 8
            end = directory.index(b"\0", pos)
            name = directory[pos:end].decode("latin-1")
            pos = end + 1
            self.entries[name.lower().replace("/", "\\")] = (off, size, name)

    def read(self, name):
        off, size, _ = self.entries[name.lower().replace("/", "\\")]
        with open(self.path, "rb") as f:
            f.seek(off)
            return f.read(size)

    def read_head(self, name, count):
        off, size, _ = self.entries[name.lower().replace("/", "\\")]
        with open(self.path, "rb") as f:
            f.seek(off)
            return f.read(min(count, size))


def find_archives(root):
    """*.big unterhalb von root in der Reihenfolge des Spiels (relativ, sortiert ohne Gross-/Klein)."""
    found = []
    for dirpath, dirnames, filenames in os.walk(root):
        rel_dir = os.path.relpath(dirpath, root)
        if rel_dir == ".":
            dirnames[:] = [d for d in dirnames if d.lower() != TIER_DIR]
        for fn in filenames:
            if fn.lower().endswith(".big"):
                rel = fn if rel_dir == "." else os.path.join(rel_dir, fn)
                found.append(rel)
    return sorted(found, key=lambda p: p.lower())


def effective_files(root, archives=None):
    """Name (klein, Backslash) -> (Archiv-Pfad relativ, BigArchive). Erster Treffer gewinnt."""
    result = {}
    opened = {}
    for rel in archives if archives is not None else find_archives(root):
        big = BigArchive(os.path.join(root, rel))
        opened[rel] = big
        for name in big.entries:
            if name not in result:
                result[name] = (rel, big)
    return result, opened


# --- Kopfdaten ------------------------------------------------------------------------------------
def texture_header(data, name):
    """(Format, Breite, Hoehe, Mip-Stufen) aus den ersten 128 Bytes einer DDS- oder TGA-Datei."""
    if name.endswith(".dds"):
        if len(data) < 128 or data[:4] != b"DDS ":
            return ("DDS?", 0, 0, 0)
        height, width = struct.unpack("<II", data[12:20])
        mips = struct.unpack("<I", data[28:32])[0] or 1
        pf_flags = struct.unpack("<I", data[80:84])[0]
        if pf_flags & 0x4:
            fmt = data[84:88].decode("latin-1")
        else:
            bits = struct.unpack("<I", data[88:92])[0]
            fmt = f"RGB{bits}"
        return (fmt, width, height, mips)
    if name.endswith(".tga"):
        if len(data) < 18:
            return ("TGA?", 0, 0, 0)
        width, height = struct.unpack("<HH", data[12:16])
        bpp = data[16]
        return (f"TGA{bpp}", width, height, 1)
    return ("?", 0, 0, 0)


def bytes_per_texel(fmt):
    """Speicher je Texel so, wie die Engine die Textur heute laedt (Abschnitt 6.1)."""
    if fmt == "DXT1":
        return 0.5
    if fmt in ("DXT2", "DXT3", "DXT4", "DXT5"):
        return 1.0
    return 4.0	# TGA und unkomprimiertes DDS werden als 32 Bit geladen


# --- Zuordnung nach Verwendung ------------------------------------------------------------------------
def read_ini_texts(files, prefix):
    for name, (rel, big) in files.items():
        if name.startswith(prefix) and name.endswith(".ini"):
            yield name, big.read(name).decode("latin-1", "replace")


def mapped_image_textures(files):
    tex = set()
    for _, text in read_ini_texts(files, "data\\ini\\mappedimages\\"):
        for m in re.finditer(r"^\s*Texture\s*=\s*(\S+)", text, re.M | re.I):
            tex.add(m.group(1).lower())
    return tex


def particle_textures(files):
    tex = set()
    for _, text in read_ini_texts(files, "data\\ini\\particlesystem.ini"):
        for m in re.finditer(r"^\s*ParticleName\s*=\s*(\S+)", text, re.M | re.I):
            tex.add(m.group(1).lower())
    return tex


def base_name(name):
    b = name.rsplit("\\", 1)[-1]
    return b.rsplit(".", 1)[0]


def classify(name, ui, particles):
    """Art einer Textur: Oberflaeche, Gelaende, Partikel, Vorschau, Lokalisiert, Modell."""
    stem = base_name(name)
    fn = name.rsplit("\\", 1)[-1]
    if name.startswith("art\\terrain\\"):
        return "Gelaende"
    if name.startswith("maps\\") or name.startswith("data\\") and "\\maps\\" in name:
        return "Vorschau"
    if fn in ui or stem + ".tga" in ui or stem + ".dds" in ui:
        return "Oberflaeche"
    if fn in particles or stem + ".tga" in particles or stem + ".dds" in particles:
        return "Partikel"
    if name.startswith("data\\"):
        return "Lokalisiert"
    return "Modell"


def is_texture(name):
    return name.endswith(".dds") or name.endswith(".tga")


def inventory(root, game):
    files, opened = effective_files(root)
    ui = mapped_image_textures(files)
    particles = particle_textures(files)
    records = []
    for name, (rel, big) in sorted(files.items()):
        if not is_texture(name):
            continue
        head = big.read_head(name, 128)
        fmt, w, h, mips = texture_header(head, name)
        records.append({
            "name": name, "archive": rel, "format": fmt, "width": w, "height": h, "mips": mips,
            "size": big.entries[name][1], "kind": classify(name, ui, particles),
        })
    # alle Texturen je Archiv, auch verdeckte (fuer Tabelle 2.1)
    per_archive = {}
    for rel, big in opened.items():
        items = []
        for name, (off, size, _) in big.entries.items():
            if is_texture(name):
                fmt, w, h, mips = texture_header(big.read_head(name, 128), name)
                items.append((name, fmt, w, h, size))
        if items:
            per_archive[rel] = items
    halved = []
    if game == "zh":
        zh_tex = [r for r in opened if r.lower() == "textureszh.big"]
        base_tex = [r for r in opened if r.lower().endswith("zh_generals\\textures.big")]
        if zh_tex and base_tex:
            a, b = opened[zh_tex[0]], opened[base_tex[0]]
            for name in a.entries:
                if is_texture(name) and name in b.entries:
                    fa = texture_header(a.read_head(name, 128), name)
                    fb = texture_header(b.read_head(name, 128), name)
                    if fa[1] * 2 == fb[1] and fa[2] * 2 == fb[2]:
                        halved.append({"name": name, "zh": fa[1:3], "generals": fb[1:3], "format": fa[0]})
    return records, per_archive, halved


# --- Ausgabe --------------------------------------------------------------------------------------
def print_tables(records, per_archive, halved):
    print("## Texturen je Archiv (alle, auch verdeckte)\n")
    print("| Archiv | Texturen | MB | Formate | Kantenlaengen (groessere Kante) |")
    print("|---|---|---|---|---|")
    for rel, items in sorted(per_archive.items(), key=lambda kv: kv[0].lower()):
        fmts = Counter(i[1] for i in items)
        edges = Counter(max(i[2], i[3]) for i in items)
        mb = sum(i[4] for i in items) / MB
        print(f"| `{rel}` | {len(items)} | {mb:.1f} | "
              + ", ".join(f"{k} {v}" for k, v in fmts.most_common()) + " | "
              + ", ".join(f"{k}: {v}" for k, v in sorted(edges.items())) + " |")
    print("\n## Wirksamer Bestand nach Verwendung\n")
    print("| Art | Anzahl | MB Datei | Mio. Texel | Speicher wie geladen (MB, mit Mips) | Formate |")
    print("|---|---|---|---|---|---|")
    by_kind = defaultdict(list)
    for r in records:
        by_kind[r["kind"]].append(r)
    total = [0, 0, 0, 0.0]
    for kind in ("Modell", "Oberflaeche", "Gelaende", "Partikel", "Lokalisiert", "Vorschau"):
        rs = by_kind.get(kind, [])
        texel = sum(r["width"] * r["height"] for r in rs)
        mem = sum(r["width"] * r["height"] * bytes_per_texel(r["format"]) for r in rs) * 4.0 / 3.0
        size = sum(r["size"] for r in rs)
        fmts = Counter(r["format"] for r in rs)
        total[0] += len(rs); total[1] += size; total[2] += texel; total[3] += mem
        print(f"| {kind} | {len(rs)} | {size / MB:.1f} | {texel / 1e6:.1f} | {mem / MB:.0f} | "
              + ", ".join(f"{k} {v}" for k, v in fmts.most_common(4)) + " |")
    print(f"| **Summe** | **{total[0]}** | **{total[1] / MB:.1f}** | **{total[2] / 1e6:.1f}** | **{total[3] / MB:.0f}** | |")
    if halved:
        print(f"\n## Zero Hour: {len(halved)} Texturen in halber Aufloesung (Generals-Fassung doppelt so gross)\n")
        fmts = Counter(h["format"] for h in halved)
        print("Formate: " + ", ".join(f"{k} {v}" for k, v in fmts.most_common()))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", choices=("generals", "zh"), default="generals")
    ap.add_argument("--dir", help="Asset-Verzeichnis (Standard je Spiel)")
    ap.add_argument("--json", help="Bestand zusaetzlich als JSON schreiben")
    a = ap.parse_args()
    root = a.dir or default_dir(a.game)
    if not os.path.isdir(root):
        raise SystemExit(f"Asset-Verzeichnis fehlt: {root}")
    records, per_archive, halved = inventory(root, a.game)
    print(f"# Texturbestand {a.game} ({root})\n")
    print("Archive in Ladereihenfolge: " + ", ".join(f"`{r}`" for r in find_archives(root)) + "\n")
    print_tables(records, per_archive, halved)
    if a.json:
        with open(a.json, "w", encoding="ascii") as f:
            json.dump({"game": a.game, "root": root, "textures": records, "halved": halved}, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
