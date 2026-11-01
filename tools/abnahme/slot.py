# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Warten, bis kein Spiel und kein WorldBuilder laeuft (Parallelbetrieb mehrerer Agenten und der Haupt-Session).

Faire Warteschlange (2026-09-24): Jeder Wartende legt eine Marke in die Schlange (SCHLANGE unten) (Name = Ankunftszeit
und PID). Nur die aelteste lebende Marke darf starten; Marken toter Prozesse werden entfernt. Vorher gewann, wer
zufaellig im richtigen Augenblick nachsah -- Serienskripte, die ein Spiel nach dem anderen starten, liessen andere
Wartende stundenlang aushungern.

Startsperre: Zwischen "frei" und dem tatsaechlichen Spielstart vergehen einige Sekunden. wait_free() nimmt deshalb
vor der Rueckkehr eine Startsperre (spielstart_<Zeit>_<PID>.lock in der Schlange) und haelt sie STARTSPERRE_S Sekunden; in der
Zeit muss das Spiel gestartet sein, danach sieht tasklist es.

Aufruf: wait_free() direkt vor dem Spielstart; aus der Shell
    python -c "import sys; sys.path.insert(0, 'tools/abnahme'); from slot import wait_free; wait_free()"
Mit parallel=3 fuer Laeufe mit -mehrfach.
"""
import os, subprocess, time

NAMES = ("gzhneo.exe", "gzhneozh.exe")
# Ein Ordner fuer den ganzen Rechner (2026-09-26, aus dem Sitzungs-Scratchpad nach tools/abnahme uebernommen):
# alle Worktrees, Agenten und die Haupt-Session muessen dieselbe Schlange sehen, sonst starten sie gleichzeitig.
# Umgebungsvariable CNC_WARTESCHLANGE ueberschreibt den Ort.
SCHLANGE = os.environ.get("CNC_WARTESCHLANGE") or os.path.join(
    os.environ.get("LOCALAPPDATA") or os.path.expanduser("~"), "CnC_Modernisierung", "spiel_warteschlange")
STARTSPERRE_S = 90
MARKE_MAX_S = 3600	# aeltere Marken gelten als verwaist (2026-09-25)


def _tasklist():
    try:
        return subprocess.run(["tasklist", "/fo", "csv", "/nh"], capture_output=True, text=True,
                              errors="replace", timeout=120).stdout
    except (OSError, subprocess.SubprocessError):
        return ""


def busy(out=None):
    """Laufende Fenster-Spiele und WorldBuilder. Runner-Laeufe (Kommandozeile mit -runner, Replay-Pruefung ohne
    Fenster) zaehlen nicht (2026-09-24): Sie umgehen die Einzelinstanz-Sperre, laufen aus ihrem eigenen Exe-Ordner
    und stoeren Bildtests nicht -- vorher hielten parallele Replay-Pruefungen die ganze Warteschlange an.
    `out` wird nur noch aus Vertraeglichkeit angenommen."""
    ps = ("Get-CimInstance Win32_Process -Filter \"name='GZHNeo.exe' or name='GZHNeoZH.exe' or name like 'WorldBuilder%'\" "
          "| ForEach-Object { $_.Name + '|' + $_.ProcessId + '|' + $_.CommandLine }")
    try:
        r = subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True, text=True,
                           errors="replace", timeout=120)
    except (OSError, subprocess.SubprocessError):
        return ["Prozessabfrage?"]
    if r.returncode != 0:
        return ["Prozessabfrage?"]
    found = []
    for line in r.stdout.splitlines():
        teile = line.split("|", 2)
        if len(teile) < 2:
            continue
        cmd = teile[2].lower() if len(teile) > 2 else ""
        if " -runner" in cmd:
            continue
        found.append(f"{teile[0]} ({teile[1]})")
    return found


def _lebende_pids(out):
    pids = set()
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if len(parts) > 1 and parts[1].isdigit():
            pids.add(int(parts[1]))
    return pids


def _marke_parallel(m):
    """Zulaessige Zahl gleichzeitiger Spiele des Wartenden (Markenname ..._p<N>.marke2); alte Marken ohne Angabe
    gelten als exklusiv (1)."""
    rest = m[:-len(".marke2")].split("_")
    if len(rest) >= 3 and rest[2].startswith("p") and rest[2][1:].isdigit():
        return int(rest[2][1:])
    return 1


def _erste_marke(meine, out, belegt=None):
    """Name der aeltesten lebenden Marke, die jetzt starten koennte; tote Marken werden entfernt.
    2026-09-25: Ein Wartender, der bei der aktuellen Belegung ohnehin nicht starten kann (er will weniger
    gleichzeitige Spiele, als gerade laufen -- etwa exklusiv), haelt die Schlange nicht mehr auf; vorher hing sie
    ueber eine Stunde hinter einem exklusiven Wartenden, weil andere Agenten laufend parallel spielen. Wird die
    Maschine frei genug, ist er als Aeltester wieder zuerst dran."""
    lebend = _lebende_pids(out)
    # Markenformat 2 (2026-09-24): nur Marken dieser Fassung zaehlen. Warter mit einer aelteren slot.py im Speicher
    # (Marken ".marke") zaehlten Runner-Replays als belegt, kamen nie an die Reihe und hielten alle anderen auf.
    marken = sorted(m for m in os.listdir(SCHLANGE) if m.endswith(".marke2"))
    for m in marken:
        try:
            pid = int(m.split("_")[1].split(".")[0])
        except (IndexError, ValueError):
            continue
        if m != meine and lebend and pid not in lebend:
            try:
                os.remove(os.path.join(SCHLANGE, m))
            except OSError:
                pass
            continue
        if m != meine and belegt is not None and belegt >= _marke_parallel(m):
            continue
        # Verwaist (2026-09-25): ein Wartender, dessen Aufrufer beendet wurde, lebt weiter, startet aber nie (seine
        # Ausgabe-Pipe ist tot) und hielt die Schlange ueber eine Stunde auf. Marken aelter als MARKE_MAX_S zaehlen
        # deshalb nicht mehr als Kopf; geloescht werden sie nicht (fremde Dateien, Sandbox).
        try:
            if m != meine and time.time() - float(m.split("_")[0]) > MARKE_MAX_S:
                continue
        except ValueError:
            pass
        return m
    return meine


def _sperre_nehmen():
    """Startsperre ohne Loeschen fremder Dateien (2026-09-24): sandboxierte Aufrufer duerfen fremde Dateien nicht
    loeschen, eine abgelaufene gemeinsame Sperrdatei blockierte deshalb alle. Jetzt legt jeder Starter eine eigene
    Datei spielstart_<Zeit>_<PID>.lock an; gesperrt ist, solange eine davon juenger als STARTSPERRE_S ist. Alte
    Dateien werden nur versuchsweise entfernt."""
    jetzt = time.time()
    for f in os.listdir(SCHLANGE):
        if not f.startswith("spielstart_"):
            continue
        pfad = os.path.join(SCHLANGE, f)
        try:
            alter = jetzt - os.path.getmtime(pfad)
        except OSError:
            continue
        if alter < STARTSPERRE_S:
            return False
        try:
            os.remove(pfad)
        except OSError:
            pass
    try:
        with open(os.path.join(SCHLANGE, f"spielstart_{jetzt:017.3f}_{os.getpid()}.lock"), "w") as fh:
            fh.write(time.ctime())
    except OSError:
        return False
    return True


def _zu_voll(b, parallel):
    """Belegt? WorldBuilder laeuft immer allein; sonst duerfen bis zu `parallel` Spiele laufen."""
    if any(x.lower().startswith("worldbuilder") or x.endswith("?") for x in b):
        return bool(b)
    return len(b) >= parallel


def wait_free(log=print, parallel=1):
    """parallel > 1 (2026-09-24): nur fuer Laeufe mit -mehrfach (run_render_test.py --mehrfach), die neben
    anderen Instanzen laufen koennen. Standard 1: wie bisher, kein anderes Spiel darf laufen."""
    os.makedirs(SCHLANGE, exist_ok=True)
    meine = f"{time.time():017.3f}_{os.getpid()}_p{parallel}.marke2"
    open(os.path.join(SCHLANGE, meine), "w").close()
    n = 0
    try:
        while True:
            out = _tasklist()
            b = busy(out)
            vorne = _erste_marke(meine, out, len(b)) == meine
            if not _zu_voll(b, parallel) and vorne:
                time.sleep(15)
                if not _zu_voll(busy(), parallel) and _sperre_nehmen():
                    return
                continue
            if n % 20 == 0:
                log(f"warte auf {b if b else 'andere in der Warteschlange'}")
            n += 1
            time.sleep(10)
    finally:
        try:
            os.remove(os.path.join(SCHLANGE, meine))
        except OSError:
            pass
