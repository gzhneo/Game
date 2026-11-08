#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Render-Tests fuer die DX11-Portierung.

Startet das Spiel mit festem Zeitschritt, laesst es an bestimmten Client-Frames Backbuffer-
Screenshots schreiben und beendet es danach selbst. Die Bilder lassen sich mit einer Referenz
vergleichen (compare_shots.py). Ausserdem misst ein Baseline-Lauf die Frame-Zeiten ohne festen
Zeitschritt.

Befehle:

  run       Szene einmal aufnehmen: Screenshots, Runner-Protokoll (run.log) und Debug-Log landen in
            --out. Mit --reference wird danach gegen einen Referenzordner verglichen.
  twice     Szene zweimal aufnehmen (--out/a, --out/b) und beide Laeufe vergleichen. Zeigt, wie
            reproduzierbar eine Szene ist; daraus werden die Toleranzen festgelegt.
  baseline  Szene ohne festen Zeitschritt und ohne Screenshots laufen lassen und die Frame-Zeiten
            (FRAMES-Zeile, frames.csv) festhalten -- Vergleichswert D3D9 gegen D3D11 (P-4.3).

Szenen (--scene):
  shellmap  Startmenue mit Shell-Map (echte Spielengine; wichtigster Ersttest)
  menu      Hauptmenue ohne Shell-Map (nur 2D)
  skirmish  Skirmish ohne Menue: --map, --ai, --seed (E-0.2)
  replay    Replay abspielen: --replay <Datei> (passend zur Architektur der Exe)
  mission   Karte direkt starten (-file, nur Debug-Builds): --map "Maps\\md_usa01.map" -- Kampagnen-
            missionen samt Zwischensequenz

Kamerafahrt: --path <Datei> faehrt die Kamera nach der Datei
(-cameraPath) und nimmt alle --every Logik-Frames ein Bild auf, solange die Fahrt laeuft
(-shotEvery). --frames entfaellt dann; die erwarteten Bilder ergeben sich aus der Datei.

Beispiele:
  python tools/render/run_render_test.py run --exe build-x64/Generals/Code/Main/Debug/GZHNeo.exe ^
      --scene shellmap --frames 300,900 --out render-out/shellmap --extra -ignoreAsserts
  python tools/render/run_render_test.py twice --exe ... --scene shellmap --out render-out/repro
  python tools/render/run_render_test.py run --exe ... --out render-out/neu --reference tests/render/shellmap
  python tools/render/run_render_test.py baseline --exe build-x64/.../Release/GZHNeo.exe --frames 1800
  python tools/render/run_render_test.py run --exe ... --scene replay ^
      --replay tests/replays/x64/e0-alpine-assault-2ai-seed1.rep --path tests/render/paths/quer.txt ^
      --out render-out/quer --extra -ignoreAsserts

Hinweise:
  - Nur eine Spielinstanz zur Zeit (Mutex). Laeuft schon GZHNeo.exe oder GZHNeoZH.exe, bricht das
    Skript ab (--force ueberspringt die Pruefung). Ausnahme --mehrfach (2026-09-24): startet das Spiel mit
    -mehrfach (keine Einzelinstanz-Sperre, Protokolle mit Vorsatz m<PID>_), mehrere Laeufe duerfen dann
    nebeneinander laufen; der Mauszeiger wird nicht geparkt (das Spiel ignoriert die echte Maus in
    Bildvergleichslaeufen, TODO 33). Nur fuer Exes ab dem Stand mit -mehrfach.
  - Das Fenster nicht minimieren (ohne -fixedTimeStep pausiert die Engine dann). Der Mauszeiger wird vor
    jedem Start an den linken Bildschirmrand geparkt (nicht in eine Ecke, siehe park_cursor): ueber dem
    Fenster wuerde er Hover-Effekte im Menue und das
    Randscrollen der Shell-Map-Kamera ausloesen (beides im Bild). Waehrend des Laufs die Maus nicht
    ueber das Fenster bewegen.
  - Frames zaehlen Client-Frames (auch im Menue); mit -fixedTimeStep ist das ein Logik-Tick pro Bild.
  - Jeder Lauf hat einen eigenen Benutzerordner (<out>/_benutzer) und eine feste Detailstufe (--stufe, Standard
    VeryHigh); Einstellungen des Nutzers fliessen nicht ein. --stufe nutzer startet wie frueher mit seinen Einstellungen.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_shots  # noqa: E402

