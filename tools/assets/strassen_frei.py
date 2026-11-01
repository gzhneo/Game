#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour (Electronic Arts).
# Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Strassen frei: free road textures (art/textures/tr*.dds) from CC0 scans and own geometry.

Replaces the 62 road textures referenced by Roads.ini (both games) with freely distributable versions. The build reads
NO EA file: scans come from Poly Haven / ambientCG (CC0), tones, widths, markings and edge shapes are own fixed values in
the tables below. EA-dependent is only the interface:
  - file names and RoadWidth / RoadWidthInTexture per texture (Roads.ini), see STRASSEN;
  - the texture layout, i.e. where the engine samples straight road, curves, junctions and the fade-out end
    (W3DRoadBuffer.cpp: loadFloat4PtSection maps u = u0 + U / (4 s), v = v0 - V / (4 s); the field origins u0, v0 and
    the quads of each piece come from loadCurve, loadTee, loadY, loadH, loadAlphaJoin, preloadRoadSegment), see FELDER.

Method: every texel of a field is mapped back into the local frame of that road piece (units of the road scale s,
U along the piece, V to its left). There the ideal road network of the piece (a straight line, an arc, the rays of a
tee, Y, slanted tee or crossing) gives lateral offset d and along coordinate a per road strip. Surface material is a
tiled, recoloured scan (texture-periodic, so the straight band tiles in u); edges, markings, ruts, joints, ties and rails
are drawn from (d, a). Markings of one strip are suppressed inside another strip, so junction boxes stay clean. Decals
(RoadWidthInTexture 3: arrows, words, symbols, cracks) are drawn in image space inside the region a road segment shows
(v band 85/512 +- 3/8, u from 0).

Commands:
  bauen      compute textures and write <ziel>/<name>.dds (BC7, full mip chain) -- --stufe 4 (UHD, default, four
             times the EA edge), 2 (HD) or 1 (EA size), --nur trtwolane ..., --png <dir> (previews)
  manifest   write <paket>/texturpaket.txt: the original (EA) size of every texture, so the engine loads the
             files of the package with a mip offset per tier (SD: EA size, HD: twice, UHD: four times)
  liste      interface table and recipes
  nachweis   credits table (scans, authors, sizes) for ASSET_CREDITS.md
  vergleich  measuring tool: sheet EA | free per texture and alpha-weighted brightness (reads EA textures, only for
             checking; never part of a build)
  pruefen    check a built folder: all 62 names present, size, BC7, mip count

Example:
  python tools/assets/strassen_frei.py bauen --ziel Data/FreieStrassen/Art/Textures --png render-out/strassen/png
  python tools/assets/strassen_frei.py manifest --paket Data/FreieStrassen
