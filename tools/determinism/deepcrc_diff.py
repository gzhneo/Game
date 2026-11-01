#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Vergleicht zwei Deep-CRC-Abzuege.

Die Abzuege entstehen mit  -crcLog <datei> -deepCRCFrames <von> <bis>  als <datei>.f<Frame>.deep,
daneben liegt je ein Index <...>.deep.idx mit Zeilen "Offset ID Name" (Objekt oder Abschnitt).

Mit zusaetzlich  -deepCRCFull  schreibt jeder Objekt-Eintrag den vollen Spielstand-Zustand (xfer()
wie beim Speichern: Physik, Locomotor, Fahrziel, KI-Zustand) statt nur des kurzen crc()-Auszugs
(Status, Matrix, Leben, Waffen). Das Dateiformat (Index "Offset ID Name", Abschnitte als Byte-
Bereiche) ist dasselbe, dieses Skript braucht dafuer keine Aenderung. So findet sich die erste verdeckte Zustandsabweichung zwischen zwei Laeufen, ohne
neuen Protokollcode im Fahrwerk. Grenze: Objekt-IDs in xfer() muessen in beiden Laeufen aus derselben
Erzeugungsreihenfolge stammen (gleiches Replay), sonst sind Abweichungen dort kein Befund.

Weil Abschnitte in beiden Abzuegen unterschiedlich lang sein koennen, wird nicht die ganze Datei
am Stueck verglichen, sondern Eintrag fuer Eintrag ueber den gemeinsamen Schluessel (ID, Name):

  python tools/determinism/deepcrc_diff.py a.f0.deep b.f0.deep [--max 20] [--bytes 64]

Ausgabe: Eintraege, die nur in einem Abzug vorkommen, und fuer jeden abweichenden Eintrag Laenge,
erste abweichende Position relativ zum Eintragsanfang und einen Hex-Ausschnitt beider Seiten.
"""
import argparse
import sys


def read_index(path):
    entries = []
    with open(path, "r", encoding="ascii", errors="replace") as f:
        for raw in f:
            parts = raw.rstrip("\n").split(" ", 2)
            if len(parts) < 3:
                continue
            entries.append((int(parts[0]), int(parts[1]), parts[2]))
    return entries


def sections(deep_path):
    data = open(deep_path, "rb").read()
    idx = read_index(deep_path + ".idx")
    result = []
    seen = {}
    for i, (off, oid, name) in enumerate(idx):
        end = idx[i + 1][0] if i + 1 < len(idx) else len(data)
        key = (oid, name)
        # gleiche Schluessel (z. B. mehrfach -1/RandomSeed) durchnummerieren
        n = seen.get(key, 0)
        seen[key] = n + 1
        result.append(((oid, name, n), data[off:end]))
    return result


def hexcut(buf, pos, count):
    start = max(0, pos - 8)
    return f"@{start}: " + buf[start:start + count].hex(" ")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--max", type=int, default=20, help="hoechstens so viele abweichende Eintraege zeigen")
    ap.add_argument("--bytes", type=int, default=48, help="Laenge des Hex-Ausschnitts")
    args = ap.parse_args()

    sa, sb = sections(args.a), sections(args.b)
    da, db = dict(sa), dict(sb)
    only_a = [k for k, _ in sa if k not in db]
    only_b = [k for k, _ in sb if k not in da]
    for k in only_a[:args.max]:
        print(f"nur in A: id {k[0]} {k[1]}")
    for k in only_b[:args.max]:
        print(f"nur in B: id {k[0]} {k[1]}")

    diffs = 0
    for key, bufa in sa:
        bufb = db.get(key)
        if bufb is None or bufa == bufb:
            continue
        diffs += 1
        if diffs <= args.max:
            pos = next((i for i in range(min(len(bufa), len(bufb))) if bufa[i] != bufb[i]), min(len(bufa), len(bufb)))
            print(f"ABWEICHUNG id {key[0]} {key[1]}: Laenge A {len(bufa)}, B {len(bufb)}, erste Abweichung bei +{pos}")
            print(f"  A {hexcut(bufa, pos, args.bytes)}")
            print(f"  B {hexcut(bufb, pos, args.bytes)}")
    print(f"{len(sa)} Eintraege in A, {len(sb)} in B; {diffs} abweichend, {len(only_a)} nur in A, {len(only_b)} nur in B")
    return 1 if diffs or only_a or only_b else 0


if __name__ == "__main__":
    sys.exit(main())
