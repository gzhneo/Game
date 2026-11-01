#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""LAN-Partie mehrerer Instanzen auf einem Rechner (nur Windows).

Startet N Instanzen (2 bis 8) im Fenster mit -mehrfach und -netzLokal <i>: Instanz i bindet die
Loopback-Adresse 127.0.0.i (Lobby 8086, Spiel 8088), Rundrufe der Lobby gehen als Einzelsendungen an
127.0.0.1 bis 127.0.0.8, der Spielername bekommt die Instanznummer angehaengt, und die Einstellungen
(Options.ini, Network.ini) werden nicht geschrieben. Wahlweise mit Netzwerkemulator (Latenz, Schwankung,
Verlust, Doppelung, Umordnung) auf allen oder einzelnen Instanzen.

Bedienung danach von Hand: in Instanz 1 Mehrspieler -> Netzwerk -> Spiel erstellen, in den anderen
beitreten. Das Skript wartet, bis alle Instanzen beendet sind (es beendet keine selbst), und wertet dann
die Debug-Protokolle aus (m<PID>_DebugLogFileD.txt im Exe-Ordner, nur Debug-Build): Emulatorzahlen,
verworfene Befehle fremder Absender, Assertions.

Mit --relais startet das Werkzeug einen lokalen Treffpunkt-Server (gzh-rendezvous auf 127.0.0.1:18090) und die Instanzen mit
-treffpunkt und -netzNurRelais: der Gastgeber meldet seinen Raum an, und jeder Weg zwischen den Spiel-Sockets geht ueber das Relais
des Servers, kein direkter Weg. Die Auswertung nennt die Wege je Instanz (GameLinks-Zeilen) und die Statistik des Servers.

Beispiele:
  python tools/netz/lan_lokal.py --exe ... --anzahl 3 --auto --karte "(4)" --relais
  python tools/netz/lan_lokal.py --exe build-x64/GeneralsMD/Code/Main/Debug/GZHNeoZH.exe --anzahl 2
  python tools/netz/lan_lokal.py --exe ... --anzahl 3 --latenz 120 --schwankung 40 --verlust 5
  python tools/netz/lan_lokal.py --exe ... --anzahl 2 --latenz 300 --nur 2      (nur Instanz 2 leidet)
