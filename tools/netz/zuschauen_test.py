#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Zuschauen in laufenden Partien, vollautomatisch (nur Windows).

Je Variante drei Instanzen auf einem Rechner (-mehrfach, -netzLokal 1 bis 3), alle im Fenster und per Testablauf
(tools/netz/szenen/zuschauen_*.txt) bedient:
  Instanz 1 eroeffnet ein Netzwerkspiel, Instanz 2 tritt bei, beide spielen --logik Logik-Frames.
  Instanz 3 startet erst, wenn die Partie seit --vorlauf Sekunden laeuft, waehlt die laufende Partie in der LAN-Liste
  und wird Zuschauer (nicht in der Lobby, mitten in der Partie). Sie schaut zu, bewegt die Kamera und beendet sich.
  Danach startet Instanz 3 ein zweites Mal und schaut wieder zu.
Jede Instanz schreibt den CRC-Verlauf jedes Logik-Frames und die TESTABLAUF-Zeilen (-crcLog, -crcLogInterval 1).

Varianten (--variante, mehrfach; Standard alle vier): direkt, relais (lokaler Treffpunkt-Server, -netzNurRelais),
emulator (-netzLatenz 80 -netzSchwankung 20 -netzVerlust 2 auf allen Instanzen), relais+emulator.

Pruefungen je Variante, Ergebnis als Tabelle OK/FEHLER in <out>/ergebnis.txt und auf der Konsole:
  1 Spieler gleich          Instanz 1 und 2 haben in jedem gemeinsamen Frame dieselbe Logik-CRC (kein Desync)
  2 Zuschauer 1 gleich      erster Beitritt: wurde Zuschauer, sah mindestens --logik/3 Frames, alle CRCs wie Instanz 1
  3 Zuschauer 2 gleich      zweiter Beitritt ebenso
  4 Mitten in der Partie    der erste Frame des Zuschauers liegt vor dem Frame, den die Spieler bei seinem Start hatten
                            (er hat aufgeholt) -- er trat nicht in der Lobby bei
  5 Kein Befehl            der Zuschauer erreicht die Partie nicht: Pruefungen 1 bis 3 gleich, im Gastgeber-Protokoll
                            kein "dropped a command" und kein Befehl eines fremden Platzes (nur Debug-Bau)
  6 Kein Stocken            nachdem der Zuschauer gegangen ist, rechnet der Gastgeber in --stocken Sekunden mindestens
                            15 Frames je Sekunde weiter, und sein Protokoll zeigt keinen Wartebildschirm (Debug)
  7 Ohne Absturz            alle Exit-Codes 0, keine neuen Absturzberichte, keine TESTABLAUF ERROR-Zeile
  8 Ohne Assertion          keine ASSERTION- und keine "CRC mismatch"-Zeile in den Debug-Protokollen (nur Debug-Bau;
                            im Release-Bau "n/a")

Aufruf (aus der Wurzel des Repos, Debug-Bau fuer alle Pruefungen):
  python tools/netz/zuschauen_test.py --exe build-x64/GeneralsMD/Code/Main/Debug/GZHNeoZH.exe
  python tools/netz/zuschauen_test.py --exe ... --variante direkt --logik 3600
