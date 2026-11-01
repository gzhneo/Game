#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Replay-CRC einer LAN-Partie, auch ueber das Relais (nur Windows).

Spielt mit tools/netz/lan_lokal.py --auto eine Partie zweier Instanzen (wahlweise --relais: jeder Weg ueber das
Relais eines lokalen Treffpunkt-Servers), schreibt dabei je Instanz den CRC-Verlauf jedes Logik-Frames
(-crcLog, -crcLogInterval 1), spielt danach das Replay der ersten Instanz mit dem Runner ab (-runner, derselbe
Schalter) und vergleicht die Verlaeufe Frame fuer Frame: Instanz 1 gegen Instanz 2 (Lockstep) und Instanz 1 gegen die
Wiedergabe (Determinismus). Exit-Code 0, wenn alle gemeinsamen Frames gleich sind.

Der Vergleich der Wiedergabe mit den im Replay aufgezeichneten CRCs (Zeile "DESYNC frame N" und Exit-Code 2 des
Spiels) taugt fuer Mehrspieler-Replays nicht: seine Warteschlange liegt um zwei Frames daneben.
Massgeblich ist deshalb dieser Vergleich.

  python tools/netz/lan_replay.py --exe build-x64/GeneralsMD/Code/Main/Debug/GZHNeoZH.exe
  python tools/netz/lan_replay.py --exe ... --relais
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "determinism"))
sys.path.insert(0, os.path.join(HERE, "..", "abnahme"))


def crc_verlauf(pfad):
    werte = {}
    with open(pfad, encoding="latin-1") as f:
        for zeile in f:
            teile = zeile.split()
            if len(teile) == 2 and teile[0].isdigit() and len(teile[1]) == 8:
                werte[int(teile[0])] = teile[1]
    return werte


def vergleiche(name_a, a, name_b, b):
    gemeinsam = sorted(set(a) & set(b))
    falsch = [f for f in gemeinsam if a[f] != b[f]]
    print(f"{name_a} gegen {name_b}: {len(gemeinsam)} gemeinsame Frames, {len(falsch)} Abweichungen"
          + (f", erste bei Frame {falsch[0]}" if falsch else ""))
    return not falsch and len(gemeinsam) > 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--exe", required=True, help="GZHNeo.exe oder GZHNeoZH.exe, Debug-Bau (die Netzprotokolle gibt es nur dort)")
    p.add_argument("--relais", action="store_true", help="jeder Weg ueber das Relais eines lokalen Treffpunkt-Servers")
    p.add_argument("--out", default="render-out/lan_replay", help="Zielordner fuer Protokolle und Verlaeufe")
    a = p.parse_args()
    import runner  # noqa: E402  (tools/determinism)

    exe = os.path.abspath(a.exe)
    out = os.path.abspath(a.out)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    live = os.path.join(out, "live{i}.crc")
    befehl = [sys.executable, os.path.join(HERE, "lan_lokal.py"), "--exe", exe, "--anzahl", "2", "--auto"]
    if a.relais:
        befehl.append("--relais")
    befehl += ["--", "-crcLog", live, "-crcLogInterval", "1"]
    with open(os.path.join(out, "lan.log"), "w", encoding="utf-8") as log:
        rc = subprocess.call(befehl, stdout=log, stderr=subprocess.STDOUT)
    print(f"LAN-Partie: lan_lokal.py Exit-Code {rc} (Protokoll {out}\\lan.log)")

    ordner = os.path.join("GZH Neo", "Generals" if os.path.basename(exe).lower() == "gzhneo.exe" else "Zero Hour")
    quelle = os.path.expanduser(os.path.join("~", "Documents", ordner, "Replays", "00000000_local1.rep"))
    rep = os.path.join(out, "instanz1.rep")
    shutil.copy(quelle, rep)
    wieder = os.path.join(out, "wiedergabe.crc")
    code, sekunden, abstuerze = runner.run_game(
        exe, ["-runner", "-replay", rep, "-crcLog", wieder, "-crcLogInterval", "1"], 900, ["-ignoreAsserts"])
    print(f"Wiedergabe: Exit-Code {code} (2 = Warteschlangenvergleich, siehe oben), {sekunden:.0f} s, Absturzberichte {len(abstuerze or [])}")

    v1 = crc_verlauf(os.path.join(out, "live1.crc"))
    v2 = crc_verlauf(os.path.join(out, "live2.crc"))
    vw = crc_verlauf(wieder)
    ok = vergleiche("Instanz 1", v1, "Instanz 2", v2)
    ok = vergleiche("Instanz 1", v1, "Wiedergabe", vw) and ok
    print("GLEICH" if ok else "ABWEICHUNG")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
