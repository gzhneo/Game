# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Narrow down the difference between Vulkan and Direct3D 11 per effect group (Windows only).

Per group: the short paths (all_camera_paths.py --group short) first with Direct3D 11, then with -vulkan and the same
switches against that run (--reference, default tolerance of compare_shots.py). At the end a table: pictures outside
the tolerance per group and path.

Usage:
  python tools/vulkan/narrow_effects.py                      the four groups of the level "Very high"
  python tools/vulkan/narrow_effects.py --single             every effect of the level "Very high" alone
  python tools/vulkan/narrow_effects.py --only ssao,shadowmap=4096 --single
  python tools/vulkan/narrow_effects.py --leave-one-out      all effects, and one run each without one of them
  python tools/vulkan/narrow_effects.py --cumulative         add the effects one after the other (FXAA and sharpen last)
  python tools/vulkan/narrow_effects.py --base ""            without -depthD32 (default: with, see below)

If the difference exists only in the interplay of several effects (each alone within the tolerance), --leave-one-out
shows the ones involved: the run without an involved effect drops to 0. --cumulative shows from which effect on it
appears.

--base (default "-depthD32") applies to both runs of every group: that way only what does not depend on the depth
format remains (AMD has no D24S8 under Vulkan). Output under render-out/effects/<group>/{d3d11,vulkan}, table in
render-out/effects/result.txt.
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
PATHS_SCRIPT = os.path.join(ROOT, "tools", "render", "all_camera_paths.py")

# The effects of the detail level "Sehr hoch" (Data/INI/GameLODOverride.ini), grouped by what they touch.
GROUPS = {
    "post": "fxaa,sharpen,colorgrade,bloom,heat,haze",
    "depth": "ssao,softparticles,shadowmap=4096",
    "terrain": "terraindetail,terrainrock,terrainnormal=2,terraintess=2,rocktess=1",
    "light": "particlelight,particleground,lights",
}
# All of them in the order --kumulativ adds them: scene and terrain first, then lights and depth effects, the post
# effects last (FXAA and sharpening decide per edge and so turn small input differences into visible ones).
ALL = ["terraindetail", "terrainrock", "terrainnormal=2", "terraintess=2", "rocktess=1", "shadowmap=4096",
        "particlelight", "particleground", "lights", "softparticles", "ssao", "haze", "heat", "bloom", "colorgrade",
        "sharpen", "fxaa"]
# Effects that need others to show anything.
REQUIRES = {
    "rocktess=1": "terrainrock,terraintess=2,rocktess=1",
    "terrainnormal=2": "terrainrock,terrainnormal=2",
}


def run(name, switches, out, reference=None):
    cmd = [sys.executable, PATHS_SCRIPT, "--group", "short", "--switches-text", switches, "--out", out]
    if reference:
        cmd += ["--reference", reference]
    print(">>", name, switches, flush=True)
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace")
    print(p.stdout, flush=True)
    return p.stdout


def outside(text):
    """Path -> pictures outside the tolerance (0 when within the tolerance)."""
    values = {}
    for z in text.splitlines():
        m = re.match(r"(\S+)\s+\d+ pictures", z)
        if not m:
            continue
        a = re.search(r"OUTSIDE \((\d+) pictures\)", z)
        values[m.group(1)] = int(a.group(1)) if a else (0 if "within tolerance" in z else -1)
    return values


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--single", action="store_true", help="every effect alone instead of the groups")
    ap.add_argument("--leave-one-out", action="store_true", help="all effects, then leave one out each")
    ap.add_argument("--cumulative", action="store_true", help="add the effects one after the other")
    ap.add_argument("--only", help="groups or effects, comma-separated (with --leave-one-out: the ones to leave out)")
    ap.add_argument("--base", default="-depthD32", help="switches for both runs of every group")
    ap.add_argument("--out", default=os.path.join(ROOT, "render-out", "effects"))
    ap.add_argument("--dry-run", action="store_true", help="only show the planned runs")
    a = ap.parse_args()

    if a.leave_one_out:
        names = a.only.split(",") if a.only else ALL
        series = [("all_effects", ",".join(ALL))]
        series += [("without_" + n.split("=")[0], ",".join(e for e in ALL if e != n)) for n in names]
    elif a.cumulative:
        series = [("up_to_" + ALL[i].split("=")[0], ",".join(ALL[:i + 1])) for i in range(len(ALL))]
    elif a.single:
        all_effects = [e for g in GROUPS.values() for e in g.split(",")]
        names = a.only.split(",") if a.only else all_effects
        series = [(n.split("=")[0], REQUIRES.get(n, n)) for n in names]
    else:
        names = a.only.split(",") if a.only else list(GROUPS)
        series = [(n, GROUPS[n]) for n in names]

    if a.dry_run:
        for name, effects in series:
            print("%-20s %s -effects %s" % (name, a.base, effects))
        return 0

    table = []
    for name, effects in series:
        base = os.path.join(a.out, name)
        switches = ("%s -effects %s" % (a.base, effects)).strip()
        run(name + " d3d11", switches, os.path.join(base, "d3d11"))
        text = run(name + " vulkan", "-vulkan " + switches, os.path.join(base, "vulkan"), os.path.join(base, "d3d11"))
        table.append((name, effects, outside(text)))

    paths = sorted({f for _, _, w in table for f in w})
    lines = ["Base: %s" % (a.base or "(none)"), "%-20s %s  Sum  Effects" % ("Run", " ".join("%10s" % f for f in paths))]
    for name, effects, w in table:
        lines.append("%-20s %s  %5d  %s" % (name, " ".join("%10d" % w.get(f, -1) for f in paths),
                                             sum(v for v in w.values() if v > 0), effects))
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, "result.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
