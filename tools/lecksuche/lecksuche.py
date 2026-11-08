#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Lecksuche: faehrt Szenen beider Spiele und zaehlt die Speicherlecks je Szene.

Jede Szene ist ein eigener Programmlauf bis zum sauberen Beenden; danach steht im DebugLogFileD.txt
der Leckbericht der Speicherpools (Zeilen "Leaked a block ...") und, ueber die Umgebungsvariable
GENERALS_CRT_LEAKFILE, der CRT-Leckbericht (malloc ausserhalb der Pools) in einer eigenen Datei.
Szenen mit Wachstumsmessung (Testablauf-Befehle merke/wachstum) schreiben zusaetzlich, was zwischen
zwei Zeitpunkten angelegt und nicht wieder freigegeben wurde -- etwa je Partie oder je Mission.
Unter Direct3D 11 (Debug-Build mit Debug-Layer) meldet das Spiel beim Beenden die noch lebenden
D3D11-Objekte (Zeilen "D3D11-LEBEND", R-140) und die Befunde des Debug-Layers ("D3D11-Debug-Layer").

Die Szenen fahren das Spiel ohne Eingabe: -testablauf <Datei> (Befehle: Kopf von
Code/GameEngine/GameClient/TestScript.cpp), Replays mit -replay. Szenen mit -runner zeichnen
in Partien nicht und haben keinen Ton, laufen dafuer schnell und neben anderen Spielen (--parallel);
Szenen ohne -runner laufen im Fenster und warten vorher auf eine freie Maschine (--warteschlange).
Jeder Lauf startet mit -mehrfach: keine Einzelinstanz-Sperre, Protokoll m<PID>_DebugLogFileD.txt.

Beispiele:
  python tools/lecksuche/lecksuche.py --build build-leak --parallel 4
  python tools/lecksuche/lecksuche.py --build build-leak --spiel zh --szenen menues,skirmish_usa
  python tools/lecksuche/lecksuche.py --build build-leak --auswahl abnahme --parallel 4
  python tools/lecksuche/lecksuche.py --liste
  python tools/lecksuche/lecksuche.py --auswerten lecksuche-out/generals-menues.log

Ausgabe in --out (Standard lecksuche-out): je Szene <spiel>-<szene>.log (Kopie des Protokolls),
.crt.txt (CRT-Bericht) und .runner.txt (Runner-/Testablauf-Protokoll), dazu bericht.txt und
bericht.json mit allen Gruppen. Exit-Code 0 = keine Lecks, kein Wachstum, alle Szenen gelaufen;
1 = Befund; 2 = Aufruffehler.

Den Build mit Aufrufketten legt man so an (eigener Ordner, etwa doppelte Laufzeit):
  cmake --preset x64-debug -B build-leak -DENABLE_MEMORYPOOL_STACKTRACE=ON
  cmake --build build-leak --config Debug --target GZHNeo GZHNeoZH
Ohne Aufrufketten (build-x64) zaehlt das Werkzeug trotzdem, gruppiert dann aber nur nach Pool und Tag.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor

HIER = os.path.dirname(os.path.abspath(__file__))
WURZEL = os.path.dirname(os.path.dirname(HIER))
SZENEN_DIR = os.path.join(HIER, "szenen")

SPIELE = {
    "generals": ("Generals", "GZHNeo.exe"),
    "zh": ("GeneralsMD", "GZHNeoZH.exe"),
}
# Benutzerdaten je Spiel unter "Eigene Dateien" (Einstellungen, Spielstaende)
BENUTZERORDNER = {
    "generals": os.path.join("GZH Neo", "Generals"),
    "zh": os.path.join("GZH Neo", "Zero Hour"),
}
# Protokollname je Konfiguration (Debug.cpp DEBUG_FILE_NAME)
LOGNAME = {"Debug": "DebugLogFileD.txt", "Internal": "DebugLogFileI.txt"}

# -fixedTimeStep: ohne Wartezeit, fester Client-Seed; zeigt das Hauptmenue ohne Mausbewegung (MainMenu.cpp).
# -noVSync: die Menues werden auch im Runner gezeichnet, ohne Bildsynchronisation laufen sie schneller.
# -mehrfach: keine Einzelinstanz-Sperre, Protokolle je Instanz (m<PID>_...), damit Laeufe nebeneinander gehen.
FENSTER = ["-win", "-xres", "800", "-yres", "600", "-fixedTimeStep", "-noVSync", "-mehrfach"]


class Szene:
    def __init__(self, name, beschreibung, skript=None, replay=None, runner=True, spiele=("generals", "zh"),
                 extra=(), extra_je_spiel=None, frist=1800, allein=False, lan=False):
        self.name = name
        self.beschreibung = beschreibung
        self.skript = skript            # Dateiname unter szenen/ (je Spiel "<name>.<spiel>.txt" bevorzugt)
        self.replay = replay            # dict spiel -> Replay-Ordner oder -Datei relativ zu tests/replays
        self.runner = runner
        self.spiele = spiele
        self.extra = list(extra)
        self.extra_je_spiel = extra_je_spiel or {}
        self.frist = frist
        # allein: nicht neben anderen eigenen Laeufen (Laden ueber das Lade-Menue nimmt den juengsten Spielstand,
        # parallele Kampagnen legen dort automatische Spielstaende an)
        self.allein = allein
        # lan: oeffnet die LAN-Lobby. Beide Spiele binden denselben LAN-Anschluss (UDP 8086); bekommt eine
        # Instanz ihn nicht, bleibt sie im Hauptmenue stehen. Solche Laeufe laufen deshalb nacheinander.
        self.lan = lan


