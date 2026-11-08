#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Richtet das eigenstaendige Asset-Verzeichnis fuer Laufzeittests ein.

Kopiert die benoetigten Spiel-Assets aus einer legalen EA-Installation in ein eigenes
Verzeichnis (echte Kopie, kein Link), damit Tests nicht von der Installation abhaengen,
und legt darin unsere Datenkorrekturen als lose Dateien ab (lose Dateien haben im Spiel
Vorrang vor den .big-Archiven, siehe FileSystem::openFile). Die EA-Installation wird nur
gelesen, nie veraendert.

Zwei Profile:
    --game generals  (Standard) Generals fuer GZHNeo.exe
    --game zh        Zero Hour fuer GZHNeoZH.exe;
                     die Generals-Basisarchive, die Zero Hour zusaetzlich braucht, landen wie in
                     der EA-App-Installation im Unterordner ZH_Generals

Tastenbelegung: Im Profil generals uebernimmt das Skript die CommandMap.ini von Zero Hour (aus
dessen Archiven, --zh-source) als lose Dateien, damit beide Spiele dieselben Tastenkuerzel haben.
--generals-tastenbelegung laesst das aus und entfernt frueher geschriebene Kopien wieder.

Quelle und Ziel kommen aus --source und --dest oder aus den Umgebungsvariablen GZH_EA_GENERALS bzw.
GZH_EA_ZH (Installation) und GZH_ASSETS_GENERALS bzw. GZH_ASSETS_ZH (Kopie), siehe tools/pfade.py.

Aufruf (beliebig oft wiederholbar):
    python tools/assets/setup_assets.py --source "<Generals-Installation>" --dest "<Generals-Kopie>"
    python tools/assets/setup_assets.py --game zh --source "<Zero-Hour-Installation>" --dest "<Zero-Hour-Kopie>"
    python tools/assets/setup_assets.py                       (Umgebungsvariablen gesetzt)
    python tools/assets/setup_assets.py --generals-tastenbelegung

