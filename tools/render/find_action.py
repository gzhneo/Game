# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Find the fighting in a replay and turn it into a camera path.

Plays the replay once with -logActions (the game writes one runner-log line per destroyed object and per
new particle system: "ACTION <frame> <kind> <name> <x> <y> <z>", world coordinates), then looks for the
places where most happens within a short window and writes a camera path (format: GameClient/CameraPath.cpp) that visits them in order.

Usage:
  python tools/render/find_action.py --exe build-x64/GeneralsMD/Code/Main/Release/GZHNeoZH.exe ^
      --replay tests/replays/zh/x64/e0zh-tournament-lake-3ai-seed2.rep --map "Tournament Lake" ^
      --von 6300 --bis 7200 --orte 4 --out tests/render/paths/see_kampf.txt
  python tools/render/find_action.py --log aktion.log --map ...      (reuse a saved action log)

Options: --spiel generals for maps that only exist in the Generals archive; --zoom, --neigung set the pose of
every stop; --halten gives the frames spent at one place (a screenshot every --halten/2 frames then shows it
twice); --log-aus saves the action lines.
"""
import argparse
import math
import os
import struct
import subprocess
import sys
import tempfile

HIER = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HIER)
import map_hang  # noqa: E402
import map_props  # noqa: E402

CELL = map_hang.CELL
# weight of one event: buildings and vehicles dying are the picture-worthy kind, particles are the noise floor
GEWICHT = {"structure": 6.0, "aircraft": 4.0, "vehicle": 3.0, "infantry": 1.0, "object": 0.2, "particle": 0.1}
FENSTER = 120		# frames in which events add up
RADIUS = 250.0		# world units: events closer than this belong to one place


def aktionen(exe, replay, timeout):
    """Play the replay and return [(frame, kind, name, x, y, z)]."""
    fd, log = tempfile.mkstemp(suffix=".log")
    os.close(fd)
    try:
        exe = os.path.abspath(exe)
        cmd = [exe, "-win", "-xres", "800", "-yres", "600", "-nologo", "-noaudio", "-ignoreAsserts", "-mehrfach",
               "-runner", "-replay", os.path.abspath(replay), "-crcLog", log, "-logActions"]
        subprocess.run(cmd, cwd=os.path.dirname(exe), timeout=timeout)
        return einlesen(log)
    finally:
        try:
            os.remove(log)
        except OSError:
            pass


def einlesen(pfad):
    out = []
    with open(pfad, encoding="latin-1") as f:
        for z in f:
            t = z.split()
            if len(t) == 7 and t[0] == "ACTION":
                out.append((int(t[1]), t[2], t[3], float(t[4]), float(t[5]), float(t[6])))
    return out


def punkte(ereignisse, von, bis, filter_ort=None):
    """Best places: for each event the weight of all events within RADIUS and FENSTER frames after it.
    Events at the origin (particle systems not placed yet, objects without a place) do not count;
    filter_ort(x, y) restricts the area."""
    ev = [e for e in ereignisse if von <= e[0] <= bis and (e[3] != 0.0 or e[4] != 0.0)
          and (filter_ort is None or filter_ort(e[3], e[4]))]
    ev.sort()
    besten = []
    for i, (f0, _, _, x0, y0, _) in enumerate(ev):
        w = 0.0
        sx = sy = 0.0
        for f, k, _, x, y, _ in ev[i:]:
            if f > f0 + FENSTER:
                break
            if math.hypot(x - x0, y - y0) <= RADIUS:
                g = GEWICHT.get(k, 0.2)
                w += g
                sx += g * x
                sy += g * y
        if w > 0:
            besten.append((w, f0, sx / w, sy / w))
    return besten


def auswahl(kandidaten, anzahl, abstand):
    """Highest weight first, but not two stops closer than `abstand` frames or RADIUS*2 units."""
    gewaehlt = []
    for w, f, x, y in sorted(kandidaten, reverse=True):
        if all(abs(f - g[1]) >= abstand and math.hypot(x - g[2], y - g[3]) > 2 * RADIUS for g in gewaehlt):
            gewaehlt.append((w, f, x, y))
        if len(gewaehlt) == anzahl:
            break
    return sorted(gewaehlt, key=lambda g: g[1])


def kartenmass(spiel, karte):
    w, h, border, _ = map_hang.height_map(map_hang.load(spiel, karte))
    return (w - 1 - 2 * border) * CELL, (h - 1 - 2 * border) * CELL


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe")
    ap.add_argument("--replay")
    ap.add_argument("--log", help="saved action lines instead of playing the replay")
    ap.add_argument("--log-aus", help="save the action lines here")
    ap.add_argument("--map", required=True, help='map name as in the archive, e.g. "Tournament Lake"')
    ap.add_argument("--spiel", choices=("zh", "generals"), default="zh")
    ap.add_argument("--von", type=int, default=0)
    ap.add_argument("--bis", type=int, default=10 ** 9)
    ap.add_argument("--orte", type=int, default=4)
    ap.add_argument("--nahe", help="only events near X,Y,R (map-relative, R as a fraction of the map width), e.g. 0.5,0.5,0.25")
    ap.add_argument("--halten", type=int, default=240, help="frames per place (default 240)")
    ap.add_argument("--zoom", default="0.6")
    ap.add_argument("--neigung", default="standard")
    ap.add_argument("--timeout", type=int, default=3600)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    if a.log:
        ev = einlesen(a.log)
    elif a.exe and a.replay:
        ev = aktionen(a.exe, a.replay, a.timeout)
    else:
        sys.exit("--log or --exe and --replay needed")
    if a.log_aus:
        with open(a.log_aus, "w") as f:
            for e in ev:
                f.write("ACTION %d %s %s %.1f %.1f %.1f\n" % e)
    if not ev:
        sys.exit("no ACTION lines: does the exe know -logActions, and did the replay play?")
    bis = min(a.bis, max(e[0] for e in ev))
    bx, by = kartenmass(a.spiel, a.map)
    filter_ort = None
    if a.nahe:
        nx, ny, nr = (float(v) for v in a.nahe.split(","))
        filter_ort = lambda x, y: math.hypot(x / bx - nx, y / by - ny) * bx <= nr * bx
    kand = punkte(ev, a.von, bis, filter_ort)
    wahl = auswahl(kand, a.orte, a.halten)
    if not wahl:
        sys.exit("no place found in frames %d to %d" % (a.von, bis))
    zeilen = ["# Generated by tools/render/find_action.py: %s, frames %d to %d, %d events, %d places." % (
        a.map, a.von, bis, len(ev), len(wahl)),
        "# Kartenrelativ, Winkel in Grad; jede Pose zwei Stuetzpunkte, damit sie bei --every stillsteht.",
        "# frame   x      y      zoom      drehung  neigung"]
    frame = a.von
    for i, (w, f, x, y) in enumerate(wahl):
        rx, ry = min(max(x / bx, 0.0), 1.0), min(max(y / by, 0.0), 1.0)
        # the camera arrives at the fight a little before the first events
        start = max(frame, f - 30)
        zeilen.append("# Ort %d: gewicht %.1f, Frame %d, Welt %.0f/%.0f" % (i + 1, w, f, x, y))
        for fr in (start, start + a.halten):
            zeilen.append("%-9d %.3f  %.3f  %-9s 0        %s" % (fr, rx, ry, a.zoom, a.neigung))
        frame = start + a.halten + 1
    with open(a.out, "w", newline="\r\n") as f:
        f.write("\n".join(zeilen) + "\n")
    print("%d Orte -> %s" % (len(wahl), a.out))
    for z in zeilen:
        if z.startswith("# Ort"):
            print(z)


if __name__ == "__main__":
    main()
