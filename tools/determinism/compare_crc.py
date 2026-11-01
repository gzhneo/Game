#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Desync-Diagnose.

Vergleicht zwei Dateien und benennt die erste Abweichung:

  * **Zustandsberichte** (`DesyncReport-<Frame>.txt`, geschrieben bei einem Desync oder per
    `-crcReport <Datei> <Frame>`): Kopf, Subsysteme und jedes Objekt einzeln. Ausgegeben wird
    das erste abweichende Subsystem bzw. Objekt mit Vorlage, Pruefsumme und Position.
  * **CRC-Protokolle** (`-crcLog`): der erste Frame, in dem sich die Gesamtpruefsumme
    unterscheidet -- damit weiss man, mit welchem `-crcReport <Frame>` man die Berichte
    aufnimmt.

Aufruf:
    python tools/determinism/compare_crc.py A.txt B.txt
    python tools/determinism/compare_crc.py lauf1.crc lauf2.crc

Exit-Code 0 = gleich, 1 = Abweichung gefunden, 2 = Aufruffehler.
"""
import re
import sys

CRC_LINE = re.compile(r"^(\d+) ([0-9A-Fa-f]{8})$")


def read_report(path):
    """Liest einen Zustandsbericht: (Kopfzeilen, Subsysteme, Objekte in Reihenfolge)."""
    header, subs, objects = {}, [], []
    with open(path, "r", encoding="latin-1", errors="replace") as f:
        for raw in f:
            line = raw.strip()
            if not line:
                continue
            parts = line.split()
            tag = parts[0]
            if tag == "SUB" and len(parts) >= 3:
                subs.append((parts[1], parts[2]))
            elif tag == "OBJ" and len(parts) >= 4:
                # OBJ <id> <template> <crc> pos x y z
                objects.append({
                    "id": parts[1],
                    "template": parts[2],
                    "crc": parts[3],
                    "rest": " ".join(parts[4:]),
                    "line": line,
                })
            elif len(parts) >= 2:
                # Kopfzeilen koennen Leerzeichen enthalten (Kartenname)
                header.setdefault(tag, " ".join(parts[1:]))
    return header, subs, objects


def read_crc_log(path):
    """Liest ein CRC-Protokoll: Liste (Frame, CRC) plus sonstige Zeilen."""
    crcs, other = [], []
    with open(path, "r", encoding="latin-1", errors="replace") as f:
        for raw in f:
            line = raw.strip()
            if not line:
                continue
            m = CRC_LINE.match(line)
            if m:
                crcs.append((int(m.group(1)), m.group(2).upper()))
            else:
                other.append(line)
    return crcs, other


def looks_like_report(path):
    with open(path, "r", encoding="latin-1", errors="replace") as f:
        return f.readline().startswith("DESYNC-REPORT")


def compare_reports(a_path, b_path):
    head_a, subs_a, objs_a = read_report(a_path)
    head_b, subs_b, objs_b = read_report(b_path)

    print(f"A: Frame {head_a.get('FRAME','?')}, CRC {head_a.get('CRC','?')}, "
          f"Karte {head_a.get('MAP','?')}, Seed {head_a.get('SEED','?')}")
    print(f"B: Frame {head_b.get('FRAME','?')}, CRC {head_b.get('CRC','?')}, "
          f"Karte {head_b.get('MAP','?')}, Seed {head_b.get('SEED','?')}")

    if head_a.get("FRAME") != head_b.get("FRAME"):
        print("FEHLER: verschiedene Frames -- Berichte muessen aus demselben Logik-Frame stammen")
        return 2
    if head_a.get("CRC") == head_b.get("CRC"):
        print("Gesamtpruefsumme identisch -- kein Desync in diesem Frame")
        return 0

    found = False
    if head_a.get("SEED") != head_b.get("SEED"):
        print(f"ABWEICHUNG Zufalls-Seed: A {head_a.get('SEED')} != B {head_b.get('SEED')}")
        found = True

    # Nach Namen vergleichen, nicht paarweise nach Position: fehlt in einem Bericht eine
    # SUB-Zeile (anderer Build, abgebrochener Bericht), verschoeben sich sonst alle folgenden
    # Paare und keine einzige Abweichung faellt auf.
    map_a = dict(subs_a)
    map_b = dict(subs_b)
    for name in [n for n, _ in subs_a] + [n for n, _ in subs_b if n not in map_a]:
        crc_a, crc_b = map_a.get(name), map_b.get(name)
        if crc_a is None or crc_b is None:
            print(f"ABWEICHUNG Subsystem {name}: nur in {'A' if crc_b is None else 'B'} vorhanden")
            found = True
        elif crc_a != crc_b:
            print(f"ABWEICHUNG Subsystem {name}: A {crc_a} != B {crc_b}")
            found = True

    by_id_b = {o["id"]: o for o in objs_b}
    only_in_a = [o for o in objs_a if o["id"] not in by_id_b]
    ids_a = {o["id"] for o in objs_a}
    only_in_b = [o for o in objs_b if o["id"] not in ids_a]

    if only_in_a or only_in_b:
        found = True
        print(f"Objektbestand unterschiedlich: nur in A {len(only_in_a)}, nur in B {len(only_in_b)}")
        for o in only_in_a[:5]:
            print(f"  nur A: Objekt {o['id']} ({o['template']})")
        for o in only_in_b[:5]:
            print(f"  nur B: Objekt {o['id']} ({o['template']})")

    differing = [(a, by_id_b[a["id"]]) for a in objs_a
                 if a["id"] in by_id_b and a["crc"] != by_id_b[a["id"]]["crc"]]
    if differing:
        found = True
        print(f"{len(differing)} Objekte mit abweichender Pruefsumme, erste 10 "
              f"(die Listenreihenfolge ist die Reihenfolge im Spiel-CRC):")
        for a, b in differing[:10]:
            print(f"  Objekt {a['id']} ({a['template']}): A {a['crc']} {a['rest']}")
            print(f"  {'':>{len(a['id']) + 8}} B {b['crc']} {b['rest']}")
        print(f"ERSTE ABWEICHUNG: Objekt {differing[0][0]['id']} ({differing[0][0]['template']})")

    if not found:
        print("Gesamtpruefsummen unterscheiden sich, aber kein Einzelteil -- "
              "Verdacht auf einen Zustand ausserhalb von Objekten und Subsystemen")
    return 1


def compare_crc_logs(a_path, b_path):
    crcs_a, _ = read_crc_log(a_path)
    crcs_b, _ = read_crc_log(b_path)
    print(f"A: {len(crcs_a)} CRC-Werte, B: {len(crcs_b)}")
    for (frame_a, crc_a), (frame_b, crc_b) in zip(crcs_a, crcs_b):
        if frame_a != frame_b:
            print(f"FEHLER: Frames laufen auseinander ({frame_a} gegen {frame_b})")
            return 2
        if crc_a != crc_b:
            print(f"ERSTE ABWEICHUNG bei Frame {frame_a}: A {crc_a} != B {crc_b}")
            print(f"Naechster Schritt: beide Laeufe mit  -crcReport bericht.txt {frame_a}  wiederholen "
                  f"und die Berichte hier vergleichen.")
            return 1
    if len(crcs_a) != len(crcs_b):
        print(f"Gemeinsamer Abschnitt identisch, aber unterschiedlich lang "
              f"({len(crcs_a)} gegen {len(crcs_b)} Werte)")
        return 1
    print("CRC-Verlauf identisch")
    return 0


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    a_path, b_path = argv[1], argv[2]
    try:
        if looks_like_report(a_path) and looks_like_report(b_path):
            return compare_reports(a_path, b_path)
        return compare_crc_logs(a_path, b_path)
    except OSError as e:
        print(f"FEHLER: {e}")
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