# Reihenfolge = Laufreihenfolge innerhalb jeder Gruppe (Runner parallel, Fenster nacheinander, "allein" zum Schluss).
SZENEN = [
    Szene("start", "Start bis ins Hauptmenue und Beenden (wie die Abnahme)", "start.txt"),
    Szene("menues", "Menues durchklicken: Optionen, LAN-Lobby, Direktverbindung, eigenes LAN-Spiel, Skirmish-Menue, "
          "Replays, Spielstaende, Mitwirkende, (ZH) Generals-Herausforderung", "menues.txt", lan=True),
    Szene("menue_wachstum", "Menues fuenfmal durchklicken, Wachstum ueber die Runden zwei bis fuenf", "menue_wachstum.txt",
          frist=2400, lan=True),
    Szene("skirmish_usa", "Skirmish USA gegen 1 KI ueber das Menue, Sieg (Strg+W), Punkte, Hauptmenue",
          "skirmish_usa.txt"),
    Szene("skirmish_china", "Skirmish China, Niederlage (alle eigenen Objekte vernichtet)", "skirmish_china.txt"),
    Szene("skirmish_gla", "Skirmish GLA, alle Gegner vernichtet (Sieg ueber die Siegbedingungen)",
          "skirmish_gla.txt"),
    Szene("skirmish_generaele", "(ZH) Skirmish je General (9 Partien, Sieg), Wachstum ueber die letzten acht",
          "skirmish_generaele.txt", spiele=("zh",), frist=5400),
    Szene("skirmish_wachstum", "vier Skirmish-Partien mit festen Fraktionen, Netto-Wachstum je Partie (dritte und vierte)",
          "skirmish_wachstum.txt", frist=3600),
    Szene("skirmish_neueki", "Skirmish gegen die neue KI (Brutal, neue KI), speichern/laden, drei Partien mit "
          "Wachstum je Partie", "skirmish_neueki.txt", frist=5400),
    Szene("skirmish_beobachter", "Skirmish als Beobachter (Platz 0), klassische gegen neue KI, speichern/laden als "
          "Beobachter, zwei Partien bis zur Punkteanzeige", "skirmish_beobachter.txt", frist=3600),
    Szene("ki_dauer", "KI gegen KI ohne Menue (-skirmish, neue und klassische KI im Bund, N1,N1,K2,K2), 20000 "
          "Logik-Frames, Wachstum ueber die zweite Haelfte", "ki_dauer.txt",
          extra_je_spiel={"generals": ["-skirmish", "Tournament Lake", "-skirmishAI", "N1,N1,K2,K2",
                                       "-skirmishSeed", "2"],
                          "zh": ["-skirmish", "Tournament Lake", "-skirmishAI", "N1,N1,K2,K2", "-skirmishSeed", "2"]},
          frist=5400),
    Szene("partie_verlassen", "Skirmish, Spielmenue, Partie verlassen, Hauptmenue", "partie_verlassen.txt"),
    Szene("lan_partie", "LAN-Spiel eroeffnen, Platz 2 mit KI, Netzwerkpartie starten, ueber das Spielmenue "
          "verlassen, Punkteanzeige, LAN-Lobby, Hauptmenue", "lan_partie.txt", frist=2400, lan=True),
    Szene("spielmenue", "Skirmish: Spielmenue mit Optionen, Speichern/Laden-Fenster, Neustart der Partie, "
          "Aufgeben", "spielmenue.txt", frist=2400),
    Szene("testhilfen_mission", "-mission USA 1 mit -testhilfen: Schnellspeichern, Schnellladen, Sieg, "
          "zweite Mission", "testhilfen_mission.txt", extra=["-testhilfen", "-mission", "USA", "1"], frist=3600),
    Szene("kampagne_usa", "Kampagne USA: drei Missionen hintereinander (zwei Missionswechsel)",
          "kampagne_usa.txt", frist=5400),
    Szene("kampagne_china", "Kampagne China: drei Missionen hintereinander (zwei Missionswechsel)",
          "kampagne_china.txt", frist=5400),
    Szene("kampagne_gla", "Kampagne GLA: drei Missionen hintereinander (zwei Missionswechsel)",
          "kampagne_gla.txt", frist=5400),
    Szene("kampagne_wachstum", "Kampagne USA 1 viermal ueber das Spielmenue verlassen, Wachstum je Mission (dritte und vierte)",
          "kampagne_wachstum.txt", frist=3600),
    Szene("challenge", "(ZH) Generals-Herausforderung: erster General, zwei Gegner nacheinander",
          "challenge.txt", spiele=("zh",), frist=3600),
    Szene("schliessen_partie", "Fenster schliessen (WM_CLOSE wie Alt+F4) mitten in der Partie",
          "schliessen_partie.txt"),
    Szene("schliessen_menue", "Fenster schliessen (WM_CLOSE wie Alt+F4) im Hauptmenue", "schliessen_menue.txt"),
    Szene("schliessen_optionen", "Fenster schliessen (WM_CLOSE wie Alt+F4) bei offenem Optionsmenue",
          "schliessen_optionen.txt"),
    Szene("schliessen_laden", "Fenster schliessen (WM_CLOSE) waehrend des Ladebildschirms einer Partie",
          "schliessen_laden.txt"),
    Szene("schliessen_lan", "Fenster schliessen (WM_CLOSE) mitten in einer LAN-Partie", "schliessen_lan.txt", lan=True),
    Szene("schliessen_punkte", "Fenster schliessen (WM_CLOSE) in der Punkteanzeige nach einer Kampagnenmission",
          "schliessen_punkte.txt", frist=2400),
    Szene("replays", "alle Referenzreplays bis zum Ende (-runner)", replay={"generals": "x64", "zh": "zh/x64"},
          frist=7200),
    # nicht neben anderen eigenen Laeufen: laedt ueber das Lade-Menue den juengsten Spielstand
    Szene("speichern_laden", "Skirmish speichern, in der Partie laden, Hauptmenue, aus dem Hauptmenue laden",
          "speichern_laden.txt", frist=2400, allein=True),
    # im Fenster, mit Zeichnen und Ton
    Szene("shellmap_lang", "Shell-Map mit Ton und Zeichnen, 6000 Bilder, Beenden-Knopf", "shellmap_lang.txt",
          runner=False, frist=1800),
    Szene("skirmish_fenster", "Skirmish mit Zeichnen und Ton, Sieg, Hauptmenue, Fenster schliessen",
          "skirmish_fenster.txt", runner=False, frist=1800),
    Szene("optionen_detail", "Optionsmenue: Detailstufen Niedrig/Ultra/Hoch, Aufloesungswechsel hin und zurueck, "
          "dazwischen Skirmish mit Zeichnen (D3D11-Nachbearbeitung an/aus)", "optionen_detail.txt", runner=False,
          frist=2400),
    Szene("replay_fenster", "ein Referenzreplay mit Zeichnen und Ton bis zum Ende",
          "replay_fenster.txt", replay={"generals": "x64/e0-alpine-assault-2ai-seed1.rep",
                                        "zh": "zh/x64/e0zh-green-pastures-2ai-seed1.rep"},
          runner=False, frist=3600),
    Szene("testhilfen", "Skirmish mit -testhilfen im Fenster: Seitenleiste, Anzeige, alle Logik-Testhilfen, "
          "Zeitraffer, EA-Diagnosekuerzel und Tageszeit; drei Partien, Wachstum ueber die dritte", "testhilfen.txt",
          extra=["-testhilfen"], runner=False, frist=3600),
    Szene("kampagne_fenster", "Kampagne USA mit Zeichnen, Ton und Videos: zwei Missionen, Sieg, Beenden",
          "kampagne_fenster.txt", runner=False, frist=3600),
]