GAME_PROCESSES = ("gzhneo.exe", "gzhneozh.exe")


def running_game_processes():
    try:
        out = subprocess.run(["tasklist", "/fo", "csv", "/nh"], capture_output=True, text=True,
                             errors="replace", timeout=30).stdout
    except (OSError, subprocess.SubprocessError):
        return []
    found = []
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if len(parts) >= 2 and parts[0].lower() in GAME_PROCESSES:
            found.append(f"{parts[0]} (PID {parts[1]})")
    return found


def scene_args(a):
    if a.scene == "shellmap":
        return []
    if a.scene == "menu":
        return ["-noshellmap"]
    if a.scene == "replay":
        if not a.replay:
            raise SystemExit("--scene replay braucht --replay")
        return ["-replay", os.path.abspath(a.replay)]
    if not a.map:
        raise SystemExit(f"--scene {a.scene} braucht --map")
    if a.scene == "mission":
        return ["-file", a.map]
    return ["-skirmish", a.map, "-skirmishAI", str(a.ai), "-skirmishSeed", str(a.seed)]


def parse_frames(text):
    frames = sorted({int(f) for f in text.split(",") if f.strip()})
    if not frames or frames[0] <= 0:
        raise SystemExit("--frames: positive Client-Frame-Nummern, z. B. 300,900")
    return frames


