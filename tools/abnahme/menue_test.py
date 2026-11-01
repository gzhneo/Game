#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Menue-Pruefung: kommt ein Testablauf durch das Hauptmenue? (nur Windows)

Startet das Spiel im Fenster mit Shell-Karte und Hauptmenue (ohne -quickstart) und einem Testablauf, der sich wie
ein Spieler durch die Menues klickt. Fuer jeden Testablauf, der ueber das Hauptmenue geht (lan_lokal.py,
zuschauen_test.py, Kampagnen- und Skirmish-Ablaeufe), muss das Hauptmenue erscheinen, ohne dass sich die echte
Maus bewegt -- Testablaeufe und -echteEingabeAus ignorieren sie.

Laeufe (je Exe):
  LAN       hauptmenue, klick ButtonMultiplayer, klick ButtonNetwork, fenster LanLobbyMenu.wnd:ButtonHost
  LAN_ohne  dasselbe mit -echteEingabeAus
  Skirmish  hauptmenue, klick ButtonSinglePlayer, klick ButtonSkirmish, fenster SkirmishGameOptionsMenu.wnd:ButtonStart

Je Lauf OK, wenn der Exit-Code 0 ist, jeder Befehl eine Zeile "TESTABLAUF OK line ..." hat und keine Zeile
"TESTABLAUF ERROR" vorkommt. Tabelle OK/FEHLER in <out>/ergebnis.txt und auf der Konsole.

Aufruf (aus der Wurzel des Repos), auch fuer beide Spiele auf einmal:
  python tools/abnahme/menue_test.py --exe build-x64/GeneralsMD/Code/Main/Release/GZHNeoZH.exe
  python tools/abnahme/menue_test.py --exe build-x64/GeneralsMD/Code/Main/Release/GZHNeoZH.exe \\
      --exe build-x64/Generals/Code/Main/Release/GZHNeo.exe
Exit-Code 0, wenn kein Lauf FEHLER meldet.
"""
import argparse, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

LAN = ["hauptmenue", "klick MainMenu.wnd:ButtonMultiplayer 900", "klick MainMenu.wnd:ButtonNetwork 900",
       "fenster LanLobbyMenu.wnd:ButtonHost 900", "beenden"]
SKIRMISH = ["hauptmenue", "klick MainMenu.wnd:ButtonSinglePlayer 900", "klick MainMenu.wnd:ButtonSkirmish 900",
            "fenster SkirmishGameOptionsMenu.wnd:ButtonStart 900", "beenden"]
LAEUFE = [("LAN", LAN, []), ("LAN_ohne", LAN, ["-echteEingabeAus"]), ("Skirmish", SKIRMISH, [])]


def zeilen(log):
    try:
        with open(log, encoding="latin-1") as f:
            return [z.rstrip("\r\n") for z in f if "TESTABLAUF" in z]
    except OSError:
        return []


def starte(a, exe, name, befehle, extra, out):
    skript = os.path.join(out, f"{name}.txt")
    with open(skript, "w", encoding="ascii", newline="\r\n") as f:
        f.write("\n".join(befehle) + "\n")
    log = os.path.join(out, f"{name}.log")
    if os.path.isfile(log):
        os.remove(log)
    xres, yres = a.res.split("x")
    cmd = [exe, "-win", "-xres", xres, "-yres", yres, "-noaudio", "-einstellungenNurLesen",
           "-crcLog", log, "-testablauf", skript] + extra
    from slot import wait_free
    wait_free()
    print(f"Lauf {name}:", " ".join(f'"{c}"' if " " in c else c for c in cmd), flush=True)
    proc = subprocess.Popen(cmd, cwd=os.path.dirname(exe))
    try:
        code = proc.wait(timeout=a.frist)
    except subprocess.TimeoutExpired:
        code = None
        print(f"Zeitlimit {a.frist} s ueberschritten, PID {proc.pid} laeuft noch -- bitte von Hand pruefen")
    return code, log


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--exe", required=True, action="append", help="GZHNeo.exe oder GZHNeoZH.exe (mehrfach moeglich)")
    p.add_argument("--res", default="1280x720")
    p.add_argument("--frist", type=int, default=300, help="Sekunden je Lauf")
    p.add_argument("--out", default="render-out/menue_test")
    a = p.parse_args()

    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    ergebnis = []
    for exe in a.exe:
        exe = os.path.abspath(exe)
        spiel = os.path.splitext(os.path.basename(exe))[0]
        for name, befehle, extra in LAEUFE:
            code, log = starte(a, exe, f"{spiel}_{name}", befehle, extra, out)
            z = zeilen(log)
            fehler = [x for x in z if "TESTABLAUF ERROR" in x]
            ok_zahl = sum(1 for x in z if "TESTABLAUF OK line" in x)
            ok = code == 0 and not fehler and ok_zahl >= len(befehle)
            text = f"Exit-Code {code}, {ok_zahl} von {len(befehle)} Befehlen OK"
            if fehler:
                text += f"; {fehler[0].split('TESTABLAUF ', 1)[-1]}"
            ergebnis.append((spiel, name, "OK" if ok else "FEHLER", text))

    text = [f"Menue-Pruefung {time.strftime('%Y-%m-%d %H:%M')}", "",
            f"{'Spiel':12}  {'Lauf':9}  Ergebnis  Einzelheiten"]
    text += [f"{s:12}  {n:9}  {e:8}  {t}" for s, n, e, t in ergebnis]
    zahl = sum(1 for e in ergebnis if e[2] == "FEHLER")
    text += ["", f"{zahl} FEHLER, {len(ergebnis) - zahl} OK"]
    with open(os.path.join(out, "ergebnis.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(text) + "\n")
    print("\n".join(text))
    print(f"Ergebnis: {os.path.join(out, 'ergebnis.txt')}")
    return 1 if zahl else 0


if __name__ == "__main__":
    sys.exit(main())