Die Build-Ausgabeordner werden danach vom Post-Build-Schritt der GZHNeo.exe bzw. GZHNeoZH.exe
automatisch mit Hardlinks/Junctions auf dieses Verzeichnis versorgt (cmake/LinkAssets.cmake).
"""
import argparse
import os
import re
import shutil
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import pfade  # noqa: E402

DATA_DIR = "Data"


# --- BIG-Archiv (offenes EA-Format: "BIGF", u32BE Gesamtgroesse, u32BE Anzahl, u32BE Body-Offset,
#     dann pro Eintrag u32BE Offset, u32BE Groesse, NUL-terminierter Name) ------------------------
def big_entries(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] not in (b"BIGF", b"BIG4"):
        raise ValueError(f"{path}: kein BIG-Archiv")
    _total, num, _body = struct.unpack(">III", data[4:16])
    pos = 16
    entries = {}
    for _ in range(num):
        off, size = struct.unpack(">II", data[pos:pos + 8])
        pos += 8
        end = data.index(b"\0", pos)
        name = data[pos:end].decode("latin-1")
        pos = end + 1
        entries[name.lower()] = (off, size)
    return data, entries


def big_read(path, entry_name):
    data, entries = big_entries(path)
    off, size = entries[entry_name.lower()]
    return data[off:off + size]


# --- Kopieren ---------------------------------------------------------------------------------
def same_file(src, dst):
    if not os.path.exists(dst):
        return False
    s, d = os.stat(src), os.stat(dst)
    return s.st_size == d.st_size and int(s.st_mtime) == int(d.st_mtime)


def copy_file(src, dst, log):
    if same_file(src, dst):
        return False
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)
    log(f"kopiert: {dst}")
    return True


def remove_excluded(path, log):
    """Entfernt eine ausgeschlossene Datei, die eine fruehere Kopie im Ziel hinterlassen hat.
    Im Ziel liegen eigene Kopien bzw. Hardlinks; os.remove loest nur diesen Namen."""
    if os.path.isfile(path):
        os.remove(path)
        log(f"entfernt (ausgeschlossen): {path}")


def copy_tree(src, dst, log, exclude=frozenset()):
    """exclude: relative Pfade unterhalb von src, klein geschrieben, mit "/" getrennt."""
    n = 0
    for root, _dirs, files in os.walk(src):
        rel = os.path.relpath(root, src)
        for name in files:
            s = os.path.join(root, name)
            d = os.path.join(dst, rel, name) if rel != "." else os.path.join(dst, name)
            relname = name if rel == "." else rel.replace(os.sep, "/") + "/" + name
            if relname.lower() in exclude:
                remove_excluded(d, log)
                continue
            if copy_file(s, d, lambda _m: None):
                n += 1
    log(f"{os.path.basename(src)}-Ordner: {n} Dateien kopiert/aktualisiert")


def copy_bigs(src, dst, exclude, log):
    bigs = sorted(n for n in os.listdir(src) if n.lower().endswith(".big") and n.lower() not in exclude)
    copied = sum(copy_file(os.path.join(src, n), os.path.join(dst, n), log) for n in bigs)
    if os.path.isdir(dst):
        for n in sorted(os.listdir(dst)):
            if n.lower() in exclude:
                remove_excluded(os.path.join(dst, n), log)
    log(f"Archive in {dst}: {len(bigs)} geprueft, {copied} kopiert")


# --- Datenkorrekturen ----------------------------------------------------------------------------
# Jede Korrektur: (Archiv, Eintrag im Archiv, Funktion bytes->bytes, Beschreibung fuer FIXES.md).
# Die Funktionen pruefen streng, dass genau die erwarteten Stellen getroffen werden, damit eine
# andere Datenversion nicht still halb korrigiert wird.

def fix_rocket_buggy_debris(ini: bytes) -> bytes:
    """A-01: GLARocketBuggyFullDebris nennt Sub-Objekte Turret/TurretUp01, die das
    Truemmermodell UVRockBug_d1.W3D nicht hat (nur Chassis und Reifen; aus GLARocketBuggy
    kopiert). Die wirkungslosen Zeilen und der Upgrade-Zustand werden entfernt."""
    m = re.search(rb"(?ms)^Object GLARocketBuggyFullDebris\r?\n.*?^End\r?\n", ini)
    if not m:
        raise RuntimeError("Object GLARocketBuggyFullDebris nicht gefunden")
    block = m.group(0)
    new = block
    # Zeilen im DefaultConditionState
    new, n1 = re.subn(rb"(?m)^[ \t]*ShowSubObject[ \t]*=[ \t]*Turret[ \t]*\r?\n", b"", new)
    new, n2 = re.subn(rb"(?m)^[ \t]*HideSubObject[ \t]*=[ \t]*TurretUp01[ \t]*\r?\n", b"", new)
    # kompletter Upgrade-Zustand
    new, n3 = re.subn(
        rb"(?ms)^[ \t]*ConditionState[ \t]*=[ \t]*WEAPONSET_PLAYER_UPGRADE[ \t]*\r?\n"
        rb"[ \t]*ShowSubObject[ \t]*=[ \t]*TurretUp01[ \t]*\r?\n"
        rb"[ \t]*HideSubObject[ \t]*=[ \t]*Turret[ \t]*\r?\n"
        rb"[ \t]*End[ \t]*\r?\n(?:[ \t]*\r?\n)?", b"", new)
    if (n1, n2, n3) != (1, 1, 1):
        raise RuntimeError(f"unerwartete Datenversion: Treffer {n1}/{n2}/{n3} statt 1/1/1")
    return ini[:m.start()] + new + ini[m.end():]


GENERALS_FIXES = [
    ("INI.big", r"Data\INI\Object\FactionUnit.ini", fix_rocket_buggy_debris,
     "A-01: `GLARocketBuggyFullDebris` ohne die wirkungslosen `Show/HideSubObject Turret/TurretUp01`-Zeilen "
     "und ohne den `WEAPONSET_PLAYER_UPGRADE`-Zustand (Modell `UVRockBug_d1.W3D` hat keinen Turm)."),
]


# --- Profile -------------------------------------------------------------------------------------
# big_exclude: PatchData.big, PatchWindow.big und PatchINI.big gibt es nur in den digitalen Fassungen
# (EA App, Steam). Sie gehoeren zum Menuepunkt "ButtonCustomMission", den der GPL-Quellstand weder in
# Generals noch in Zero Hour kennt (siehe BUILD.md): PatchWindow.big ersetzt MainMenu.wnd und
# MapSelectMenu.wnd, PatchData.big bringt die Texte, PatchINI.big eine WindowTransitions.ini mit den
# Uebergaengen. PatchWindow.big wuerde alphabetisch vor Window.big bzw. WindowZH.big geladen und die
# Menues ueberdecken; PatchINI.big verliert gegen INI.big bzw. INIZH.big und bleibt der Einheit halber
# ebenfalls draussen. Liegen sie von einer frueheren Kopie im Ziel, werden sie entfernt.
# data_exclude: Dateien unter Data\, die nicht kopiert werden (relativ, klein, "/" getrennt).
PROFILES = {
    "generals": dict(
        marker="INI.big",
        big_exclude={"patchdata.big", "patchwindow.big", "patchini.big"},
        data_exclude=frozenset(),
        # Kleine Dateien neben den Archiven, die das Spiel im Exe-Ordner erwartet.
        extra_files=["Generals.dat", "Install_Final.bmp", "00000000.016", "00000000.256"],
        base_subdir=None,
        fixes=GENERALS_FIXES,
    ),
    "zh": dict(
        marker="INIZH.big",
        big_exclude={"patchdata.big", "patchwindow.big", "patchini.big"},
        # Ein veraltetes zweites INIZH.big, das das Spiel beim Start loescht (GameEngine::init, EAs
        # Patch 1.01). Ueber die Data-Junction wuerde es sonst aus dem Asset-Ordner geloescht.
        data_exclude=frozenset({"ini/inizh.big"}),
        # langdata.dat: Wortfilter (LanguageFilter.h)
        extra_files=["Generals.dat", "Install_Final.bmp", "00000000.016", "00000000.256",
                     "00000001.016", "00000001.256", "langdata.dat"],
        # Generals-Basisarchive; Win32BIGFileSystem::init laedt sie rekursiv aus dem Exe-Ordner
        # (nach allen ZH-Archiven, erster Treffer gewinnt) und ignoriert dann den Registry-Pfad.
        # Nur die .big: lose Dateien liest das Spiel ausschliesslich aus dem eigenen Data-Ordner.
        base_subdir="ZH_Generals",
        # Der A-01-Fehler steckt nur in INI.big (Generals); INIZH.big ersetzt FactionUnit.ini
        # vollstaendig und hat ihn nicht.
        fixes=[],
    ),
}


def apply_fixes(dest, fixes, log):
    lines = ["# Datenkorrekturen in diesem Asset-Verzeichnis", "",
             "Erzeugt von `tools/assets/setup_assets.py`. Lose Dateien unter `Data\\` haben im Spiel Vorrang",
             "vor den `.big`-Archiven; die Archive selbst sind unveraenderte Kopien der EA-Installation.", ""]
    for big, entry, func, desc in fixes:
        original = big_read(os.path.join(dest, big), entry)
        fixed = func(original)
        out = os.path.join(dest, *entry.split("\\"))
        if os.path.exists(out) and open(out, "rb").read() == fixed:
            log(f"Korrektur aktuell: {entry}")
        else:
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with open(out, "wb") as f:
                f.write(fixed)
            log(f"Korrektur geschrieben: {entry} ({len(original)} -> {len(fixed)} Byte)")
        lines.append(f"- `{entry}` (aus `{big}`): {desc}")
    if not fixes:
        lines.append("Keine Korrekturen.")
    return lines


def write_fixes_md(dest, lines):
    with open(os.path.join(dest, "FIXES.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


# --- Tastenbelegung von Zero Hour ----------------------------------------------------------------
# Das Spiel laedt Data\<Sprache>\CommandMap.ini und danach Data\INI\CommandMap.ini, beide
# ueberschreibend (GameEngine::init, SubsystemInterfaceList::initSubsystem); je Befehl gibt es einen
# Eintrag (MetaMap::getMetaMapRec). Damit Generals genau die Zero-Hour-Belegung bekommt, erhaelt jeder
# Pfad, den eines der beiden Spiele hat, eine lose Datei mit dem Zero-Hour-Inhalt. Hat Zero Hour einen
# Pfad nicht, kommt dort eine Zero-Hour-Datei hin, die ohnehin geladen wuerde; zweimal laden aendert
# nichts. So kann keine Generals-Datei aus dem Archiv die Zero-Hour-Belegung ueberdecken.
COMMANDMAP_RE = re.compile(r"^data\\([^\\]+)\\commandmap\.ini$")
COMMANDMAP_INI = "ini"


def commandmap_files(data_dir, big_dirs, exclude):
    """{Ordnername klein: Inhalt} der wirksamen CommandMap.ini je Pfad. Reihenfolge wie im Spiel:
    lose Dateien unter Data, dann die Archive der Ordner in big_dirs alphabetisch, erster Treffer gewinnt."""
    found = {}
    data_root = os.path.join(data_dir, DATA_DIR)
    if os.path.isdir(data_root):
        for sub in os.listdir(data_root):
            f = os.path.join(data_root, sub, "CommandMap.ini")
            if os.path.isfile(f):
                with open(f, "rb") as h:
                    found[sub.lower()] = h.read()
    for d in big_dirs:
        if not os.path.isdir(d):
            continue
        for n in sorted(n for n in os.listdir(d) if n.lower().endswith(".big") and n.lower() not in exclude):
            data, entries = big_entries(os.path.join(d, n))
            for name, (off, size) in entries.items():
                m = COMMANDMAP_RE.match(name)
                if m and m.group(1) not in found:
                    found[m.group(1)] = data[off:off + size]
    return found


def zh_commandmap_plan(generals, zh):
    """{Ordner: Inhalt}: die losen Dateien fuer das Generals-Verzeichnis."""
    if not zh:
        raise RuntimeError("keine CommandMap.ini in den Zero-Hour-Daten gefunden")
    zh_langs = sorted(k for k in zh if k != COMMANDMAP_INI)
    gen_langs = sorted(k for k in generals if k != COMMANDMAP_INI)
    plan = {}
    for target in sorted(set(generals) | set(zh)):
        if target in zh:
            plan[target] = zh[target]
        elif target != COMMANDMAP_INI:
            # Sprache, die Zero Hour nicht hat: dort laedt ZH nur seine Data\INI-Datei.
            if COMMANDMAP_INI in zh:
                plan[target] = zh[COMMANDMAP_INI]
            elif len(zh_langs) == 1:
                plan[target] = zh[zh_langs[0]]
            else:
                raise RuntimeError(f"Sprache {target}: keine eindeutige Zero-Hour-Belegung")
        else:
            # Generals hat Data\INI\CommandMap.ini, Zero Hour nicht: die Sprachdatei doppelt laden.
            lang = gen_langs[0] if len(gen_langs) == 1 and gen_langs[0] in zh else (
                zh_langs[0] if len(zh_langs) == 1 else None)
            if lang is None:
                raise RuntimeError("Data\\INI\\CommandMap.ini: keine eindeutige Zero-Hour-Sprachdatei")
            plan[target] = zh[lang]
    return plan


def commandmap_path(dest, folder):
    # Schreibweise der EA-Ordner (Data\INI, Data\English ...), die Archivnamen sind klein gespeichert.
    name = "INI" if folder == COMMANDMAP_INI else folder.capitalize()
    return os.path.join(dest, DATA_DIR, name, "CommandMap.ini")


def apply_zh_commandmap(dest, zh_src, exclude, log):
    """Schreibt die Zero-Hour-Belegung; gibt die Zeilen fuer FIXES.md zurueck."""
    # Nur die Pfade zaehlen (auch lose Kopien eines frueheren Laufs); der Inhalt kommt aus Zero Hour.
    generals = commandmap_files(dest, [dest], exclude)
    zh = commandmap_files(zh_src, [zh_src, os.path.join(zh_src, PROFILES["zh"]["base_subdir"])],
                          PROFILES["zh"]["big_exclude"])
    plan = zh_commandmap_plan(generals, zh)
    for folder, content in plan.items():
        out = commandmap_path(dest, folder)
        rel = os.path.relpath(out, dest)
        if os.path.isfile(out) and open(out, "rb").read() == content:
            log(f"Tastenbelegung aktuell: {rel}")
            continue
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, "wb") as f:
            f.write(content)
        log(f"Tastenbelegung von Zero Hour geschrieben: {rel}")
    names = ", ".join(f"`{os.path.relpath(commandmap_path(dest, k), dest)}`" for k in sorted(plan))
    return ["", f"- Tastenbelegung von Zero Hour (aus `{zh_src}`): {names}. "
            "Abschalten: `setup_assets.py --generals-tastenbelegung`."]


def remove_zh_commandmap(dest, zh_src, log):
    """Entfernt nur lose CommandMap.ini, die bytegleich einer Zero-Hour-Datei sind (von diesem Skript)."""
    zh = commandmap_files(zh_src, [zh_src, os.path.join(zh_src, PROFILES["zh"]["base_subdir"])],
                          PROFILES["zh"]["big_exclude"]) if os.path.isdir(zh_src) else {}
    zh_contents = set(zh.values())
    data_root = os.path.join(dest, DATA_DIR)
    if not os.path.isdir(data_root):
        return
    for sub in os.listdir(data_root):
        f = os.path.join(data_root, sub, "CommandMap.ini")
        if not os.path.isfile(f):
            continue
        if open(f, "rb").read() in zh_contents:
            os.remove(f)
            log(f"Tastenbelegung von Zero Hour entfernt: Data\\{sub}\\CommandMap.ini")
        else:
            log(f"WARNUNG: {f} ist keine Zero-Hour-Kopie und bleibt")


# Oberflaeche von Zero Hour fuer Generals: Layouts, die Generals wie Zero Hour zeigen soll. Sie kommen aus
# dem Fensterarchiv von Zero Hour und werden als lose Dateien abgelegt (lose Dateien haben Vorrang vor den
# Archiven). Die Texte der Zero-Hour-Layouts, die Generals nicht hat, stehen in Data/Text (Projekttexte).
ZH_FENSTER_ARCHIV = "WindowZH.big"
ZH_FENSTER_DATEIEN = (
    "Window" + chr(92) + "Menus" + chr(92) + "OptionsMenu.wnd",
)


def zh_fenster_pfad(dest, eintrag):
    return os.path.join(dest, *eintrag.split(chr(92)))


def apply_zh_fenster(dest, zh_src, log):
    """Schreibt die Zero-Hour-Layouts als lose Dateien; gibt die Zeilen fuer FIXES.md zurueck."""
    archiv = os.path.join(zh_src, ZH_FENSTER_ARCHIV)
    if not os.path.isfile(archiv):
        raise RuntimeError(f"{ZH_FENSTER_ARCHIV} fehlt in {zh_src}")
    namen = []
    for eintrag in ZH_FENSTER_DATEIEN:
        inhalt = big_read(archiv, eintrag)
        out = zh_fenster_pfad(dest, eintrag)
        rel = os.path.relpath(out, dest)
        namen.append(f"`{rel}`")
        if os.path.isfile(out) and open(out, "rb").read() == inhalt:
            log(f"Zero-Hour-Layout aktuell: {rel}")
            continue
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, "wb") as f:
            f.write(inhalt)
        log(f"Zero-Hour-Layout geschrieben: {rel}")
    return ["", f"- Oberflaeche von Zero Hour (aus `{archiv}`): {', '.join(namen)}. "
            "Abschalten: Dateien loeschen."]


def remove_zh_fenster(dest, zh_src, log):
    """Entfernt nur lose Layouts, die bytegleich der Zero-Hour-Datei sind (von diesem Skript)."""
    archiv = os.path.join(zh_src, ZH_FENSTER_ARCHIV)
    if not os.path.isfile(archiv):
        return
    for eintrag in ZH_FENSTER_DATEIEN:
        out = zh_fenster_pfad(dest, eintrag)
        if os.path.isfile(out) and open(out, "rb").read() == big_read(archiv, eintrag):
            os.remove(out)
            log(f"Zero-Hour-Layout entfernt: {os.path.relpath(out, dest)}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", choices=sorted(PROFILES), default="generals", help="Asset-Profil")
    ap.add_argument("--source", help="EA-Installation (nur lesend), Vorgabe GZH_EA_GENERALS bzw. GZH_EA_ZH")
    ap.add_argument("--dest", help="Ziel-Asset-Verzeichnis, Vorgabe GZH_ASSETS_GENERALS bzw. GZH_ASSETS_ZH")
    ap.add_argument("--zh-source", help="Zero-Hour-Installation fuer die Tastenbelegung (nur lesend), "
                    "Vorgabe GZH_EA_ZH")
    ap.add_argument("--generals-tastenbelegung", action="store_true",
                    help="Profil generals: eigene Generals-Tastenbelegung behalten (keine ZH-Kopie)")
    args = ap.parse_args()
    prof = PROFILES[args.game]
    src = os.path.abspath(args.source or pfade.ea_installation(args.game, hinweis="oder --source"))
    dst = os.path.abspath(args.dest or pfade.assets(args.game, hinweis="oder --dest"))
    if not os.path.isfile(os.path.join(src, prof["marker"])):
        sys.exit(f"Quelle ohne {prof['marker']}: {src}")
    if os.path.normcase(src) == os.path.normcase(dst):
        sys.exit("Quelle und Ziel sind identisch; die EA-Installation wird nie beschrieben")
    os.makedirs(dst, exist_ok=True)
    log = print

    copy_bigs(src, dst, prof["big_exclude"], log)
    copied = 0
    for name in prof["extra_files"]:
        s = os.path.join(src, name)
        if not os.path.isfile(s):
            log(f"WARNUNG: fehlt in der Quelle: {name}")
            continue
        copied += copy_file(s, os.path.join(dst, name), log)
    log(f"Zusatzdateien: {len(prof['extra_files'])} geprueft, {copied} kopiert")
    copy_tree(os.path.join(src, DATA_DIR), os.path.join(dst, DATA_DIR), log, prof["data_exclude"])
    if prof["base_subdir"]:
        base_src = os.path.join(src, prof["base_subdir"])
        if not os.path.isdir(base_src):
            sys.exit(f"Quelle ohne Basisarchive: {base_src}")
        copy_bigs(base_src, os.path.join(dst, prof["base_subdir"]), prof["big_exclude"], log)
    lines = apply_fixes(dst, prof["fixes"], log)
    if args.game == "generals":
        zh_src = args.zh_source or pfade.ea_installation("zh", pflicht=False)
        zh_src = os.path.abspath(zh_src) if zh_src else None
        if zh_src and os.path.normcase(zh_src) == os.path.normcase(dst):
            sys.exit("Zero-Hour-Quelle und Ziel sind identisch")
        if args.generals_tastenbelegung:
            if zh_src:
                remove_zh_commandmap(dst, zh_src, log)
                remove_zh_fenster(dst, zh_src, log)
        elif zh_src and os.path.isfile(os.path.join(zh_src, PROFILES["zh"]["marker"])):
            try:
                lines += apply_zh_commandmap(dst, zh_src, prof["big_exclude"], log)
            except RuntimeError as e:
                log(f"WARNUNG: Tastenbelegung nicht angeglichen: {e}")
            try:
                lines += apply_zh_fenster(dst, zh_src, log)
            except RuntimeError as e:
                log(f"WARNUNG: Zero-Hour-Oberflaeche nicht uebernommen: {e}")
        else:
            log(f"WARNUNG: keine Zero-Hour-Installation unter {zh_src or '(GZH_EA_ZH nicht gesetzt)'} (--zh-source); "
                "Generals behaelt seine Tastenbelegung")
    write_fixes_md(dst, lines)
    log(f"fertig: {dst}")


if __name__ == "__main__":
    main()
