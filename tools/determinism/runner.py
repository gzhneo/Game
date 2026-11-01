#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Determinismus-Werkzeug fuer Replays (E-0.2/E-0.3).

Zwei Befehle:

  record  Startet einen Skirmish ohne Menue (lokaler Spieler Beobachter, N KI-Gegner), laesst ihn
          ungebremst bis --frames laufen und legt dabei das Replay (.rep) und den CRC-Verlauf
          (.crc) als Referenz ab.

  check   Spielt ein Replay mit -runner ab und vergleicht den CRC-Verlauf mit der Referenz-.crc.
          Exit-Code 0 = identisch, 1 = Abweichung/Fehler.

Beispiele:
  python tools/determinism/runner.py record --exe build-x64-profile/Generals/Code/Main/Release/GZHNeo.exe ^
      --map "Alpine Assault" --ai 3 --seed 1 --frames 9000 --out tests/replays/alpine-4ai-seed1
  python tools/determinism/runner.py check --exe ... --replay tests/replays/alpine-4ai-seed1.rep
  python tools/determinism/runner.py check --exe ... --all tests/replays

Hinweise:
  - Das Spiel laesst nur eine Instanz zu (Mutex). Laeufe sind deshalb seriell; ein parallel
    laufendes GZHNeo.exe laesst den Start sofort scheitern.
  - Das Fenster darf nicht minimiert werden (minimiert pausiert die Engine), Fokus ist egal.
  - Debug-Builds brauchen --extra -ignoreAsserts, sonst blockieren Assert-Dialoge.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
import time

CRC_LINE = re.compile(r"^(\d+) ([0-9A-F]{8})$")
WINDOW_ARGS = ["-win", "-xres", "800", "-yres", "600"]


def read_crc(path):
    """Liefert (crc-Zeilen als Liste von (frame, crc), sonstige Zeilen)."""
    crcs, other = [], []
    with open(path, "r", encoding="ascii", errors="replace") as f:
        for raw in f:
            line = raw.strip()
            if not line:
                continue
            m = CRC_LINE.match(line)
            if m:
                crcs.append((int(m.group(1)), m.group(2)))
            else:
                other.append(line)
    return crcs, other


def crash_reports(game_dir):
    """Ordner der Absturzberichte (CrashReport.cpp legt sie unter Crashes\\ im Arbeitsordner an)."""
    root = os.path.join(game_dir, "Crashes")
    try:
        return {n for n in os.listdir(root) if os.path.isdir(os.path.join(root, n))}
    except OSError:
        return set()


def run_game(exe, args, timeout, extra):
    """Liefert (Exit-Code oder None bei Zeitlimit, Sekunden, neue Absturzberichte als Pfade).

    Der Watchdog schreibt bei einem Haenger einen Absturzbericht, ohne das Spiel zu beenden; der
    Exit-Code bleibt dann 0. Neue Berichtsordner zaehlen deshalb eigens als Fehler."""
    exe = os.path.abspath(exe)
    game_dir = os.path.dirname(exe)
    cmd = [exe] + WINDOW_ARGS + args + list(extra or [])
    before = crash_reports(game_dir)
    start = time.time()
    try:
        proc = subprocess.run(cmd, cwd=game_dir, timeout=timeout)
        code = proc.returncode
    except subprocess.TimeoutExpired:
        code = None
    crashes = [os.path.join(game_dir, "Crashes", n) for n in sorted(crash_reports(game_dir) - before)]
    return code, time.time() - start, crashes


def crash_message(crashes):
    return "Absturzbericht " + ", ".join(crashes)