# Kurze Auswahl fuer die Abnahme: Start, Menues mit Wachstum, Skirmish mit Wachstum
# je Partie, Kampagnenmission mit Wachstum je Laden, LAN-Partie, Beenden mitten in der Partie und im
# Ladebildschirm, Speichern/Laden und die Fensterszene mit Zeichnen und Ton. Mit --parallel 4 unter 10 Minuten
# fuer beide Spiele (Aufrufketten-Build build-leak; ohne Aufrufketten wird Wachstum nur gezaehlt).
AUSWAHL = {
    "abnahme": ["start", "menue_wachstum", "skirmish_wachstum", "kampagne_wachstum", "lan_partie",
                "schliessen_partie", "schliessen_laden", "speichern_laden", "skirmish_fenster"],
}

LEAK_RE = re.compile(r"^Leaked a block of size (-?\d+), tagstring (.*), from pool/dma (.*)$")
TOTAL_RE = re.compile(r"There were (\d+) memory leaks")
RAW_RE = re.compile(r"Leaked a total of (\d+) raw bytes")
UNFREED_RE = re.compile(r"^\*\*Unfreed")
CRT_BLOCK_RE = re.compile(r"^(?:(.*)\(\d+\) : )?\{(\d+)\} (\w+) block at 0x[0-9A-Fa-f]+, (\d+) bytes long\.")
D3D_MELDUNG_RE = re.compile(r"D3D11-Debug-Layer \[(\w+), ID (-?\d+)\]: (.*)$")
D3D_LEBEND_RE = re.compile(r"D3D11-LEBEND (.*)$")
STACK_SUFFIX = ",    STACKTRACE"


def stapelzeile(zeile):
    """'...,    STACKTRACE' -> Text ohne Anhang, sonst None."""
    z = zeile.rstrip("\r\n")
    if z.endswith("STACKTRACE"):
        z = z[: z.rfind(",")] if "," in z else z
        return z.strip()
    return None


def pfad_kurz(text):
    """Tag-Namen enthalten oft __FILE__: Pfad bis Code\\ weglassen (auch aus anderen Worktrees)."""
    return re.sub(r"[A-Za-z]:[\\/][^,]*?[\\/](Code[\\/])", r"\1", text)


def rahmen_kurz(rahmen):
    """Aufrufkette auf die aussagekraeftigen Rahmen kuerzen (ohne Speicherverwaltung und new)."""
    aus = []
    for r in rahmen:
        if r.startswith("Allocation Stack Trace"):
            continue
        low = r.lower()
        if any(x in low for x in ("gamememory.cpp", "operator new", "allocateblock", "allocatebytes",
                                  "newinstance", "xmemory", "\\vc\\tools\\msvc", "memorypool")):
            continue
        # Adresse weg, Pfade relativ zur Wurzel des Quellbaums (auch aus anderen Worktrees)
        r = re.sub(r"; 0x[0-9A-Fa-f]+$", "", r)
        r = re.sub(r"^.*?[\\/](Code[\\/])", r"\1", r)
        aus.append(r)
    return aus


def leeres_ergebnis(fehler):
    return {"lecks": [], "wachstum": {}, "fehler": list(fehler), "gemeldet": None, "raw": 0, "unfreed": [],
            "leckbericht": False, "d3d_meldungen": [], "d3d_lebend": []}