Needs numpy, Pillow and texconv.
"""
import argparse
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HIER = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HIER))
sys.path.insert(0, HIER)
import boden_detail as bd  # noqa: E402  (image helpers; nothing called here reads EA data)
import boden_frei as bf  # noqa: E402  (scan loading and tiling)

TEXCONV = bf.TEXCONV
SS = 2					# supersampling factor of the computation
METER_JE_EINHEIT = 0.25		# world unit in metres (a terrain cell of 10 units is about 2.5 m)

# -------------------------------------------------------------------------------------------------
# Interface (Roads.ini of both games): texture -> (EA edge length in pixels, RoadWidth, RoadWidthInTexture)
# -------------------------------------------------------------------------------------------------
STRASSEN = {
    "trtwolane": (512, 35, 0.9), "trtwolane5": (512, 35, 0.9), "trtwolane6": (512, 35, 0.9),
    "trtwolane2": (512, 60, 0.9), "trtwolane3": (512, 60, 0.9), "trtwolane4": (512, 60, 0.9),
    "trtwolane2s": (512, 60, 0.9), "trtwolane4s": (512, 60, 0.9),
    "trfourlane": (512, 60, 0.9), "trfourlane2": (512, 60, 0.9),
    "trsidewalk": (256, 8, 0.9), "trsidewalk2": (256, 16, 0.9), "trsidewalk3": (256, 22, 0.9), "trsidewalk4": (256, 28, 0.9),
    "trcobbroad": (512, 30, 0.9), "trgrassstrip": (256, 8, 0.9), "trgrassstrips": (256, 8, 0.9),
    "trdirtroad": (512, 52, 0.95), "trdirtroad2": (512, 33, 0.95), "trdirtroad3": (512, 33, 0.95),
    "trdirtroad4": (512, 52, 0.95), "trdirtroad5": (512, 60, 0.95), "trdirtroad6": (512, 52, 0.95),
    "trdirtroad7": (512, 52, 0.95), "trdirtroad8": (512, 52, 0.95), "trdirtroad9": (512, 52, 0.95),
    "trdirtpath": (256, 10, 0.95), "trdirtpath2": (256, 10, 0.95), "trdirtpath3": (256, 10, 0.95),
    "trgravelroad": (512, 33, 0.95), "trgravelroads": (512, 33, 0.95), "trsandroad": (512, 33, 0.95),
    "trtraintrack": (512, 35, 0.95), "trtraintrack2": (512, 35, 0.95), "trtraintrack3": (512, 35, 0.95),
    "trparkinglines": (512, 35, 0.95), "trparklinesdiag": (512, 35, 0.95),
    "trcautionstrip": (512, 20, 0.9), "trcautionarea": (512, 20, 0.9), "trcautionareay": (512, 20, 0.9),
    "trcrosswalk": (512, 20, 0.9), "trcrosswalky": (512, 20, 0.9), "trthickline": (512, 20, 0.9),
    "trsinglewhite": (512, 20, 0.9), "trdashedwhite": (512, 20, 0.9),
    "trarrow": (256, 10, 3), "trarrowbidir": (256, 10, 3), "trarrowlng": (256, 10, 3), "trcirclecross": (256, 10, 3),
    "trcross": (256, 15, 3), "trnosymbol": (256, 10, 3), "trhelipad": (256, 15, 3), "trparkspace": (256, 15, 3),
    "trwdcaution": (256, 10, 3), "trwdstophere": (256, 10, 3), "trwdyield": (256, 8, 3),
    "trcobblestones": (256, 40, 3), "trcracks": (256, 40, 3),
    "trcrackearth01": (512, 40, 3), "trcrackearth02": (512, 40, 3), "trcrackearth03": (512, 40, 3),
    "trcrackearth04": (512, 40, 3),
    # StreetHoleCover: in Roads.ini, but no EA file exists (the engine logs a failed load); size chosen here
    "trstrtholecvr": (256, 10, 3),
}

# Layout (W3DRoadBuffer.cpp). Origin (u0, v0) in 1/512 of the texture, quad corners in the local frame of the piece
# (units of s; h = RoadWidthInTexture / 2) and the road network of the piece:
#   linie (origin, direction), strahl = ray from origin (round cap behind it), bogen (centre, radius).
R2 = math.sqrt(0.5)


def felder(wit):
    h = wit / 2.0

    def rechteck(u1, u2, v1, v2):
        return [(u1, v1), (u2, v1), (u2, v2), (u1, v2)]
    return {
        # loadCurve: 30-degree arc pieces, centre on the left, radius CORNER_RADIUS 1.5 resp. TIGHT 0.5
        "bogen_weit": ((4, 255), [(-0.02, -1.2 * h), (1.1, -0.6 * h), (0.6, 1.4 * h), (-0.02, h)],
                       [("bogen", (0.0, 1.5), 1.5)], None),
        "bogen_eng": ((4, 425), [(-0.02, -1.1 * h), (0.6, -0.8 * h), (0.1, 1.2 * h), (-0.02, h)],
                      [("bogen", (0.0, 0.5), 0.5)], None),
        # loadTee: through road along V, branch along +U; quad -h..0.515 x +-0.515 (TEE_WIDTH_ADJUSTMENT 1.03)
        "tee": ((425, 255), rechteck(-h, 0.515, -0.515, 0.515),
                [("linie", (0.0, 0.0), (0.0, 1.0)), ("strahl", (0.0, 0.0), (1.0, 0.0))], None),
        # loadTee with is4Way: two crossing roads
        "kreuz": ((425, 425), rechteck(-0.515, 0.515, -0.515, 0.515),
                  [("linie", (0.0, 0.0), (1.0, 0.0)), ("linie", (0.0, 0.0), (0.0, 1.0))], None),
        # loadY: stem along +V, legs at 225 and 315 degrees (offsetY: arm = up rotated by +-135 degrees)
        "y": ((255, 226), rechteck(-0.795, 0.795, -0.79, 0.29),
              [("strahl", (0.0, 0.0), (0.0, 1.0)), ("strahl", (0.0, 0.0), (-R2, -R2)), ("strahl", (0.0, 0.0), (R2, -R2))],
              None),
        # loadH: through road along V, slanted arm at -45 degrees (offsetH: arm stump 1.05 s from the centre)
        "h": ((202, 364), rechteck(-h, 1.2, -1.08, 0.27),
              [("linie", (0.0, 0.0), (0.0, 1.0)), ("strahl", (0.0, 0.0), (R2, -R2))], None),
        # loadAlphaJoin: end of a road at another road type; a short piece centred on the end line (U = 0), soft on
        # both sides, so the result does not depend on which side of the end the join quad reaches further
        "uebergang": ((106, 425), rechteck(-0.244, 0.131, -0.531, 0.531),
                      [("linie", (0.0, 0.0), (1.0, 0.0))], (0.07, 0.03)),
    }


GERADE_V0 = 85			# preloadRoadSegment: uOffset 0, vOffset 85/512, u repeats every 4 s
DECAL_V0 = 85 / 512.0		# RoadWidthInTexture 3: a segment shows v in 85/512 +- 3/8 (wrapping), u from 0

# -------------------------------------------------------------------------------------------------
# Own values: materials, tones, recipes
# -------------------------------------------------------------------------------------------------
PH, AC = "polyhaven", "ambientcg"
# material: scans (mixed by a periodic noise mask), own mean tone (sRGB), spread of log brightness, large stains,
# optional massstab (scan drawn larger than its real size), glatt (share of the scan's own large blotches removed)
BELAEGE = {
    "asphalt_hell": {"scans": [(PH, "asphalt_04")], "ton": (134, 136, 134), "streu": 0.13, "flecken": 0.05, "glatt": 0.92},
    "asphalt_dunkel": {"scans": [(PH, "asphalt_04")], "ton": (102, 106, 106), "streu": 0.13, "flecken": 0.05, "glatt": 0.92},
    "asphalt_alt": {"scans": [(PH, "cracked_concrete"), (PH, "road_damaged")], "ton": (170, 166, 154), "streu": 0.16, "flecken": 0.06, "glatt": 0.92},
    "asphalt_alt_hell": {"scans": [(PH, "cracked_concrete"), (PH, "road_damaged")], "ton": (168, 162, 148), "streu": 0.16, "flecken": 0.06, "glatt": 0.92},
    "asphalt_alt_braun": {"scans": [(PH, "cracked_concrete"), (PH, "road_damaged")], "ton": (148, 136, 108), "streu": 0.16, "flecken": 0.06, "glatt": 0.92},
    "asphalt_alt_schnee": {"scans": [(PH, "cracked_concrete"), (PH, "road_damaged")], "ton": (172, 170, 164), "streu": 0.15, "flecken": 0.06, "glatt": 0.92},
    "asphalt_alt_braun_schnee": {"scans": [(PH, "cracked_concrete"), (PH, "road_damaged")], "ton": (164, 156, 136), "streu": 0.15, "flecken": 0.06, "glatt": 0.92},
    "beton_blau": {"scans": [(PH, "concrete_floor_01"), (PH, "concrete_floor_worn_001")], "ton": (166, 174, 178), "streu": 0.10, "flecken": 0.07, "glatt": 0.92},
    "beton_grau": {"scans": [(PH, "concrete_floor_01"), (PH, "concrete_floor_worn_001")], "ton": (176, 176, 168), "streu": 0.11, "flecken": 0.08, "glatt": 0.92},
    "beton_beige": {"scans": [(PH, "concrete_floor_01"), (PH, "concrete_floor_worn_001")], "ton": (206, 198, 174), "streu": 0.10, "flecken": 0.08, "glatt": 0.92},
    "beton_beige_hell": {"scans": [(PH, "concrete_floor_01"), (PH, "concrete_floor_worn_001")], "ton": (214, 206, 182), "streu": 0.10, "flecken": 0.08, "glatt": 0.92},
    "pflaster": {"scans": [(PH, "cobblestone_02")], "ton": (122, 106, 98), "streu": 0.45, "flecken": 0.04, "massstab": 9.0, "glatt": 0.92},
    "bordstein": {"scans": [(PH, "concrete_floor_01")], "ton": (168, 164, 156), "streu": 0.08, "flecken": 0.0},
    "gras": {"scans": [(PH, "sparse_grass")], "ton": (132, 148, 84), "streu": 0.14, "flecken": 0.06},
    "schnee": {"scans": [(AC, "Snow006")], "ton": (222, 224, 228), "streu": 0.05, "flecken": 0.03},
    "gras_rand": {"scans": [(PH, "sparse_grass")], "ton": (104, 112, 70), "streu": 0.14, "flecken": 0.05},
    "sand_saum": {"scans": [(PH, "dirt_floor")], "ton": (198, 160, 100), "streu": 0.10, "flecken": 0.05},
    "erde_saum": {"scans": [(PH, "dirt_floor"), (PH, "dirt")], "ton": (150, 136, 110), "streu": 0.12, "flecken": 0.06},
    "schotter_braun": {"scans": [(PH, "rocky_gravel")], "ton": (100, 94, 84), "streu": 0.13, "flecken": 0.04},
    "schotter_grau": {"scans": [(PH, "gravel_stones")], "ton": (132, 132, 130), "streu": 0.13, "flecken": 0.04},
    "schotter_dunkel": {"scans": [(PH, "gravel_stones")], "ton": (94, 94, 92), "streu": 0.13, "flecken": 0.04},
    "holz": {"scans": [(PH, "weathered_brown_planks")], "ton": (96, 80, 62), "streu": 0.12, "flecken": 0.0},
}
WEISS = (226, 224, 214)
GELB = (212, 172, 60)
GELB_HELL = (218, 202, 76)		# signal yellow of decals, caution and crossing marks (lighter than the centre lines)
ROT = (184, 44, 34)
SCHIENE = (168, 166, 160)

# Recipes per texture (the ways are in strassen_wege.py). belag: surface material (None: markings only); rand: edge; deko: features, drawn in order.
# Feature units: s (RoadWidth); d lateral (left positive), a along; widths are full widths.
#   ("linie", d0, breite, farbe[, (strich, periode)][, deck])        painted line, worn
#   ("quer", periode, breite, d_von, d_bis, farbe[, deck])           transverse painted bars (zebra, stall lines)
#   ("schraeg", periode, breite, d_von, d_bis, winkel, farbe)       slanted painted bars (diagonal stalls, hatching)
#   ("wechsel", d0, breite, periode, farbe1, farbe2)                  line alternating two colours
#   ("leiter", d0, breite, periode, loch_laenge, loch_breite, farbe)  line with a row of rectangular holes in its middle
#   ("fuge", periode, breite, faktor) / ("laengsfuge", d0, breite, faktor)   joints (darkening)
#   ("saum", breite, belag, anteil)                                   fringe of another material along the edge
#   ("streifen", d0, breite, belag)                                    band of another material (curb)
#   ("schwellen", periode, breite, d_halb, belag)                     sleepers
#   ("schiene", d0, breite, farbe)                                     rail with shadow
#   ("oel", d0, breite, faktor)                                        oil and rubber along a wheel path
#   ("schmutz", staerke, flecken) / ("risse", anteil, faktor) / ("flicken", anzahl, laenge, breite)
#                                                                     dirt, crack regions, asphalt patches
#   ("zebra", tiefe, periode, breite, farbe)                          crossing stripes in tee and crossing
# verblassen: fading of all painted lines (0 = none)
GLATT = {"art": "glatt", "weich": 0.004}
FRANS = {"art": "frans", "amp": 0.035, "weich": 0.012, "loch": 0.0}
DASH_ZWEI = (0.34, 1.0)
REZEPTE = {
    # asphalt: dirt, cracks, patches, oil along the wheel paths, dirt at the edge, faded lines, chipped curbs
    "trtwolane": {"belag": "asphalt_hell", "rand": GLATT, "verblassen": 0.45, "deko": [
        ("schmutz", 0.06, 0.10), ("risse", 0.35, 0.55), ("flicken", 7, 0.5, 0.22), ("oel", 0.11, 0.07, 0.88), ("oel", 0.29, 0.07, 0.88), ("oel", -0.11, 0.07, 0.88), ("oel", -0.29, 0.07, 0.88), ("saum", 0.06, "erde_saum", 0.55), ("streifen", 0.437, 0.026, "bordstein"), ("streifen", -0.437, 0.026, "bordstein"),
        ("linie", 0.40, 0.017, WEISS), ("linie", -0.40, 0.017, WEISS),
        ("linie", 0.013, 0.012, GELB), ("linie", -0.013, 0.012, GELB), ("zebra", 0.09, 0.07, 0.035, WEISS)]},
    "trtwolane5": {"belag": "asphalt_dunkel", "rand": GLATT, "verblassen": 0.45, "deko": [
        ("schmutz", 0.06, 0.10), ("risse", 0.35, 0.55), ("flicken", 7, 0.5, 0.22), ("oel", 0.11, 0.07, 0.88), ("oel", 0.29, 0.07, 0.88), ("oel", -0.11, 0.07, 0.88), ("oel", -0.29, 0.07, 0.88), ("saum", 0.06, "erde_saum", 0.55), ("streifen", 0.437, 0.026, "bordstein"), ("streifen", -0.437, 0.026, "bordstein"),
        ("linie", 0.40, 0.017, WEISS), ("linie", -0.40, 0.017, WEISS),
        ("linie", 0.013, 0.012, GELB), ("linie", -0.013, 0.012, GELB), ("zebra", 0.09, 0.07, 0.035, WEISS)]},
    "trtwolane6": {"belag": "asphalt_dunkel", "rand": GLATT, "verblassen": 0.45, "deko": [
        ("schmutz", 0.06, 0.10), ("risse", 0.35, 0.55), ("flicken", 7, 0.5, 0.22), ("oel", 0.11, 0.07, 0.88), ("oel", 0.29, 0.07, 0.88), ("oel", -0.11, 0.07, 0.88), ("oel", -0.29, 0.07, 0.88), ("saum", 0.06, "erde_saum", 0.55), ("streifen", 0.437, 0.026, "bordstein"), ("streifen", -0.437, 0.026, "bordstein"),
        ("linie", 0.40, 0.017, WEISS), ("linie", -0.40, 0.017, WEISS), ("linie", 0.0, 0.017, WEISS, DASH_ZWEI), ("zebra", 0.09, 0.07, 0.035, WEISS)]},
    "trtwolane2": {"belag": "asphalt_alt", "rand": FRANS, "verblassen": 0.7, "deko": [
        ("schmutz", 0.07, 0.12), ("risse", 0.7, 0.5), ("flicken", 10, 0.45, 0.25), ("oel", 0.11, 0.07, 0.88), ("oel", 0.29, 0.07, 0.88), ("oel", -0.11, 0.07, 0.88), ("oel", -0.29, 0.07, 0.88), ("saum", 0.09, "erde_saum", 0.85), ("linie", 0.0, 0.012, GELB, (0.3, 0.571), 0.6)]},
    "trtwolane3": {"belag": "asphalt_alt_hell", "rand": FRANS, "verblassen": 0.7, "deko": [
        ("schmutz", 0.07, 0.12), ("risse", 0.7, 0.5), ("flicken", 10, 0.45, 0.25), ("oel", 0.11, 0.07, 0.88), ("oel", 0.29, 0.07, 0.88), ("oel", -0.11, 0.07, 0.88), ("oel", -0.29, 0.07, 0.88), ("saum", 0.12, "sand_saum", 1.0), ("linie", 0.0, 0.012, GELB, (0.3, 0.571), 0.5)]},
    "trtwolane4": {"belag": "asphalt_alt_braun", "rand": FRANS, "deko": [
        ("schmutz", 0.07, 0.12), ("risse", 0.7, 0.5), ("flicken", 10, 0.45, 0.25), ("oel", 0.11, 0.07, 0.88), ("oel", 0.29, 0.07, 0.88), ("oel", -0.11, 0.07, 0.88), ("oel", -0.29, 0.07, 0.88), ("saum", 0.10, "erde_saum", 0.9)]},
    "trfourlane": {"belag": "asphalt_hell", "rand": GLATT, "verblassen": 0.45, "deko": [
        ("schmutz", 0.06, 0.10), ("risse", 0.35, 0.55), ("flicken", 7, 0.5, 0.22), ("oel", 0.06, 0.05, 0.88), ("oel", 0.15, 0.05, 0.88), ("oel", 0.27, 0.05, 0.88), ("oel", 0.36, 0.05, 0.88),
        ("oel", -0.06, 0.05, 0.88), ("oel", -0.15, 0.05, 0.88), ("oel", -0.27, 0.05, 0.88), ("oel", -0.36, 0.05, 0.88), ("saum", 0.06, "erde_saum", 0.55), ("streifen", 0.437, 0.026, "bordstein"), ("streifen", -0.437, 0.026, "bordstein"),
        ("linie", 0.42, 0.010, WEISS), ("linie", -0.42, 0.010, WEISS),
        ("linie", 0.21, 0.010, WEISS, (0.2, 0.5)), ("linie", -0.21, 0.010, WEISS, (0.2, 0.5)),
        ("linie", 0.008, 0.007, GELB), ("linie", -0.008, 0.007, GELB), ("zebra", 0.06, 0.045, 0.022, WEISS)]},
    "trfourlane2": {"belag": "asphalt_dunkel", "rand": GLATT, "verblassen": 0.45, "deko": [
        ("schmutz", 0.06, 0.10), ("risse", 0.35, 0.55), ("flicken", 7, 0.5, 0.22), ("oel", 0.06, 0.05, 0.88), ("oel", 0.15, 0.05, 0.88), ("oel", 0.27, 0.05, 0.88), ("oel", 0.36, 0.05, 0.88),
        ("oel", -0.06, 0.05, 0.88), ("oel", -0.15, 0.05, 0.88), ("oel", -0.27, 0.05, 0.88), ("oel", -0.36, 0.05, 0.88), ("saum", 0.06, "erde_saum", 0.55), ("streifen", 0.437, 0.026, "bordstein"), ("streifen", -0.437, 0.026, "bordstein"),
        ("linie", 0.42, 0.010, WEISS), ("linie", -0.42, 0.010, WEISS),
        ("linie", 0.21, 0.010, WEISS, (0.2, 0.5)), ("linie", -0.21, 0.010, WEISS, (0.2, 0.5)),
        ("linie", 0.008, 0.007, GELB), ("linie", -0.008, 0.007, GELB), ("zebra", 0.06, 0.045, 0.022, WEISS)]},
    # sidewalks: slabs of about 1 m (sidewalk: 1 s = 2 m), curb joint near both edges, dirt and cracks
    "trsidewalk": {"belag": "beton_blau", "rand": GLATT, "deko": [
        ("schmutz", 0.08, 0.12), ("risse", 0.25, 0.6),
        ("fuge", 0.5, 0.012, 0.72), ("laengsfuge", 0.39, 0.012, 0.72), ("laengsfuge", -0.39, 0.012, 0.72)]},
    "trsidewalk2": {"belag": "beton_grau", "rand": FRANS, "deko": [
        ("schmutz", 0.10, 0.15), ("risse", 0.4, 0.55), ("fuge", 0.25, 0.008, 0.74), ("laengsfuge", 0.0, 0.008, 0.80),
        ("saum", 0.06, "erde_saum", 0.6)]},
    "trsidewalk3": {"belag": "beton_beige", "rand": GLATT, "deko": [
        ("schmutz", 0.08, 0.12), ("risse", 0.25, 0.6),
        ("fuge", 4.0 / 18, 0.006, 0.74), ("laengsfuge", 0.0, 0.006, 0.78),
        ("laengsfuge", 0.42, 0.007, 0.70), ("laengsfuge", -0.42, 0.007, 0.70)]},
    "trsidewalk4": {"belag": "beton_beige_hell", "rand": GLATT, "deko": [
        ("schmutz", 0.08, 0.12), ("risse", 0.25, 0.6),
        ("fuge", 4.0 / 19, 0.005, 0.74), ("laengsfuge", 0.15, 0.005, 0.78), ("laengsfuge", -0.15, 0.005, 0.78),
        ("laengsfuge", 0.43, 0.006, 0.70), ("laengsfuge", -0.43, 0.006, 0.70)]},
    "trcobbroad": {"belag": "pflaster", "rand": GLATT, "deko": [
        ("schmutz", 0.10, 0.12), ("saum", 0.06, "erde_saum", 0.5),
        ("streifen", 0.425, 0.04, "bordstein"), ("streifen", -0.425, 0.04, "bordstein")]},
    "trgrassstrip": {"belag": "gras", "rand": {"art": "glatt", "weich": 0.012}, "deko": []},
    "trgrassstrips": {"belag": "gras", "zweit": ("schnee", 0.6), "rand": {"art": "glatt", "weich": 0.012}, "deko": []},
    # railway: standard gauge 1.435 m (1 s = 8.75 m), sleepers 2.6 m x 0.23 m every 0.6 m, ballast bed
    "trtraintrack": {"belag": "schotter_braun", "rand": FRANS, "deko": [
        ("schwellen", 4.0 / 58, 0.027, 0.149, "holz"), ("schiene", 0.082, 0.008, SCHIENE), ("schiene", -0.082, 0.008, SCHIENE)]},
    "trtraintrack2": {"belag": "schotter_grau", "rand": FRANS, "deko": [
        ("schwellen", 4.0 / 58, 0.027, 0.149, "holz"), ("schiene", 0.082, 0.008, SCHIENE), ("schiene", -0.082, 0.008, SCHIENE)]},
    "trtraintrack3": {"belag": "schotter_dunkel", "rand": FRANS, "deko": [
        ("schwellen", 4.0 / 58, 0.027, 0.149, "holz"), ("schiene", 0.082, 0.008, SCHIENE), ("schiene", -0.082, 0.008, SCHIENE)]},
    # parking: stalls 2.5 m wide (1 s = 8.75 m: period 4/14), line 0.15 m, stall lines 0.75 s long from the edge line
    "trparkinglines": {"belag": None, "nur_gerade": True, "deko": [("linie", 0.43, 0.017, WEISS), ("quer", 4.0 / 14, 0.017, 0.43, -0.32, WEISS)]},
    "trparklinesdiag": {"belag": None, "nur_gerade": True, "deko": [
        ("linie", 0.43, 0.017, WEISS), ("schraeg", 4.0 / 12, 0.017, 0.43, -0.32, 60.0, WEISS)]},
    # line types: 1 s = 5 m
    "trcautionstrip": {"belag": None, "deko": [("wechsel", 0.0, 0.03, 0.25, ROT, GELB)]},
    "trcautionarea": {"belag": None, "deko": [
        ("linie", 0.085, 0.016, WEISS), ("linie", -0.085, 0.016, WEISS), ("schraeg", 0.1, 0.03, 0.075, -0.075, 45.0, WEISS)]},
    "trcautionareay": {"belag": None, "deko": [
        ("linie", 0.085, 0.016, GELB_HELL), ("linie", -0.085, 0.016, GELB_HELL), ("schraeg", 0.1, 0.03, 0.075, -0.075, 45.0, GELB_HELL)]},
    "trcrosswalk": {"belag": None, "deko": [("quer", 0.2, 0.1, 0.25, -0.25, WEISS)]},
    "trcrosswalky": {"belag": None, "deko": [("quer", 0.2, 0.1, 0.25, -0.25, GELB_HELL)]},
    # band 0.10 s wide with holes 0.08 s long and 0.04 s wide every 4/23 s (a ladder seen from above)
    "trthickline": {"belag": None, "deko": [("leiter", 0.0, 0.10, 4.0 / 23, 0.08, 0.04, WEISS)]},
    "trsinglewhite": {"belag": None, "deko": [("linie", 0.0, 0.03, WEISS)]},
    "trdashedwhite": {"belag": None, "deko": [("streifen", 0.0, 0.14, "asphalt_dunkel"),
                                              ("linie", 0.0, 0.02, WEISS, (0.3, 4.0 / 7))]},
}
# Decals (RoadWidthInTexture 3), drawn in image space; box centre and size as texture fractions.
DECALS = {
    "trarrow": ("pfeil", WEISS), "trarrowbidir": ("doppelpfeil", WEISS), "trarrowlng": ("langpfeil", WEISS),
    "trcirclecross": ("kreiskreuz", ROT), "trcross": ("kreuz", GELB_HELL), "trnosymbol": ("verbot", ROT),
    "trhelipad": ("helipad", WEISS), "trparkspace": ("parkplatz", WEISS),
    "trwdcaution": ("text:CAUTION", WEISS), "trwdstophere": ("text:STOP/HERE", WEISS), "trwdyield": ("text:YIELD", WEISS),
    "trcobblestones": ("fugen", (44, 44, 42)), "trcracks": ("risse:0", (28, 28, 26)),
    "trcrackearth01": ("risse:1", (56, 54, 50)), "trcrackearth02": ("risse:2", (40, 40, 36)),
    "trcrackearth03": ("risse:3", (32, 32, 30)), "trcrackearth04": ("risse:4", (24, 24, 22)),
    "trstrtholecvr": ("kanaldeckel", (72, 72, 74)),
}
WEAR_SCAN = (AC, "RoadLines004")		# worn painted line (colour, opacity): edge chips and wear of all paint
RISS_SCAN = (PH, "mud_cracked_dry_03")		# crack network of the crack decals (from its height map)
FUGEN_SCAN = (PH, "cobblestone_05")		# joint network of the cobblestone decal (from its height map)


def saat_fuer(name):
    return zlib.crc32(name.encode("ascii")) % 100000


# -------------------------------------------------------------------------------------------------
# Materials
# -------------------------------------------------------------------------------------------------
def scan_meter(quelle, aid, download):
    with open(os.path.join(download, quelle, aid, "meta.json"), encoding="utf-8") as f:
        m = json.load(f)
    mm = (m.get("masse_mm") or [0, 0])[0]
    return mm / 1000.0 if mm else 2.0


def faerben(f, ton, streu):
    """Factor image -> own mean tone and spread (std of log brightness); colour deviation damped."""
    lum = bf.lum601(f)
    lg = np.log(np.maximum(lum, 1e-4))
    lgc = np.log(np.maximum(f, 1e-4)) - lg[..., None]
    lg = lg - lg.mean()
    k = streu / max(float(lg.std()), 1e-6)
    out = np.exp(lg[..., None] * k + lgc * 0.6)
    ton = np.asarray(ton, dtype=np.float64) / 255.0
    for _ in range(3):
        out = np.clip(out * (ton / np.maximum(out.reshape(-1, 3).mean(0), 1e-6)), 0.0, 1.0)
    return out


VARIANTEN = 4


def varianten(f, hoehe, k, M, saat):
    """Scan tiled k x k over M x M, as a soft mix of VARIANTEN copies with own offset, rotation and mirroring in
    cells of about 1.5 tiles: periodic over the texture, but without the visible grid of a plain repetition."""
    rng = np.random.default_rng(saat)
    n = f.shape[0]
    summe_f = np.zeros((M, M, 3))
    summe_h = np.zeros((M, M))
    summe_w = np.zeros((M, M))
    for j in range(VARIANTEN):
        dy, dx = rng.integers(0, n, 2)
        dreh = int(rng.integers(0, 4))
        spiegel = bool(rng.integers(0, 2))

        def form(x):
            x = np.roll(np.roll(x, dy, 0), dx, 1)
            x = np.rot90(x, dreh, axes=(0, 1))
            return x[:, ::-1] if spiegel else x
        lf = bf.lege(form(f), k, M, M, False, (0, 0))
        lh = bf.lege(form(hoehe), k, M, M, False, (0, 0))
        w = np.exp(4.0 * bf.rauschen_periodisch(M, max(2.0, k / 1.5), saat + 7 * j + 1))
        summe_f += lf * w[..., None]
        summe_h += lh * w
        summe_w += w
    return summe_f / summe_w[..., None], summe_h / summe_w


_BELAG_CACHE = {}


def belag_bild(name, M, breite_m, download, saat):
    """Material `name` over the whole texture (M x M, periodic), texture width = breite_m metres."""
    key = (name, M, breite_m, saat)
    if key in _BELAG_CACHE:
        return _BELAG_CACHE[key]
    p = BELAEGE[name]
    lagen = []
    for i, (q, aid) in enumerate(p["scans"]):
        # massstab > 1 enlarges the scan (stones readable from the game camera)
        k = max(1, int(round(breite_m / (scan_meter(q, aid, download) * p.get("massstab", 1.0)))))
        if i > 0 and k > 2:
            k -= 1				# second layer repeats with another period
        n = int(np.ceil(M / k))
        farbe, hoehe, ao, rel = bf.scan_karten(q, aid, download, n)
        f = farbe * (np.power(ao, 0.6) * np.power(rel, 0.4))[..., None]
        f = bf.normiert(f, p.get("glatt", 0.6))
        lagen.append(varianten(f, hoehe, k, M, saat + 31 * i))
    if len(lagen) == 1:
        f = lagen[0][0]
    else:
        # mixing cells of about a third of the road width: larger regions of one scan read as clouds
        roh = bf.rauschen_periodisch(M, 12, saat + 3) * 0.25 + (lagen[1][1] - lagen[0][1])
        w = bf.maske_mit_anteil(roh, 0.45, 0.2)[..., None]
        f = lagen[0][0] * (1 - w) + lagen[1][0] * w
    out = faerben(f, p["ton"], p["streu"])
    if p["flecken"]:
        # stains after the colouring (otherwise the spread scaling amplifies them), small against a field: the
        # fields sit at different places of the texture, large stains would make them differ in brightness
        out = np.clip(out * np.exp(p["flecken"] * bf.rauschen_periodisch(M, 12, saat + 5))[..., None], 0, 1)
    _BELAG_CACHE[key] = out
    return out


_WEAR = {}


def wear_streifen(download):
    """(H x w) opacity strip of a worn painted line, columns = across the line (with chipped edges)."""
    if "s" not in _WEAR:
        d = bd.lies(os.path.join(download, *WEAR_SCAN, "deckkraft.png"), grau=True)
        spalten = d.mean(axis=0)
        innen = np.where(spalten > 0.5)[0]
        lo, hi = max(0, innen[0] - 2), min(d.shape[1], innen[-1] + 3)
        _WEAR["s"] = d[:, lo:hi]
    return _WEAR["s"]


def farbe(c):
    return np.asarray(c, dtype=np.float64) / 255.0


# -------------------------------------------------------------------------------------------------
# Geometry
# -------------------------------------------------------------------------------------------------
def strip_werte(art, P_U, P_V, a1, a2):
    """Lateral offset d (left positive), along a, distance r to the centre line (round cap behind a ray)."""
    if art == "bogen":
        (cx, cy), R = a1, a2
        x, y = P_U - cx, P_V - cy
        rr = np.hypot(x, y)
        d = R - rr
        a = R * np.arctan2(x, -y)
        return d, a, np.abs(d)
    (ox, oy), (dx, dy) = a1, a2
    x, y = P_U - ox, P_V - oy
    a = x * dx + y * dy
    d = dx * y - dy * x
    r = np.abs(d)
    if art == "strahl":
        r = np.where(a < 0, np.hypot(x, y), r)
    return d, a, r


def periode_fuer(p, feld):
    """Period of a feature along the piece: whole number per texture (straight band) or per 30-degree arc."""
    if feld == "gerade":
        return 4.0 / max(1, round(4.0 / p))
    if feld.startswith("bogen"):
        bogen = (1.5 if feld == "bogen_weit" else 0.5) * math.pi / 6
        return bogen / max(1, round(bogen / p))
    return p


def glatt_stufe(x, px):
    return np.clip(x / px + 0.5, 0.0, 1.0)


def strich_maske(a, strich, periode, px):
    t = np.mod(a, periode)
    innen = np.minimum(t, strich - t)
    aussen = -np.minimum(t - strich, periode - t)
    return glatt_stufe(np.where(t < strich, innen, aussen), px)


class Kontext:
    """Per-texture inputs shared by all fields: material images, noise, scan strips."""

    def __init__(self, name, M, download):
        self.name = name
        self.M = M
        self.kante, self.breite_einheiten, self.wit = STRASSEN[name]
        self.meter_je_s = self.breite_einheiten * METER_JE_EINHEIT
        self.breite_m = 4 * self.meter_je_s
        self.download = download
        self.saat = saat_fuer(name)
        self.px = 4.0 / M
        self.rez = REZEPTE.get(name, {})
        self.rausch1 = np.clip(bf.rauschen_periodisch(M, 40, self.saat + 11) * 0.6
                               + bf.rauschen_periodisch(M, 140, self.saat + 12) * 0.4, -2.5, 2.5)
        self.rausch2 = bf.rauschen_periodisch(M, 220, self.saat + 13)
        # low-frequency field for faded markings and dirt
        self.rausch_tief = bf.rauschen_periodisch(M, 10, self.saat + 14)
        self._bel = {}
        self._cache = {}

    def belag(self, name):
        if name not in self._bel:
            self._bel[name] = belag_bild(name, self.M, self.breite_m, self.download, self.saat)
        return self._bel[name]

    def riss(self, anteil):
        """Crack lines (0..1) over the texture: high-pass of a cracked-concrete height map, only in regions of
        share `anteil`."""
        key = ("riss", anteil)
        if key not in self._cache:
            n = max(16, int(round(self.M / max(1, round(self.breite_m / 2.0)))))
            k = max(1, int(round(self.M / n)))
            f, hoehe, _, _ = bf.scan_karten(PH, "cracked_concrete", self.download, n)
            _, hm = varianten(f, hoehe, k, self.M, self.saat + 17)
            hp = hm - bd.gauss_periodisch(hm, max(2.0, n / 20.0))
            hp = hp / max(hp.std(), 1e-6)
            linie = np.clip((-1.3 - hp) / 0.8, 0, 1)
            region = bf.maske_mit_anteil(bf.rauschen_periodisch(self.M, 5, self.saat + 18), anteil, 0.15)
            self._cache[key] = linie * region
        return self._cache[key]

    def flicken(self, anzahl, laenge, breite):
        """Patches of newer asphalt: (mask 0..1, brightness factor per patch, seam 0..1), periodic."""
        key = ("flicken", anzahl, laenge, breite)
        if key not in self._cache:
            M = self.M
            rng = np.random.default_rng(self.saat + 19)
            m = np.zeros((M, M))
            fak = np.ones((M, M))
            for _ in range(anzahl):
                lu = max(4, int(laenge * M / 4 * rng.uniform(0.5, 1.5)))
                lv = max(4, int(breite * M / 4 * rng.uniform(0.6, 1.4)))
                u0, v0 = rng.integers(0, M, 2)
                ys = (np.arange(lv) + v0) % M
                xs = (np.arange(lu) + u0) % M
                m[np.ix_(ys, xs)] = 1.0
                fak[np.ix_(ys, xs)] = rng.uniform(0.72, 0.95)
            weich = bd.gauss_periodisch(m, M / 900.0)
            naht = np.clip(1.0 - np.abs(weich - 0.5) / 0.25, 0, 1)
            self._cache[key] = (np.clip(weich, 0, 1), fak, naht)
        return self._cache[key]

    def blass(self, staerke):
        """Factor for faded markings: low-frequency fading plus small chips."""
        return np.clip(1.0 - staerke * (0.5 + 0.5 * np.clip(self.rausch_tief, -1, 1)), 0.15, 1.0) * \
            (1.0 - 0.7 * glatt_stufe(self.rausch2 - 1.2, 0.3))

    def grund(self):
        rez = self.rez
        b = rez["belag"]
        if b is None:
            return None
        g = self.belag(b)
        if rez.get("zweit"):
            z, anteil = rez["zweit"]
            roh = bf.rauschen_periodisch(self.M, 40, self.saat + 21) * 0.3 + self.rausch1 * 0.15
            w = bf.maske_mit_anteil(roh, anteil, 0.12)[..., None]
            g = g * (1 - w) + self.belag(z) * w
        return g

    def wear(self, lat, a_m):
        """Paint opacity from the worn line scan: lat 0..1 across the line, a_m along in metres."""
        s = wear_streifen(self.download)
        H, w = s.shape
        laenge_m = H / float(w) * 0.15
        zeile = (np.floor(np.mod(a_m / laenge_m, 1.0) * H)).astype(np.int64) % H
        spalte = np.clip((lat * (w - 1)).astype(np.int64), 0, w - 1)
        return s[zeile, spalte]


def male_feld(ctx, feld, U, V, strips, sl):
    """RGBA of one field (arrays over a texel region `sl`)."""
    rez = ctx.rez
    h = ctx.wit / 2.0
    px = ctx.px
    werte = [strip_werte(s[0], U, V, s[1], s[2]) for s in strips]
    r = np.min(np.stack([w[2] for w in werte]), axis=0)
    innen = [w[2] < h * 0.98 for w in werte]
    grund = ctx.grund()
    n1, n2 = ctx.rausch1[sl], ctx.rausch2[sl]
    shape = U.shape
    if grund is not None:
        rgb = grund[sl].copy()
    else:
        rgb = np.zeros(shape + (3,))
    alpha = np.zeros(shape)
    blass = ctx.blass(rez.get("verblassen", 0.0))[sl] if rez.get("verblassen") else 1.0
    kern = glatt_stufe(h - 0.015 - r, px)
    if rez["belag"]:
        rand = rez["rand"]
        if rand["art"] == "glatt":
            alpha = glatt_stufe(h - r, max(px * 1.2, rand["weich"]))
        else:
            hh = h - rand["amp"] * 0.5 + rand["amp"] * np.clip(n1, -2, 2) * 0.5
            alpha = glatt_stufe(hh - r, max(px, rand["weich"]))
            if rand["loch"]:
                zone = np.clip((r - (h - 0.12)) / 0.12, 0.0, 1.0)
                alpha *= 1.0 - rand["loch"] * zone * glatt_stufe(n2 - 0.3, 0.3)
    farbe_gesetzt = grund is not None

    def ueber(maske, c):
        nonlocal rgb, alpha, farbe_gesetzt
        c = np.asarray(c, dtype=np.float64)
        if c.ndim == 1:
            c = np.broadcast_to(c, shape + (3,))
        if not farbe_gesetzt:
            # no surface: colour everywhere = first paint colour, so mips do not darken towards the edge
            rgb = np.array(c, dtype=np.float64, copy=True)
            farbe_gesetzt = True
        m = maske[..., None]
        if rez["belag"]:
            rgb = rgb * (1 - m) + c * m
        else:
            neu_a = maske + alpha * (1 - maske)
            rgb = (c * m + rgb * (alpha * (1 - maske))[..., None]) / np.maximum(neu_a, 1e-6)[..., None]
            rgb = np.where((neu_a > 1e-6)[..., None], rgb, c)
            alpha = neu_a

    for f in rez["deko"]:
        art = f[0]
        for i, (d, a, ri) in enumerate(werte):
            # markings of strip i are suppressed inside the other strips (junction box)
            frei = np.ones(shape)
            if art in ("linie", "quer", "schraeg", "fuge", "laengsfuge") and rez["belag"]:
                for j in range(len(werte)):
                    if j != i:
                        frei *= 1.0 - innen[j]
            if art == "leiter":
                d0, breite, periode, loch_l, loch_b, c = f[1], f[2], periode_fuer(f[3], feld), f[4], f[5], farbe(f[6])
                x = d - d0
                m = glatt_stufe(breite / 2 - np.abs(x), px)
                m = m * (0.55 + 0.45 * ctx.wear(np.clip(x / (1.15 * breite) + 0.5, 0.0, 1.0), a * ctx.meter_je_s))
                loch = strich_maske(a + loch_l / 2, loch_l, periode, px) * glatt_stufe(loch_b / 2 - np.abs(x), px)
                m = m * (1.0 - loch)
                if strips[i][0] == "strahl":
                    m = m * (a > 0.0)
                ueber(m * blass, c)
                continue
            if art in ("linie", "wechsel"):
                d0, breite, c = f[1], f[2], f[3] if art == "linie" else f[4]
                halb = breite / 2.0
                x = d - d0
                m = glatt_stufe(halb * 1.15 - np.abs(x), px)
                lat = np.clip(x / (2.3 * halb) + 0.5, 0.0, 1.0)
                m = m * ctx.wear(lat, a * ctx.meter_je_s)
                if art == "linie" and len(f) > 4 and f[4]:
                    strich, periode = f[4]
                    periode = periode_fuer(periode, feld)
                    m = m * strich_maske(a, strich * periode / f[4][1], periode, px)
                deck = f[5] if art == "linie" and len(f) > 5 else 1.0
                if art == "wechsel":
                    periode = periode_fuer(f[3], feld)
                    rot = strich_maske(a, periode / 2, periode, px)
                    c = farbe(f[4])[None, None, :] * rot[..., None] + farbe(f[5])[None, None, :] * (1 - rot[..., None])
                else:
                    c = farbe(c)
                if strips[i][0] == "strahl":
                    # surface roads: the junction box hides the start; painted-only lines start at the centre
                    m = m * (a > (-h if rez["belag"] else 0.0))
                ueber(m * frei * deck * blass, c)
            elif art in ("quer", "schraeg"):
                periode, breite, d_von, d_bis = f[1], f[2], f[3], f[4]
                c = farbe(f[6] if art == "schraeg" else f[5])
                periode = periode_fuer(periode, feld)
                aa = a if art == "quer" else a + d / math.tan(math.radians(f[5]))
                t = np.mod(aa + periode / 2, periode) - periode / 2
                quer = glatt_stufe(breite * 0.575 - np.abs(t), px)
                lo, hi = min(d_von, d_bis), max(d_von, d_bis)
                laengs = glatt_stufe(np.minimum(d - lo, hi - d), px)
                lat = np.clip(t / (2.3 * breite) + 0.5, 0.0, 1.0)
                m = quer * laengs * ctx.wear(lat, d * ctx.meter_je_s + 7.0 * np.floor((aa + periode / 2) / periode))
                if strips[i][0] == "strahl":
                    m = m * (a > 0)
                ueber(m * frei * blass, c)
            elif art == "zebra":
                # crossing stripes just inside the junction box (square |U|, |V| < h), only in tee and crossing
                if feld not in ("tee", "kreuz"):
                    continue
                tiefe, periode, breite, c = f[1], f[2], f[3], farbe(f[4])
                zone = glatt_stufe(np.minimum(np.abs(a) - (h - tiefe - 0.012), (h - 0.012) - np.abs(a)), px)
                t = np.mod(d + periode / 2, periode) - periode / 2
                m = zone * glatt_stufe(breite / 2 - np.abs(t), px) * glatt_stufe(h - 0.05 - np.abs(d), px)
                if strips[i][0] == "strahl":
                    m = m * (a > 0)
                lat = np.clip(t / (2.3 * breite) + 0.5, 0.0, 1.0)
                m = m * ctx.wear(lat, a * ctx.meter_je_s + 5.0 * np.floor((d + periode / 2) / periode))
                ueber(m * blass, c)
            elif art == "oel":
                # oil and rubber along the wheel paths
                d0, breite, faktor = f[1], f[2], f[3]
                m = np.exp(-2.0 * ((d - d0) / (breite / 2)) ** 2) * (0.65 + 0.35 * np.clip(ctx.rausch_tief[sl], -1, 1))
                m = m * (1.0 + 0.4 * np.clip(n1, -1, 1)) * kern * frei_streifen(innen, i)
                rgb = rgb * (1 - (1 - faktor) * np.clip(m, 0, 1.3)[..., None])
            elif art in ("risse", "flicken", "schmutz"):
                if i > 0:
                    continue
                if art == "risse":
                    m = ctx.riss(f[1])[sl] * kern
                    rgb = rgb * (1 - (1 - f[2]) * m[..., None])
                elif art == "flicken":
                    mm, fak, naht = ctx.flicken(f[1], f[2], f[3])
                    mm, fak, naht = mm[sl] * kern, fak[sl], naht[sl] * kern
                    rgb = rgb * (1 + (fak - 1) * mm)[..., None] * (1 - 0.3 * naht)[..., None]
                else:
                    # low-frequency dirt and darker blotches (texture space, small against a field)
                    if "schmutz" not in ctx._cache:
                        ctx._cache["schmutz"] = (bf.rauschen_periodisch(ctx.M, 8, ctx.saat + 22),
                                                 glatt_stufe(bf.rauschen_periodisch(ctx.M, 24, ctx.saat + 23) - 1.0, 0.5))
                    tief, flecke = ctx._cache["schmutz"]
                    rgb = rgb * (np.exp(f[1] * tief[sl]) * (1 - f[2] * flecke[sl]))[..., None]
            elif art == "fuge":
                periode = periode_fuer(f[1], feld)
                t = np.mod(a + periode / 2, periode) - periode / 2
                m = glatt_stufe(f[2] / 2 - np.abs(t), px) * (ri < h * 0.97)
                if strips[i][0] == "strahl":
                    m = m * (a > 0)
                rgb = rgb * (1 - (1 - f[3]) * (m * frei)[..., None])
            elif art == "laengsfuge":
                m = glatt_stufe(f[2] / 2 - np.abs(d - f[1]), px)
                if strips[i][0] == "strahl":
                    m = m * (a > 0)
                rgb = rgb * (1 - (1 - f[3]) * (m * frei)[..., None])
            elif art == "saum":
                breite, b2, anteil = f[1], f[2], f[3]
                w = glatt_stufe(ri - (h - breite) + 0.04 * n1 + 0.02 * n2, 0.03) * anteil
                if i == 0:
                    w_ges = w
                else:
                    w_ges = np.minimum(w_ges, w)
                if i == len(werte) - 1:
                    rgb = rgb * (1 - w_ges[..., None]) + ctx.belag(b2)[sl] * w_ges[..., None]
            elif art == "streifen":
                d0, breite, b2 = f[1], f[2], f[3]
                m = glatt_stufe(breite / 2 - np.abs(d - d0), px)
                if strips[i][0] == "strahl":
                    m = m * (a > -0.0)
                if rez["belag"]:
                    m = m * frei_streifen(innen, i)
                    stein = ctx.belag(b2)[sl]
                    if b2 == "bordstein":
                        # curb stones of about 1 m with dark joints, chipped spots and dirt
                        per = 1.0 / ctx.meter_je_s
                        t = np.mod(a + per / 2, per) - per / 2
                        fuge = glatt_stufe(0.004 - np.abs(t), px)
                        ausbruch = glatt_stufe(n2 - 1.1, 0.35)
                        schmutz = 0.82 + 0.18 * np.clip(0.5 + 0.5 * ctx.rausch_tief[sl], 0, 1)
                        stein = stein * ((1 - 0.45 * fuge) * (1 - 0.4 * ausbruch) * schmutz)[..., None]
                    rgb = rgb * (1 - m[..., None]) + stein * m[..., None]
                else:
                    ueber(m, ctx.belag(b2)[sl])
            elif art == "schwellen":
                periode, breite, d_halb, b2 = f[1], f[2], f[3], f[4]
                periode = periode_fuer(periode, feld)
                t = np.mod(a + periode / 2, periode) - periode / 2
                m = glatt_stufe(breite / 2 - np.abs(t), px) * glatt_stufe(d_halb - np.abs(d) + 0.004 * n2, px)
                if strips[i][0] == "strahl":
                    m = m * (a > 0)
                schatten = glatt_stufe(breite / 2 + 0.006 - np.abs(t - 0.004), px) * glatt_stufe(d_halb + 0.004 - np.abs(d), px)
                rgb = rgb * (1 - 0.35 * (schatten * (1 - m))[..., None])
                rgb = rgb * (1 - m[..., None]) + ctx.belag(b2)[sl] * m[..., None]
            elif art == "schiene":
                d0, breite, c = f[1], f[2], f[3]
                x = d - d0
                if strips[i][0] == "strahl":
                    vorn = a > -0.02
                else:
                    vorn = np.ones(shape, dtype=bool)
                schatten = glatt_stufe(breite * 1.6 - np.abs(x + breite * 0.6), px) * vorn
                rgb = rgb * (1 - 0.5 * schatten[..., None])
                m = glatt_stufe(breite / 2 - np.abs(x), px) * vorn
                glanz = 0.85 + 0.25 * np.clip(1 - np.abs(x) / (breite / 2), 0, 1)
                rgb = rgb * (1 - m[..., None]) + (farbe(c)[None, None, :] * glanz[..., None]) * m[..., None]
    return np.clip(rgb, 0, 1), np.clip(alpha, 0, 1)


def frei_streifen(innen, i):
    m = np.ones(innen[0].shape)
    for j in range(len(innen)):
        if j != i:
            m *= 1.0 - innen[j]
    return m


def polygon_maske(M, punkte, rand):
    im = Image.new("L", (M, M), 0)
    ImageDraw.Draw(im).polygon([(x, y) for x, y in punkte], fill=255)
    if rand > 0:
        im = im.filter(ImageFilter.MaxFilter(2 * rand + 1))
    return np.asarray(im) > 0


def strasse_bild(name, M, download):
    """RGBA float image (M x M) of a band road texture."""
    ctx = Kontext(name, M, download)
    wit = ctx.wit
    h = wit / 2.0
    grund = ctx.grund()
    out_rgb = np.zeros((M, M, 3))
    out_a = np.zeros((M, M))
    jj = (np.arange(M) + 0.5) / M
    # straight band: rows with |V| <= h + margin, u along
    v = jj
    V = (GERADE_V0 / 512.0 - v) * 4.0
    zeilen = np.where(np.abs(V) <= h + 0.1)[0]
    sl = (slice(zeilen[0], zeilen[-1] + 1), slice(0, M))
    U2, V2 = np.meshgrid(jj * 4.0, V[sl[0]])
    rgb, a = male_feld(ctx, "gerade", U2, V2, [("linie", (0.0, 0.0), (1.0, 0.0))], sl)
    out_rgb[sl] = rgb
    out_a[sl] = a
    belegt = np.zeros((M, M), dtype=bool)
    belegt[sl] = True
    rand_px = max(3, M // 170)
    # parking rows are only drawn as straight segments; their fields stay empty
    for fname, ((u0, v0), quad, strips, fade) in ({} if ctx.rez.get("nur_gerade") else felder(wit)).items():
        u0f, v0f = u0 / 512.0, v0 / 512.0
        poly = [((u0f + U / 4.0) * M, (v0f - V_ / 4.0) * M) for U, V_ in quad]
        maske = polygon_maske(M, poly, rand_px)
        ys, xs = np.where(maske)
        sl = (slice(ys.min(), ys.max() + 1), slice(xs.min(), xs.max() + 1))
        Ug, Vg = np.meshgrid((jj[sl[1]] - u0f) * 4.0, (v0f - jj[sl[0]]) * 4.0)
        rgb, a = male_feld(ctx, fname, Ug, Vg, strips, sl)
        if fade:
            halb, weich = fade if ctx.rez.get("rand", GLATT)["art"] == "glatt" else (fade[0] + 0.03, fade[1] + 0.05)
            a = a * np.clip((halb - np.abs(Ug)) / weich + 0.5, 0, 1)
        m = maske[sl] & (a >= out_a[sl])
        if belegt[sl].any():
            m &= ~belegt[sl] | (a > out_a[sl] + 0.02)
        out_rgb[sl] = np.where(m[..., None], rgb, out_rgb[sl])
        out_a[sl] = np.where(m, a, out_a[sl])
        belegt[sl] |= maske[sl]
    # colour of texels outside every field: surface (or paint) colour, so mip levels keep the colour at the edges
    if grund is not None:
        leer = ~belegt
        out_rgb[leer] = grund[leer]
    else:
        gew = out_a > 0.3
        if gew.any():
            out_rgb[~gew] = out_rgb[gew].mean(axis=0)
    return out_rgb, out_a


# -------------------------------------------------------------------------------------------------
# Decals
# -------------------------------------------------------------------------------------------------
# Stroke font (own): letters on a 4 x 8 grid, y up; polylines.
SCHRIFT = {
    "A": [[(0, 0), (0, 6.5), (1.5, 8), (2.5, 8), (4, 6.5), (4, 0)], [(0, 3.8), (4, 3.8)]],
    "C": [[(4, 6.8), (3, 8), (1, 8), (0, 7), (0, 1), (1, 0), (3, 0), (4, 1.2)]],
    "D": [[(0, 0), (0, 8), (2.5, 8), (4, 6.5), (4, 1.5), (2.5, 0), (0, 0)]],
    "E": [[(4, 8), (0, 8), (0, 0), (4, 0)], [(0, 4.1), (3.2, 4.1)]],
    "H": [[(0, 0), (0, 8)], [(4, 0), (4, 8)], [(0, 4.1), (4, 4.1)]],
    "I": [[(2, 0), (2, 8)]],
    "L": [[(0, 8), (0, 0), (4, 0)]],
    "N": [[(0, 0), (0, 8), (4, 0), (4, 8)]],
    "O": [[(1, 0), (0, 1), (0, 7), (1, 8), (3, 8), (4, 7), (4, 1), (3, 0), (1, 0)]],
    "P": [[(0, 0), (0, 8), (3, 8), (4, 7), (4, 5), (3, 4), (0, 4)]],
    "R": [[(0, 0), (0, 8), (3, 8), (4, 7), (4, 5), (3, 4), (0, 4)], [(2, 4), (4, 0)]],
    "S": [[(4, 6.8), (3, 8), (1, 8), (0, 7), (0, 5.2), (1, 4.2), (3, 3.8), (4, 2.8), (4, 1), (3, 0), (1, 0), (0, 1.2)]],
    "T": [[(0, 8), (4, 8)], [(2, 8), (2, 0)]],
    "U": [[(0, 8), (0, 1), (1, 0), (3, 0), (4, 1), (4, 8)]],
    "Y": [[(0, 8), (2, 4.2), (4, 8)], [(2, 4.2), (2, 0)]],
}


def zeichne_linien(dr, linien, dicke, fill):
    r = dicke / 2.0
    for zug in linien:
        dr.line(zug, fill=fill, width=int(round(dicke)))
        for x, y in zug:
            dr.ellipse((x - r, y - r, x + r, y + r), fill=fill)


def text_maske(M, zeilen, box):
    """Words in tall stroke letters inside box (u1, v1, u2, v2) as texture fractions; rows stacked."""
    im = Image.new("L", (M, M), 0)
    dr = ImageDraw.Draw(im)
    u1, v1, u2, v2 = [x * M for x in box]
    n_z = len(zeilen)
    luecke = 0.12
    zeilen_h = (v2 - v1) / (n_z + luecke * (n_z - 1))
    for zi, wort in enumerate(zeilen):
        breite_einh = len(wort) * 4 + (len(wort) - 1) * 1.4 + 1.2
        s_y = zeilen_h / 9.2
        s_x = min((u2 - u1) / breite_einh, s_y * 1.1)
        x0 = u1 + ((u2 - u1) - breite_einh * s_x) / 2 + 0.6 * s_x
        y_unten = v1 + zi * zeilen_h * (1 + luecke) + zeilen_h - 0.6 * s_y
        dicke = 1.9 * s_x
        for k, ch in enumerate(wort):
            ox = x0 + k * 5.4 * s_x
            zuege = [[(ox + x * s_x, y_unten - y * s_y) for x, y in zug] for zug in SCHRIFT[ch]]
            zeichne_linien(dr, zuege, dicke, 255)
    return np.asarray(im, dtype=np.float64) / 255.0


def form_maske(M, art):
    im = Image.new("L", (M, M), 0)
    dr = ImageDraw.Draw(im)

    def p(u, v):
        return (u * M, v * M)
    if art == "pfeil":
        dr.polygon([p(0.25, 0.03), p(0.45, 0.22), p(0.32, 0.22), p(0.32, 0.47), p(0.18, 0.47), p(0.18, 0.22),
                    p(0.05, 0.22)], fill=255)
    elif art == "doppelpfeil":
        dr.polygon([p(0.03, 0.25), p(0.15, 0.12), p(0.15, 0.2), p(0.35, 0.2), p(0.35, 0.12), p(0.47, 0.25),
                    p(0.35, 0.38), p(0.35, 0.3), p(0.15, 0.3), p(0.15, 0.38)], fill=255)
    elif art == "langpfeil":
        dr.polygon([p(0.02, 0.22), p(0.17, 0.08), p(0.17, 0.17), p(1.01, 0.17), p(1.01, 0.27), p(0.17, 0.27),
                    p(0.17, 0.36)], fill=255)
    elif art in ("kreiskreuz", "verbot"):
        c, r, w = 0.25, 0.2, 0.035
        dr.ellipse((p(c - r, c - r), p(c + r, c + r)), outline=255, width=int(w * M))
        q = r * 0.72
        dicke = w * M
        if art == "kreiskreuz":
            zeichne_linien(dr, [[p(c - q, c - q), p(c + q, c + q)], [p(c - q, c + q), p(c + q, c - q)]], dicke, 255)
        else:
            zeichne_linien(dr, [[p(c - q, c + q), p(c + q, c - q)]], dicke, 255)
    elif art == "kreuz":
        zeichne_linien(dr, [[p(0.05, 0.25), p(0.45, 0.25)], [p(0.25, 0.05), p(0.25, 0.45)]], 0.035 * M, 255)
    elif art == "parkplatz":
        w = 0.03 * M
        zeichne_linien(dr, [[p(0.12, 0.05), p(0.05, 0.05), p(0.05, 0.45), p(0.12, 0.45)],
                            [p(0.38, 0.05), p(0.45, 0.05), p(0.45, 0.45), p(0.38, 0.45)]], w, 255)
    elif art == "parkplatz_gelb":
        # yellow bracket around the white one, open towards the inside
        w = 0.022 * M
        zeichne_linien(dr, [[p(0.07, 0.008), p(0.012, 0.008), p(0.012, 0.492), p(0.07, 0.492)],
                            [p(0.43, 0.008), p(0.488, 0.008), p(0.488, 0.492), p(0.43, 0.492)]], w, 255)
    return np.asarray(im, dtype=np.float64) / 255.0


def helipad_bilder(M):
    gelb = Image.new("L", (M, M), 0)
    weiss = Image.new("L", (M, M), 0)
    dg, dw = ImageDraw.Draw(gelb), ImageDraw.Draw(weiss)
    w = int(0.025 * M)
    dg.rectangle((0.03 * M, 0.03 * M, 0.47 * M, 0.47 * M), outline=255, width=w)
    dw.ellipse((0.07 * M, 0.07 * M, 0.43 * M, 0.43 * M), outline=255, width=w)
    # yellow cross in the circle
    zeichne_linien(dg, [[(0.16 * M, 0.25 * M), (0.34 * M, 0.25 * M)], [(0.25 * M, 0.16 * M), (0.25 * M, 0.34 * M)]],
                   0.03 * M, 255)
    return np.asarray(gelb, dtype=np.float64) / 255.0, np.asarray(weiss, dtype=np.float64) / 255.0


def netz_maske(M, scan, download, saat, ganz, schwelle):
    """Crack / joint network from the high-pass of a scan's height map (low = crack), M x M."""
    q, aid = scan
    hoehe = bd.lies(os.path.join(download, q, aid, "hoehe.png"), grau=True)
    n = hoehe.shape[0]
    rng = np.random.default_rng(saat)
    y0, x0 = rng.integers(0, n // 2, 2)
    teil = hoehe if ganz else np.roll(np.roll(hoehe, -y0, 0), -x0, 1)[: n // 2, : n // 2]
    teil = np.asarray(Image.fromarray(teil.astype(np.float32), mode="F").resize((M, M), Image.LANCZOS), dtype=np.float64)
    hp = teil - bd.gauss_periodisch(teil, M / 40.0)
    hp = hp / max(hp.std(), 1e-6)
    return np.clip((schwelle - hp) / 0.6, 0.0, 1.0)


def riss_maske(M, download, saat, schwelle=-1.8, weich=0.3):
    """Thin crack network: the deepest valleys of a finer high-pass of the dried-mud height map. Cells of about a
    quarter of the decal area, cracks one to three texels wide (the coarse joint mask drew them as broad grooves)."""
    q, aid = RISS_SCAN
    hoehe = bd.lies(os.path.join(download, q, aid, "hoehe.png"), grau=True)
    n = hoehe.shape[0]
    rng = np.random.default_rng(saat)
    y0, x0 = rng.integers(0, n // 2, 2)
    k = int(n * 0.7)
    teil = np.roll(np.roll(hoehe, -y0, 0), -x0, 1)[:k, :k]
    teil = np.asarray(Image.fromarray(teil.astype(np.float32), mode="F").resize((M, M), Image.LANCZOS), dtype=np.float64)
    hp = teil - bd.gauss_periodisch(teil, M / 120.0)
    hp = hp / max(hp.std(), 1e-6)
    return np.clip((schwelle - hp) / weich, 0.0, 1.0)


def kanaldeckel(M, rgb, rausch):
    """Cast-iron manhole cover, 0.6 m wide (texture 10 m), with a frame ring and a raised grid."""
    yy, xx = (np.mgrid[0:M, 0:M] + 0.5) / M
    r = np.hypot(xx - 0.25, yy - 0.25)
    px = 1.0 / M
    a = glatt_stufe(0.034 - r, px)
    ring = glatt_stufe(0.0045 - np.abs(r - 0.031), px)
    gitter = (glatt_stufe(0.0012 - np.abs(np.mod(xx - 0.25 + 0.0035, 0.007) - 0.0035), px)
              + glatt_stufe(0.0012 - np.abs(np.mod(yy - 0.25 + 0.0035, 0.007) - 0.0035), px)).clip(0, 1)
    hell = 1.0 + 0.45 * ring + 0.25 * gitter * (r < 0.026) + 0.05 * np.clip(rausch, -1, 1)
    return a, rgb * hell[..., None]


def decal_bild(name, M, download):
    art, c = DECALS[name]
    saat = saat_fuer(name)
    rng_rausch = bf.rauschen_periodisch(M, 90, saat + 1)
    # paint wear: small chips (thresholded fine noise) and a slightly uneven opacity
    fein = bf.rauschen_periodisch(M, 260, saat + 2)
    chips = glatt_stufe(fein - 1.7, 0.25)
    abnutzung = np.clip((1.0 - 0.85 * chips) * (0.9 + 0.1 * np.clip(rng_rausch, -1, 1)), 0, 1)
    rgb = np.broadcast_to(farbe(c), (M, M, 3)).copy()
    if art.startswith("text:"):
        a = text_maske(M, art[5:].split("/"), (0.03, 0.05, 0.47, 0.45) if "/" in art else (0.02, 0.14, 0.48, 0.36))
        a = a * abnutzung
    elif art == "parkplatz":
        weiss = form_maske(M, "parkplatz")
        gelb = form_maske(M, "parkplatz_gelb")
        a = np.maximum(weiss, gelb) * abnutzung
        rgb = np.where((gelb > weiss)[..., None], farbe(GELB_HELL), farbe(WEISS))
    elif art == "helipad":
        gelb, weiss = helipad_bilder(M)
        a = np.maximum(gelb, weiss) * abnutzung
        rgb = np.where((gelb > weiss)[..., None], farbe(GELB_HELL), farbe(WEISS))
    elif art.startswith("risse"):
        k = int(art.split(":")[1])
        # trcracks: a broad, branching network (as in the original); trcrackearth: thin cracks
        m = riss_maske(M, download, saat, -0.45, 0.3) if k == 0 else riss_maske(M, download, saat)
        # fade out towards the border of the region a segment shows
        yy, xx = np.mgrid[0:M, 0:M] / M
        gr = 0.56 if k == 0 else 0.54
        rad = np.maximum(np.abs(xx - gr / 2), np.abs(yy - gr / 2)) / (gr / 2)
        a = m * np.clip((1.0 - rad) / 0.25 + 0.2 * rng_rausch, 0, 1)
        rgb = rgb * (0.85 + 0.15 * np.clip(rng_rausch, -1, 1))[..., None]
    elif art == "kanaldeckel":
        a, rgb = kanaldeckel(M, rgb, rng_rausch)
    elif art == "fugen":
        a = netz_maske(M, FUGEN_SCAN, download, saat, True, -0.7) * 0.9
    else:
        a = form_maske(M, art) * abnutzung
    return np.clip(rgb, 0, 1), np.clip(a, 0, 1)


# -------------------------------------------------------------------------------------------------
# Output
# -------------------------------------------------------------------------------------------------
def textur(name, stufe, download):
    N = STRASSEN[name][0] * stufe
    M = N * SS
    import strassen_wege
    if name in DECALS:
        rgb, a = decal_bild(name, M, download)
    elif name in strassen_wege.WEGE:
        rgb, a = strassen_wege.weg_bild(name, M, download)
    else:
        rgb, a = strasse_bild(name, M, download)
    # alpha-weighted downsampling; texels without coverage keep the plain mean colour (no dark halo in the mips)
    ak = bd.box(a, SS)
    gewichtet = bd.box(rgb * a[..., None], SS) / np.maximum(ak, 1e-6)[..., None]
    w = np.clip(ak / 0.02, 0, 1)[..., None]
    rgb = gewichtet * w + bd.box(rgb, SS) * (1 - w)
    return np.clip(rgb, 0, 1), np.clip(ak, 0, 1)


def rgba8(rgb, a):
    return np.concatenate([np.clip(np.rint(rgb * 255), 0, 255), np.clip(np.rint(a * 255), 0, 255)[..., None]],
                          axis=2).astype(np.uint8)


def dds_schreiben(b8, name, ziel, tmp):
    src = os.path.join(tmp, name + ".png")
    Image.fromarray(b8, "RGBA").save(src)
    subprocess.run([TEXCONV, "-nologo", "-y", "-f", "BC7_UNORM", "-o", tmp, src], check=True, stdout=subprocess.DEVNULL)
    shutil.move(os.path.join(tmp, name + ".dds"), os.path.join(ziel, name + ".dds"))


def _rechne(arg):
    name, stufe, download, ziel, png = arg
    rgb, a = textur(name, stufe, download)
    b8 = rgba8(rgb, a)
    tmp = tempfile.mkdtemp(prefix="strassenfrei_")
    try:
        if png:
            Image.fromarray(b8, "RGBA").save(os.path.join(png, name + ".png"))
        if ziel:
            dds_schreiben(b8, name, ziel, tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    deck = a > 0.5
    mittel = [round(float(v) * 255, 1) for v in (rgb[deck].mean(0) if deck.any() else rgb.reshape(-1, 3).mean(0))]
    return name, {"kante": int(a.shape[0]), "deckend": round(float(deck.mean()), 3), "mittel_deckend": mittel}


def cmd_bauen(a):
    namen = [n.lower() for n in (a.nur or [])] or sorted(STRASSEN)
    for n in namen:
        if n not in STRASSEN:
            raise SystemExit("unknown texture: " + n)
    for d in (a.ziel, a.png):
        if d:
            os.makedirs(d, exist_ok=True)
    arbeit = [(n, a.stufe, a.download, a.ziel, a.png) for n in namen]
    werte = {}
    if a.prozesse > 1:
        import multiprocessing
        with multiprocessing.Pool(a.prozesse) as pool:
            for name, info in pool.imap_unordered(_rechne, arbeit):
                werte[name] = info
                print("%-16s %s" % (name, info), flush=True)
    else:
        for arg in arbeit:
            name, info = _rechne(arg)
            werte[name] = info
            print("%-16s %s" % (name, info), flush=True)
    if a.png:
        with open(os.path.join(a.png, "werte.json"), "w", encoding="utf-8") as f:
            json.dump(werte, f, indent=1, sort_keys=True)


def cmd_liste(a):
    for n in sorted(STRASSEN):
        kante, breite, wit = STRASSEN[n]
        rez = ("decal " + DECALS[n][0]) if n in DECALS else ("belag %s, %d features" % (
            REZEPTE[n]["belag"] or "-", len(REZEPTE[n]["deko"]))) if n in REZEPTE else "weg (strassen_wege.py)"
        print("%-16s %4d px  RoadWidth %3d (%5.2f m/s)  wit %.2f  %s" % (n, kante, breite, breite * METER_JE_EINHEIT, wit, rez))
    print(len(STRASSEN), "textures")


def verwendete_scans():
    genutzt = set()
    for rez in REZEPTE.values():
        if rez.get("belag"):
            genutzt.add(rez["belag"])
        if rez.get("zweit"):
            genutzt.add(rez["zweit"][0])
        for f in rez["deko"]:
            genutzt.update(x for x in f if isinstance(x, str) and x in BELAEGE)
    liste = []
    for n in sorted(genutzt):
        for sc in BELAEGE[n]["scans"]:
            liste.append((n, sc))
    liste += [("Markierungen (Abnutzung)", WEAR_SCAN), ("Risse", RISS_SCAN), ("Pflasterfugen", FUGEN_SCAN)]
    import strassen_wege
    return liste + strassen_wege.verwendete_scans()


def cmd_nachweis(a):
    gesehen = {}
    for verwendung, sc in verwendete_scans():
        gesehen.setdefault(sc, []).append(verwendung)
    print("| Scan | Quelle | Autoren | Ma\u00dfe | verwendet f\u00fcr | Lizenz |")
    print("|---|---|---|---|---|---|")
    for (q, aid), verw in sorted(gesehen.items(), key=lambda x: x[0][1].lower()):
        with open(os.path.join(a.download, q, aid, "meta.json"), encoding="utf-8") as f:
            m = json.load(f)
        mm = m.get("masse_mm") or [0, 0]
        masse = ("%.1f x %.1f m" % (mm[0] / 1000.0, mm[1] / 1000.0)).replace(".", ",") if mm[0] else "k. A."
        print("| %s (`%s`) | %s, <%s> | %s | %s | %s | CC0 1.0 |" % (
            m["name"], aid, m["quelle"], m["seite"], ", ".join(m["autoren"]), masse, ", ".join(sorted(set(verw)))))


def lies_dds_kopf(pfad):
    with open(pfad, "rb") as f:
        d = f.read(148)
    h, w = struct.unpack("<II", d[12:20])
    mips = struct.unpack("<I", d[28:32])[0]
    fourcc = d[84:88]
    dxgi = struct.unpack("<I", d[128:132])[0] if fourcc == b"DX10" else None
    return w, h, mips, fourcc, dxgi


def cmd_pruefen(a):
    fehler = 0
    for n in sorted(STRASSEN):
        p = os.path.join(a.ordner, n + ".dds")
        if not os.path.exists(p):
            print("FEHLT", n)
            fehler += 1
            continue
        w, h, mips, fourcc, dxgi = lies_dds_kopf(p)
        soll = STRASSEN[n][0] * a.stufe
        ok = w == soll and h == soll and dxgi == 98 and mips == int(math.log2(soll)) + 1
        if not ok:
            fehler += 1
        print("%-16s %4d x %-4d mips %2d %s dxgi %s %s" % (n, w, h, mips, fourcc, dxgi, "ok" if ok else "FEHLER"))
    extra = [x for x in os.listdir(a.ordner) if x.lower().endswith(".dds") and x[:-4].lower() not in STRASSEN]
    print("zusaetzliche Dateien:", extra)
    print("Fehler:", fehler)
    return 1 if fehler else 0


STUFEN_NAME = {1: "SD", 2: "HD", 4: "UHD"}


def cmd_manifest(a):
    """Manifest of the free content folder: header with the highest tier, then path and EA size per texture."""
    zeilen = ["TEXTURPAKET 1 " + STUFEN_NAME[a.stufe],
              "; free road textures (strassen_frei.py), files in %s size, sizes are the EA originals" % STUFEN_NAME[a.stufe]]
    for n in sorted(STRASSEN):
        kante = STRASSEN[n][0]
        zeilen.append("Art\\Textures\\%s.dds %d %d" % (n, kante, kante))
    with open(os.path.join(a.paket, "texturpaket.txt"), "w", encoding="ascii", newline="\r\n") as f:
        f.write("\n".join(zeilen) + "\n")
    print(len(STRASSEN), "textures in", os.path.join(a.paket, "texturpaket.txt"))


def cmd_vergleich(a):
    """Measuring tool (reads EA textures): sheet EA | free per texture, alpha-weighted brightness."""
    import io
    import texture_inventory as inv
    files, _ = inv.effective_files(bd.root_zh() if not a.generals else bd.root_g())
    os.makedirs(a.aus, exist_ok=True)
    zeilen = []
    for n in sorted(STRASSEN):
        k = "art\\textures\\" + n + ".dds"
        if k not in files:
            continue
        ea = np.asarray(Image.open(io.BytesIO(files[k][1].read(k))).convert("RGBA"), dtype=np.float64) / 255
        p = os.path.join(a.frei, n + ".png")
        if not os.path.exists(p):
            continue
        fr = np.asarray(Image.open(p).convert("RGBA"), dtype=np.float64) / 255

        def kenn(x):
            al = x[..., 3]
            l = bf.lum601(x[..., :3])
            return float((l * al).sum() / max(al.sum(), 1e-6) * 255), float(al.mean())
        le, ae = kenn(ea)
        lf, af = kenn(fr)
        zeilen.append((n, le, lf, ae, af))
        T = 384

        def ansicht(x):
            W = x.shape[0]
            yy, xx = np.mgrid[0:W, 0:W]
            ch = np.where(((yy * 24 // W) + (xx * 24 // W)) % 2 == 0, 0.30, 0.45)[..., None] * np.array([0.9, 1.0, 0.8])
            rgb = x[..., :3] * x[..., 3:] + ch * (1 - x[..., 3:])
            return np.asarray(Image.fromarray((rgb * 255).astype(np.uint8)).resize((T, T), Image.LANCZOS))
        bogen = np.concatenate([ansicht(ea), np.full((T, 8, 3), 255, np.uint8), ansicht(fr)], axis=1)
        Image.fromarray(bogen).save(os.path.join(a.aus, n + ".png"))
    with open(os.path.join(a.aus, "helligkeit.txt"), "w", encoding="utf-8") as f:
        f.write("textur EA frei diff deckung_EA deckung_frei\n")
        for n, le, lf, ae, af in zeilen:
            f.write("%-16s %6.1f %6.1f %+6.1f %5.3f %5.3f\n" % (n, le, lf, lf - le, ae, af))
    for n, le, lf, ae, af in zeilen:
        print("%-16s EA %6.1f frei %6.1f  %+6.1f   Deckung %.3f / %.3f" % (n, le, lf, lf - le, ae, af))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--download", default=bd.DOWNLOAD, required=bd.DOWNLOAD is None)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("bauen")
    s.add_argument("--stufe", type=int, default=4, choices=(1, 2, 4), help="edge factor over the EA size (4 = UHD)")
    s.add_argument("--nur", nargs="*")
    s.add_argument("--ziel", help="folder of the DDS files (e.g. Data/FreieStrassen/Art/Textures)")
    s.add_argument("--png", help="folder for PNG previews and werte.json")
    s.add_argument("--prozesse", type=int, default=1)
    sub.add_parser("liste")
    sub.add_parser("nachweis")
    s = sub.add_parser("pruefen")
    s.add_argument("--ordner", default=os.path.join(REPO, "Data", "FreieStrassen", "Art", "Textures"))
    s.add_argument("--stufe", type=int, default=4)
    s = sub.add_parser("manifest")
    s.add_argument("--paket", default=os.path.join(REPO, "Data", "FreieStrassen"))
    s.add_argument("--stufe", type=int, default=4, choices=(1, 2, 4), help="edge factor of the files in the package")
    s = sub.add_parser("vergleich")
    s.add_argument("--frei", required=True, help="PNG folder of a build (--png)")
    s.add_argument("--aus", required=True)
    s.add_argument("--generals", action="store_true")
    a = ap.parse_args()
    r = {"bauen": cmd_bauen, "liste": cmd_liste, "nachweis": cmd_nachweis, "pruefen": cmd_pruefen,
         "manifest": cmd_manifest, "vergleich": cmd_vergleich}[a.cmd](a)
    sys.exit(r or 0)


if __name__ == "__main__":
    main()
