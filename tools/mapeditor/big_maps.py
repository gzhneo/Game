# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Alle Karten (.map) aus .big-Archiven in einen Ordner kopieren; die Archive werden nur gelesen.

Aufruf: python tools/mapeditor/big_maps.py <zielordner> <ordner oder archiv.big> ...

Ordner werden rekursiv nach .big durchsucht. Jede Karte landet unter
<zielordner>/<archivname>/<pfad im archiv>. Ausgabe: je Archiv eine Zeile mit der Anzahl, am Ende
die Summe. Rueckgabewert 0, auch wenn keine Karte gefunden wurde (das steht dann im Bericht).
"""
import os
import struct
import sys


def entries(path):
    with open(path, "rb") as f:
        head = f.read(16)
        if len(head) < 16 or head[:4] not in (b"BIGF", b"BIG4"):
            return []
        count = struct.unpack(">I", head[8:12])[0]
        out = []
        for _ in range(count):
            raw = f.read(8)
            if len(raw) < 8:
                break
            offset, size = struct.unpack(">II", raw)
            name = bytearray()
            while True:
                c = f.read(1)
                if not c or c == b"\0":
                    break
                name += c
            out.append((name.decode("latin-1"), offset, size))
        return out


def archives(arg):
    if os.path.isfile(arg):
        yield arg
        return
    for folder, _dirs, files in os.walk(arg):
        for name in files:
            if name.lower().endswith(".big"):
                yield os.path.join(folder, name)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    dest = sys.argv[1]
    total = 0
    for arg in sys.argv[2:]:
        for big in sorted(set(archives(arg))):
            try:
                maps = [e for e in entries(big) if e[0].lower().endswith(".map")]
            except OSError as err:
                print(f"{big}: nicht lesbar ({err})")
                continue
            if not maps:
                continue
            base = os.path.join(dest, os.path.splitext(os.path.basename(big))[0])
            with open(big, "rb") as f:
                for name, offset, size in maps:
                    target = os.path.join(base, *name.replace("/", "\\").split("\\"))
                    os.makedirs(os.path.dirname(target), exist_ok=True)
                    f.seek(offset)
                    with open(target, "wb") as o:
                        o.write(f.read(size))
            print(f"{big}: {len(maps)} Karten")
            total += len(maps)
    print(f"zusammen {total} Karten")
    return 0


if __name__ == "__main__":
    sys.exit(main())