def lies_log(pfad):
    """Wertet ein DebugLogFileD.txt aus: Lecks, Wachstum, Testablauf-Fehler, D3D11-Befunde."""
    with open(pfad, "r", encoding="latin-1", errors="replace") as f:
        zeilen = f.readlines()
    lecks = []          # (groesse, tag, pool, [rahmen])
    wachstum = {}       # label -> {"gesamt": (n, bytes, gruppen), "gruppen": [(n, bytes, owner, tag, [rahmen])]}
    fehler = []
    gemeldet = None
    raw = 0
    unfreed = []
    d3d_meldungen = []  # (schwere, id, text)
    d3d_lebend = []
    i = 0
    n = len(zeilen)
    while i < n:
        z = zeilen[i].rstrip("\r\n")
        m = LEAK_RE.match(z)
        if m:
            rahmen = []
            i += 1
            while i < n:
                s = stapelzeile(zeilen[i])
                if s is None:
                    break
                rahmen.append(s)
                i += 1
            lecks.append((int(m.group(1)), pfad_kurz(m.group(2)), m.group(3), rahmen))
            continue
        if z.startswith("WACHSTUM,"):
            teile = z.split(",", 6)
            label = teile[1]
            w = wachstum.setdefault(label, {"gesamt": None, "gruppen": []})
            if teile[2] == "gesamt":
                w["gesamt"] = tuple(int(x) for x in teile[3:6] if x)
            elif teile[2] == "gruppe":
                rahmen = []
                i += 1
                while i < n:
                    s = stapelzeile(zeilen[i])
                    if s is None:
                        break
                    rahmen.append(s)
                    i += 1
                w["gruppen"].append((int(teile[3]), int(teile[4]), teile[5], pfad_kurz(teile[6]) if len(teile) > 6 else "",
                                     rahmen))
                continue
            i += 1
            continue
        if "TESTABLAUF ERROR" in z or "TESTABLAUF FEHLER" in z:  # FEHLER: Protokolle vor Nr. 75
            fehler.append(z[z.find("TESTABLAUF"):])
        if "EXCEPTION DUMP" in z and "END EXCEPTION" not in z:
            fehler.append("Absturz (EXCEPTION DUMP im Protokoll, Crashes-Ordner neben der Exe)")
        if z.startswith(("ABORT:", "ABBRUCH:")):  # ABBRUCH: Protokolle vor Nr. 75
            fehler.append("Abbruch (abort(), Stapel im Protokoll)")
        m = D3D_MELDUNG_RE.search(z)
        if m:
            d3d_meldungen.append((m.group(1), int(m.group(2)), m.group(3)))
        m = D3D_LEBEND_RE.search(z)
        if m:
            d3d_lebend.append(m.group(1))
        m = TOTAL_RE.search(z)
        if m:
            gemeldet = int(m.group(1))
        m = RAW_RE.search(z)
        if m:
            raw = int(m.group(1))
        if UNFREED_RE.match(z):
            unfreed.append(z)
        i += 1
    return {"lecks": lecks, "wachstum": wachstum, "fehler": fehler, "gemeldet": gemeldet, "raw": raw,
            "unfreed": unfreed, "leckbericht": any("Begin Simple Leak Report" in z for z in zeilen),
            "d3d_meldungen": d3d_meldungen, "d3d_lebend": d3d_lebend}


def lies_crt(pfad):
    """CRT-Leckbericht (_CrtDumpMemoryLeaks): Bloecke mit Nummer und Groesse."""
    if not os.path.isfile(pfad):
        return None
    bloecke = []
    with open(pfad, "r", encoding="latin-1", errors="replace") as f:
        for z in f:
            m = CRT_BLOCK_RE.match(z.strip())
            if m:
                bloecke.append((m.group(1) or "", int(m.group(2)), m.group(3), int(m.group(4))))
    return bloecke


def gruppiere(lecks, tiefe=6):
    gruppen = {}
    for groesse, tag, pool, rahmen in lecks:
        kurz = tuple(rahmen_kurz(rahmen)[:tiefe])
        key = (pool, tag, kurz)
        g = gruppen.setdefault(key, [0, 0])
        g[0] += 1
        g[1] += max(groesse, 0)
    return sorted(((v[0], v[1], k[0], k[1], list(k[2])) for k, v in gruppen.items()), key=lambda x: (-x[0], -x[1]))


def eigene_dateien():
    """Ordner 'Eigene Dateien' (CSIDL_PERSONAL); dort legt das Spiel seine Benutzerdaten an."""
    try:
        import ctypes
        puffer = ctypes.create_unicode_buffer(260)
        if ctypes.windll.shell32.SHGetFolderPathW(None, 5, None, 0, puffer) == 0:
            return puffer.value
    except (AttributeError, OSError):
        pass
    return os.path.join(os.path.expanduser("~"), "Documents")


def spielstaende(spiel):
    """Dateien im Spielstand-Ordner des Spiels."""
    ordner = os.path.join(eigene_dateien(), BENUTZERORDNER[spiel], "Save")
    try:
        return set(os.listdir(ordner))
    except OSError:
        return set()


def spielstaende_aufraeumen(spiel, vorher):
    """Neue Spielstaende der Laeufe entfernen: Test-Spielstaende, Schnellspeicher und die automatischen
    Spielstaende beim Missionswechsel der Kampagne (00000nnn.sav). Dateien, die vorher schon da waren, bleiben."""
    ordner = os.path.join(eigene_dateien(), BENUTZERORDNER[spiel], "Save")
    for name in spielstaende(spiel) - vorher:
        if name.lower().endswith(".sav"):
            try:
                os.remove(os.path.join(ordner, name))
            except OSError:
                pass


def benutzer_ini_sichern(spiel):
    """Die Szenen gehen durch die Menues, und die schreiben Einstellungen (Options.ini: Kampagnen-
    Schwierigkeit, Detailstufe, Aufloesung; Skirmish.ini: Fraktion, Karte, ...). Einmal vor allen Laeufen
    sichern, nach jedem Lauf zurueckschreiben, damit die Einstellungen des Nutzers unveraendert bleiben."""
    gesichert = {}
    basis = eigene_dateien()
    for o in [os.path.join(basis, BENUTZERORDNER[spiel])]:
        try:
            namen = os.listdir(o)
        except OSError:
            continue
        gesichert[o] = {n: open(os.path.join(o, n), "rb").read() for n in namen
                        if n.lower().endswith(".ini") and os.path.isfile(os.path.join(o, n))}
    return gesichert


def benutzer_ini_zurueck(gesichert):
    for o, dateien in gesichert.items():
        try:
            jetzt = [n for n in os.listdir(o) if n.lower().endswith(".ini")]
        except OSError:
            continue
        for n in jetzt:
            if n not in dateien:
                try:
                    os.remove(os.path.join(o, n))
                except OSError:
                    pass
        for n, inhalt in dateien.items():
            p = os.path.join(o, n)
            try:
                if not os.path.isfile(p) or open(p, "rb").read() != inhalt:
                    with open(p, "wb") as f:
                        f.write(inhalt)
            except OSError:
                pass


def exe_pfad(build, spiel, konfig):
    baum, exe = SPIELE[spiel]
    return os.path.join(os.path.abspath(build), baum, "Code", "Main", konfig, exe)


