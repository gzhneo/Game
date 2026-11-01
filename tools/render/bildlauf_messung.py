# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Bildlauf-Messung: Kamerageschwindigkeit je Auflösung, Bildlauf-Art und Reglerstellung.

Startet das Spiel je Auflösung im Fenster mit einem Skirmish (lokaler Spieler Beobachter) und einem Testablauf
(-testablauf). Der Befehl `bildlauf` speist Eingaben ein (Umschalt+Pfeiltaste, Zeiger am Bildschirmrand, rechte
Taste gezogen) und protokolliert die Kamerabewegung in Weltkoordinaten pro Sekunde und als Anteil des sichtbaren
Bodens pro Sekunde (Mittelzeile bzw. Mittelspalte zu Beginn des Laufs), Zeilen "TESTABLAUF BILDLAUF ..." im
Runner-Protokoll. `bildlaufregler` setzt den Bildlauf-Regler (ScrollFactor) nur für den Lauf. Ausgabe: Tabelle
und CSV (`bildlauf.csv`).

Aufruf (Windows, Pfade im Windows-Format):
    python tools/render/bildlauf_messung.py --exe build-x64/GeneralsMD/Code/Main/Release/GZHNeoZH.exe \\
        --out render-out/bildlauf [--res 1280x720,1920x1080,2560x1080,3440x1440] [--regler 10,50,100]
        [--map "Alpine Assault"] [--bildrate 10] [--extra -schalter ...]