"""
import argparse, ctypes, glob, os, subprocess, sys, tempfile, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "abnahme"))

SERVER_PORT = 18090
MERKMALE = ("GameLinks - the way", "relay", "network emulator", "dropped a command", "ASSERTION", "CRC mismatch", "disconnect", "TESTABLAUF ERROR", "sealed traffic", "no keys for", "no usable key")


def skript(a, i):
    """Testablauf fuer Instanz i (--auto): 1 gastgebt, die anderen treten nacheinander bei."""
    name = "lan_host.txt" if i == 1 else "lan_beitritt.txt"
    text = open(os.path.join(HERE, "szenen", name), encoding="utf-8").read()
    # Beitretende warten, bis der Gastgeber angekuendigt hat; der Gastgeber, bis alle beigetreten sind
    warte = 2400 + 300 * (a.anzahl - 1) if i == 1 else 600 + 300 * (i - 2)
    pfad = os.path.join(a.skriptdir, f"lan_{i}.txt")
    with open(pfad, "w", encoding="utf-8") as f:
        karte = ""
        if i == 1 and a.karte:
            karte = ("klick LanGameOptionsMenu.wnd:ButtonSelectMap\n"
                     "fenster LanMapSelectMenu.wnd:ListboxMap\n"
                     f"auswahlText LanMapSelectMenu.wnd:ListboxMap {a.karte}\n"
                     "klick LanMapSelectMenu.wnd:ButtonOK\n"
                     "fenster LanGameOptionsMenu.wnd:ButtonStart\n")
        f.write(text.replace("{WARTE}", str(warte)).replace("{KARTE}", karte))
    return pfad


def maus_anstossen(pid, sekunden=45):
    """Das Hauptmenue erscheint erst nach der ersten Mausbewegung; ohne Eingabe (Testablauf) bliebe es unsichtbar.
    Schickt dem Fenster Mausbewegungen als Nachricht, der echte Mauszeiger bleibt unberuehrt."""
    from tastatur_test import fenster_von
    try:
        hwnd = fenster_von(pid)
    except Exception:
        return
    ende = time.time() + sekunden
    k = 0
    while time.time() < ende:
        ctypes.windll.user32.PostMessageW(hwnd, 0x0200, 0, (300 + k % 40) | ((300 + k % 30) << 16))	# WM_MOUSEMOVE
        k += 1
        time.sleep(0.5)


def finde_server(a):
    if a.server:
        return os.path.abspath(a.server)
    wurzel = os.path.abspath(os.path.join(HERE, "..", ".."))
    for ordner in ("build-x64", "build-x64-tools"):
        for kfg in ("Release", "Debug"):
            pfad = os.path.join(wurzel, ordner, "Code", "Tools", "Rendezvous", kfg, "gzh-rendezvous.exe")
            if os.path.isfile(pfad):
                return pfad
    return None


def befehl(a, i):
    # -ignoreAsserts: an assertion dialog would hold an instance forever, this tool waits for all of them;
    # the log check below still counts every ASSERTION line
    cmd = [a.exe, "-win", "-mehrfach", "-netzLokal", str(i), "-xres", str(a.xres), "-yres", str(a.yres), "-nologo",
           "-ignoreAsserts"]
    if a.nur is None or i in a.nur:
        for schalter, wert in (("-netzLatenz", a.latenz), ("-netzSchwankung", a.schwankung),
                               ("-netzVerlust", a.verlust), ("-netzDoppelt", a.doppelt),
                               ("-netzUmordnung", a.umordnung)):
            if wert:
                cmd += [schalter, str(wert)]
        if a.saat:
            cmd += ["-netzSaat", str(a.saat + i)]
    for mod in a.mod_alle or []:
        cmd += ["-mod", mod]
    if i == 1:
        for mod in a.mod_host or []:
            cmd += ["-mod", mod]
    if a.relais:
        cmd += ["-treffpunkt", a.treffpunkt or f"127.0.0.1:{SERVER_PORT}", "-netzNurRelais"]
    if a.auto:
        cmd += ["-testablauf", skript(a, i)]
    return cmd + [x.replace("{i}", str(i)) for x in (a.extra or [])]  # {i} = instance number, e.g. for a log file per instance


def auswerten(exe_dir, pids, start):
    fehler = 0
    for pid in pids:
        for log in sorted(glob.glob(os.path.join(exe_dir, f"m{pid}_DebugLogFile*.txt"))):
            if os.path.getmtime(log) < start - 1:
                continue
            print(f"== {os.path.basename(log)}")
            with open(log, encoding="latin-1", errors="replace") as f:
                for zeile in f:
                    if any(m in zeile for m in MERKMALE):
                        print("  " + zeile.rstrip())
                        if "ASSERTION" in zeile or "dropped a command" in zeile or "CRC mismatch" in zeile or "TESTABLAUF ERROR" in zeile:
                            fehler += 1
    return fehler


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--exe", required=True, help="GZHNeo.exe oder GZHNeoZH.exe (mit verlinkten Assets im selben Ordner)")
    p.add_argument("--anzahl", type=int, default=2, help="Instanzen, 2 bis 8")
    p.add_argument("--xres", type=int, default=1024)
    p.add_argument("--yres", type=int, default=768)
    p.add_argument("--latenz", type=int, default=0, help="ms Verzoegerung empfangener Pakete")
    p.add_argument("--schwankung", type=int, default=0, help="ms zusaetzliche zufaellige Verzoegerung")
    p.add_argument("--verlust", type=int, default=0, help="Prozent verlorener Pakete")
    p.add_argument("--doppelt", type=int, default=0, help="Prozent doppelt zugestellter Pakete")
    p.add_argument("--umordnung", type=int, default=0, help="Prozent zurueckgehaltener Pakete")
    p.add_argument("--saat", type=int, default=0, help="Saat des Emulators (je Instanz + Nummer); 0 = aus der PID")
    p.add_argument("--nur", type=int, nargs="+", help="Emulator nur auf diesen Instanzen")
    p.add_argument("--karte", help="--auto: Text im Kartennamen, etwa \"(4)\" fuer die erste Karte mit vier Plaetzen (Standard: die voreingestellte Karte, zwei Plaetze)")
    p.add_argument("--auto", action="store_true", help="Menues per Testablauf bedienen: Instanz 1 hostet, die anderen treten bei, alle spielen 900 Logik-Frames und beenden")
    p.add_argument("--relais", action="store_true", help="lokalen Treffpunkt-Server starten, jeder Weg zwischen den Spielern geht ueber sein Relais (-netzNurRelais)")
    p.add_argument("--server", help="--relais: gzh-rendezvous.exe (Standard: aus build-x64 oder build-x64-tools)")
    p.add_argument("--mod-alle", nargs="+", help="-mod <Ordner oder Archiv> fuer alle Instanzen")
    p.add_argument("--mod-host", nargs="+", help="-mod <Ordner oder Archiv> nur fuer Instanz 1 (den Gastgeber)")
    p.add_argument("--treffpunkt", help="--relais: ein laufender Treffpunkt-Server host[:port] statt des lokalen (es wird keiner gestartet)")
    p.add_argument("extra", nargs="*", help="weitere Startschalter (nach --)")
    a = p.parse_args()
    if not 2 <= a.anzahl <= 8:
        p.error("--anzahl muss zwischen 2 und 8 liegen")
    exe = os.path.abspath(a.exe)
    if not os.path.isfile(exe):
        p.error(f"{exe} fehlt")
    a.exe = exe
    a.skriptdir = tempfile.mkdtemp(prefix="lan_lokal_")

    from slot import wait_free
    wait_free(parallel=a.anzahl)

    server = None
    server_log = os.path.join(a.skriptdir, "server.txt")
    if a.relais and a.treffpunkt:
        print(f"Treffpunkt-Server {a.treffpunkt} (extern, wird nicht gestartet)")
    elif a.relais:
        exe_server = finde_server(a)
        if exe_server is None:
            p.error("--relais: gzh-rendezvous.exe nicht gefunden (--server angeben)")
        server = subprocess.Popen([exe_server, "--port", str(SERVER_PORT), "--bind", "127.0.0.1", "--stats", "5"],
                                  stdout=open(server_log, "w"), stderr=subprocess.STDOUT)
        print(f"Treffpunkt-Server {exe_server} (PID {server.pid}) auf 127.0.0.1:{SERVER_PORT}")
        time.sleep(1)

    start = time.time()
    procs = []
    for i in range(1, a.anzahl + 1):
        cmd = befehl(a, i)
        print(f"Instanz {i}:", " ".join(cmd))
        procs.append(subprocess.Popen(cmd, cwd=os.path.dirname(exe)))
        if a.auto:
            threading.Thread(target=maus_anstossen, args=(procs[-1].pid,), daemon=True).start()
        time.sleep(2)	# Fenster nacheinander, Protokolle mit eigener PID
    print("Warte auf das Ende aller Instanzen." if a.auto else "Warte auf das Ende aller Instanzen (von Hand beenden).")
    codes = [pr.wait() for pr in procs]
    if server is not None:
        time.sleep(6)	# one more statistics line
        server.terminate()
        server.wait()
        print("== Statistik des Treffpunkt-Servers (letzte Zeilen)")
        with open(server_log, encoding="latin-1", errors="replace") as f:
            for zeile in f.read().splitlines()[-4:]:
                print("  " + zeile)
    for i, (pr, code) in enumerate(zip(procs, codes), 1):
        print(f"Instanz {i} (PID {pr.pid}): Exit-Code {code}")
    fehler = auswerten(os.path.dirname(exe), [pr.pid for pr in procs], start)
    schlecht = sum(1 for c in codes if c != 0)
    print(f"{schlecht} Instanzen mit Exit-Code ungleich 0, {fehler} Fehlerzeilen in den Protokollen")
    return 1 if schlecht or fehler else 0


if __name__ == "__main__":
    sys.exit(main())