def laeuft_spiel():
    try:
        out = subprocess.run(["tasklist", "/fo", "csv", "/nh"], capture_output=True, text=True,
                             errors="replace", timeout=120).stdout.lower()
    except (OSError, subprocess.SubprocessError):
        return False
    return any(n in out for n in ('"gzhneo.exe"', '"gzhneozh.exe"', '"worldbuilder'))


def warte_frei(slot):
    """Vor Fensterlaeufen: faire Warteschlange (slot.py mit wait_free()) oder einfaches Warten. Die Laeufe haben
    -mehrfach, neben einem Spiel des Nutzers oder eines anderen Agenten darf also eines laufen (parallel=2)."""
    if slot:
        import importlib.util
        spec = importlib.util.spec_from_file_location("lecksuche_slot", os.path.abspath(slot))
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        try:
            mod.wait_free(parallel=2)
        except TypeError:
            mod.wait_free()
        return
    while laeuft_spiel():
        print("  warte auf freie Maschine ...", flush=True)
        time.sleep(30)


class Lauf:
    def __init__(self, szene, spiel, name, args):
        self.szene = szene
        self.spiel = spiel
        self.name = name
        self.args = args


def plane(szene, spiel, a):
    """Laeufe einer Szene fuer ein Spiel: (laufname, args)."""
    laeufe = []
    skript = None
    if szene.skript:
        skript = os.path.join(SZENEN_DIR, szene.skript.replace(".txt", f".{spiel}.txt"))
        if not os.path.isfile(skript):
            skript = os.path.join(SZENEN_DIR, szene.skript)
    modus = ["-runner"] if szene.runner else ["-nologo"]
    extra = szene.extra + szene.extra_je_spiel.get(spiel, [])
    if szene.replay:
        pfad = os.path.join(WURZEL, "tests", "replays", szene.replay[spiel])
        reps = [pfad] if os.path.isfile(pfad) else [os.path.join(pfad, r) for r in sorted(os.listdir(pfad))
                                                      if r.lower().endswith(".rep")]
        for rep in reps:
            name = f"{spiel}-{szene.name}" + ("" if os.path.isfile(pfad) else f"-{os.path.splitext(os.path.basename(rep))[0]}")
            args = modus + ["-replay", rep] + (["-testablauf", skript] if skript else []) + extra
            laeufe.append(Lauf(szene, spiel, name, args))
    else:
        laeufe.append(Lauf(szene, spiel, f"{spiel}-{szene.name}", modus + ["-testablauf", skript] + extra))
    return laeufe


class Werkstatt:
    """Gemeinsamer Zustand aller Laeufe: gesicherte Einstellungen je Spiel (vor allen Laeufen), Sperre fuer das
    Zurueckschreiben und die Ausgabe."""

    def __init__(self, a, spiele):
        self.a = a
        self.sperre = threading.Lock()
        self.ini = {s: benutzer_ini_sichern(s) for s in spiele}
        self.saves = {s: spielstaende(s) for s in spiele}
        # beide Spiele binden denselben LAN-Anschluss (UDP 8086): je Zeitpunkt nur ein LAN-Lauf
        self.lan_sperre = threading.Lock()

    def protokoll(self, t):
        with self.sperre:
            print(t, flush=True)

    def aufraeumen(self):
        for s, ini in self.ini.items():
            benutzer_ini_zurueck(ini)
        for s, vorher in self.saves.items():
            spielstaende_aufraeumen(s, vorher)


def fuehre_aus(lauf, w):
    """Einen Lauf ausfuehren; liefert (laufname, ergebnis-dict)."""
    a = w.a
    exe = exe_pfad(a.build, lauf.spiel, a.konfig)
    if not os.path.isfile(exe):
        return (lauf.name, leeres_ergebnis([f"Exe fehlt: {exe}"]))
    exe_dir = os.path.dirname(exe)
    out = os.path.abspath(a.out)
    runner_log = os.path.join(out, lauf.name + ".runner.txt")
    crt_log = os.path.join(out, lauf.name + ".crt.txt")
    for p in (runner_log, crt_log):
        if os.path.exists(p):
            os.remove(p)
    if not lauf.szene.runner:
        warte_frei(a.warteschlange)
    env = dict(os.environ, GENERALS_CRT_LEAKFILE=crt_log)
    ton = [] if a.mit_ton else ["-noaudio"]
    cmd = [exe] + FENSTER + ton + lauf.args + ["-ignoreAsserts", "-crcLog", runner_log] + a.extra
    if a.benutzerdaten:
        # je Spiel ein Unterordner, damit Spielstaende und Replays der beiden Spiele getrennt bleiben
        cmd += ["-benutzerdaten", os.path.join(a.benutzerdaten, "ZeroHour" if lauf.spiel == "zh" else "Generals")]
    w.protokoll(f"  {lauf.name}: {' '.join(os.path.basename(c) if c == exe else c for c in cmd[1:])}")
    start = time.time()
    proc = subprocess.Popen(cmd, cwd=exe_dir, env=env)
    try:
        code = proc.wait(timeout=lauf.szene.frist)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        code = None
    dauer = time.time() - start
    with w.sperre:
        benutzer_ini_zurueck(w.ini[lauf.spiel])
    # -mehrfach: Protokolle mit Vorsatz m<PID>_
    vorsatz = f"m{proc.pid}_"
    quelle = os.path.join(exe_dir, vorsatz + LOGNAME.get(a.konfig, "DebugLogFile.txt"))
    log_ziel = os.path.join(out, lauf.name + ".log")
    if os.path.isfile(quelle):
        shutil.copyfile(quelle, log_ziel)
        erg = lies_log(log_ziel)
    else:
        erg = leeres_ergebnis([f"kein Protokoll {os.path.basename(quelle)}"])
    for f in os.listdir(exe_dir):
        if f.startswith(vorsatz):
            try:
                os.remove(os.path.join(exe_dir, f))
            except OSError:
                pass
    erg["exit"] = code
    erg["dauer"] = dauer
    erg["crt"] = lies_crt(crt_log)
    if code is None:
        erg["fehler"].append(f"Zeitlimit {lauf.szene.frist} s")
    elif code not in (0, 1):
        erg["fehler"].append(f"Exit-Code {code}")
    if not erg["leckbericht"]:
        erg["fehler"].append("kein Leckbericht im Protokoll (nicht sauber beendet?)")
    return (lauf.name, erg)


