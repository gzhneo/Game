# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Alle Kamerafahrten aus tests/render/paths zweimal fahren, vergleichen und die Bilder pruefen.

Je Fahrt: run_render_test.py twice (Lauf a und b, Bild fuer Bild verglichen), danach inspect_shots.py
auf Lauf a. Ergebnis je Fahrt: Zahl der Bilder, davon nicht pixelgleich, Befund der Bildpruefung.
Die Fahrten laufen in Release, bis zu --parallel Spiele gleichzeitig (Warteschlange tools/abnahme/slot.py).

Aufruf:
  python tools/render/alle_fahrten.py                      alle Fahrten (rund zwei Stunden)
  python tools/render/alle_fahrten.py --gruppe kurz        die Fahrten der Abnahme (fuenf ohne, zwei mit Kantenglaettung)
  python tools/render/alle_fahrten.py --gruppe gefecht     Gefechtsfahrten (see_kampf, lawine_kampf, tundra_nacht)
  python tools/render/alle_fahrten.py --nur quer,zoom      einzelne Fahrten
  python tools/render/alle_fahrten.py --gruppe tess_breit --aufloesung 1920x1080 --schalter-text "-effects ... -logCamera"
                                                           Gelaende-Tessellation in der Breite (TODO 87), Auswertung mit
                                                           tools/render/tess_zuordnung.py
  python tools/render/alle_fahrten.py --liste              Fahrten und Gruppen auflisten
  GZH_BAU=build-x64-vulkan-only python tools/render/alle_fahrten.py ...
                                                           anderer Build-Ordner (Release-Exes) statt build-x64
  python tools/render/alle_fahrten.py --schalter -vulkan --out render-out/fahrten-vulkan --referenz render-out/alle-fahrten
                                                           unter Vulkan, Lauf a jeder Fahrt gegen Lauf a der Referenz
                                                           (Standardtoleranz von compare_shots.py)

