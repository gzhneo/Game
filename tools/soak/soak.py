#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Dauerlauf.

Spielt ueber Stunden KI-gegen-KI-Partien mit zufaelligen Karten und Seeds, beschleunigt und ohne
Zeichnen (`-runner`), und sammelt jeden Absturz, jeden Assert und jeden Desync mit dem
zugehoerigen Replay in einem Fundordner. Gedacht fuer den Lauf ueber Nacht auf dem eigenen
Rechner -- keine CI noetig.

Beispiele:

    # zwei Stunden, 7 KI, Karten aus der Liste unten
    python tools/soak/soak.py --exe build-x64\\GeneralsMD\\Code\\Main\\Release\\GZHNeoZH.exe --hours 2

    # eine feste Karte, 20 Laeufe, ASan-Build mit Paralleltest
    python tools/soak/soak.py --exe build-asan\\...\\GZHNeoZH.exe --runs 20 ^
        --map "Tournament Desert" --extra -verifyParallel

Jeder Lauf bekommt einen eigenen Unterordner in `--out` (Standard `soak-runs`) mit Replay,
CRC-Protokoll und Prozessausgabe; nur Laeufe mit Befund bleiben erhalten (mit `--keep-all` alle).
Am Ende steht eine Zusammenfassung in `--out/fundliste.txt`.