# Zwischenspeicher, die bis zum Programmende leben und nur mit neuen Inhalten wachsen (eine Partie mit anderen
# Fraktionen, Einheiten oder Karten laedt Neues nach). Wachstum hier ist kein Leck; es waechst nicht je Partie,
# sondern je noch nicht gesehenem Inhalt. Muster: Teiltext eines Rahmens der Aufrufkette oder des Tags.
ZWISCHENSPEICHER = [
    ("W3D-Modelle, Animationen, Texturen (Asset-Manager)",
     ("WW3DAssetManager::Load_3D_Assets", "WW3DAssetManager::Load_Prototype", "HAnimManagerClass::",
      "HTreeManagerClass::", "WW3DAssetManager::Get_Texture", "TextureLoadTaskClass::", "TextureLoader::")),
    ("Namensschluessel (NameKeyGenerator)", ("NameKeyGenerator::nameToKey", "NameKeyGenerator::nameToLowercaseKey",
                                              ": NAMEKEY()", "NameKeyBucketPool")),
    ("Dateiexistenz (FileSystem::doesFileExist)", ("FileSystem::doesFileExist",)),
    ("Modellzustaende je Vorlage (ModelConditionInfo)", ("ModelConditionInfo::",)),
    ("Schrift-Glyphen (FontCharsClass)", ("FontCharsClass",)),
    ("beste Treffer je Zustand (SparseMatchFinder)", ("SparseMatchFinder<",)),
    ("Freiliste Geisterobjekte (W3DGhostObjectManager)", ("W3DGhostObjectManager::addGhostObject",)),
    ("Audiodateien (AudioFileCache, begrenzt)", ("AudioFileCache::openFile",)),
    ("Upgrade-Masken je Modul (UpgradeMuxData)", ("UpgradeMuxData::getUpgradeActivationMasks",)),
    # std::deque gibt Bloecke beim Leeren nicht frei, sondern nimmt sie wieder; waechst nur mit der Hoechstlaenge
    ("Warteschlange Sichtaufhebung (std::deque-Bloecke)", ("PartitionManager::queueUndoShroudReveal",)),
    ("Warteschlange des Job-Systems (waechst nur mit der Hoechstlaenge)", ("JobSystem::Impl::push",)),
    ("Ressourcen der Gelaende-Tessellation (beim ersten Gebrauch angelegt)", ("RenderCore::Tt_Ensure_Resources",
                                                                           "RenderCore::Tt_Query_Begin")),
    ("Einstellungsdokument im Speicher (Settings::Document)", ("Settings::Document::set",)),
    ("Nachschlagetabelle der Drawables (waechst mit der hoechsten Kennung)", ("GameClient::addDrawableToLookupTable",)),
    ("Laufzeitstatistik je Modulart (nur mit -crcLog)", ("PhaseStatistics::moduleTime",)),
    ("Klippentabelle in Kartengroesse (je Karte neu angelegt)", ("BaseHeightMapRenderObjClass::updateViewImpassableAreas",)),
    ("Generalsfaehigkeiten je Spieler (geleert je Partie, Kapazitaet bis zur groessten Zahl)", ("Player::addScience",)),
]


def einfaerbe_vorlage(rahmen):
    """W3DAssetManager::Create_Render_Obj legt fuer jede neue Kombination aus Modell, Farbe und Massstab eine
    eigene Vorlage an (W3DPrototypeClass mit einem eingefaerbten Exemplar, Add_Prototype) und klont kuenftig
    davon -- ein Zwischenspeicher. Erkennbar an der aeussersten Stelle in W3DAssetManager::Create_Render_Obj:
    Liegt direkt darunter W3DPrototypeClass::Create oder WW3DAssetManager::Create_Render_Obj, ist es ein
    gewoehnliches Exemplar (Klon); alles andere dort (Anlegen des Grundexemplars, Make_Unique, Umfaerben,
    Name) baut die Vorlage."""
    kurz = rahmen_kurz(rahmen)
    idx = [i for i, r in enumerate(kurz) if "W3DAssetManager::Create_Render_Obj" in r
           and "WW3DAssetManager::Create_Render_Obj" not in r]
    if not idx:
        return False
    i = idx[-1]
    if i == 0:
        return True
    darunter = kurz[i - 1]
    return "W3DPrototypeClass::Create" not in darunter and "WW3DAssetManager::Create_Render_Obj" not in darunter


def einordnen(gruppe):
    """Wachstumsgruppe (n, bytes, pool, tag, rahmen) -> Name des Zwischenspeichers oder None (Rest)."""
    text = [gruppe[3]] + list(gruppe[4])
    for name, muster in ZWISCHENSPEICHER:
        if any(m in t for t in text for m in muster):
            return name
    if einfaerbe_vorlage(gruppe[4]) or gruppe[2] == "W3DPrototypeClass":
        return "eingefaerbte Modellvorlagen (W3DAssetManager, Add_Prototype)"
    if any(("TestScript.cpp" in t or "Testablauf.cpp" in t) for t in rahmen_kurz(gruppe[4])[:2]):
        return "Testablauf selbst (laufender Befehl)"
    return None


def mit_stapel(v):
    """Hat die Messung Aufrufketten (Build mit ENABLE_MEMORYPOOL_STACKTRACE)? Ohne sie laesst sich Wachstum
    nicht nach Zwischenspeichern trennen."""
    return any(g[4] for g in v["gruppen"])


def wachstum_rest(v):
    """Bloecke und Gruppen einer Wachstumsmessung ausserhalb der Zwischenspeicher."""
    rest = [g for g in v["gruppen"] if einordnen(g) is None]
    return sum(g[0] for g in rest), rest