Wartet vor jedem Start auf die Spielstart-Warteschlange (tools/abnahme/slot.py). Settings.ini/Options.ini werden
nicht geschrieben (-einstellungenNurLesen); der Randbildlauf wird nur für die Dauer des Befehls im Fenster erlaubt.
Die Pfeiltasten wählen in der deutschen Tastenbelegung Einheiten aus, deshalb mit Umschalt (LookAtXlat wertet
die Umschalttaste nicht aus); der erste Lauf mit Umschalt dient nur zum Aufwärmen (Modifikator-Wechsel).
"""
import argparse
import csv
import os
import re
import subprocess
import sys
import time

HIER = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HIER, "..", "abnahme"))

# (Art, Richtung, Zugweg in Pixeln, Teiler der Laufzeit). Hin und zurück, damit die Kamera nicht an den
# Kartenrand läuft; schnelle Läufe kürzer.
LAEUFE = [
    ("taste", "rechts", 0, 1), ("taste", "links", 0, 1),
    ("taste", "hoch", 0, 1), ("taste", "runter", 0, 1),
    ("rand", "rechts", 0, 1), ("rand", "links", 0, 1),
    ("rand", "hoch", 0, 1), ("rand", "runter", 0, 1),
    ("ziehen", "rechts", 100, 3), ("ziehen", "links", 100, 3),
    ("ziehen", "hoch", 100, 3), ("ziehen", "runter", 100, 3),
    ("ziehen", "rechts", 300, 6), ("ziehen", "links", 300, 6),
]

ZEILE = re.compile(r"BILDLAUF (art=.*)$")


def ablauf_text(ticks, regler, bildrate=0):
    zeilen = ["# Bildlauf-Messung (tools/render/bildlauf_messung.py)", "partie 30000", "logik 90",
              "bildlauf taste rechts 1", "logik 10"]
    if bildrate:
        zeilen += [f"bildrate {bildrate}", "logik 30"]
    for r in regler:
        zeilen.append(f"bildlaufregler {r}")
        for art, richtung, weg, teiler in LAEUFE:
            zeilen.append(f"bildlauf {art} {richtung} {max(1, ticks // teiler)}" + (f" {weg}" if weg else ""))
            zeilen.append("logik 10")
    zeilen.append("beenden")
    return "\n".join(zeilen) + "\n"


def lauf(a, res, out, regler):
    xres, yres = res.split("x")
    ziel = os.path.join(out, res)
    os.makedirs(ziel, exist_ok=True)
    skript = os.path.join(ziel, "ablauf.txt")
    with open(skript, "w", encoding="ascii", newline="\r\n") as f:
        f.write(ablauf_text(a.ticks, regler, a.bildrate))
    log = os.path.join(ziel, "run.log")
    if os.path.isfile(log):
        os.remove(log)
    exe = os.path.abspath(a.exe)
    cmd = [exe, "-win", "-xres", xres, "-yres", yres, "-quickstart", "-noaudio", "-d3d11",
           "-einstellungenNurLesen", "-crcLog", log, "-testablauf", skript,
           "-skirmish", a.map, "-skirmishAI", "1", "-skirmishSeed", "1"] + list(a.extra or [])
    from slot import wait_free
    wait_free()
    print("Start:", " ".join(f'"{c}"' if " " in c else c for c in cmd), flush=True)
    start = time.time()
    proc = subprocess.Popen(cmd, cwd=os.path.dirname(exe))
    try:
        code = proc.wait(timeout=a.timeout)
    except subprocess.TimeoutExpired:
        code = None
        print(f"Zeitlimit {a.timeout} s überschritten, PID {proc.pid} läuft noch -- bitte von Hand prüfen")
    print(f"Ende nach {time.time() - start:.1f} s, Exit-Code {code}")
    werte, fehler = [], []
    if os.path.isfile(log):
        with open(log, encoding="latin-1") as f:
            for zeile in f:
                m = ZEILE.search(zeile)
                if m:
                    werte.append(dict(kv.split("=", 1) for kv in m.group(1).split()))
                elif "ERROR" in zeile:
                    fehler.append(zeile.strip())
    # der Aufwärmlauf (ein Takt) zählt nicht
    werte = [w for w in werte if w["ticks"] != "1"]
    for z in fehler:
        print("  " + z)
    return code, werte, fehler


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--res", default="1280x720,1920x1080,2560x1080,3440x1440")
    ap.add_argument("--regler", default="50", help="Reglerstellungen 0..100, kommagetrennt")
    ap.add_argument("--map", default="Alpine Assault")
    ap.add_argument("--bildrate", type=int, default=0,
                    help="reine Bildframes auf N je Sekunde begrenzen (niedrige Bildrate, mehrere Takte je Durchlauf)")
    ap.add_argument("--ticks", type=int, default=30, help="Logik-Takte je Lauf (30 = eine Sekunde)")
    ap.add_argument("--timeout", type=int, default=900)
    ap.add_argument("--extra", nargs=argparse.REMAINDER, help="weitere Spielparameter (am Ende angeben)")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    regler = [r.strip() for r in a.regler.split(",") if r.strip()]
    alle, fehlerzahl = [], 0
    for res in a.res.split(","):
        code, werte, fehler = lauf(a, res, a.out, regler)
        fehlerzahl += len(fehler) + (0 if code == 0 else 1)
        alle += werte
    felder = ["res", "factor", "art", "dir", "drag", "ticks", "ms", "world", "world_s", "across", "down", "view_s",
              "screen_px", "screen_s", "zoom", "zoom_end", "height", "pitch"]
    with open(os.path.join(a.out, "bildlauf.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=felder, extrasaction="ignore")
        w.writeheader()
        for z in alle:
            w.writerow(z)
    print(f"\n{'Auflösung':>10} {'Regler':>6} {'Art':>6} {'Richtung':>8} {'Zug':>4} {'Welt/s':>8} {'Sicht':>7} "
          f"{'Bild/s':>7} {'Zoom':>6}")
    for z in alle:
        sicht = z["across"] if z["dir"] in ("rechts", "links") else z["down"]
        print(f"{z['res']:>10} {float(z['factor']):6.2f} {z['art']:>6} {z['dir']:>8} {z['drag']:>4} "
              f"{float(z['world_s']):8.1f} {float(sicht):7.1f} {float(z['view_s']):7.3f} {float(z['zoom']):6.3f}")
    print(f"\nCSV: {os.path.join(a.out, 'bildlauf.csv')}")
    return 1 if fehlerzahl else 0


if __name__ == "__main__":
    sys.exit(main())
