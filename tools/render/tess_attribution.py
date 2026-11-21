#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Assign black areas in camera paths to the runs and to the camera.

Input: several runs of the same path, each as NAME=FOLDER (the path folder of all_camera_paths.py with subfolder a,
or directly a folder with shot_*.bmp and run.log). The runs differ in the effects, e.g. without tessellation, with
terraintess=2, with terraintess=2,rocktess=2, each with and without shadowmap; all with -logCamera.

Per picture (logic frame from the file name): finding of the picture inspection (inspect_shots.py, checks black and
empty) per run, plus the camera values of the frame from the runner log (CAMERA lines: clearance = eye above the
terrain, nearplane = smallest height of the near plane above the terrain). At the end a classification per picture
with a finding:

  camera-in-terrain    nearplane < 0: black is to be expected without tessellation too (camera guidance)
  nearplane-tess       black only with tessellation, nearplane between 0 and --shift (the tessellated surface
                       cuts the near plane)
  tile-edge            only in runs without shadowmap (name contains "no-sm" or "nosm"), tile exclusion without
                       margin
  tess-only            only with tessellation, camera free: neither near plane nor tile edge -- investigate anew
  always               in all runs, camera free: not the tessellation (horizon, terrain window, for instance)
  mixed                otherwise

Whether a run has tessellation is in its name ("tess" or "rock"; "no-tess" means without).

  python tools/render/tess_attribution.py no=render-out/t0/tess_steil tess=render-out/t2/tess_steil \\
      rock=render-out/t3/tess_steil no-sm=render-out/t0n/tess_steil tess-no-sm=render-out/t2n/tess_steil

Exit code 0 = no picture with a finding, 1 = findings (table), 2 = input error.
"""
import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SHOT = re.compile(r"shot_(\d+)\.bmp$", re.I)
CAMERA = re.compile(r"CAMERA (\d+) eye \S+ \S+ \S+ ground \S+ clearance (\S+) nearplane (\S+) near \S+ zoom (\S+) pitch (\S+)")


def run_folder(path):
    a = os.path.join(path, "a")
    return a if os.path.isdir(a) else path


def camera_values(folder):
    """Logic frame -> (clearance, nearplane, zoom, pitch); per frame the smallest nearplane value of the drawn pictures."""
    values = {}
    log = os.path.join(folder, "run.log")
    if not os.path.isfile(log):
        return values
    with open(log, encoding="latin-1", errors="replace") as f:
        for z in f:
            m = CAMERA.search(z)
            if not m:
                continue
            frame = int(m.group(1))
            w = tuple(float(m.group(i)) for i in range(2, 6))
            if frame not in values or w[1] < values[frame][1]:
                values[frame] = w
    return values


def findings(folder):
    """Logic frame -> list of findings (check, crop)."""
    p = subprocess.run([sys.executable, os.path.join(HERE, "inspect_shots.py"), folder, "--json", "--only", "black,empty"],
                       capture_output=True, text=True, errors="replace")
    if p.returncode not in (0, 1):
        raise SystemExit("inspect_shots.py failed on %s: %s" % (folder, p.stderr.strip()[-300:]))
    data = json.loads(p.stdout)
    out = {}
    for b in data["images"]:
        m = SHOT.search(b["image"].replace("\\", "/"))
        if not m:
            continue
        out[int(m.group(1))] = [(f["check"], f.get("crop")) for f in b.get("findings", [])]
    return out


def camera_for_picture(values, frame):
    """Camera values of the picture: the picture is made in the frame itself, otherwise in the nearest earlier frame."""
    if frame in values:
        return values[frame]
    earlier = [f for f in values if f <= frame]
    return values[max(earlier)] if earlier else None


def has_tess(name):
    n = name.lower()
    return ("tess" in n or "rock" in n) and "no-tess" not in n


def without_shadowmap(name):
    n = name.lower()
    return "no-sm" in n or "nosm" in n


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("runs", nargs="+", help="NAME=FOLDER per run")
    ap.add_argument("--shift", type=float, default=4.0,
                    help="largest displacement of the tessellated surface in world units (strength 2 with rock about 3.4; default 4)")
    a = ap.parse_args()
    runs = []
    for arg in a.runs:
        if "=" not in arg:
            print("run without NAME=: %s" % arg)
            return 2
        name, path = arg.split("=", 1)
        folder = run_folder(path)
        if not os.path.isdir(folder):
            print("folder missing: %s" % folder)
            return 2
        runs.append((name, folder, findings(folder), camera_values(folder)))

    frames = sorted(set().union(*(set(b) for _, _, b, _ in runs)))
    print("picture " + " ".join("%-14s" % n[:14] for n, _, _, _ in runs) + "  clearance nearplane  zoom  pitch  class")
    with_finding = 0
    counts = {}
    for frame in frames:
        black = {n: bool(b.get(frame)) for n, _, b, _ in runs}
        if not any(black.values()):
            continue
        with_finding += 1
        # camera values from the first run that logged them (the path is the same in every run)
        cam = next((camera_for_picture(k, frame) for _, _, _, k in runs if camera_for_picture(k, frame)), None)
        cells = []
        for n, _, b, _ in runs:
            f = b.get(frame)
            cells.append("%-14s" % (",".join(sorted({p for p, _ in f}))[:14] if f else "-"))
        tess_l = [n for n in black if has_tess(n)]
        without_l = [n for n in black if not has_tess(n)]
        if cam and cam[1] < 0:
            kind = "camera-in-terrain"
        elif all(black.values()):
            kind = "always"
        elif any(black[n] for n in tess_l) and not any(black[n] for n in without_l):
            only_without_sm = all(without_shadowmap(n) for n in tess_l if black[n]) and any(not without_shadowmap(n) for n in tess_l)
            if only_without_sm:
                kind = "tile-edge"
            elif cam and cam[1] < a.shift:
                kind = "nearplane-tess"
            else:
                kind = "tess-only"
        else:
            kind = "mixed"
        counts[kind] = counts.get(kind, 0) + 1
        cam_text = "%9.2f %9.2f %5.3f %6.1f" % cam if cam else "    (no CAMERA line)       "
        print("%06d  %s  %s  %s" % (frame, " ".join(cells), cam_text, kind))
    if not with_finding:
        print("no picture with a black area or an empty picture in %d pictures" % len(frames))
        return 0
    print("\n%d pictures with a finding: %s" % (with_finding, ", ".join("%s %d" % kv for kv in sorted(counts.items()))))
    without_cam = [n for n, _, _, k in runs if not k]
    if without_cam:
        print("without CAMERA lines (switch -logCamera missing?): " + ", ".join(without_cam))
    return 1


if __name__ == "__main__":
    sys.exit(main())