def zusammenfassung(laufname, erg):
    lecks = erg.get("lecks", [])
    crt = erg.get("crt") or []
    wachs = erg.get("wachstum", {})
    w = ", ".join(f"{k}: {v['gesamt'][0] if v['gesamt'] else '?'} "
                  + (f"(Rest {wachstum_rest(v)[0]})" if mit_stapel(v) else "(ohne Aufrufketten)")
                  for k, v in wachs.items())
    d3d = len(erg.get("d3d_lebend", [])), len(erg.get("d3d_meldungen", []))
    return (f"{laufname:45s} Lecks {len(lecks):5d}  CRT {len(crt):4d}  Rohbytes {erg.get('raw', 0):8d}"
            + (f"  D3D11 lebend {d3d[0]} Meldungen {d3d[1]}" if any(d3d) else "")
            + (f"  Wachstum [{w}]" if w else "")
            + (f"  Exit {erg.get('exit')}" if erg.get("exit") not in (0, 1) else "")
            + (f"  {erg.get('dauer', 0):.0f} s" if erg.get("dauer") else "")
            + (f"  FEHLER: {'; '.join(erg['fehler'])}" if erg.get("fehler") else ""))


def bericht(alle, pfad_txt, pfad_json):
    zeilen = ["Lecksuche -- Bericht", ""]
    for laufname, erg in alle:
        zeilen.append(zusammenfassung(laufname, erg))
    zeilen.append("")
    daten = {}
    for laufname, erg in alle:
        gruppen = gruppiere(erg.get("lecks", []))
        daten[laufname] = {
            "exit": erg.get("exit"), "dauer": round(erg.get("dauer", 0), 1), "fehler": erg.get("fehler", []),
            "lecks": len(erg.get("lecks", [])), "gemeldet": erg.get("gemeldet"), "rohbytes": erg.get("raw", 0),
            "unfreed": erg.get("unfreed", []),
            "gruppen": [{"anzahl": g[0], "bytes": g[1], "pool": g[2], "tag": g[3], "stapel": g[4]} for g in gruppen],
            "crt": [{"datei": c[0], "nr": c[1], "art": c[2], "bytes": c[3]} for c in (erg.get("crt") or [])],
            "d3d11_lebend": erg.get("d3d_lebend", []),
            "d3d11_meldungen": [{"schwere": m[0], "id": m[1], "text": m[2]} for m in erg.get("d3d_meldungen", [])],
            "wachstum": {k: {"gesamt": v["gesamt"], "ausserhalb_zwischenspeicher": wachstum_rest(v)[0], "gruppen": [
                {"anzahl": g[0], "bytes": g[1], "pool": g[2], "tag": g[3], "zwischenspeicher": einordnen(g),
                 "stapel": rahmen_kurz(g[4])[:8]}
                for g in v["gruppen"]]} for k, v in erg.get("wachstum", {}).items()},
        }
        if (gruppen or erg.get("crt") or erg.get("wachstum") or erg.get("unfreed") or erg.get("d3d_lebend")
                or erg.get("d3d_meldungen")):
            zeilen.append(f"== {laufname}")
            for g in gruppen:
                zeilen.append(f"  {g[0]:5d} x, {g[1]:8d} Byte  {g[2]} / {g[3]}")
                for r in g[4]:
                    zeilen.append(f"        {r}")
            for u in erg.get("unfreed", []):
                zeilen.append(f"  {u}")
            if erg.get("crt"):
                crt = erg["crt"]
                zeilen.append(f"  CRT: {len(crt)} Bloecke, {sum(c[3] for c in crt)} Byte; Groessen: "
                              + ", ".join(str(c[3]) for c in crt[:30]))
            for m in erg.get("d3d_meldungen", []):
                zeilen.append(f"  D3D11-Debug-Layer [{m[0]}, ID {m[1]}]: {m[2][:300]}")
            for l in erg.get("d3d_lebend", [])[:60]:
                zeilen.append(f"  D3D11 lebend: {l[:300]}")
            for k, v in erg.get("wachstum", {}).items():
                n_rest, rest = wachstum_rest(v)
                zeilen.append(f"  Wachstum {k}: {v['gesamt']}, davon ausserhalb der Zwischenspeicher {n_rest}")
                speicher = {}
                for g in v["gruppen"]:
                    name = einordnen(g)
                    if name:
                        s = speicher.setdefault(name, [0, 0])
                        s[0] += g[0]
                        s[1] += g[1]
                for name, s in sorted(speicher.items(), key=lambda x: -x[1][0]):
                    zeilen.append(f"    Zwischenspeicher {name}: {s[0]} Bloecke, {s[1]} Byte")
                for g in rest[:40]:
                    zeilen.append(f"    {g[0]:5d} x, {g[1]:8d} Byte  {g[2]} / {g[3]}")
                    for r in rahmen_kurz(g[4])[:8]:
                        zeilen.append(f"          {r}")
            zeilen.append("")
    with open(pfad_txt, "w", encoding="utf-8") as f:
        f.write("\n".join(zeilen) + "\n")
    with open(pfad_json, "w", encoding="utf-8") as f:
        json.dump(daten, f, indent=1, ensure_ascii=False)


