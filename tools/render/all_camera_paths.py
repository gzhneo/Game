# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Run every camera path in tests/render/paths twice, compare the two runs and inspect the pictures.

Per path: run_render_test.py twice (runs a and b, compared picture by picture), then inspect_shots.py on run a.
Result per path: number of pictures, how many are not pixel-identical, finding of the picture inspection.
The paths run in Release, up to --parallel games at the same time (queue in tools/abnahme/slot.py).

Usage:
  python tools/render/all_camera_paths.py                      all paths (about two hours)
  python tools/render/all_camera_paths.py --group short        the paths of the acceptance run (five without, two with multisampling)
  python tools/render/all_camera_paths.py --group combat       battle paths (see_kampf, lawine_kampf, tundra_nacht)
  python tools/render/all_camera_paths.py --only quer,zoom     single paths
  python tools/render/all_camera_paths.py --group tess_wide --resolution 1920x1080 --switches-text "-effects ... -logCamera"
                                                           terrain tessellation across the maps, evaluate with
                                                           tools/render/tess_attribution.py
  python tools/render/all_camera_paths.py --group buildings --single-vs-batched
                                                           every path with and without batched drawing (-noMeshInstancing), run a of
                                                           the single-draw path against run a of the batched path; group buildings:
                                                           many equal buildings, capturing
  python tools/render/all_camera_paths.py --list               list paths and groups
  GZH_BUILD=build-x64-vulkan-only python tools/render/all_camera_paths.py ...
                                                           another build folder (Release exes) instead of build-x64
  python tools/render/all_camera_paths.py --switches -vulkan --out render-out/paths-vulkan --reference render-out/all-paths
                                                           under Vulkan, run a of every path against run a of the reference
                                                           (default tolerance of compare_shots.py)