Exit-Code 0 = keine Funde, 1 = Funde, 2 = Aufruffehler.
"""
import argparse
import os
import random
import shutil
import subprocess
import sys
import time

# Karten mit mindestens 4 Startplaetzen (Stand E-0.3; Namen wie im MapCache).
DEFAULT_MAPS = [
    "Tournament Desert", "Tournament Island", "Tournament Lake", "Tournament Tundra",
    "Dark Mountain", "Fallen Empire", "Golden Oasis", "Homeland Alliance",
    "Lone Eagle", "Rogue Agent", "Armored Fury", "Fortress Avalanche",
    "Twilight Flame", "Whiteout",
]

# Exit-Codes des Spiels (GameEngine.h): 0 ok, 2 Replay-Desync, 3 Replay/Karte nicht gefunden.
EXIT_OK, EXIT_DESYNC, EXIT_NOT_FOUND = 0, 2, 3


def crc_log_findings(path):
    """Sucht im CRC-Protokoll nach Auffaelligkeiten (DESYNC, ERROR)."""
    findings = []
    if not os.path.isfile(path):
        return ["kein CRC-Protokoll geschrieben"]
    with open(path, "r", encoding="latin-1", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("DESYNC") or line.startswith("ERROR"):
                findings.append(line)
    return findings


# E-8.1: Ein Absturz legt seit E-8.1 einen eigenen Ordner `Crashes\\JJJJ-MM-TT_HHMMSS` neben der
# Exe an (Minidump, Info.txt, EngineLog.txt, Replay). Die alten Einzeldateien kann es daneben
# weiter geben (aelterer Build, Ausnahmefilter von Windows), deshalb werden beide gesucht.
CRASH_DIR = "Crashes"
LEGACY_CRASH_FILES = ("CrashDump.dmp", "ReleaseCrashInfo.txt", "except.txt")


def crash_folders(exe_dir):
    """Namen der vorhandenen Absturzordner (E-8.1)."""
    base = os.path.join(exe_dir, CRASH_DIR)
    if not os.path.isdir(base):
        return set()
    return {name for name in os.listdir(base) if os.path.isdir(os.path.join(base, name))}


def log_findings(exe_dir, new_crashes):
    """Sucht neben der Exe nach frischen Absturzberichten und Desync-Berichten."""
    findings = []
    for name in sorted(new_crashes):
        findings.append(f"Absturzbericht {CRASH_DIR}\\{name}")
    for name in LEGACY_CRASH_FILES:
        if os.path.isfile(os.path.join(exe_dir, name)):
            findings.append(f"{name} vorhanden")
    for name in os.listdir(exe_dir):
        if name.startswith("DesyncReport-"):
            findings.append(f"{name} vorhanden")
    return findings


def clear_artifacts(exe_dir):
    """Alte Einzeldateien wegraeumen, damit ein Fund eindeutig zum Lauf gehoert.

    Der Ordner `Crashes` wird **nicht** geleert -- dort koennen Berichte frueherer Sitzungen
    liegen, die niemandem gehoeren, den dieses Werkzeug loeschen darf. Stattdessen merkt sich
    one_run() die vorhandenen Ordner und wertet nur neu hinzugekommene aus.
    """
    for name in list(os.listdir(exe_dir)):
        if name in LEGACY_CRASH_FILES or name.startswith("DesyncReport-"):
            try:
                os.remove(os.path.join(exe_dir, name))
            except OSError:
                pass


def collect_artifacts(exe_dir, run_dir, new_crashes):
    for name in list(os.listdir(exe_dir)):
        if name in LEGACY_CRASH_FILES or name.startswith("DesyncReport-"):
            try:
                shutil.move(os.path.join(exe_dir, name), os.path.join(run_dir, name))
            except OSError:
                pass
    for name in sorted(new_crashes):
        try:
            shutil.move(os.path.join(exe_dir, CRASH_DIR, name), os.path.join(run_dir, name))
        except OSError:
            pass


def one_run(a, index, rng):
    exe = os.path.abspath(a.exe)
    exe_dir = os.path.dirname(exe)
    game_map = a.map if a.map else rng.choice(a.maps)
    seed = rng.randint(1, 1000000)
    run_dir = os.path.join(os.path.abspath(a.out), f"run{index:04d}")
    os.makedirs(run_dir, exist_ok=True)

    replay = os.path.join(run_dir, "soak.rep")
    crc_log = os.path.join(run_dir, "soak.crc")
    args = [exe, "-runner", "-win", "-xres", "800", "-yres", "600",
            "-skirmish", game_map, "-skirmishAI", str(a.ai), "-skirmishSeed", str(seed),
            "-maxFrames", str(a.frames), "-recordReplay", replay,
            "-crcLog", crc_log, "-crcLogInterval", str(a.interval)]
    if a.workers is not None:
        args += ["-workers", str(a.workers)]
    args += list(a.extra or [])

    clear_artifacts(exe_dir)
    crashes_before = crash_folders(exe_dir)
    started = time.time()
    try:
        with open(os.path.join(run_dir, "ausgabe.txt"), "w", encoding="latin-1", errors="replace") as out:
            proc = subprocess.run(args, cwd=exe_dir, stdout=out, stderr=subprocess.STDOUT,
                                  timeout=a.timeout)
        code = proc.returncode
        timed_out = False
    except subprocess.TimeoutExpired:
        code, timed_out = None, True
    seconds = time.time() - started

    findings = []
    if timed_out:
        findings.append(f"Zeitlimit {a.timeout} s ueberschritten (Haenger?)")
    elif code == EXIT_DESYNC:
        findings.append("Exit-Code 2: Desync")
    elif code == EXIT_NOT_FOUND:
        findings.append(f"Exit-Code 3: Karte '{game_map}' oder Replay nicht gefunden")
    elif code != EXIT_OK:
        findings.append(f"Exit-Code {code}")
    new_crashes = crash_folders(exe_dir) - crashes_before
    findings += crc_log_findings(crc_log)
    findings += log_findings(exe_dir, new_crashes)
    collect_artifacts(exe_dir, run_dir, new_crashes)

    status = "FUND" if findings else "ok"
    print(f"[{index:04d}] {status:4s} {game_map!r} Seed {seed} {a.frames} Frames, {seconds:6.1f} s"
          + ("" if not findings else "  -- " + "; ".join(findings)))

    if not findings and not a.keep_all:
        shutil.rmtree(run_dir, ignore_errors=True)

    return {
        "index": index, "map": game_map, "seed": seed, "seconds": seconds,
        "code": code, "findings": findings, "dir": run_dir,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", required=True, help="GZHNeoZH.exe mit verlinkten Assets")
    ap.add_argument("--out", default="soak-runs", help="Ordner fuer Laeufe mit Befund")
    ap.add_argument("--hours", type=float, help="so lange laufen lassen")
    ap.add_argument("--runs", type=int, help="so viele Partien (Vorgabe 10, wenn --hours fehlt)")
    ap.add_argument("--map", help="feste Karte statt Zufallsauswahl")
    ap.add_argument("--ai", type=int, default=7, help="KI-Gegner je Partie (Standard 7)")
    ap.add_argument("--frames", type=int, default=36000, help="Logik-Frames je Partie (36000 = 20 min Spielzeit)")
    ap.add_argument("--interval", type=int, default=30, help="CRC alle N Frames")
    ap.add_argument("--timeout", type=int, default=3600, help="Sekunden je Partie, bis ein Haenger angenommen wird")
    ap.add_argument("--workers", type=int, help="-workers N (0 = seriell)")
    ap.add_argument("--seed", type=int, default=0, help="Seed der Seed-Auswahl (0 = Zeit)")
    ap.add_argument("--keep-all", action="store_true", help="auch Laeufe ohne Befund behalten")
    ap.add_argument("--extra", nargs=argparse.REMAINDER, help="weitere Spielparameter (am Ende)")
    a = ap.parse_args()

    if not os.path.isfile(a.exe):
        print(f"FEHLER: {a.exe} nicht gefunden")
        return 2
    a.maps = DEFAULT_MAPS
    os.makedirs(os.path.abspath(a.out), exist_ok=True)

    rng = random.Random(a.seed if a.seed else None)
    deadline = time.time() + a.hours * 3600 if a.hours else None
    total = a.runs if a.runs else (None if a.hours else 10)

    results, index = [], 0
    while True:
        if total is not None and index >= total:
            break
        if deadline is not None and time.time() >= deadline:
            break
        index += 1
        results.append(one_run(a, index, rng))

    findings = [r for r in results if r["findings"]]
    summary = os.path.join(os.path.abspath(a.out), "fundliste.txt")
    with open(summary, "w", encoding="latin-1", errors="replace") as f:
        f.write(f"Dauerlauf: {len(results)} Partien, {len(findings)} mit Befund\n\n")
        for r in findings:
            f.write(f"Lauf {r['index']:04d}  Karte {r['map']}  Seed {r['seed']}  Exit {r['code']}\n")
            for finding in r["findings"]:
                f.write(f"    {finding}\n")
            f.write(f"    Ordner: {r['dir']}\n\n")

    print(f"\n{len(results)} Partien, {len(findings)} mit Befund -- Zusammenfassung: {summary}")
    print("Funde als Befund festhalten.")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