def cmd_record(a):
    out = os.path.abspath(a.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    rep, crc = out + ".rep", out + ".crc"
    args = ["-runner", "-skirmish", a.map, "-skirmishAI", str(a.ai), "-skirmishSeed", str(a.seed),
            "-maxFrames", str(a.frames), "-recordReplay", rep, "-crcLog", crc,
            "-crcLogInterval", str(a.interval)]
    code, secs, crashes = run_game(a.exe, args, a.timeout, a.extra)
    if code is None:
        print(f"FEHLER: Zeitlimit {a.timeout} s ueberschritten")
        return 1
    if crashes:
        print(f"FEHLER: {crash_message(crashes)}")
        return 1
    if code != 0:
        print(f"FEHLER: Exit-Code {code}")
        return 1
    if not os.path.isfile(rep) or not os.path.isfile(crc):
        print("FEHLER: Replay oder CRC-Datei fehlt")
        return 1
    crcs, other = read_crc(crc)
    print(f"aufgenommen: {rep} ({os.path.getsize(rep)} Byte), {len(crcs)} CRC-Werte bis Frame "
          f"{crcs[-1][0] if crcs else 0}, {secs:.1f} s; {'; '.join(other)}")
    return 0


def check_one(a, rep):
    rep = os.path.abspath(rep)
    ref = os.path.splitext(rep)[0] + ".crc"
    if not os.path.isfile(ref):
        print(f"{os.path.basename(rep)}: FEHLER keine Referenz {ref}")
        return False
    want_ref, _ = read_crc(ref)
    fd, log = tempfile.mkstemp(suffix=".crc")
    os.close(fd)
    try:
        if a.audio and want_ref:
            # --audio (2026-09-18): -runner schaltet den Ton ab (-noAudio), dann laeuft kein
            # Logik-Audio-Ereignis (TheLogicAudio, E-4b) durch. Ohne -runner mit Ton, festem
            # Zeitschritt und Ende per -quitAfter kurz nach dem letzten Referenz-Frame.
            args = ["-fixedTimeStep", "-replay", rep, "-crcLog", log, "-crcLogInterval", str(a.interval),
                    "-quitAfter", str(want_ref[-1][0] + 5)]
        else:
            args = ["-runner", "-replay", rep, "-crcLog", log, "-crcLogInterval", str(a.interval)]
        code, secs, crashes = run_game(a.exe, args, a.timeout, a.extra)
        got, got_other = read_crc(log) if os.path.getsize(log) > 0 else ([], [])
    finally:
        keep = a.keep and os.path.isfile(log)
        if not keep:
            try:
                os.remove(log)
            except OSError:
                pass
    want, _ = read_crc(ref)
    name = os.path.basename(rep)
    if code is None:
        print(f"{name}: FEHLER Zeitlimit {a.timeout} s")
        return False
    # Die Referenz aus 'record' endet mit dem letzten Frame vor dem Abbruch; das Replay laeuft bis
    # zu seinem letzten Befehl weiter. Verglichen wird der gemeinsame Abschnitt, er muss die ganze
    # Referenz abdecken.
    n = min(len(got), len(want))
    for i in range(n):
        if got[i] != want[i]:
            print(f"{name}: ABWEICHUNG bei Frame {want[i][0]}: Referenz {want[i][1]}, jetzt {got[i][1]} "
                  f"(Frame im Lauf {got[i][0]})")
            return False
    if len(got) < len(want):
        print(f"{name}: FEHLER Lauf endete nach {len(got)} von {len(want)} CRC-Werten; "
              f"Exit-Code {code}; {'; '.join(got_other)}")
        return False
    if code != 0:
        print(f"{name}: FEHLER Exit-Code {code}; {'; '.join(got_other)}")
        return False
    if crashes:
        print(f"{name}: FEHLER {crash_message(crashes)}")
        return False
    print(f"{name}: OK, {len(want)} CRC-Werte identisch, {secs:.1f} s")
    return True


def cmd_check(a):
    reps = []
    if a.replay:
        reps.append(a.replay)
    if a.all:
        reps += sorted(os.path.join(a.all, n) for n in os.listdir(a.all)
                       if n.lower().endswith(".rep") and os.path.isfile(os.path.join(a.all, os.path.splitext(n)[0] + ".crc")))
    if not reps:
        print("keine Replays mit Referenz gefunden")
        return 1
    ok = all([check_one(a, r) for r in reps])
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--exe", required=True, help="GZHNeo.exe (mit verlinkten Assets im selben Ordner)")
    common.add_argument("--interval", type=int, default=30, help="CRC alle N Logik-Frames (Standard 30)")
    common.add_argument("--timeout", type=int, default=1800, help="Sekunden pro Lauf")
    common.add_argument("--extra", nargs=argparse.REMAINDER, help="weitere Spielparameter (am Ende angeben)")

    r = sub.add_parser("record", parents=[common], help="Referenz-Replay aufnehmen")
    r.add_argument("--map", required=True, help='z. B. "Alpine Assault"')
    r.add_argument("--ai", default="3",
                   help='Zahl der KI-Gegner oder Belegung je Platz wie -skirmishAI, z. B. "N1,N1,K2,K2" (neue KI, TODO 50)')
    r.add_argument("--seed", type=int, default=1)
    r.add_argument("--frames", type=int, default=9000, help="Logik-Frames (30 = 1 s Spielzeit)")
    r.add_argument("--out", required=True, help="Pfad ohne Endung; legt .rep und .crc an")

    c = sub.add_parser("check", parents=[common], help="Replay gegen Referenz pruefen")
    c.add_argument("--replay")
    c.add_argument("--all", help="Ordner: alle .rep mit gleichnamiger .crc")
    c.add_argument("--keep", action="store_true", help="CRC-Protokoll des Laufs nicht loeschen")
    c.add_argument("--audio", action="store_true",
                   help="mit Ton und festem Zeitschritt statt -runner (prueft Logik-Audio, dauert laenger)")

    # ctest uebergibt fuer nicht zutreffende Generatorausdruecke (z. B. Debug-Zusatz im Release-Build)
    # leere Argumente; die werden hier verworfen.
    a = ap.parse_args([arg for arg in sys.argv[1:] if arg != ""])
    return cmd_record(a) if a.cmd == "record" else cmd_check(a)


if __name__ == "__main__":
    sys.exit(main())