The paths run in the detail level Very High with their own user folder (--level), independent of the user's settings.
The battle paths are made with tools/render/find_action.py. Exit code 1 if a run fails or the picture inspection
finds something; pictures that are not pixel-identical are no error by themselves, they are listed in the report.
"""
import argparse
import concurrent.futures as cf
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools", "abnahme"))
from slot import wait_free  # noqa: E402

BUILD = os.environ.get("GZH_BUILD", "build-x64")  # Build-Ordner, etwa build-x64-vulkan-only
G = os.path.join(ROOT, BUILD, "Generals", "Code", "Main", "Release", "GZHNeo.exe")
Z = os.path.join(ROOT, BUILD, "GeneralsMD", "Code", "Main", "Release", "GZHNeoZH.exe")
REP_G = os.path.join(ROOT, "tests", "replays", "x64")
REP_Z = os.path.join(ROOT, "tests", "replays", "zh", "x64")
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


PATHS = {}
GROUPS = {"short": [], "combat": [], "tess_wide": [], "buildings": []}

# the five paths of the acceptance run
for p in ("quer", "rundum", "zoom", "rand"):
    PATHS[p] = replay(G, ALPINE, p)
    GROUPS["short"].append(p)
PATHS["rundum-zh"] = replay(Z, GREEN_Z, "rundum")
GROUPS["short"].append("rundum-zh")
# Multisampling as a case of its own, one path per game; all other paths run with one sample.
MSAA = {"quer-msaa8": 8, "rundum-zh-msaa8": 8}
PATHS["quer-msaa8"] = replay(G, ALPINE, "quer")
PATHS["rundum-zh-msaa8"] = replay(Z, GREEN_Z, "rundum")
GROUPS["short"] += ["quer-msaa8", "rundum-zh-msaa8"]
for p in ("zoom_weit", "zoom_weit_1", "zoom_weit_12", "zoom_weit_125", "zoom_weit_13", "zoom_weit_14", "zoom_weit_15"):
    PATHS[p] = replay(G, ALPINE, p, 120)
for p in ("zoom_schleier_1", "zoom_schleier_15"):
    PATHS[p] = skirmish(G, "Alpine Assault", p, 120)
# water
for p in ("see", "wasser_see", "wasser_nah"):
    PATHS[p + "-g"] = skirmish(G, "Tournament Lake", p)
    PATHS[p + "-z"] = skirmish(Z, "Tournament Lake", p)
for p in ("wasser_bewegung", "wasser_bewegung_nah"):
    PATHS[p + "-g"] = skirmish(G, "Tournament Lake", p, 2)
    PATHS[p + "-z"] = skirmish(Z, "Tournament Lake", p, 2)
for p, karte in (("wasser_nacht", "Silent River"), ("wasser_fluss", "Heartland Shield")):
    PATHS[p + "-g"] = skirmish(G, karte, p)
    PATHS[p + "-z"] = skirmish(Z, karte, p)
PATHS["wasser_insel"] = skirmish(Z, "Tournament Island", "wasser_insel")
for p, karte in (("badlands", "BarrenBadlands"), ("fluss", "Winding River"), ("insel", "Tournament Island"),
                 ("oase", "Golden Oasis"), ("whiteout", "Whiteout"), ("winter", "Bitter Winter")):
    PATHS["wassertextur_" + p] = skirmish(Z, karte, "wassertextur_" + p)
# mountains, rock, ground, trees
for p, karte in (("alpine", "Alpine Assault"), ("dust", "Dust Devil"), ("fortress", "Fortress Avalanche"),
                 ("lake", "Tournament Lake"), ("nacht", "Lights Out"), ("winter", "Winter Wolf")):
    PATHS["berge_" + p] = skirmish(Z, karte, "berge_" + p, 120)
PATHS["fels_alpine"] = skirmish(Z, "Alpine Assault", "fels_alpine", 120)
PATHS["fels_fortress"] = skirmish(Z, "Fortress Avalanche", "fels_fortress", 120)
PATHS["tess_steil"] = skirmish(Z, "Alpine Assault", "tess_steil", 30)
PATHS["boden_detail"] = skirmish(Z, "Alpine Assault", "boden_detail", 120)
PATHS["boden_nah"] = skirmish(Z, "Tournament Lake", "boden_nah", 120)
PATHS["schaerfe_flimmern"] = skirmish(Z, "Tournament Lake", "schaerfe_flimmern", 20)
for p, karte in (("iron", "Iron Dragon"), ("redrock", "Red Rock"), ("tl", "Tournament Lake"), ("wald", "ForgottenForestZH")):
    PATHS["baum_" + p] = skirmish(Z, karte, "baum_" + p)
# roads
for p, karte, exe in (("alpine", "Alpine Assault", G), ("loneeagle", "Lone Eagle", G), ("nacht_silentriver", "Silent River", G),
                      ("bitterwinter", "Bitter Winter", Z), ("city", "Tournament City", Z), ("deathvalley", "Death Valley", Z),
                      ("dogsofwar", "DogsOfWar", Z), ("irondragon", "Iron Dragon", Z), ("urban", "Tournament Urban", Z),
                      ("whiteout", "Whiteout", Z)):
    PATHS["strassen_" + p] = skirmish(exe, karte, "strassen_" + p)
PATHS["strassen_abzieh_gla05"] = (Z, ["--scene", "mission", "--map", "Maps\\md_gla05\\md_gla05.map"], "strassen_abzieh_gla05", 60)
for p, karte in (("frontline", "TheFrontline"), ("redrock", "Red Rock"), ("urban", "Tournament Urban")):
    PATHS["strassen_abzieh_" + p] = skirmish(Z, karte, "strassen_abzieh_" + p)
# paths that belong to a replay (Tournament Lake, Zero Hour)
for p, every in (("bc7_nah", 200), ("humvee_basen", 200), ("humvee_fahrt", 300), ("humvee_frueh", 100), ("humvee_nah", 50),
                 ("humvee_raeder", 4), ("cc0_baum", 4), ("cc0_leistung", 300), ("cc0_natur", 60), ("cc0_wald_spaet", 300)):
    PATHS[p] = replay(Z, LAKE_Z, p, every)
PATHS["cc0_alpine"] = replay(G, ALPINE, "cc0_alpine")
PATHS["humvee_beschaedigt"] = skirmish(Z, "Tournament Lake", "humvee_beschaedigt", 20, 3, 7)
PATHS["humvee_farben"] = skirmish(Z, "Tournament Lake", "humvee_farben", 100, 3, 7)
# many equal buildings, also while they change owner (single against batched drawing)
for p, every in (("gebaeude_basen", 100), ("gebaeude_einnehmen", 100)):
    PATHS[p] = replay(Z, LAKE_Z, p, every)
    GROUPS["buildings"].append(p)
PATHS["cc0_shell"] = (Z, ["--scene", "shellmap"], "cc0_shell", 60)
# battle paths made with find_action.py, Tundra at night with snow in both games
PATHS["see_kampf"] = replay(Z, LAKE_Z, "see_kampf")
PATHS["lawine_kampf"] = replay(Z, FORTRESS_Z, "lawine_kampf")
PATHS["tundra_nacht-g"] = replay(G, TUNDRA_G, "tundra_nacht", 300)
PATHS["tundra_nacht-z"] = replay(Z, TUNDRA_Z, "tundra_nacht", 300)
GROUPS["combat"] += ["see_kampf", "lawine_kampf", "tundra_nacht-g", "tundra_nacht-z"]
# Terrain tessellation across the maps: steep slopes, mountains, roads, bridges and shores
GROUPS["tess_wide"] += ["tess_steil", "fels_alpine", "fels_fortress"] + ["berge_" + p for p in (
    "alpine", "dust", "fortress", "lake", "nacht", "winter")] + ["strassen_" + p for p in (
    "alpine", "loneeagle", "nacht_silentriver", "bitterwinter", "dogsofwar", "irondragon", "whiteout")] + [
    "wasser_fluss-z", "wasser_see-z", "wasser_insel", "wassertextur_fluss", "wassertextur_winter", "see-z"]


# Known findings of inspect_shots.py: trees in front of black at the far edge; black beyond the map with a low camera;
# the -file start of md_gla05 keeps the main menu over the map, its frame counts as a seam; a large explosion flash in
# cc0_wald_spaet counts as a jump.
EXPECTED = {"zoom": {"shot_000840.bmp", "shot_000900.bmp"},
            "cc0_wald_spaet": {"shot_011700.bmp"},
            "berge_winter": {"shot_000720.bmp"},
            "humvee_frueh": {"shot_005200.bmp"},
            "humvee_nah": {"shot_007400.bmp"},
            "wasser_insel": {"shot_000420.bmp", "shot_000480.bmp"},
            "strassen_abzieh_gla05": {"shot_000180.bmp", "shot_000300.bmp", "shot_001020.bmp", "shot_001080.bmp",
                                      "shot_001140.bmp", "shot_001200.bmp", "shot_001380.bmp"}}


def count_pictures(text):
    """(pictures, not pixel-identical) from the output of run_render_test.py twice."""
    zeilen = [z for z in text.splitlines() if z.startswith(("OK", "DIFFERS"))]
    return len(zeilen), sum(1 for z in zeilen if "PSNR identical" not in z)


def run_path(name, ziel, out, parallel, bildpruefung, schalter=(), referenz=None, max_diff_ratio=None, aufloesung=None, msaa=None,
          stufe="VeryHigh"):
    exe, szene, pfad, every = ziel
    ordner = os.path.join(out, name)
    wait_free(parallel=parallel)
    cmd = [sys.executable, os.path.join(HERE, "run_render_test.py"), "twice", "--exe", exe] + szene + [
        "--path", os.path.join(ROOT, "tests", "render", "paths", pfad + ".txt"), "--every", str(every),
        "--multiInstance", "--out", ordner, "--timeout", "1500"] + (
        ["--xres", aufloesung[0], "--yres", aufloesung[1]] if aufloesung else []) + [
        "--msaa", str(msaa if msaa is not None else MSAA.get(name, 1)), "--level", stufe, "--extra", "-ignoreAsserts"] + list(schalter)
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace")
    os.makedirs(ordner, exist_ok=True)
    with open(os.path.join(ordner, "twice.txt"), "w", encoding="utf-8") as f:
        f.write(p.stdout + "\n--- stderr\n" + p.stderr)
    bilder, ungleich = count_pictures(p.stdout)
    befunde = 0
    if bildpruefung and bilder:
        q = subprocess.run([sys.executable, os.path.join(HERE, "inspect_shots.py"), os.path.join(ordner, "a")],
                           cwd=ROOT, capture_output=True, text=True, errors="replace")
        gefunden = {os.path.basename(z.split()[1].replace("\\", "/")) for z in q.stdout.splitlines() if z.startswith("FINDING")}
        # Exit code 1 means findings, but a crash also ends with 1; only the closing summary line proves a full run.
        vollstaendig = any(re.match(r"\d+ pictures, \d+ with a finding", z) for z in q.stdout.splitlines())
        befunde = len(gefunden - EXPECTED.get(name, set())) + (1 if q.returncode not in (0, 1) or not vollstaendig else 0)
        if not vollstaendig:
            print("picture inspection did not finish for %s:\n%s" % (name, q.stderr.strip()[-2000:]))
    fehler = p.returncode not in (0, 1) or bilder == 0
    vergleich = ""
    if referenz and bilder:
        # Another renderer: tolerance instead of pixel equality.
        toleranz = ["--max-diff-ratio", str(max_diff_ratio)] if max_diff_ratio is not None else []
        r = subprocess.run([sys.executable, os.path.join(HERE, "compare_shots.py"), os.path.join(referenz, name, "a"),
                            os.path.join(ordner, "a")] + toleranz, cwd=ROOT, capture_output=True, text=True, errors="replace")
        with open(os.path.join(ordner, "referenz.txt"), "w", encoding="utf-8") as f:
            f.write(r.stdout + "\n--- stderr\n" + r.stderr)
        aussen = sum(1 for z in r.stdout.splitlines() if z.startswith("ERROR") or z.startswith("DIFFERS"))
        if r.returncode != 0:
            befunde += 1
        vergleich = ", against reference %s" % ("within tolerance" if r.returncode == 0 else "OUTSIDE (%d pictures)" % aussen)
    print("%-26s %4d pictures, %3d not pixel-identical, picture inspection %s%s%s" % (
        name, bilder, ungleich, "no finding" if not befunde else "FINDING", vergleich, ", RUN FAILED" if fehler else ""),
        flush=True)
    return name, bilder, ungleich, befunde, fehler


def run_single_vs_batched(name, ziel, out, parallel, bildpruefung, schalter, max_diff_ratio, aufloesung, msaa, stufe):
    """Run a path batched (folder batched) and unbatched (folder single, -noMeshInstancing); the single-draw run is compared with the batched one."""
    batched = os.path.join(out, "batched")
    single = os.path.join(out, "single")
    run_path(name, ziel, batched, parallel, bildpruefung, schalter, None, max_diff_ratio, aufloesung, msaa, stufe)
    return run_path(name, ziel, single, parallel, False, list(schalter) + ["-noMeshInstancing"], batched, max_diff_ratio, aufloesung,
                 msaa, stufe)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", help="paths, comma-separated")
    ap.add_argument("--group", choices=sorted(GROUPS), help="short = paths of the acceptance run, combat = battle paths, "
                    "tess_wide = steep slopes, mountains, roads, bridges and shores for the terrain tessellation, "
                    "buildings = many equal buildings and capturing")
    ap.add_argument("--resolution", help="picture size WxH instead of 1280x720, e.g. 1920x1080 or 3440x1440")
    ap.add_argument("--msaa", type=int, help="multisampling (samples) for all paths; 1 without it, only the paths "
                    "*-msaa8 use 8")
    ap.add_argument("--level", default="VeryHigh", choices=("Low", "Medium", "High", "VeryHigh", "user"),
                    help="detail level of all paths with their own user folder (default VeryHigh); user = from the "
                    "user's settings")
    ap.add_argument("--parallel", type=int, default=3)
    ap.add_argument("--out", default=os.path.join(ROOT, "render-out", "all-paths"))
    ap.add_argument("--no-image-check", action="store_true")
    ap.add_argument("--single-vs-batched", action="store_true", help="run every path batched and with -noMeshInstancing and "
                    "compare the two runs (folders batched and single under --out)")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--switches", nargs="+", default=[], help="more game switches for all paths, e.g. -vulkan")
    ap.add_argument("--switches-text", default="", help="game switches as one text, also with values that start with -, e.g. \"-d3d11Effects none\"")
    ap.add_argument("--reference", help="output folder of an earlier run: run a of every path is compared against it with a tolerance")
    ap.add_argument("--max-diff-ratio", type=float, help="--reference: share of differing pixels above which a picture is outside (default of compare_shots.py 0.001; "
                    "Vulkan against D3D11 with all effects needs 0.003)")
    a = ap.parse_args()
    if a.list:
        for n in PATHS:
            print(n, [g for g, m in GROUPS.items() if n in m])
        return 0
    aufloesung = None
    if a.resolution:
        teile = a.resolution.lower().split("x")
        if len(teile) != 2 or not all(t.isdigit() for t in teile):
            sys.exit("--resolution: WxH, e.g. 1920x1080")
        aufloesung = teile
    namen = a.only.split(",") if a.only else (GROUPS[a.group] if a.group else list(PATHS))
    unbekannt = [n for n in namen if n not in PATHS]
    if unbekannt:
        sys.exit("unknown paths: " + ", ".join(unbekannt))
    if a.single_vs_batched:
        if a.reference:
            sys.exit("--single-vs-batched and --reference exclude each other")
        with cf.ThreadPoolExecutor(a.parallel) as ex:
            res = list(ex.map(lambda n: run_single_vs_batched(
                n, PATHS[n], os.path.join(a.out, n), a.parallel, not a.no_image_check,
                list(a.switches) + a.switches_text.split(), a.max_diff_ratio, aufloesung, a.msaa, a.level), namen))
        print("DONE (single against batched): %d paths, %d outside the tolerance or failed" % (
            len(res), sum(1 for r in res if r[3] or r[4])))
        return 1 if any(r[3] or r[4] for r in res) else 0
    with cf.ThreadPoolExecutor(a.parallel) as ex:
        res = list(ex.map(lambda n: run_path(n, PATHS[n], a.out, a.parallel, not a.no_image_check, list(a.switches) + a.switches_text.split(), a.reference, a.max_diff_ratio, aufloesung, a.msaa, a.level),
                          namen))
    bilder = sum(r[1] for r in res)
    ungleich = sum(r[2] for r in res)
    print("DONE: %d paths, %d pictures, %d not pixel-identical, %d with a finding of the picture inspection, %d failed" % (
        len(res), bilder, ungleich, sum(1 for r in res if r[3]), sum(1 for r in res if r[4])))
    return 1 if any(r[3] or r[4] for r in res) else 0


if __name__ == "__main__":
    sys.exit(main())