def path_frames(path, every):
    """Bilder einer Kamerafahrt: -shotEvery nimmt im Bereich erster bis letzter Stuetzpunkt auf,
    jeweils beim ersten Frame eines neuen Vielfachen von every, benannt nach dem Vielfachen."""
    if every <= 0:
        raise SystemExit("--every: positive Anzahl Logik-Frames")
    keys = []
    with open(path, "r", encoding="ascii", errors="replace") as f:
        for line in f:
            fields = line.split("#", 1)[0].split()
            if fields:
                keys.append(int(fields[0]))
    if not keys:
        raise SystemExit(f"--path: keine Stuetzpunkte in {path}")
    first, last = keys[0], keys[-1]
    return [n * every for n in range(max(first // every, 1), last // every + 1)], last


def shot_plan(a):
    """Erwartete Screenshot-Frames und -quitAfter fuer diesen Lauf."""
    if a.path:
        frames, last = path_frames(a.path, a.every)
        return frames, a.quit or last + 30
    frames = parse_frames(a.frames)
    return frames, a.quit or frames[-1] + 30


def read_log(path):
    if not os.path.isfile(path):
        return []
    with open(path, "r", encoding="ascii", errors="replace") as f:
        return [line.strip() for line in f if line.strip()]


def park_cursor():
    """Mauszeiger ausserhalb des Spielfensters parken (TODO 12/16): ueber dem Fenster loest er Hover-Effekte
    im Hauptmenue und das Randscrollen der Shell-Map-Kamera aus, beides landet im Bild und macht zwei
    Laeufe verschieden. TODO 34 (2026-09-18): NICHT in die untere rechte Ecke -- dort liegt die
    Schaltflaeche "Desktop anzeigen"; beim Spielstart wurden dadurch alle Fenster minimiert, das Spiel
    wartete minimiert bis zum Zeitlimit und behielt danach die Anfangsgroesse 800x600. Auch nicht in die
    oberen Ecken (Schliessen bzw. Systemmenue maximierter Fenster). Mitte des linken Bildschirmrands."""
    try:
        import ctypes
        u = ctypes.windll.user32
        u.SetCursorPos(0, u.GetSystemMetrics(1) // 2)
    except (AttributeError, OSError):
        pass


CDB = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "WindowsApps", "cdbX64.exe")


def dump_hung_process(pid, out):
    """TODO 34: haengt das Spiel bis zum Zeitlimit, vor dem Abbruch einen Speicherauszug und die
    Aufrufstapel aller Threads sichern (cdb nicht-invasiv angehaengt, -pv). Ergebnis in out:
    hang.dmp und hang_stacks.txt."""
    if not os.path.isfile(CDB):
        print(f"Kein cdb unter {CDB} -- kein Speicherauszug")
        return
    dmp = os.path.join(out, "hang.dmp")
    stacks = os.path.join(out, "hang_stacks.txt")
    cmds = f'.dump /ma /o "{dmp}"; ~*kb 40; qd'
    try:
        r = subprocess.run([CDB, "-pv", "-p", str(pid), "-c", cmds], capture_output=True, text=True,
                           errors="replace", timeout=300)
        with open(stacks, "w", encoding="utf-8") as f:
            f.write(r.stdout)
        print(f"Haenger: Speicherauszug {dmp}, Aufrufstapel {stacks}")
    except (OSError, subprocess.SubprocessError) as e:
        print(f"Speicherauszug fehlgeschlagen: {e}")


def run_game(a, out, fixed, frames, quit_after):
    exe = os.path.abspath(a.exe)
    if not os.path.isfile(exe):
        raise SystemExit(f"Exe nicht gefunden: {exe}")
    os.makedirs(out, exist_ok=True)
    log = os.path.join(out, "run.log")
    for stale in [log, log + ".frames.csv", log + ".ticks.csv"] + \
            [os.path.join(out, n) for n in os.listdir(out) if n.startswith("shot_") and n.endswith(".bmp")]:
        if os.path.isfile(stale):
            os.remove(stale)
    # Referenzbilder immer mit den Original-Texturen (SD); "Auto" haengt von
    # Aufloesung, Grafikspeicher und den vorhandenen Stufen ab. --texture-quality ueberschreibt das.
    cmd = [exe, "-win", "-xres", str(a.xres), "-yres", str(a.yres), "-nologo", "-noaudio",
           "-textureQuality", a.texture_quality,
           "-gamma", str(a.gamma),
           "-msaa", str(a.msaa),
           "-crcLog", log, "-quitAfter", str(quit_after)] + scene_args(a)
    if fixed:
        cmd += ["-fixedTimeStep", "-shotDir", out]
        if getattr(a, "path", None):
            cmd += ["-cameraPath", os.path.abspath(a.path), "-shotEvery", str(a.every)]
        else:
            for f in frames:
                cmd += ["-shotAfter", str(f)]
    if getattr(a, "mehrfach", False):
        cmd += ["-mehrfach"]
    extra = list(a.extra or [])
    extra_lower = [e.lower() for e in extra]
    # Eigener Benutzerordner und feste Detailstufe: sonst kaemen Stufe, Effektliste und weitere Optionen aus der
    # Settings.ini des Nutzers, und die Bilder hingen vom Rechner ab. Die fast leere Settings.ini verhindert, dass der
    # erste Start die Options.ini des Originalspiels uebernimmt.
    if a.stufe != "nutzer":
        if "-benutzerdaten" not in extra_lower:
            benutzer = os.path.join(out, "_benutzer")
            if os.path.isdir(benutzer):
                shutil.rmtree(benutzer)
            os.makedirs(benutzer)
            with open(os.path.join(benutzer, "Settings.ini"), "w", encoding="utf-8", newline="\r\n") as f:
                f.write("[General]\n")
            cmd += ["-benutzerdaten", benutzer + os.sep, "-einstellungenNurLesen"]
        if "-staticlod" not in extra_lower:
            cmd += ["-staticLOD", a.stufe]
    # -d3d11 ausdruecklich, damit ein alter Eintrag "RenderBackend = D3D9" in der Options.ini keine Meldung ausloest.
    if not any(e.lower() in ("-d3d9", "-d3d11", "-vulkan") for e in extra):
        cmd += ["-d3d11"]
    cmd += extra
    print("Start:", " ".join(f'"{c}"' if " " in c else c for c in cmd))
    if not getattr(a, "mehrfach", False):
        park_cursor()
    start = time.time()
    proc = subprocess.Popen(cmd, cwd=os.path.dirname(exe))
    try:
        code = proc.wait(timeout=a.timeout)
    except subprocess.TimeoutExpired:
        code = None
        dump_hung_process(proc.pid, out)
        proc.kill()
        proc.wait()
    secs = time.time() - start
    debug_log = os.path.join(os.path.dirname(exe), (f"m{proc.pid}_" if getattr(a, "mehrfach", False) else "")
                             + "DebugLogFileD.txt")
    if os.path.isfile(debug_log) and os.path.getmtime(debug_log) >= start - 1:
        shutil.copy2(debug_log, os.path.join(out, "DebugLogFileD.txt"))
    lines = read_log(log)
    print(f"Ende nach {secs:.1f} s, Exit-Code {code}")
    for line in lines:
        if line.startswith(("SHOT", "FRAMES", "BENCH", "END", "EXIT", "ERROR", "CAMERAPATH", "SHADERFEHLER")):
            print("  " + line)
    return code, lines


def check_run(a, out, frames, code, lines):
    ok = True
    if code is None:
        print(f"FEHLER: Zeitlimit {a.timeout} s ueberschritten")
        ok = False
    elif code != 0:
        # Ein Exit-Code ungleich 0 nach regulaerem Ende (END quitAfter, alle Screenshots da) kommt vom
        # Leckbericht bzw. der Assert-Box beim Beenden -- Warnung, kein Abbruch, sonst
        # faellt der Bildvergleich aus, obwohl die Bilder vollstaendig sind.
        print(f"WARNUNG: Exit-Code {code} (Beenden-Pfad); Bilder werden trotzdem verglichen")
    for line in lines:
        # A shader that did not compile drops its effect silently; the game names it in the release build too.
        if line.startswith(("CAMERAPATH ERROR", "CAMERAPATH Fehler", "SHADERFEHLER")):
            print("FEHLER: " + line)
            ok = False
    if not any(line.startswith("END quitAfter") for line in lines):
        print("FEHLER: Lauf endete nicht ueber -quitAfter (Absturz oder vorzeitiges Ende? DebugLogFileD.txt pruefen)")
        ok = False
    for f in frames:
        path = os.path.join(out, f"shot_{f:06d}.bmp")
        if not os.path.isfile(path):
            print(f"FEHLER: Screenshot fehlt: {path}")
            ok = False
    return ok


def compare_dirs(ref, new, diff, a):
    args = argparse.Namespace(reference=ref, candidate=new, diff=diff, threshold=a.threshold,
                              min_psnr=a.min_psnr, max_diff_ratio=a.max_diff_ratio,
                              ignore=getattr(a, "ignore", None) or [],
                              json=os.path.join(new, "compare.json"))
    return compare_shots.run_compare(args)


def guard_mutex(a):
    if a.force or getattr(a, "mehrfach", False):
        return
    others = running_game_processes()
    if others:
        raise SystemExit("Abbruch: es laeuft bereits " + ", ".join(others) +
                         " (Mutex; ein weiterer Start beendet sich sofort). --force ueberspringt die Pruefung.")


def cmd_run(a):
    guard_mutex(a)
    frames, quit_after = shot_plan(a)
    out = os.path.abspath(a.out)
    code, lines = run_game(a, out, True, frames, quit_after)
    if not check_run(a, out, frames, code, lines):
        return 2
    if a.reference:
        return compare_dirs(os.path.abspath(a.reference), out, os.path.join(out, "diff"), a)
    return 0


def cmd_twice(a):
    guard_mutex(a)
    frames, quit_after = shot_plan(a)
    base = os.path.abspath(a.out)
    runs = []
    for name in ("a", "b"):
        out = os.path.join(base, name)
        code, lines = run_game(a, out, True, frames, quit_after)
        if not check_run(a, out, frames, code, lines):
            return 2
        runs.append(out)
    print("Vergleich Lauf a gegen Lauf b:")
    return compare_dirs(runs[0], runs[1], os.path.join(base, "diff"), a)


def cmd_baseline(a):
    guard_mutex(a)
    out = os.path.abspath(a.out)
    code, lines = run_game(a, out, False, [], a.frames_total)
    frames_line = next((line for line in lines if line.startswith("FRAMES")), None)
    if code != 0 or frames_line is None:
        print("FEHLER: keine FRAMES-Zeile (Exit-Code %s)" % code)
        return 2
    print(f"Baseline: {frames_line}")
    print(f"Einzelwerte: {os.path.join(out, 'run.log.frames.csv')}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--exe", required=True, help="GZHNeo.exe (mit verlinkten Assets im selben Ordner)")
    common.add_argument("--scene", choices=("shellmap", "menu", "skirmish", "replay", "mission"), default="shellmap")
    common.add_argument("--replay", help="Replay-Datei fuer --scene replay")
    common.add_argument("--map", help='Skirmish-Karte, z. B. "Alpine Assault"; mission: Kartenpfad, z. B. "Maps\\md_usa01.map"')
    common.add_argument("--ai", type=int, default=3, help="Skirmish: Anzahl KI-Gegner")
    common.add_argument("--seed", type=int, default=1, help="Skirmish: Spiel-Seed")
    # 2026-09-25: Helligkeitsregler neutral; er wirkt unter D3D11 auch im Fenster und kaeme sonst aus der
    # Options.ini des Nutzers (Referenzbilder waeren dann vom Regler abhaengig).
    common.add_argument("--gamma", type=int, default=50, help="Stellung des Helligkeitsreglers 0..100 (-gamma), Standard 50 = neutral")
    common.add_argument("--texture-quality", default="sd", choices=("sd", "hd", "uhd", "auto"),
                        help="Texturstufe, Standard sd fuer reproduzierbare Referenzen")
    # R-683: the anti-aliasing would otherwise come from the user's Settings.ini (8 samples on one machine, 1 on another),
    # and with multisampling the depth effects took another path.
    common.add_argument("--msaa", type=int, default=1, help="Kantenglaettung, Samples (-msaa), Standard 1 = aus; "
                        "unabhaengig von der Settings.ini des Nutzers")
    common.add_argument("--stufe", default="VeryHigh", choices=("Low", "Medium", "High", "VeryHigh", "nutzer"),
                        help="Detailstufe (-staticLOD) mit eigenem Benutzerordner unter --out, Standard VeryHigh; "
                             "nutzer = Stufe und Einstellungen aus dem Benutzerordner des Nutzers wie frueher")
    common.add_argument("--xres", type=int, default=1280)
    common.add_argument("--yres", type=int, default=720)
    common.add_argument("--timeout", type=int, default=600, help="Sekunden pro Lauf")
    common.add_argument("--force", action="store_true", help="Pruefung auf laufende Spielinstanzen ueberspringen")
    common.add_argument("--mehrfach", action="store_true",
                        help="Spiel mit -mehrfach starten: mehrere Instanzen nebeneinander, Protokolle je Instanz")
    common.add_argument("--extra", nargs=argparse.REMAINDER, help="weitere Spielparameter (am Ende angeben)")

    shots = argparse.ArgumentParser(add_help=False)
    shots.add_argument("--frames", default="300,900", help="Client-Frames fuer Screenshots, kommagetrennt")
    shots.add_argument("--path", help="Kamerafahrt (-cameraPath), ersetzt --frames")
    shots.add_argument("--every", type=int, default=60, help="mit --path: alle N Logik-Frames ein Bild")
    shots.add_argument("--quit", type=int, help="nach N Client-Frames beenden (Standard: letzter Screenshot + 30)")
    shots.add_argument("--threshold", type=int, default=compare_shots.DEFAULT_THRESHOLD)
    shots.add_argument("--min-psnr", type=float, default=compare_shots.DEFAULT_MIN_PSNR)
    shots.add_argument("--max-diff-ratio", type=float, default=compare_shots.DEFAULT_MAX_DIFF_RATIO)
    shots.add_argument("--ignore", action="append", type=compare_shots.Rect, metavar="X,Y,B,H", default=[],
                       help="Bildbereich vom Vergleich ausnehmen (Pixel oder Prozent), mehrfach angebbar. "
                            "Fuer die Shell-Map sinnvoll, weil die Menue-Einblendung nicht deterministisch "
                            "erscheint (TODO Nr. 16)")

    r = sub.add_parser("run", parents=[common, shots], help="Szene aufnehmen, optional mit Referenz vergleichen")
    r.add_argument("--out", required=True, help="Zielordner")
    r.add_argument("--reference", help="Referenzordner mit shot_*.bmp")

    t = sub.add_parser("twice", parents=[common, shots], help="Szene zweimal aufnehmen und vergleichen")
    t.add_argument("--out", required=True, help="Zielordner (Unterordner a, b, diff)")

    b = sub.add_parser("baseline", parents=[common], help="Frame-Zeiten ohne festen Zeitschritt messen")
    b.add_argument("--out", default="render-out/baseline", help="Zielordner")
    b.add_argument("--frames", dest="frames_total", type=int, default=1800,
                   help="Laufzeit in Client-Frames (Standard 1800, etwa 60 s)")

    a = ap.parse_args([arg for arg in sys.argv[1:] if arg != ""])
    return {"run": cmd_run, "twice": cmd_twice, "baseline": cmd_baseline}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
