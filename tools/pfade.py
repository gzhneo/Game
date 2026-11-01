#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Rechnerabhaengige Pfade der Werkzeuge, zentral an einer Stelle statt fest im Code.

    GZH_ASSETS_GENERALS   Asset-Kopie von Generals (tools/assets/setup_assets.py --game generals)
    GZH_ASSETS_ZH         Asset-Kopie von Zero Hour (setup_assets.py --game zh)
    GZH_EA_GENERALS       EA-Installation von Generals, nur gelesen (Quelle fuer setup_assets.py)
    GZH_EA_ZH             EA-Installation von Zero Hour, nur gelesen
    GZH_WERKZEUGE         Arbeitsordner fuer Werkzeuge und Rohdaten ausserhalb des Repos
                          (texconv, CC0-Downloads, Blender-Arbeitsstaende, KI-Umgebung)

Reihenfolge, in der ein Wert gesucht wird:
    1. Umgebungsvariable gleichen Namens (gewinnt immer)
    2. Rechnerdatei pfade.json (JSON-Objekt mit diesen Namen), gilt fuer alle Klone und Worktrees des Benutzers:
       Windows %LOCALAPPDATA%\\GZHNeo\\pfade.json, sonst $XDG_CONFIG_HOME bzw. ~/.config/gzhneo/pfade.json
    3. nur die Asset-Kopien: das Ziel der Data-Junction in einem vorhandenen Build-Ordner (build*/Generals bzw.
       build*/GeneralsMD, Code/Main/<Konfiguration>/Data), im eigenen Repo und im Hauptverzeichnis des Worktrees