def befund(erg, wachstum_schwelle):
    if (erg.get("fehler") or erg.get("lecks") or erg.get("crt") or erg.get("raw") or erg.get("unfreed")
            or erg.get("d3d_lebend")):
        return True
    if any(m[0] in ("FEHLER", "KORRUPTION") for m in erg.get("d3d_meldungen", [])):
        return True
    for v in erg.get("wachstum", {}).values():
        # ohne Aufrufketten nur gezaehlt, kein Befund (Zwischenspeicher nicht erkennbar; build-leak verwenden)
        if mit_stapel(v) and wachstum_rest(v)[0] > wachstum_schwelle:
            return True
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default="build-leak", help="Build-Ordner (Standard build-leak)")
    ap.add_argument("--konfig", default="Debug", help="Konfiguration (nur Debug hat den Leckbericht)")
    ap.add_argument("--spiel", default="beide", choices=["beide", "generals", "zh"])
    ap.add_argument("--szenen", help="Komma-Liste; Standard alle (siehe --liste)")
    ap.add_argument("--auswahl", choices=sorted(AUSWAHL), help="vordefinierte Szenenauswahl (abnahme)")
    ap.add_argument("--ohne-fenster", action="store_true", help="nur -runner-Szenen (keine Warteschlange)")
    ap.add_argument("--mit-ton", action="store_true",
                    help="mit Ton starten (prueft auch die Tonpfade auf Lecks); Standard ohne Ton (-noaudio)")
    ap.add_argument("--nur-fenster", action="store_true", help="nur Fensterszenen")
    ap.add_argument("--parallel", type=int, default=1,
                    help="so viele Runner-Laeufe gleichzeitig (Fensterlaeufe laufen daneben nacheinander)")
    ap.add_argument("--out", default="lecksuche-out")
    ap.add_argument("--warteschlange", help="Python-Datei mit wait_free() fuer Fensterlaeufe (faire Warteschlange)")
    ap.add_argument("--wachstum-schwelle", type=int, default=0,
                    help="so viele gewachsene Bloecke je Messung gelten noch nicht als Befund")
    ap.add_argument("--liste", action="store_true", help="Szenen auflisten")
    ap.add_argument("--skript", help="eigene Testablauf-Datei als einmalige Szene (Name = Dateiname)")
    ap.add_argument("--skript-fenster", action="store_true", help="--skript im Fenster statt im Runner")
    ap.add_argument("--skript-extra", default="", help="weitere Parameter fuer --skript (durch Leerzeichen getrennt)")
    ap.add_argument("--auswerten", nargs="+", help="nur vorhandene Protokolle auswerten")
    ap.add_argument("--benutzerdaten", help="eigener Benutzerordner (Unterordner Generals und ZeroHour), etwa fuer eine andere Sprache")
    ap.add_argument("--extra", nargs=argparse.REMAINDER, default=[], help="weitere Spielparameter (am Ende)")
    a = ap.parse_args()

    if a.liste:
        for s in SZENEN:
            art = ("Runner" if s.runner else "Fenster") + (", allein" if s.allein else "")
            print(f"{s.name:20s} {art:16s} {'/'.join(s.spiele):12s} {s.beschreibung}")
        for k, v in AUSWAHL.items():
            print(f"\nAuswahl {k}: {', '.join(v)}")
        return 0
    if a.auswerten:
        alle = []
        for p in a.auswerten:
            erg = lies_log(p)
            crt = os.path.splitext(p)[0] + ".crt.txt"
            erg["crt"] = lies_crt(crt)
            erg["exit"] = 0
            alle.append((os.path.splitext(os.path.basename(p))[0], erg))
        os.makedirs(a.out, exist_ok=True)
        bericht(alle, os.path.join(a.out, "bericht.txt"), os.path.join(a.out, "bericht.json"))
        for laufname, erg in alle:
            print(zusammenfassung(laufname, erg))
        return 0

    if a.skript:
        name = os.path.splitext(os.path.basename(a.skript))[0]
        SZENEN.append(Szene(name, f"eigener Ablauf {a.skript}", os.path.abspath(a.skript), runner=not a.skript_fenster,
                            extra=a.skript_extra.split(), frist=5400))
        namen = [name]
    elif a.szenen:
        namen = a.szenen.split(",")
    elif a.auswahl:
        namen = AUSWAHL[a.auswahl]
    else:
        namen = [s.name for s in SZENEN]
    unbekannt = [n for n in namen if n not in {s.name for s in SZENEN}]
    if unbekannt:
        print(f"unbekannte Szenen: {unbekannt}")
        return 2
    spiele = ["generals", "zh"] if a.spiel == "beide" else [a.spiel]
    os.makedirs(a.out, exist_ok=True)

    laeufe = []
    for s in SZENEN:
        if s.name not in namen or (a.ohne_fenster and not s.runner) or (a.nur_fenster and s.runner):
            continue
        for spiel in spiele:
            if spiel in s.spiele:
                laeufe.extend(plane(s, spiel, a))
    parallel_laeufe = [l for l in laeufe if l.szene.runner and not l.szene.allein]
    fenster_laeufe = [l for l in laeufe if not l.szene.runner]
    allein_laeufe = [l for l in laeufe if l.szene.runner and l.szene.allein]

    w = Werkstatt(a, spiele)
    alle = []
    ergebnis_sperre = threading.Lock()

    def erledigt(laufname, erg):
        with ergebnis_sperre:
            alle.append((laufname, erg))
            reihenfolge = {l.name: i for i, l in enumerate(laeufe)}
            alle.sort(key=lambda x: reihenfolge.get(x[0], 0))
            bericht(alle, os.path.join(a.out, "bericht.txt"), os.path.join(a.out, "bericht.json"))
        w.protokoll("  " + zusammenfassung(laufname, erg))

    def einzeln(lauf):
        if lauf.szene.lan:
            with w.lan_sperre:
                w.protokoll(f"== {lauf.name}: {lauf.szene.beschreibung}")
                erledigt(*fuehre_aus(lauf, w))
            return
        w.protokoll(f"== {lauf.name}: {lauf.szene.beschreibung}")
        erledigt(*fuehre_aus(lauf, w))

    def fenster_reihe():
        for lauf in fenster_laeufe:
            einzeln(lauf)

    try:
        fenster_faden = threading.Thread(target=fenster_reihe)
        fenster_faden.start()
        with ThreadPoolExecutor(max_workers=max(1, a.parallel)) as pool:
            list(pool.map(einzeln, parallel_laeufe))
        fenster_faden.join()
        for lauf in allein_laeufe:
            einzeln(lauf)
    finally:
        w.aufraeumen()
    print()
    for laufname, erg in alle:
        print(zusammenfassung(laufname, erg))
    return 1 if any(befund(erg, a.wachstum_schwelle) for _, erg in alle) else 0


if __name__ == "__main__":
    sys.exit(main())