Exit-Code 0, wenn alle Pruefungen OK sind.
"""
import argparse, glob, os, shutil, subprocess, sys, tempfile, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "abnahme"))
sys.path.insert(0, HERE)

import lan_lokal  # noqa: E402  (maus_anstossen, finde_server, SERVER_PORT)

VARIANTEN = ("direkt", "relais", "emulator", "relais+emulator")
EMULATOR = ["-netzLatenz", "80", "-netzSchwankung", "20", "-netzVerlust", "2"]


def crc_verlauf(pfad):
    werte = {}
    if not os.path.isfile(pfad):
        return werte
    with open(pfad, encoding="latin-1", errors="replace") as f:
        for zeile in f:
            teile = zeile.split()
            if len(teile) == 2 and teile[0].isdigit() and len(teile[1]) == 8:
                werte[int(teile[0])] = teile[1]
    return werte


def testablauf(pfad):
    if not os.path.isfile(pfad):
        return []
    with open(pfad, encoding="latin-1", errors="replace") as f:
        return [z.rstrip() for z in f if z.startswith("TESTABLAUF")]


def gleich(a, b):
    gemeinsam = sorted(set(a) & set(b))
    falsch = [f for f in gemeinsam if a[f] != b[f]]
    return gemeinsam, falsch


def skript(ordner, name, ersetzen):
    text = open(os.path.join(HERE, "szenen", name), encoding="utf-8").read()
    for k, v in ersetzen.items():
        text = text.replace("{" + k + "}", str(v))
    pfad = os.path.join(ordner, name)
    with open(pfad, "w", encoding="utf-8") as f:
        f.write(text)
    return pfad


class Lauf:
    """Eine Variante: drei Instanzen, Protokolle in einem eigenen Ordner."""

    def __init__(self, a, variante, out):
        self.a = a
        self.variante = variante
        self.out = out
        self.exe_dir = os.path.dirname(a.exe)
        self.procs = []
        self.ergebnis = []

    def befehl(self, i, crc, szene):
        cmd = [self.a.exe, "-win", "-mehrfach", "-netzLokal", str(i), "-xres", str(self.a.xres), "-yres", str(self.a.yres),
               "-nologo", "-ignoreAsserts", "-crcLog", crc, "-crcLogInterval", "1", "-testablauf", szene]
        if "emulator" in self.variante:
            cmd += EMULATOR + ["-netzSaat", str(100 + i)]
        if "relais" in self.variante:
            cmd += ["-treffpunkt", f"127.0.0.1:{lan_lokal.SERVER_PORT}", "-netzNurRelais"]
        return cmd

    def starte(self, i, crc, szene):
        cmd = self.befehl(i, crc, szene)
        with open(os.path.join(self.out, "befehle.txt"), "a", encoding="utf-8") as f:
            f.write(" ".join(cmd) + "\n")
        pr = subprocess.Popen(cmd, cwd=self.exe_dir)
        threading.Thread(target=lan_lokal.maus_anstossen, args=(pr.pid,), daemon=True).start()
        self.procs.append((f"Instanz {i}", pr))
        return pr

    def warte_auf_zeile(self, pfad, text, frist):
        ende = time.time() + frist
        while time.time() < ende:
            if any(text in z for z in testablauf(pfad)):
                return True
            if any(pr.poll() not in (None, 0) for _, pr in self.procs):
                return False
            time.sleep(1)
        return False

    def pruefe(self, nummer, name, ok, text):
        zustand = "n/a" if ok is None else ("OK" if ok else "FEHLER")
        self.ergebnis.append((self.variante, nummer, name, zustand, text))

    def zuschauer(self, nummer, crc_host):
        """Instanz 3 schaut zu; liefert (Startframe der Spieler beim Start, Pfad des CRC-Verlaufs, Exit-Zeit)."""
        crc = os.path.join(self.out, f"zuschauer{nummer}.crc")
        frame_bei_start = max(crc_verlauf(crc_host) or {0: None})
        pr = self.starte(3, crc, self.szene_zuschauer)
        try:
            pr.wait(timeout=self.a.frist)
        except subprocess.TimeoutExpired:
            pr.kill()
        return frame_bei_start, crc, time.time()

    def lauf(self):
        a = self.a
        ordner = os.path.join(self.out, "szenen")
        os.makedirs(ordner, exist_ok=True)
        werte = {"LOGIK": a.logik, "WARTE": 2400}
        szene_host = skript(ordner, "zuschauen_host.txt", werte)
        szene_spieler = skript(ordner, "zuschauen_spieler.txt", werte)
        self.szene_zuschauer = skript(ordner, "zuschauen_zuschauer.txt", {"LOGIK": a.logik // 4})

        server = None
        if "relais" in self.variante:
            exe_server = lan_lokal.finde_server(a)
            if exe_server is None:
                self.pruefe(0, "Treffpunkt-Server", False, "gzh-rendezvous.exe nicht gefunden (--server)")
                return
            server = subprocess.Popen([exe_server, "--port", str(lan_lokal.SERVER_PORT), "--bind", "127.0.0.1", "--stats", "5"],
                                      stdout=open(os.path.join(self.out, "server.txt"), "w"), stderr=subprocess.STDOUT)
            time.sleep(1)

        start = time.time()
        crash_vorher = set(glob.glob(os.path.join(self.exe_dir, "Crashes", "*")))
        crc1 = os.path.join(self.out, "instanz1.crc")
        crc2 = os.path.join(self.out, "instanz2.crc")
        self.starte(1, crc1, szene_host)
        time.sleep(2)
        self.starte(2, crc2, szene_spieler)

        laeuft = self.warte_auf_zeile(crc1, "LOG SPIEL BEGINNT", a.frist)
        z1 = z2 = None
        stocken = None
        if laeuft:
            time.sleep(a.vorlauf)
            z1 = self.zuschauer(1, crc1)
            # the game goes on without the spectator
            vorher = max(crc_verlauf(crc1) or {0: None})
            time.sleep(a.stocken)
            nachher = max(crc_verlauf(crc1) or {0: None})
            stocken = (vorher, nachher)
            z2 = self.zuschauer(2, crc1)
        for _, pr in self.procs:
            try:
                pr.wait(timeout=a.frist)
            except subprocess.TimeoutExpired:
                pr.kill()
        if server is not None:
            server.terminate()
            server.wait()

        v1, v2 = crc_verlauf(crc1), crc_verlauf(crc2)
        gem, falsch = gleich(v1, v2)
        self.pruefe(1, "Spieler gleich", laeuft and len(gem) >= a.logik // 2 and not falsch,
                    f"{len(gem)} gemeinsame Frames, {len(falsch)} Abweichungen"
                    + (f", erste bei {falsch[0]}" if falsch else "") + ("" if laeuft else ", Partie begann nicht"))
        zuschauer_ok = []
        for nummer, z in ((1, z1), (2, z2)):
            if z is None:
                self.pruefe(1 + nummer, f"Zuschauer {nummer} gleich", False, "nicht gestartet")
                zuschauer_ok.append(False)
                continue
            vz = crc_verlauf(z[1])
            zeilen = testablauf(z[1])
            sah = any("LOG ZUSCHAUER SIEHT ZU" in t for t in zeilen)
            gem, falsch = gleich(v1, vz)
            ok = sah and len(gem) >= a.logik // 12 and not falsch
            zuschauer_ok.append(ok)
            self.pruefe(1 + nummer, f"Zuschauer {nummer} gleich", ok,
                        f"{'wurde Zuschauer' if sah else 'kein Zuschauen'}, {len(gem)} gemeinsame Frames, {len(falsch)} Abweichungen"
                        + (f", erste bei {falsch[0]}" if falsch else ""))
        if z1 is not None and crc_verlauf(z1[1]):
            erster = min(crc_verlauf(z1[1]))
            self.pruefe(4, "Mitten in der Partie", erster < z1[0] and z1[0] > 0,
                        f"Spieler bei Frame {z1[0]} beim Start, erster Frame des Zuschauers {erster}")
        else:
            self.pruefe(4, "Mitten in der Partie", False, "keine Frames des Zuschauers")

        logs = self.debug_logs(start)
        fremd = sum(1 for z in self.zeilen(logs.get("Instanz 1", []), ("dropped a command",)))
        debug = bool(logs)
        self.pruefe(5, "Kein Befehl", all(zuschauer_ok) and (fremd == 0),
                    f"Spieler- und Zuschauer-CRCs gleich, {fremd if debug else 'n/a'} fremde Befehle im Gastgeber-Protokoll")
        warten = self.zeilen(logs.get("Instanz 1", []), ("turnOnScreen",))
        if stocken is None:
            self.pruefe(6, "Kein Stocken", False, "kein Zuschauer gestartet")
        else:
            rate = (stocken[1] - stocken[0]) / max(a.stocken, 1)
            self.pruefe(6, "Kein Stocken", rate >= 15 and not warten,
                        f"{rate:.1f} Frames/s in {a.stocken} s nach dem Gehen, Wartebildschirm {len(warten) if debug else 'n/a'}")
        codes = [(n, pr.returncode) for n, pr in self.procs]
        crashes = sorted(set(glob.glob(os.path.join(self.exe_dir, "Crashes", "*"))) - crash_vorher)
        fehlerzeilen = [t for p in (crc1, crc2) + tuple(z[1] for z in (z1, z2) if z) for t in testablauf(p) if "TESTABLAUF ERROR" in t]
        self.pruefe(7, "Ohne Absturz", all(c == 0 for _, c in codes) and not crashes and not fehlerzeilen,
                    ", ".join(f"{n}: {c}" for n, c in codes) + f"; Absturzberichte {len(crashes)}; ERROR-Zeilen {len(fehlerzeilen)}"
                    + (f" ({fehlerzeilen[0]})" if fehlerzeilen else ""))
        asserts = [z for zeilen in logs.values() for z in self.zeilen(zeilen, ("ASSERTION", "CRC mismatch"))]
        self.pruefe(8, "Ohne Assertion", (not asserts) if debug else None,
                    (f"{len(asserts)} Zeilen" + (f", erste: {asserts[0][:120]}" if asserts else "")) if debug else "Release-Bau: kein Debug-Protokoll")

    def debug_logs(self, start):
        """Debug-Protokolle (m<PID>_DebugLogFile*.txt im Exe-Ordner, nur Debug-Bau) je Instanz; kopiert nach <out>."""
        logs = {}
        for name, pr in self.procs:
            for log in sorted(glob.glob(os.path.join(self.exe_dir, f"m{pr.pid}_DebugLogFile*.txt"))):
                if os.path.getmtime(log) < start - 1:
                    continue
                shutil.copy(log, os.path.join(self.out, f"{name.replace(' ', '')}_{pr.pid}_{os.path.basename(log)}"))
                with open(log, encoding="latin-1", errors="replace") as f:
                    logs.setdefault(name, []).extend(f.read().splitlines())
        return logs

    @staticmethod
    def zeilen(zeilen, merkmale):
        return [z for z in zeilen if any(m in z for m in merkmale)]


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--exe", required=True, help="GZHNeo.exe oder GZHNeoZH.exe (Debug-Bau fuer alle Pruefungen)")
    p.add_argument("--variante", action="append", choices=VARIANTEN, help="Standard: alle vier")
    p.add_argument("--logik", type=int, default=3600, help="Logik-Frames der Spieler (Standard 3600, zwei Minuten)")
    p.add_argument("--vorlauf", type=int, default=20, help="Sekunden Partie, bevor der Zuschauer startet")
    p.add_argument("--stocken", type=int, default=10, help="Sekunden, in denen die Partie nach dem Gehen weiterlaufen muss")
    p.add_argument("--frist", type=int, default=900, help="Sekunden je Instanz bis zum Abbruch")
    p.add_argument("--xres", type=int, default=800)
    p.add_argument("--yres", type=int, default=600)
    p.add_argument("--server", help="gzh-rendezvous.exe fuer die Relais-Varianten (Standard: aus build-x64 oder build-x64-tools)")
    p.add_argument("--out", default="render-out/zuschauen_test", help="Ordner fuer Ergebnis, CRC-Verlaeufe und Protokolle")
    a = p.parse_args()
    a.exe = os.path.abspath(a.exe)
    if not os.path.isfile(a.exe):
        p.error(f"{a.exe} fehlt")
    out = os.path.abspath(a.out)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)

    from slot import wait_free
    ergebnis = []
    for variante in a.variante or VARIANTEN:
        ordner = os.path.join(out, variante.replace("+", "_"))
        os.makedirs(ordner)
        print(f"== Variante {variante}: warte auf freie Spielstart-Warteschlange", flush=True)
        wait_free(parallel=3)
        lauf = Lauf(a, variante, ordner)
        lauf.lauf()
        ergebnis += lauf.ergebnis
        for v, n, name, zustand, text in lauf.ergebnis:
            print(f"   {zustand:6} {n} {name}: {text}", flush=True)

    breite = max(len(v) for v, *_ in ergebnis)
    zeilen = [f"Zuschauen-Test {time.strftime('%Y-%m-%d %H:%M')}, {a.exe}", ""]
    zeilen += [f"{'Variante':{breite}}  Nr  {'Pruefung':22}  Ergebnis  Einzelheiten"]
    for v, n, name, zustand, text in ergebnis:
        zeilen.append(f"{v:{breite}}  {n:2}  {name:22}  {zustand:8}  {text}")
    fehler = sum(1 for e in ergebnis if e[3] == "FEHLER")
    zeilen += ["", f"{fehler} FEHLER, {sum(1 for e in ergebnis if e[3] == 'OK')} OK, {sum(1 for e in ergebnis if e[3] == 'n/a')} n/a"]
    with open(os.path.join(out, "ergebnis.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(zeilen) + "\n")
    print("\n".join(zeilen))
    print(f"Ergebnis: {os.path.join(out, 'ergebnis.txt')}")
    return 1 if fehler else 0


if __name__ == "__main__":
    sys.exit(main())