Die Fahrten laufen in der Detailstufe Sehr hoch mit eigenem Benutzerordner (--stufe), unabhaengig von den
Einstellungen des Nutzers. Die Gefechtsfahrten entstehen mit tools/render/find_action.py. Rueckgabewert 1, wenn ein Lauf scheitert
oder die Bildpruefung etwas findet; nicht pixelgleiche Bilder allein sind kein Fehler, sie stehen im Bericht.
"""
import argparse
import concurrent.futures as cf
import os
import subprocess
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
WURZEL = os.path.dirname(os.path.dirname(HIER))
sys.path.insert(0, os.path.join(WURZEL, "tools", "abnahme"))
from slot import wait_free  # noqa: E402

BAU = os.environ.get("GZH_BAU", "build-x64")  # Build-Ordner, etwa build-x64-vulkan-only
G = os.path.join(WURZEL, BAU, "Generals", "Code", "Main", "Release", "GZHNeo.exe")
Z = os.path.join(WURZEL, BAU, "GeneralsMD", "Code", "Main", "Release", "GZHNeoZH.exe")
REP_G = os.path.join(WURZEL, "tests", "replays", "x64")
REP_Z = os.path.join(WURZEL, "tests", "replays", "zh", "x64")
ALPINE = os.path.join(REP_G, "e0-alpine-assault-2ai-seed1.rep")
LAKE_Z = os.path.join(REP_Z, "e0zh-tournament-lake-3ai-seed2.rep")
GREEN_Z = os.path.join(REP_Z, "e0zh-green-pastures-2ai-seed1.rep")
FORTRESS_Z = os.path.join(REP_Z, "e0zh-fortress-avalanche-7ai-seed3.rep")
TUNDRA_G = os.path.join(REP_G, "e0-tournament-tundra-3ai-seed2.rep")
TUNDRA_Z = os.path.join(REP_Z, "e0zh-tournament-tundra-3ai-seed2.rep")


def skirmish(exe, karte, pfad, every=60, ai=1, seed=1):
    return (exe, ["--scene", "skirmish", "--map", karte, "--ai", str(ai), "--seed", str(seed)], pfad, every)


def replay(exe, rep, pfad, every=60):
    return (exe, ["--scene", "replay", "--replay", rep], pfad, every)


FAHRTEN = {}
GRUPPEN = {"kurz": [], "gefecht": [], "tess_breit": []}

# the five paths of the acceptance run
for p in ("quer", "rundum", "zoom", "rand"):
    FAHRTEN[p] = replay(G, ALPINE, p)
    GRUPPEN["kurz"].append(p)
FAHRTEN["rundum-zh"] = replay(Z, GREEN_Z, "rundum")
GRUPPEN["kurz"].append("rundum-zh")
# Multisampling as a case of its own, one path per game (R-683); all other paths run with one sample.
MSAA = {"quer-msaa8": 8, "rundum-zh-msaa8": 8}
FAHRTEN["quer-msaa8"] = replay(G, ALPINE, "quer")
FAHRTEN["rundum-zh-msaa8"] = replay(Z, GREEN_Z, "rundum")
GRUPPEN["kurz"] += ["quer-msaa8", "rundum-zh-msaa8"]
for p in ("zoom_weit", "zoom_weit_1", "zoom_weit_12", "zoom_weit_125", "zoom_weit_13", "zoom_weit_14", "zoom_weit_15"):
    FAHRTEN[p] = replay(G, ALPINE, p, 120)
for p in ("zoom_schleier_1", "zoom_schleier_15"):
    FAHRTEN[p] = skirmish(G, "Alpine Assault", p, 120)
# water
for p in ("see", "wasser_see", "wasser_nah"):
    FAHRTEN[p + "-g"] = skirmish(G, "Tournament Lake", p)
    FAHRTEN[p + "-z"] = skirmish(Z, "Tournament Lake", p)
for p in ("wasser_bewegung", "wasser_bewegung_nah"):
    FAHRTEN[p + "-g"] = skirmish(G, "Tournament Lake", p, 2)
    FAHRTEN[p + "-z"] = skirmish(Z, "Tournament Lake", p, 2)
for p, karte in (("wasser_nacht", "Silent River"), ("wasser_fluss", "Heartland Shield")):
    FAHRTEN[p + "-g"] = skirmish(G, karte, p)
    FAHRTEN[p + "-z"] = skirmish(Z, karte, p)
FAHRTEN["wasser_insel"] = skirmish(Z, "Tournament Island", "wasser_insel")
for p, karte in (("badlands", "BarrenBadlands"), ("fluss", "Winding River"), ("insel", "Tournament Island"),
                 ("oase", "Golden Oasis"), ("whiteout", "Whiteout"), ("winter", "Bitter Winter")):
    FAHRTEN["wassertextur_" + p] = skirmish(Z, karte, "wassertextur_" + p)
# mountains, rock, ground, trees
for p, karte in (("alpine", "Alpine Assault"), ("dust", "Dust Devil"), ("fortress", "Fortress Avalanche"),
                 ("lake", "Tournament Lake"), ("nacht", "Lights Out"), ("winter", "Winter Wolf")):
    FAHRTEN["berge_" + p] = skirmish(Z, karte, "berge_" + p, 120)
FAHRTEN["fels_alpine"] = skirmish(Z, "Alpine Assault", "fels_alpine", 120)
FAHRTEN["fels_fortress"] = skirmish(Z, "Fortress Avalanche", "fels_fortress", 120)
FAHRTEN["tess_steil"] = skirmish(Z, "Alpine Assault", "tess_steil", 30)
FAHRTEN["boden_detail"] = skirmish(Z, "Alpine Assault", "boden_detail", 120)
FAHRTEN["boden_nah"] = skirmish(Z, "Tournament Lake", "boden_nah", 120)
FAHRTEN["schaerfe_flimmern"] = skirmish(Z, "Tournament Lake", "schaerfe_flimmern", 20)
for p, karte in (("iron", "Iron Dragon"), ("redrock", "Red Rock"), ("tl", "Tournament Lake"), ("wald", "ForgottenForestZH")):
    FAHRTEN["baum_" + p] = skirmish(Z, karte, "baum_" + p)
# roads
for p, karte, exe in (("alpine", "Alpine Assault", G), ("loneeagle", "Lone Eagle", G), ("nacht_silentriver", "Silent River", G),
                      ("bitterwinter", "Bitter Winter", Z), ("city", "Tournament City", Z), ("deathvalley", "Death Valley", Z),
                      ("dogsofwar", "DogsOfWar", Z), ("irondragon", "Iron Dragon", Z), ("urban", "Tournament Urban", Z),
                      ("whiteout", "Whiteout", Z)):
    FAHRTEN["strassen_" + p] = skirmish(exe, karte, "strassen_" + p)
FAHRTEN["strassen_abzieh_gla05"] = (Z, ["--scene", "mission", "--map", "Maps\\md_gla05\\md_gla05.map"], "strassen_abzieh_gla05", 60)
for p, karte in (("frontline", "TheFrontline"), ("redrock", "Red Rock"), ("urban", "Tournament Urban")):
    FAHRTEN["strassen_abzieh_" + p] = skirmish(Z, karte, "strassen_abzieh_" + p)
# paths that belong to a replay (Tournament Lake, Zero Hour)
for p, every in (("bc7_nah", 200), ("humvee_basen", 200), ("humvee_fahrt", 300), ("humvee_frueh", 100), ("humvee_nah", 50),
                 ("humvee_raeder", 4), ("cc0_baum", 4), ("cc0_leistung", 300), ("cc0_natur", 60), ("cc0_wald_spaet", 300)):
    FAHRTEN[p] = replay(Z, LAKE_Z, p, every)
FAHRTEN["cc0_alpine"] = replay(G, ALPINE, "cc0_alpine")
FAHRTEN["humvee_beschaedigt"] = skirmish(Z, "Tournament Lake", "humvee_beschaedigt", 20, 3, 7)
FAHRTEN["humvee_farben"] = skirmish(Z, "Tournament Lake", "humvee_farben", 100, 3, 7)
FAHRTEN["cc0_shell"] = (Z, ["--scene", "shellmap"], "cc0_shell", 60)
# battle paths made with find_action.py, Tundra at night with snow in both games
FAHRTEN["see_kampf"] = replay(Z, LAKE_Z, "see_kampf")
FAHRTEN["lawine_kampf"] = replay(Z, FORTRESS_Z, "lawine_kampf")
FAHRTEN["tundra_nacht-g"] = replay(G, TUNDRA_G, "tundra_nacht", 300)
FAHRTEN["tundra_nacht-z"] = replay(Z, TUNDRA_Z, "tundra_nacht", 300)
GRUPPEN["gefecht"] += ["see_kampf", "lawine_kampf", "tundra_nacht-g", "tundra_nacht-z"]
# Terrain tessellation across the maps: steep slopes, mountains, roads, bridges and shores (TODO 87)
GRUPPEN["tess_breit"] += ["tess_steil", "fels_alpine", "fels_fortress"] + ["berge_" + p for p in (
    "alpine", "dust", "fortress", "lake", "nacht", "winter")] + ["strassen_" + p for p in (
    "alpine", "loneeagle", "nacht_silentriver", "bitterwinter", "dogsofwar", "irondragon", "whiteout")] + [
    "wasser_fluss-z", "wasser_see-z", "wasser_insel", "wassertextur_fluss", "wassertextur_winter", "see-z"]


# Known findings of inspect_shots.py: trees in front of black at the far edge; black beyond the map with a low camera;
# the -file start of md_gla05 keeps the main menu over the map, its frame counts as a seam.
ERWARTET = {"zoom": {"shot_000840.bmp", "shot_000900.bmp"},
            "berge_winter": {"shot_000720.bmp"},
            "humvee_frueh": {"shot_005200.bmp"},
            "humvee_nah": {"shot_007400.bmp"},
            "wasser_insel": {"shot_000420.bmp", "shot_000480.bmp"},
            "strassen_abzieh_gla05": {"shot_000180.bmp", "shot_000300.bmp", "shot_001020.bmp", "shot_001080.bmp",
                                      "shot_001140.bmp", "shot_001200.bmp", "shot_001380.bmp"}}


def zaehle(text):
    """(Bilder, nicht pixelgleich) aus der Ausgabe von run_render_test.py twice."""
    zeilen = [z for z in text.splitlines() if z.startswith(("OK", "ABWEICHT"))]
    return len(zeilen), sum(1 for z in zeilen if "PSNR identisch" not in z)


def fahre(name, ziel, out, parallel, bildpruefung, schalter=(), referenz=None, max_diff_ratio=None, aufloesung=None, msaa=None,
          stufe="VeryHigh"):
    exe, szene, pfad, every = ziel
    ordner = os.path.join(out, name)
    wait_free(parallel=parallel)
    cmd = [sys.executable, os.path.join(HIER, "run_render_test.py"), "twice", "--exe", exe] + szene + [
        "--path", os.path.join(WURZEL, "tests", "render", "paths", pfad + ".txt"), "--every", str(every),
        "--mehrfach", "--out", ordner, "--timeout", "1500"] + (
        ["--xres", aufloesung[0], "--yres", aufloesung[1]] if aufloesung else []) + [
        "--msaa", str(msaa if msaa is not None else MSAA.get(name, 1)), "--stufe", stufe, "--extra", "-ignoreAsserts"] + list(schalter)
    p = subprocess.run(cmd, cwd=WURZEL, capture_output=True, text=True, errors="replace")
    os.makedirs(ordner, exist_ok=True)
    with open(os.path.join(ordner, "twice.txt"), "w", encoding="utf-8") as f:
        f.write(p.stdout + "\n--- stderr\n" + p.stderr)
    bilder, ungleich = zaehle(p.stdout)
    befunde = 0
    if bildpruefung and bilder:
        q = subprocess.run([sys.executable, os.path.join(HIER, "inspect_shots.py"), os.path.join(ordner, "a")],
                           cwd=WURZEL, capture_output=True, text=True, errors="replace")
        gefunden = {os.path.basename(z.split()[1].replace("\\", "/")) for z in q.stdout.splitlines() if z.startswith("BEFUND")}
        befunde = len(gefunden - ERWARTET.get(name, set())) + (1 if q.returncode not in (0, 1) else 0)
    fehler = p.returncode not in (0, 1) or bilder == 0
    vergleich = ""
    if referenz and bilder:
        # Another renderer: tolerance instead of pixel equality.
        toleranz = ["--max-diff-ratio", str(max_diff_ratio)] if max_diff_ratio is not None else []
        r = subprocess.run([sys.executable, os.path.join(HIER, "compare_shots.py"), os.path.join(referenz, name, "a"),
                            os.path.join(ordner, "a")] + toleranz, cwd=WURZEL, capture_output=True, text=True, errors="replace")
        with open(os.path.join(ordner, "referenz.txt"), "w", encoding="utf-8") as f:
            f.write(r.stdout + "\n--- stderr\n" + r.stderr)
        aussen = sum(1 for z in r.stdout.splitlines() if z.startswith("FEHLER") or z.startswith("ABWEICHT"))
        if r.returncode != 0:
            befunde += 1
        vergleich = ", gegen Referenz %s" % ("in der Toleranz" if r.returncode == 0 else "AUSSERHALB (%d Bilder)" % aussen)
    print("%-26s %4d Bilder, %3d nicht pixelgleich, Bildpruefung %s%s%s" % (
        name, bilder, ungleich, "ohne Befund" if not befunde else "BEFUND", vergleich, ", LAUF GESCHEITERT" if fehler else ""),
        flush=True)
    return name, bilder, ungleich, befunde, fehler


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--nur", help="Fahrten, kommagetrennt")
    ap.add_argument("--gruppe", choices=sorted(GRUPPEN), help="kurz = Fahrten der Abnahme, gefecht = Gefechtsfahrten, "
                    "tess_breit = Steilhaenge, Berge, Strassen, Bruecken und Ufer fuer die Gelaende-Tessellation (TODO 87)")
    ap.add_argument("--aufloesung", help="Bildgroesse BxH statt 1280x720, etwa 1920x1080 oder 3440x1440")
    ap.add_argument("--msaa", type=int, help="Kantenglaettung (Samples) fuer alle Fahrten; ohne Angabe 1, nur die Fahrten "
                    "*-msaa8 mit 8 (R-683)")
    ap.add_argument("--stufe", default="VeryHigh", choices=("Low", "Medium", "High", "VeryHigh", "nutzer"),
                    help="Detailstufe aller Fahrten mit eigenem Benutzerordner (Standard VeryHigh); nutzer = aus den "
                    "Einstellungen des Nutzers")
    ap.add_argument("--parallel", type=int, default=3)
    ap.add_argument("--out", default=os.path.join(WURZEL, "render-out", "alle-fahrten"))
    ap.add_argument("--ohne-bildpruefung", action="store_true")
    ap.add_argument("--liste", action="store_true")
    ap.add_argument("--schalter", nargs="+", default=[], help="weitere Spielschalter fuer alle Fahrten, etwa -vulkan")
    ap.add_argument("--schalter-text", default="", help="Spielschalter als ein Text, auch mit Werten, die mit - beginnen, etwa \"-d3d11Effects none\"")
    ap.add_argument("--referenz", help="Ausgabeordner eines frueheren Laufs: Lauf a jeder Fahrt mit Toleranz dagegen")
    ap.add_argument("--max-diff-ratio", type=float, help="--referenz: Anteil abweichender Pixel, ab dem ein Bild ausserhalb liegt (Standard von compare_shots.py 0.001; "
                    "Vulkan gegen D3D11 mit allen Effekten braucht 0.003)")
    a = ap.parse_args()
    if a.liste:
        for n in FAHRTEN:
            print(n, [g for g, m in GRUPPEN.items() if n in m])
        return 0
    aufloesung = None
    if a.aufloesung:
        teile = a.aufloesung.lower().split("x")
        if len(teile) != 2 or not all(t.isdigit() for t in teile):
            sys.exit("--aufloesung: BxH, etwa 1920x1080")
        aufloesung = teile
    namen = a.nur.split(",") if a.nur else (GRUPPEN[a.gruppe] if a.gruppe else list(FAHRTEN))
    unbekannt = [n for n in namen if n not in FAHRTEN]
    if unbekannt:
        sys.exit("unbekannte Fahrten: " + ", ".join(unbekannt))
    with cf.ThreadPoolExecutor(a.parallel) as ex:
        res = list(ex.map(lambda n: fahre(n, FAHRTEN[n], a.out, a.parallel, not a.ohne_bildpruefung, list(a.schalter) + a.schalter_text.split(), a.referenz, a.max_diff_ratio, aufloesung, a.msaa, a.stufe),
                          namen))
    bilder = sum(r[1] for r in res)
    ungleich = sum(r[2] for r in res)
    print("FERTIG: %d Fahrten, %d Bilder, %d nicht pixelgleich, %d mit Befund der Bildpruefung, %d gescheitert" % (
        len(res), bilder, ungleich, sum(1 for r in res if r[3]), sum(1 for r in res if r[4])))
    return 1 if any(r[3] or r[4] for r in res) else 0


if __name__ == "__main__":
    sys.exit(main())
