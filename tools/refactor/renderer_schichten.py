# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Schichten des Renderers: Include-Regeln zwischen Common/, D3D11/ und Vulkan/ unter WW3D2.

Regeln:
  - Common/ bindet weder D3D11/ noch Vulkan/ ein, auch nicht deren System-Koepfe (d3d11*.h, dxgi*.h, d3dcompiler.h bzw.
    vulkan/*.h, volk.h, vk_mem_alloc.h); dxgiformat.h (nur Formatnummern) ist neutral;
  - D3D11/ bindet nichts aus Vulkan/ ein und Vulkan/ nichts aus D3D11/ (samt System-Koepfen);
  - der Rest von Code/ (Szenen-Klassen in WW3D2, Engine, Plattform, Werkzeuge) bindet keine der beiden Schichten und keinen
    ihrer System-Koepfe ein, damit sich jede Schicht aus dem Bau nehmen laesst (GZH_RENDER_D3D11, GZH_RENDER_VULKAN).
    Ausgenommen sind Code/Tests/ (die Tests der Schichten) und die benannten Stellen in AUSSERHALB.
Welche Datei zu welcher Schicht gehoert, sagt ihr Ordner; liegt sie noch flach in WW3D2, die Tabelle
tools/refactor/renderer_ordner.tsv. So laeuft die Pruefung vor der Verschiebung (gegen die Tabelle) und danach gleich.
Bekannte Verstoesse stehen unten als benannte Ausnahmen; eine Ausnahme, die nicht mehr vorkommt, wird gemeldet und soll
dann aus der Liste.

Der Ordner WW3D2 wird gesucht (heute Code/Libraries/Source/WWVegas/WW3D2, spaeter Code/Renderer/WW3D2) oder mit --ww3d2 gesetzt.

Aufruf:  python3 tools/refactor/renderer_schichten.py [--ww3d2 <Pfad>]
Rueckgabewert 0: keine Verstoesse ausser den Ausnahmen; 1: neue Verstoesse oder eine veraltete Ausnahme.
"""
import argparse
import os
import re
import subprocess
import sys

WURZEL = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WW3D2 = "Code/Libraries/Source/WWVegas/WW3D2"
TABELLE = os.path.join(WURZEL, "tools", "refactor", "renderer_ordner.tsv")
SCHICHTEN = ("Common", "D3D11", "Vulkan")
INCLUDE = re.compile(r'^\s*#\s*include\s*["<]([^">]+)[">]')
API = (
    ("D3D11", re.compile(r"^(?:d3d11[\w]*|dxgi(?!format)[\w]*|d3dcompiler|d3dcommon)\.h$", re.I)),
    ("Vulkan", re.compile(r"^(?:vulkan/.*|volk\.h|vk_mem_alloc\.h)$", re.I)),
)
VERBOTEN = {"Common": {"D3D11", "Vulkan"}, "D3D11": {"Vulkan"}, "Vulkan": {"D3D11"}}

# Violations inside the layers (includer, included head).
AUSNAHMEN = {
}
# Outside the layers (path below Code/, included head): queries of the machine that load their DLL at runtime and work in
# every build.
AUSSERHALB = {
    ("Core/EngineCore/Engine/TextureTier.cpp", "dxgi1_4.h"):
        "Grafikspeicher fuer die automatische Texturstufe, dxgi.dll dynamisch geladen (Core kann den Renderer nicht fragen)",
    ("Core/EngineCore/Engine/TextureTier.cpp", "d3d11.h"):
        "UMA-Kennung ueber ein Geraet ohne Fenster, d3d11.dll dynamisch geladen, in jedem Bau (Windows hat d3d11.dll immer)",
}


def finde_ww3d2(dateien, vorgabe=None):
    """Ordner von WW3D2: --ww3d2, sonst der eine Ordner namens WW3D2 unter Code/ mit rendercore.h oder Common/rendercore.h."""
    if vorgabe:
        return vorgabe.rstrip("/")
    kandidaten = set()
    for p in dateien:
        if p.startswith("Code/") and (p.endswith("/WW3D2/rendercore.h") or p.endswith("/WW3D2/Common/rendercore.h")):
            kandidaten.add(p[:p.index("/WW3D2/") + len("/WW3D2")])
    if len(kandidaten) != 1:
        sys.exit("Ordner WW3D2 nicht eindeutig gefunden (%s); bitte --ww3d2 <Pfad> angeben" % ", ".join(sorted(kandidaten)) or "keiner")
    return kandidaten.pop()


def tabelle():
    t = {}
    with open(TABELLE, encoding="utf-8") as f:
        for z in f:
            z = z.rstrip("\n")
            if z and not z.startswith("#"):
                teile = z.split("\t")
                if len(teile) >= 2:
                    t[teile[0]] = teile[1]
    return t


def dateien():
    out = subprocess.run(["git", "ls-files", "--", WW3D2], cwd=WURZEL, capture_output=True, text=True, check=True).stdout
    return [p for p in out.splitlines() if p]


def alle_dateien():
    out = subprocess.run(["git", "ls-files", "--", "Code"], cwd=WURZEL, capture_output=True, text=True, check=True).stdout
    return [p for p in out.splitlines() if p]


def schicht_von(pfad, t):
    rest = pfad[len(WW3D2) + 1:]
    if "/" in rest and rest.split("/")[0] in SCHICHTEN:
        return rest.split("/")[0], False
    z = t.get(rest)
    return (z, True) if z in SCHICHTEN else (None, False)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ww3d2", help="Ordner von WW3D2 (Standard: gefunden)")
    a = ap.parse_args()
    global WW3D2
    WW3D2 = finde_ww3d2(alle_dateien(), a.ww3d2)
    t = tabelle()
    liste = dateien()
    # head name -> layer (by folder or table)
    kopf = {}
    simuliert = 0
    for p in liste:
        s, sim = schicht_von(p, t)
        simuliert += sim
        if s:
            kopf[p.rsplit("/", 1)[-1]] = s
    verstoesse, ausnahmen = [], set()
    aussen = set()
    schichtkoepfe = {k for k, v in kopf.items() if v in ("D3D11", "Vulkan")}
    for p in alle_dateien():
        if p.startswith(WW3D2 + "/") or p.startswith("Code/Tests/") or not p.endswith((".cpp", ".c", ".h", ".hpp", ".inl")):
            continue
        with open(os.path.join(WURZEL, p), encoding="latin-1") as f:
            zeilen = f.read().splitlines()
        for nr, z in enumerate(zeilen, 1):
            m = INCLUDE.match(z)
            if not m:
                continue
            text = m.group(1)
            name = text.rsplit("/", 1)[-1]
            if name not in schichtkoepfe and not any(muster.match(text) for _, muster in API):
                continue
            schluessel = (p[len("Code/"):], name)
            if schluessel in AUSSERHALB:
                aussen.add(schluessel)
                continue
            verstoesse.append("%s:%d: ausserhalb der Schichten, bindet %s ein" % (p, nr, text))
    for p in liste:
        if not p.endswith((".cpp", ".h", ".inl")):
            continue
        s, _ = schicht_von(p, t)
        name = p.rsplit("/", 1)[-1]
        with open(os.path.join(WURZEL, p), encoding="latin-1") as f:
            zeilen = f.read().splitlines()
        for nr, z in enumerate(zeilen, 1):
            m = INCLUDE.match(z)
            if not m:
                continue
            text = m.group(1)
            ziel = kopf.get(text.rsplit("/", 1)[-1])
            if ziel is None:
                for api, muster in API:
                    if muster.match(text):
                        ziel = api
            if not ziel:
                continue
            if s is None:
                if ziel != "Common":
                    verstoesse.append("%s:%d: ausserhalb der Schichten, bindet %s ein (%s)" % (p, nr, text, ziel))
                continue
            if ziel not in VERBOTEN[s]:
                continue
            schluessel = (name, text.rsplit("/", 1)[-1])
            if schluessel in AUSNAHMEN:
                ausnahmen.add(schluessel)
                continue
            verstoesse.append("%s:%d: %s/ bindet %s ein (%s)" % (p, nr, s, text, ziel))
    print("Schichten: %d Dateien in Common, D3D11 und Vulkan%s" % (
        sum(1 for p in liste if schicht_von(p, t)[0]), " (%d davon noch flach, nach der Tabelle)" % simuliert if simuliert else ""))
    for v in verstoesse:
        print("VERSTOSS", v)
    for k in sorted(ausnahmen):
        print("Ausnahme  %s -> %s: %s" % (k[0], k[1], AUSNAHMEN[k]))
    for k in sorted(set(AUSNAHMEN) - ausnahmen):
        print("Ausnahme kommt nicht mehr vor, bitte aus der Liste nehmen: %s -> %s" % k)
    for k in sorted(aussen):
        print("Ausserhalb %s -> %s: %s" % (k[0], k[1], AUSSERHALB[k]))
    for k in sorted(set(AUSSERHALB) - aussen):
        print("Stelle ausserhalb kommt nicht mehr vor, bitte aus der Liste nehmen: %s -> %s" % k)
    print("%d Verstoesse, %d benannte Ausnahmen, %d benannte Stellen ausserhalb" % (len(verstoesse), len(ausnahmen), len(aussen)))
    veraltet = (set(AUSNAHMEN) - ausnahmen) | (set(AUSSERHALB) - aussen)
    return 1 if verstoesse or veraltet else 0


if __name__ == "__main__":
    sys.exit(main())