Einrichten je Rechner: "python tools/pfade.py --einrichten" legt die Rechnerdatei an (erkennt, was es kann, und
behaelt vorhandene Eintraege); "python tools/pfade.py --setzen GZH_EA_ZH D:\\Spiele\\ZH" setzt einen Wert. Ohne
Argument zeigt das Programm Wert und Herkunft. Die PowerShell-Werkzeuge lesen dieselbe Datei (tools/pfade.ps1),
CMake auch (cmake/GzhPfade.cmake).
"""
import glob
import json
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

_ASSETS = {"generals": "GZH_ASSETS_GENERALS", "zh": "GZH_ASSETS_ZH"}
_EA = {"generals": "GZH_EA_GENERALS", "zh": "GZH_EA_ZH"}
NAMEN = list(_ASSETS.values()) + list(_EA.values()) + ["GZH_WERKZEUGE"]
# Unterordner der Build-Ordner je Asset-Variable
_BUILD_UNTER = {"GZH_ASSETS_GENERALS": "Generals", "GZH_ASSETS_ZH": "GeneralsMD"}


def _spiel(spiel):
    s = spiel.lower()
    if s in ("generals", "gen", "g"):
        return "generals"
    if s in ("zh", "zerohour", "zero hour", "generalsmd", "md"):
        return "zh"
    raise ValueError(f"unbekanntes Spiel: {spiel}")


def rechnerdatei():
    """Pfad der Rechnerdatei (muss nicht existieren)."""
    if os.name == "nt":
        basis = os.environ.get("LOCALAPPDATA") or os.path.join(os.path.expanduser("~"), "AppData", "Local")
        return os.path.join(basis, "GZHNeo", "pfade.json")
    basis = os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config")
    return os.path.join(basis, "gzhneo", "pfade.json")


def _datei_lesen():
    try:
        with open(rechnerdatei(), encoding="utf-8") as f:
            daten = json.load(f)
    except (OSError, ValueError):
        return {}
    return {k: v.strip() for k, v in daten.items() if k in NAMEN and isinstance(v, str) and v.strip()} \
        if isinstance(daten, dict) else {}


def _haupt_repo():
    """Hauptverzeichnis des Repositorys (bei einem Worktree das des Hauptklons), sonst None."""
    try:
        gemeinsam = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=REPO,
                                   capture_output=True, text=True, timeout=20).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return None
    if gemeinsam and os.path.basename(gemeinsam) == ".git":
        return os.path.dirname(gemeinsam)
    return None


def _erkennen(name):
    """Asset-Kopie aus der Data-Junction eines vorhandenen Build-Ordners ableiten, sonst None."""
    unter = _BUILD_UNTER.get(name)
    if not unter:
        return None
    wurzeln = [REPO]
    haupt = _haupt_repo()
    if haupt and os.path.normcase(haupt) != os.path.normcase(REPO):
        wurzeln.append(haupt)
    for wurzel in wurzeln:
        for data in sorted(glob.glob(os.path.join(wurzel, "build*", unter, "Code", "Main", "*", "Data"))):
            ziel = os.path.realpath(data)
            if os.path.normcase(ziel) != os.path.normcase(os.path.abspath(data)) and os.path.isdir(ziel):
                return os.path.dirname(ziel)
    return None


def finde(name):
    """(Wert, Herkunft) einer Variablen; (None, None), wenn nirgends zu finden."""
    wert = os.environ.get(name, "").strip()
    if wert:
        return wert, "Umgebungsvariable"
    wert = _datei_lesen().get(name)
    if wert:
        return wert, "Rechnerdatei"
    wert = _erkennen(name)
    if wert:
        return wert, "aus dem Build-Ordner erkannt"
    return None, None


def _lies(name, pflicht, hinweis):
    wert, _ = finde(name)
    if wert:
        return wert
    if pflicht:
        sys.exit(f"{name} nicht gesetzt ({hinweis}); einrichten mit: python tools/pfade.py --setzen {name} <Pfad>"
                 if hinweis else
                 f"{name} nicht gesetzt; einrichten mit: python tools/pfade.py --setzen {name} <Pfad>")
    return None


def assets(spiel, pflicht=True, hinweis=""):
    """Asset-Kopie eines Spiels ('generals' oder 'zh') oder None, wenn nicht gesetzt und nicht Pflicht."""
    return _lies(_ASSETS[_spiel(spiel)], pflicht, hinweis)


def ea_installation(spiel, pflicht=True, hinweis=""):
    """EA-Installation eines Spiels, nur zum Lesen."""
    return _lies(_EA[_spiel(spiel)], pflicht, hinweis)


def werkzeuge(*teile, pflicht=True, hinweis=""):
    """Pfad unterhalb von GZH_WERKZEUGE, etwa werkzeuge('cc0', 'download')."""
    wurzel = _lies("GZH_WERKZEUGE", pflicht, hinweis)
    return os.path.join(wurzel, *teile) if wurzel else None


def unter_werkzeuge(pfad):
    """Absoluter Pfad bleibt, ein relativer gilt unterhalb von GZH_WERKZEUGE (Pfade in den Auftragsdateien)."""
    if os.path.isabs(pfad) or (len(pfad) > 1 and pfad[1] == ":"):
        return pfad
    return werkzeuge(*pfad.replace("\\", "/").split("/"), hinweis=f"fuer {pfad}")


def alle_assets():
    """Die gesetzten Asset-Kopien beider Spiele (fuer Werkzeuge, die beide pruefen)."""
    return [p for p in (assets("generals", False), assets("zh", False)) if p]


def _datei_schreiben(daten):
    pfad = rechnerdatei()
    os.makedirs(os.path.dirname(pfad), exist_ok=True)
    with open(pfad, "w", encoding="utf-8", newline="\n") as f:
        json.dump({k: daten[k] for k in NAMEN if k in daten}, f, indent=2, ensure_ascii=False)
        f.write("\n")


def _zeigen():
    print(f"Rechnerdatei: {rechnerdatei()}" + ("" if os.path.isfile(rechnerdatei()) else " (fehlt)"))
    for name in NAMEN:
        wert, herkunft = finde(name)
        print(f"{name:20} {wert or '(nicht gesetzt)'}" + (f"   [{herkunft}]" if herkunft else ""))


def _einrichten():
    """Rechnerdatei aus dem anlegen, was sich finden laesst; vorhandene Eintraege bleiben."""
    daten = _datei_lesen()
    for name in NAMEN:
        if name not in daten:
            wert, _ = finde(name)
            if wert:
                daten[name] = wert
    _datei_schreiben(daten)
    _zeigen()
    fehlt = [n for n in NAMEN if n not in daten]
    if fehlt:
        print("\nNicht erkennbar, bei Bedarf setzen mit 'python tools/pfade.py --setzen NAME PFAD': " + ", ".join(fehlt))


def _umgebung():
    """Alle gefundenen Werte als NAME=Wert, eine je Zeile (fuer Skripte)."""
    for name in NAMEN:
        wert, _ = finde(name)
        if wert:
            print(f"{name}={wert}")


if __name__ == "__main__":
    args = sys.argv[1:]
    if not args:
        _zeigen()
    elif args[0] == "--einrichten":
        _einrichten()
    elif args[0] == "--umgebung":
        _umgebung()
    elif args[0] == "--setzen" and len(args) == 3 and args[1] in NAMEN:
        daten = _datei_lesen()
        daten[args[1]] = args[2]
        _datei_schreiben(daten)
        _zeigen()
    else:
        sys.exit("Aufruf: pfade.py [--einrichten | --umgebung | --setzen NAME PFAD]\nNamen: " + ", ".join(NAMEN))
