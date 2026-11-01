#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour (Electronic Arts).
# Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Ways for strassen_frei.py: field roads, wheel tracks, footpaths, gravel and snow roads.

Geometry: every road piece gets the vehicle paths a driver would use. Each through road carries its wheel tracks
straight across; between two angularly adjacent arms a turning arc joins the two facing wheel tracks (a fillet tangent
to both); curve pieces carry concentric arcs. The tracks keep a fixed gauge and run without lateral wobble, so they meet
the tracks of the neighbouring piece at the joint. The way surface is the soft union of the arms (rounded inner corners).

Material: winter ways lay photographed wheel ruts (strips cut from a CC0 scan of tyre tracks in snow) along every track,
also through the arcs, over a snow cover; the mud of the photo can be swapped for gravel or wet asphalt. Summer ways
draw compacted ruts with a depression, raised borders and streaks along the track over a dirt, sand or gravel surface.
Everything lateral (streaks, rut profile) is a function of the offset to the track, so it continues across joints.
"""
import math
import os

import numpy as np

import boden_detail as bd
import boden_frei as bf
import strassen_frei as sf

PH, AC = sf.PH, sf.AC
R2 = math.sqrt(0.5)
SPUR_SCAN = (PH, "snow_04")		# tyre ruts in snow, three ruts running along the image height
# columns of the three ruts in SPUR_SCAN (fraction of the width), each including its snow shoulders
SPUR_STREIFEN = [(0.075, 0.315), (0.395, 0.635), (0.695, 0.975)]

# Way recipes. Units of s (RoadWidth).
#   gleise      lateral offsets of the tracks (two wheels, or one worn lane for footpaths)
#   kehle       distance from the corner of two facing tracks to the tangent points of the turning arc
#   flaeche     surface material (texture space) or None (tracks only)
#   kern, saum  half width of the opaque surface and width of its frayed edge
#   aussen      also the outer wheel of each turning vehicle (default True; tracks-only ways keep the inner arc only)
#   spur        ("foto", width, material replacing the photo's mud or None) or
#               ("erde", width, material, depth, border[, lightness of a worn footpath lane])
#   foto_hell   share of a light grey-brown mixed into the photo's mud
#   unterlage   (material, share): old road under snow showing through in patches along the driven corridor
#   rand_mat    (material, width, share): fringe of another material along the edge (grass)
#   streifen    strength of the streaks along the tracks
#   klumpen     strength of the clumps taken from the photographed ruts
#   spur_flecken (base, amplitude): visibility of the tracks along the road, low values let them fade in stretches
#   deckung     overall opacity (the terrain shows through broad surfaces)
G_WEG = 0.12
WEGE = {
    # winter: snow cover, photographed ruts
    "trdirtroad5": {"gleise": (-G_WEG, G_WEG), "kehle": 0.40, "flaeche": "schnee_weg", "kern": 0.295, "saum": 0.10,
                    "spur": ("foto", 0.23, None), "deckung": 1.0, "foto_hell": 0.25},
    "trgravelroads": {"gleise": (-G_WEG, G_WEG), "kehle": 0.40, "flaeche": "schnee_weg", "kern": 0.295, "saum": 0.10,
                      "spur": ("foto", 0.23, "kies_spur"), "deckung": 1.0},
    "trtwolane2s": {"gleise": (-G_WEG, G_WEG), "kehle": 0.40, "flaeche": "schnee_weg", "kern": 0.295, "saum": 0.10,
                    "spur": ("foto", 0.14, "asphalt_nass"), "deckung": 1.0,
                    "unterlage": ("asphalt_alt_schnee", 0.3), "foto_hell": 0.15, "spur_flecken": (0.75, 0.4)},
    "trtwolane4s": {"gleise": (-G_WEG, G_WEG), "kehle": 0.40, "flaeche": "schnee_weg", "kern": 0.295, "saum": 0.10,
                    "spur": ("foto", 0.14, "matsch_braun"), "deckung": 1.0,
                    "unterlage": ("asphalt_alt_braun_schnee", 0.3), "foto_hell": 0.15, "spur_flecken": (0.75, 0.4)},
    # summer field roads: surface with two compacted ruts
    "trdirtroad": {"gleise": (-G_WEG, G_WEG), "kehle": 0.40, "flaeche": "weg_hell", "kern": 0.295, "saum": 0.10,
                   "spur": ("erde", 0.10, "rinne_hell", 0.12, 0.06), "streifen": 0.08, "klumpen": 0.85, "deckung": 0.9, "spur_flecken": (0.42, 0.62)},
    "trdirtroad2": {"gleise": (-G_WEG, G_WEG), "kehle": 0.40, "flaeche": "weg_dunkel", "kern": 0.295, "saum": 0.10,
                    "spur": ("erde", 0.10, "rinne_dunkel", 0.14, 0.06), "streifen": 0.08, "klumpen": 0.85, "deckung": 0.9, "spur_flecken": (0.42, 0.62)},
    "trdirtroad3": {"gleise": (-G_WEG, G_WEG), "kehle": 0.40, "flaeche": "weg_orange", "kern": 0.295, "saum": 0.10,
                    "spur": ("erde", 0.10, "rinne_orange", 0.12, 0.06), "streifen": 0.08, "klumpen": 0.8, "deckung": 0.9, "spur_flecken": (0.42, 0.62)},
    "trdirtroad4": {"gleise": (-G_WEG, G_WEG), "kehle": 0.40, "flaeche": "weg_grau", "kern": 0.295, "saum": 0.10,
                    "spur": ("erde", 0.10, "rinne_grau", 0.14, 0.06), "streifen": 0.08, "klumpen": 0.9, "deckung": 0.9, "spur_flecken": (0.42, 0.62)},
    "trdirtroad6": {"gleise": (-G_WEG, G_WEG), "kehle": 0.40, "flaeche": "weg_sand", "kern": 0.295, "saum": 0.10,
                    "spur": ("erde", 0.10, "rinne_sand", 0.10, 0.06), "streifen": 0.08, "klumpen": 0.7, "deckung": 0.9, "spur_flecken": (0.42, 0.62)},
    "trsandroad": {"gleise": (-G_WEG, G_WEG), "kehle": 0.40, "flaeche": "weg_weiss", "kern": 0.295, "saum": 0.10,
                   "spur": ("erde", 0.10, "rinne_weiss", 0.08, 0.05), "streifen": 0.07, "klumpen": 0.5, "deckung": 0.95, "spur_flecken": (0.42, 0.62)},
    "trgravelroad": {"gleise": (-G_WEG, G_WEG), "kehle": 0.40, "flaeche": "kies_weg", "kern": 0.295, "saum": 0.10,
                     "spur": ("erde", 0.10, "kies_fest", 0.14, 0.06),
                     "streifen": 0.05, "klumpen": 0.7, "deckung": 0.92, "spur_flecken": (0.42, 0.62)},
    # wheel tracks only (no surface)
    "trdirtroad7": {"gleise": (-G_WEG, G_WEG), "kehle": 0.40, "flaeche": None, "aussen": False,
                    "spur": ("erde", 0.10, "spur_grau", 0.0, 0.0), "streifen": 0.08, "klumpen": 0.95},
    "trdirtroad8": {"gleise": (-G_WEG, G_WEG), "kehle": 0.40, "flaeche": None, "aussen": False,
                    "spur": ("erde", 0.10, "spur_tan", 0.0, 0.0), "streifen": 0.08, "klumpen": 0.6},
    "trdirtroad9": {"gleise": (-G_WEG, G_WEG), "kehle": 0.40, "flaeche": None, "aussen": False,
                    "spur": ("erde", 0.10, "spur_orange", 0.0, 0.0), "streifen": 0.08, "klumpen": 0.55},
    # footpaths: one worn lane in the middle, grassy edge
    "trdirtpath": {"gleise": (0.0,), "kehle": 0.22, "flaeche": "pfad_erde", "kern": 0.295, "saum": 0.10,
                   "spur": ("erde", 0.30, "pfad_lauf", 0.0, 0.0, 0.85), "rand_mat": ("gras_rand", 0.16, 0.7),
                   "streifen": 0.08, "deckung": 0.85},
    "trdirtpath2": {"gleise": (0.0,), "kehle": 0.22, "flaeche": "pfad_erde", "kern": 0.295, "saum": 0.10,
                    "spur": ("erde", 0.28, "pfad_lauf", 0.0, 0.0, 0.8), "rand_mat": ("gras_rand", 0.18, 0.8),
                    "streifen": 0.08, "deckung": 0.85},
    "trdirtpath3": {"gleise": (0.0,), "kehle": 0.22, "flaeche": "pfad_laub", "kern": 0.295, "saum": 0.10,
                    "spur": ("erde", 0.28, "pfad_lauf", 0.0, 0.0, 0.75), "rand_mat": ("gras_rand", 0.18, 0.75),
                    "streifen": 0.08, "deckung": 0.85},
}

# Materials of the ways (texture space), added to the material table of strassen_frei.
BELAEGE_WEGE = {
    "schnee_weg": {"scans": [(PH, "snow_02")], "ton": (230, 232, 236), "streu": 0.06, "flecken": 0.025, "glatt": 0.9},
    "kies_spur": {"scans": [(PH, "gravel_road"), (PH, "gravel_floor")], "ton": (140, 136, 130), "streu": 0.2, "flecken": 0.02,
                  "glatt": 0.9},
    "asphalt_nass": {"scans": [(PH, "cracked_concrete"), (PH, "road_damaged")], "ton": (135, 135, 133), "streu": 0.14,
                     "flecken": 0.03, "glatt": 0.92},
    "matsch_braun": {"scans": [(PH, "brown_mud_dry")], "ton": (122, 110, 92), "streu": 0.16, "flecken": 0.03, "glatt": 0.9},
    "weg_hell": {"scans": [(PH, "dirt_floor"), (PH, "dirt")], "ton": (225, 192, 136), "streu": 0.13, "flecken": 0.03, "glatt": 0.9},
    "weg_dunkel": {"scans": [(PH, "dirt_floor"), (PH, "brown_mud_dry")], "ton": (112, 100, 81), "streu": 0.15, "flecken": 0.03,
                   "glatt": 0.9},
    "weg_orange": {"scans": [(PH, "dirt_floor"), (PH, "red_dirt_mud_01")], "ton": (208, 164, 80), "streu": 0.14, "flecken": 0.03,
                   "glatt": 0.9},
    "weg_grau": {"scans": [(PH, "dirt_floor"), (PH, "gravelly_sand")], "ton": (140, 136, 117), "streu": 0.13, "flecken": 0.03,
                 "glatt": 0.9},
    "weg_sand": {"scans": [(PH, "sand_02"), (PH, "dirt_floor")], "ton": (237, 214, 170), "streu": 0.1, "flecken": 0.025, "glatt": 0.9},
    "weg_weiss": {"scans": [(PH, "sand_02")], "ton": (255, 243, 208), "streu": 0.08, "flecken": 0.02, "glatt": 0.9},
    "rinne_hell": {"scans": [(PH, "dirt_floor")], "ton": (189, 159, 112), "streu": 0.16, "flecken": 0.02, "glatt": 0.9},
    "rinne_dunkel": {"scans": [(PH, "dirt_floor")], "ton": (92, 81, 67), "streu": 0.17, "flecken": 0.02, "glatt": 0.9},
    "rinne_orange": {"scans": [(PH, "dirt_floor")], "ton": (174, 130, 63), "streu": 0.16, "flecken": 0.02, "glatt": 0.9},
    "rinne_grau": {"scans": [(PH, "dirt_floor"), (PH, "gravel_road")], "ton": (117, 112, 98), "streu": 0.16, "flecken": 0.02,
                   "glatt": 0.9},
    "rinne_sand": {"scans": [(PH, "dirt_floor")], "ton": (206, 182, 138), "streu": 0.12, "flecken": 0.02, "glatt": 0.9},
    "rinne_weiss": {"scans": [(PH, "sand_02")], "ton": (227, 212, 178), "streu": 0.1, "flecken": 0.02, "glatt": 0.9},
    "kies_weg": {"scans": [(PH, "gravel_road"), (PH, "gravel_stones")], "ton": (153, 149, 142), "streu": 0.32, "flecken": 0.03,
                 "glatt": 0.9},
    "spur_grau": {"scans": [(PH, "dirt_floor"), (PH, "gravel_road")], "ton": (164, 161, 146), "streu": 0.2, "flecken": 0.02,
                  "glatt": 0.9},
    "spur_tan": {"scans": [(PH, "dirt_floor")], "ton": (239, 205, 143), "streu": 0.18, "flecken": 0.02, "glatt": 0.9},
    "spur_orange": {"scans": [(PH, "dirt_floor")], "ton": (217, 170, 83), "streu": 0.18, "flecken": 0.02, "glatt": 0.9},
    "kies_fest": {"scans": [(PH, "gravel_floor")], "ton": (129, 125, 117), "streu": 0.14, "flecken": 0.02, "glatt": 0.9},
    "pfad_erde": {"scans": [(PH, "dirt"), (PH, "forest_ground_05")], "ton": (99, 92, 74), "streu": 0.18, "flecken": 0.03,
                  "glatt": 0.85},
    "pfad_laub": {"scans": [(PH, "forest_ground_05")], "ton": (81, 74, 58), "streu": 0.18, "flecken": 0.03, "glatt": 0.85},
    "pfad_lauf": {"scans": [(PH, "dirt_floor")], "ton": (148, 139, 114), "streu": 0.14, "flecken": 0.02, "glatt": 0.9},
}
sf.BELAEGE.update(BELAEGE_WEGE)


def verwendete_scans():
    """(use, scan) of every material the way recipes reference, the rut photo and the crack map."""
    genutzt = set()
    for rez in WEGE.values():
        if rez.get("flaeche"):
            genutzt.add(rez["flaeche"])
        if rez["spur"][2]:
            genutzt.add(rez["spur"][2])
        for k in ("rand_mat", "unterlage"):
            if rez.get(k):
                genutzt.add(rez[k][0])
    liste = [("Wege: " + n, sc) for n in sorted(genutzt) for sc in sf.BELAEGE[n]["scans"]]
    liste.append(("Wege: Reifenspuren im Schnee", SPUR_SCAN))
    if any(rez.get("unterlage") for rez in WEGE.values()):
        liste.append(("Wege: Risse", (PH, "cracked_concrete")))
    return liste


# -------------------------------------------------------------------------------------------------
# Geometry: tracks of a road piece
# -------------------------------------------------------------------------------------------------
def rot90(v):
    return (-v[1], v[0])


class Gerade:
    """Track along a straight line: origin o, unit direction e, parameter range [t0, t1]; along coordinate
    a = a0 + sig * (t - t_ref), lateral s = sig * (left offset)."""

    def __init__(self, o, e, t0=-math.inf, t1=math.inf, a0=0.0, t_ref=0.0, sig=1.0):
        self.o, self.e, self.t0, self.t1 = o, e, t0, t1
        self.a0, self.t_ref, self.sig = a0, t_ref, sig

    def werte(self, U, V):
        x, y = U - self.o[0], V - self.o[1]
        t = x * self.e[0] + y * self.e[1]
        s = self.e[0] * y - self.e[1] * x
        dist = np.where((t >= self.t0) & (t <= self.t1), np.abs(s), np.inf)
        a = self.a0 + self.sig * (np.clip(t, -1e6, 1e6) - self.t_ref)
        return dist, a, self.sig * s


class Bogen:
    """Track along a circular arc: centre c, radius r, start angle b0, signed sweep dw (None: full circle); along
    coordinate a = a0 + sig * r * theta (theta from the start), lateral s = left offset of the direction of rising a."""

    def __init__(self, c, r, b0=0.0, dw=None, a0=0.0, sig=1.0):
        self.c, self.r, self.b0, self.dw, self.a0, self.sig = c, r, b0, dw, a0, sig

    def werte(self, U, V):
        x, y = U - self.c[0], V - self.c[1]
        rho = np.hypot(x, y)
        ang = np.arctan2(y, x)
        richtung = 1.0 if (self.dw is None or self.dw >= 0) else -1.0
        rel = np.mod((ang - self.b0) * richtung, 2 * math.pi)
        # left of the direction of increasing theta: the centre for counter-clockwise travel
        s = (self.r - rho) * richtung * self.sig
        if self.dw is None:
            return np.abs(rho - self.r), self.a0 + self.sig * self.r * rel, s
        span = abs(self.dw)
        dist = np.where(rel <= span, np.abs(rho - self.r), np.inf)
        return dist, self.a0 + self.sig * self.r * np.minimum(rel, span), s


def _schnitt(p, d, q, e):
    """Intersection of the lines p + t d and q + u e: (t, u)."""
    det = d[0] * (-e[1]) - d[1] * (-e[0])
    rx, ry = q[0] - p[0], q[1] - p[1]
    t = (rx * (-e[1]) - ry * (-e[0])) / det
    u = (d[0] * ry - d[1] * rx) / det
    return t, u


def arme(strips):
    """Arms of a junction piece: (angle, unit direction, kind, index of the through line or None)."""
    out = []
    for k, (art, a1, a2) in enumerate(strips):
        d = (float(a2[0]), float(a2[1]))
        if art == "linie":
            out.append((math.atan2(d[1], d[0]), d, "durch", k))
            out.append((math.atan2(-d[1], -d[0]), (-d[0], -d[1]), "durch", k))
        elif art == "strahl":
            out.append((math.atan2(d[1], d[0]), d, "strahl", None))
    out.sort(key=lambda x: x[0])
    return out


# Interface (W3DRoadBuffer.cpp, offsetY and offsetH): distance from the junction centre (units of s) at which the
# adjoining road segment starts, per arm direction of a field. The segment runs from there outwards and is drawn over
# the field; beyond this point the field must stay empty, otherwise two sets of tracks overlap. The engine keeps the
# nominal arm angles of the field even when the real road arrives at another angle, so the tracks meet at the stump.
STUMPF = {
    "y": {(0.0, 1.0): 0.275, (-R2, -R2): 0.55, (R2, -R2): 0.55},
    "h": {(R2, -R2): 1.05},
}


def _stumpf(feld, d):
    for (x, y), S in STUMPF.get(feld, {}).items():
        if abs(x - d[0]) < 1e-6 and abs(y - d[1]) < 1e-6:
            return S
    return None


def _bis_rand(p, d, quad):
    """Distance from p along d to the edge of the convex quad (inf if p lies outside or d leaves it nowhere)."""
    best = math.inf
    n = len(quad)
    for k in range(n):
        a, b = quad[k], quad[(k + 1) % n]
        e = (b[0] - a[0], b[1] - a[1])
        det = d[0] * (-e[1]) - d[1] * (-e[0])
        if abs(det) < 1e-12:
            continue
        rx, ry = a[0] - p[0], a[1] - p[1]
        t = (rx * (-e[1]) - ry * (-e[0])) / det
        u = (d[0] * ry - d[1] * rx) / det
        if t > 1e-9 and -1e-9 <= u <= 1 + 1e-9:
            best = min(best, t)
    return best


def bahnen(feld, strips, gleise, kehle, quad=None, aussen=True):
    """Track primitives of one piece for the lanes `gleise` (lateral offsets). kehle: largest distance from the corner
    of two facing tracks to the tangent points of their turning arc; the arc ends a little inside the piece, so the
    track meets the neighbouring piece straight."""
    if feld == "gerade" or feld == "uebergang":
        return [Gerade((0.0, g), (1.0, 0.0)) for g in gleise]
    if feld.startswith("bogen"):
        (cx, cy), R = strips[0][1], strips[0][2]
        # lateral offset g (left positive) of the road; the arc turns left around (cx, cy)
        return [Bogen((cx, cy), R - g, b0=-math.pi / 2, dw=None, a0=0.0, sig=1.0) for g in gleise]
    A = arme(strips)
    n = len(A)
    out = []
    for (art, a1, a2) in strips:
        if art == "linie":
            d = (float(a2[0]), float(a2[1]))
            nrm = rot90(d)
            for g in gleise:
                out.append(Gerade((g * nrm[0], g * nrm[1]), d))
    g_max, g_min = max(gleise), min(gleise)
    strahl_start = {}		# (arm index, lane) -> (t at the tangent point, a there, sig)
    for i in range(n):
        j = (i + 1) % n
        wi, di, ki, li = A[i]
        wj, dj, kj, lj = A[j]
        gap = (wj - wi) % (2 * math.pi)
        if gap >= math.pi - 1e-6 or gap < 1e-6:
            continue
        ni, nj = rot90(di), rot90(dj)
        # facing lanes: the left one of arm i (towards j) and the right one of arm j (towards i)
        pi_ = (g_max * ni[0], g_max * ni[1])
        pj = (g_min * nj[0], g_min * nj[1])
        ti, tj = _schnitt(pi_, di, pj, dj)
        C = (pi_[0] + ti * di[0], pi_[1] + ti * di[1])
        tk = kehle
        if quad is not None:
            tk = min(tk, _bis_rand(C, di, quad) - 0.04, _bis_rand(C, dj, quad) - 0.04)
        # the arc ends a little before the stump, so the track runs straight into the adjoining segment
        for d_, S in ((di, _stumpf(feld, di)), (dj, _stumpf(feld, dj))):
            if S is not None:
                tk = min(tk, S - 0.05 - (C[0] * d_[0] + C[1] * d_[1]))
        tk = max(tk, 0.05)
        rf = tk * math.tan(gap / 2.0)
        bis = (di[0] + dj[0], di[1] + dj[1])
        bl = math.hypot(*bis)
        bis = (bis[0] / bl, bis[1] / bl)
        O = (C[0] + bis[0] * rf / math.sin(gap / 2.0), C[1] + bis[1] * rf / math.sin(gap / 2.0))
        Ti = (C[0] + tk * di[0], C[1] + tk * di[1])
        Tj = (C[0] + tk * dj[0], C[1] + tk * dj[1])
        # arc from Tj to Ti: the vehicle comes in along arm j and leaves along arm i
        bj = math.atan2(Tj[1] - O[1], Tj[0] - O[0])
        bi = math.atan2(Ti[1] - O[1], Ti[0] - O[0])
        dw = (bi - bj + math.pi) % (2 * math.pi) - math.pi
        # along coordinate continuous with a through line at Tj (its a = t along its own direction)
        if kj == "durch":
            d_line = strips[lj][2]
            t_line = Tj[0] * d_line[0] + Tj[1] * d_line[1]
            # travel along the arc enters along -dj; the line's a rises along d_line
            sig = 1.0 if (d_line[0] * -dj[0] + d_line[1] * -dj[1]) > 0 else -1.0
            a_j = t_line
        else:
            sig, a_j = 1.0, 0.0
        out.append(Bogen(O, rf, b0=bj, dw=dw, a0=a_j, sig=sig))
        if aussen and len(gleise) > 1:
            # outer wheel of the same turning vehicle: concentric arc on the far lanes, it passes near the middle
            out.append(Bogen(O, rf + (g_max - g_min), b0=bj, dw=dw, a0=a_j + 0.5, sig=sig))
        a_i = a_j + sig * rf * abs(dw)
        t_i = Ti[0] * di[0] + Ti[1] * di[1] - (g_max * ni[0] * di[0] + g_max * ni[1] * di[1])
        if ki == "strahl":
            strahl_start[(i, g_max)] = (t_i, a_i, sig)
        if kj == "strahl":
            t_j = Tj[0] * dj[0] + Tj[1] * dj[1]
            strahl_start[(j, g_min)] = (t_j, a_j, -sig)
    for i, (wi, di, ki, li) in enumerate(A):
        if ki != "strahl":
            continue
        ni = rot90(di)
        for g in gleise:
            o = (g * ni[0], g * ni[1])
            if (i, g) in strahl_start:
                t0, a0, sig = strahl_start[(i, g)]
                out.append(Gerade(o, di, t0=t0, a0=a0, t_ref=t0, sig=sig))
            else:
                out.append(Gerade(o, di, t0=0.0))
    return out


def flaeche_abstand(strips, U, V, weich=0.035):
    """Soft union of the arms: distance to the nearest centre line, inner corners rounded."""
    r = np.stack([sf.strip_werte(s[0], U, V, s[1], s[2])[2] for s in strips])
    if len(strips) == 1:
        return r[0]
    m = r.min(axis=0)
    return m - weich * np.log(np.exp(-(r - m) / weich).sum(axis=0))


# -------------------------------------------------------------------------------------------------
# Materials of the tracks
# -------------------------------------------------------------------------------------------------
class Wegkontext:
    def __init__(self, ctx, rez):
        self.ctx, self.rez = ctx, rez
        self.M = ctx.M
        self.px = 4.0 / ctx.M
        rng = np.random.default_rng(ctx.saat + 301)
        # lateral streak profile: smooth 1-D noise over the offset (units of s), shared by all tracks
        self._quer = bd.gauss_periodisch(rng.standard_normal((1, 4096)).repeat(2, 0), 3.0)[0]
        self._quer = (self._quer - self._quer.mean()) / max(self._quer.std(), 1e-9)
        self._quer_fein = bd.gauss_periodisch(rng.standard_normal((1, 4096)).repeat(2, 0), 1.2)[0]
        self._quer_fein = (self._quer_fein - self._quer_fein.mean()) / max(self._quer_fein.std(), 1e-9)
        self._foto = None
        M = ctx.M
        self.rand_mittel = bf.rauschen_periodisch(M, 24, ctx.saat + 401)		# edge bays of about 0.17 s
        self.spur_flecken = bf.rauschen_periodisch(M, 12, ctx.saat + 402)	# stretches where a track fades (about 0.35 s)
        self.spur_breite = bf.rauschen_periodisch(M, 18, ctx.saat + 403)	# width of a track along the road

    def quer(self, s, fein=False):
        q = self._quer_fein if fein else self._quer
        i = np.mod(np.round(s * 2048.0).astype(np.int64), q.size)
        return q[i]

    def foto_streifen(self, breite):
        """Rut strips cut from SPUR_SCAN at the target resolution: list of (rgb Hy x Wx x 3, snow 0..1), along period."""
        if self._foto is not None and self._foto[0] == breite:
            return self._foto[1]
        ctx = self.ctx
        d = os.path.join(ctx.download, *SPUR_SCAN)
        roh = bd.lies(os.path.join(d, "farbe.png"))[..., :3]
        H0, W0 = roh.shape[:2]
        Wx = max(8, int(round(breite * self.M / 4.0)))
        out = []
        for lo, hi in SPUR_STREIFEN:
            c0, c1 = int(lo * W0), int(hi * W0)
            teil = roh[:, c0:c1]
            # along: whole strips per 4 s, so the straight band tiles in u
            hy_iso = Wx * H0 / float(c1 - c0)
            k = max(1, int(round(4.0 / (breite * H0 / float(c1 - c0)))))
            Hy = max(8, int(round(self.M / k)))
            t = np.stack([bf.lege_resize(teil[..., c], Wx, Hy) for c in range(3)], axis=-1)
            t = np.clip(t, 0, 1)
            lum = bf.lum601(t)
            schnee = np.clip((lum - 0.40) / 0.28, 0.0, 1.0)
            out.append((t, schnee, 4.0 / k, hy_iso))
        self._foto = (breite, out)
        return out


def _lies_streifen(t, a_rel, s_rel):
    """Bilinear lookup in a strip (rows periodic along a_rel 0..1, columns clamped along s_rel 0..1)."""
    Hy, Wx = t.shape[:2]
    y = np.mod(a_rel, 1.0) * Hy
    x = np.clip(s_rel, 0.0, 1.0) * (Wx - 1.001)
    y0 = np.floor(y).astype(np.int64)
    x0 = np.floor(x).astype(np.int64)
    fy, fx = y - y0, x - x0
    y0 %= Hy
    y1 = (y0 + 1) % Hy
    x1 = np.minimum(x0 + 1, Wx - 1)
    if t.ndim == 3:
        fy, fx = fy[..., None], fx[..., None]
    return (t[y0, x0] * (1 - fx) + t[y0, x1] * fx) * (1 - fy) + (t[y1, x0] * (1 - fx) + t[y1, x1] * fx) * fy


def male_weg(ctx, feld, U, V, strips, sl, quad=None):
    """RGBA of one field of a way texture."""
    rez = WEGE[ctx.name]
    wk = ctx._wk
    n1, n2 = ctx.rausch1[sl], ctx.rausch2[sl]
    shape = U.shape
    prims = bahnen(feld, strips, rez["gleise"], rez["kehle"], quad, rez.get("aussen", True))
    werte = [p.werte(U, V) for p in prims]
    flaeche = rez.get("flaeche")
    art = rez["spur"][0]
    if flaeche:
        rgb = ctx.belag(flaeche)[sl].copy()
        r = flaeche_abstand(strips, U, V)
        kern, saum = rez["kern"], rez["saum"]
        # frayed edge: a soft fringe with noise, holes in the outer part
        nm = wk.rand_mittel[sl]
        rand = (kern + saum * 0.5 + saum * 0.45 * np.clip(n1, -2, 2) * 0.5 + 0.032 * np.clip(nm, -2.5, 2.5)
                + 0.022 * np.clip(n2, -2, 2))
        alpha = glatt(rand - r, saum * 0.45)
        zone = np.clip((r - (kern - 0.02)) / max(saum, 1e-6), 0, 1)
        alpha *= 1.0 - 0.85 * zone * glatt(n2 + 0.1 + 0.3 * nm, 0.5)

        if rez.get("rand_mat"):
            mat, breite, anteil = rez["rand_mat"]
            w = glatt(r - (kern + saum * 0.5 - breite) + 0.04 * n1 + 0.02 * n2, 0.04) * anteil
            rgb = rgb * (1 - w[..., None]) + ctx.belag(mat)[sl] * w[..., None]
    else:
        rgb = None
        alpha = np.zeros(shape)

    if art == "foto":
        _, breite, mat = rez["spur"]
        streifen = wk.foto_streifen(breite)
        if rez.get("unterlage"):
            # old road under snow: the cracked asphalt shows through in patches along the driven corridor
            mat_u, anteil = rez["unterlage"]
            D = np.min(np.stack([w_[0] for w_ in werte]), axis=0)
            gang = glatt(breite * 0.5 + 0.03 - D + 0.03 * n1, 0.08)
            flecke = glatt(0.6 * n1 + 0.5 * n2 + (anteil - 0.5) * 2.2, 0.7)
            w = (gang * flecke * 0.9)[..., None]
            rgb = rgb * (1 - w) + ctx.belag(mat_u)[sl] * w
            rgb = rgb * (1.0 - 0.7 * ctx.riss(0.9)[sl] * w[..., 0])[..., None]
        for k, (dist, a, s) in enumerate(werte):
            t, schnee, laenge, _ = streifen[k % len(streifen)]
            s_eff = np.where(np.isfinite(dist), s, 1e3)
            s_eff = s_eff / (1.0 + 0.18 * np.clip(wk.spur_breite[sl], -2, 2))
            s_rel = s_eff / breite + 0.5
            a_rel = a / laenge
            w = np.clip((0.5 - np.abs(s_eff) / breite) / 0.16, 0.0, 1.0)
            if not w.any():
                continue
            farbe_k = _lies_streifen(t, a_rel, s_rel)
            grau = bf.lum601(farbe_k)[..., None]
            farbe_k = farbe_k * 0.7 + grau * 0.3
            farbe_k = farbe_k * (1.0 - rez.get("foto_hell", 0.0)) + rez.get("foto_hell", 0.0) * np.array([0.62, 0.59, 0.54])
            sn = _lies_streifen(schnee, a_rel, s_rel)
            if mat:
                # swap the mud of the photo for another material, keep its light and shade
                lum = bf.lum601(farbe_k)
                schatten = np.clip(lum / 0.3, 0.55, 1.35)[..., None]
                farbe_k = farbe_k * sn[..., None] + ctx.belag(mat)[sl] * schatten * (1 - sn[..., None])
            # the photo's snow blends into the snow cover, the mud covers it
            deck = w * (0.35 + 0.65 * (1.0 - sn))
            if rez.get("spur_flecken"):
                b0, amp = rez["spur_flecken"]
                deck = deck * np.clip(b0 + amp * wk.spur_flecken[sl], 0.2, 1.0)
            rgb = rgb * (1 - deck[..., None]) + farbe_k * deck[..., None]
        # compacted, greyer snow along the ruts (the middle ridge between them stays brighter)
        D = np.min(np.stack([w_[0] for w_ in werte]), axis=0)
        halo = glatt(breite * 0.5 + 0.035 - D + 0.02 * n1, 0.07) * glatt(D - breite * 0.25, 0.08)
        rgb = rgb * (1.0 - 0.08 * halo)[..., None]
    else:
        breite, mat, tiefe, wulst = rez["spur"][1:5]
        hell = rez["spur"][5] if len(rez["spur"]) > 5 else 0.0
        stx = rez.get("streifen", 0.0)
        rinne = np.zeros(shape)
        bord = np.zeros(shape)
        streif = np.zeros(shape)
        klumpen = rez.get("klumpen", 0.18)
        detail = np.zeros(shape)
        streifen_foto = wk.foto_streifen(0.23)
        linien = np.zeros(shape)
        for k, (dist, a, s) in enumerate(werte):
            x = dist / (breite * 0.5 * (1.0 + 0.2 * np.clip(wk.spur_breite[sl], -2, 2)))
            # rut: flat bottom, soft wall; border: raised lip just outside the wall
            m = glatt(1.0 - x + 0.12 * np.clip(n2, -2, 2), 0.35)
            b = np.exp(-((x - 1.25) / 0.3) ** 2)
            q = wk.quer(s + 0.37 * k) * 0.6 + wk.quer(s * 2.3, fein=True) * 0.4
            t_f, _, laenge_f, _ = streifen_foto[k % len(streifen_foto)]
            s_eff = np.where(np.isfinite(dist), s, 1e3)
            lum = bf.lum601(_lies_streifen(t_f, a / laenge_f * (0.23 / breite) * 0.5, 0.5 + s_eff / breite * 0.55))
            det = np.clip((lum - 0.45) / 0.2, -2.0, 2.0)
            # tyre mark: one dark core along the rut, faint walls (two strong walls would read as four tracks)
            xs = np.clip(s_eff / (breite * 0.5), -3.0, 3.0)
            lin = np.maximum(0.85 * np.exp(-(xs / 0.55) ** 2), 0.3 * np.exp(-((np.abs(xs) - 0.85) / 0.15) ** 2))
            linien = np.maximum(linien, lin * glatt(1.4 - x, 0.3))
            detail = np.where(m > rinne, det, detail)
            streif = np.where(m > rinne, q, streif)
            rinne = np.maximum(rinne, m)
            bord = np.maximum(bord, b)
        bord *= 1.0 - rinne
        b0, amp = rez.get("spur_flecken", (0.88, 0.12))
        sicht = np.clip(b0 + amp * np.clip(wk.spur_flecken[sl], -2.5, 2.5), 0.2, 1.0)
        if feld in ("tee", "kreuz", "y", "h"):
            # junctions are driven over most: tracks stay visible there
            sicht = 0.55 + 0.45 * sicht
        if flaeche and stx:
            # longitudinal streaks of the compacted surface, following the nearest track (lateral, joint-safe)
            D = np.stack([w_[0] for w_ in werte])
            S = np.stack([w_[2] for w_ in werte])
            k_n = np.argmin(D, axis=0)
            s_n = np.take_along_axis(S, k_n[None], 0)[0]
            nah = glatt(0.30 - np.take_along_axis(D, k_n[None], 0)[0], 0.15)
            q = wk.quer(s_n * 1.7 + 0.11 * k_n) * 0.5 + wk.quer(s_n * 4.1, fein=True) * 0.5
            rgb = rgb * (1.0 + stx * 0.8 * q * nah)[..., None]
        fak = 1.0 + stx * streif + klumpen * 0.5 * np.where(detail < 0, detail * 1.25, detail * 0.6)
        rinne_f = ctx.belag(mat)[sl] * np.clip(fak, 0.15, 2.0)[..., None]
        if rgb is None:
            # tracks only: the rut is the visible surface, darker towards its soft edge
            rgb = rinne_f * (1.0 - 0.18 * (1.0 - rinne) - 0.22 * linien * np.clip(0.75 - 0.25 * detail, 0.3, 1.3))[..., None]
            alpha = np.clip(rinne * sicht, 0, 1)
        else:
            if hell:
                # footpath: a lighter worn lane
                w = (rinne * hell * sicht)[..., None]
                rgb = rgb * (1 - w) + rinne_f * w
            else:
                w = (rinne * 0.6 * sicht)[..., None]
                rgb = rgb * (1 - w) + rinne_f * (1.0 - tiefe * 0.4) * w
                dunkel = tiefe * 2.2 * linien * sicht * np.clip(0.75 - 0.25 * detail, 0.3, 1.3)
                rgb = rgb * (1.0 - np.clip(dunkel, 0, 0.6))[..., None]
                # shading of the relief: dark wall at the rut, light border lip
                rgb = rgb * (1.0 + wulst * bord * sicht - tiefe * 0.35 * glatt(rinne - 0.15, 0.2) * glatt(0.9 - rinne, 0.2))[..., None]
    for (dx, dy), S in STUMPF.get(feld, {}).items():
        # beyond the stump the adjoining segment draws; a short overlap hides the joint
        a_arm = U * dx + V * dy
        quer = np.abs(dx * V - dy * U)
        jenseits = glatt(a_arm - S - 0.03, 0.05) * glatt(0.6 - quer, 0.1)
        alpha = alpha * (1.0 - jenseits)
    if rez.get("deckung"):
        alpha = alpha * rez["deckung"]
    if rgb is None:
        rgb = np.broadcast_to(sf.farbe(sf.BELAEGE[rez["spur"][2]]["ton"]), shape + (3,)).copy()
    return np.clip(rgb, 0, 1), np.clip(alpha, 0, 1)


def glatt(x, weich):
    return np.clip(x / max(weich, 1e-6) + 0.5, 0.0, 1.0)


def weg_bild(name, M, download):
    """RGBA float image (M x M) of a way texture (same layout as strassen_frei.strasse_bild)."""
    ctx = sf.Kontext(name, M, download)
    rez = WEGE[name]
    ctx._wk = Wegkontext(ctx, rez)
    h = ctx.wit / 2.0
    out_rgb = np.zeros((M, M, 3))
    out_a = np.zeros((M, M))
    jj = (np.arange(M) + 0.5) / M
    V = (sf.GERADE_V0 / 512.0 - jj) * 4.0
    zeilen = np.where(np.abs(V) <= h + 0.1)[0]
    sl = (slice(zeilen[0], zeilen[-1] + 1), slice(0, M))
    U2, V2 = np.meshgrid(jj * 4.0, V[sl[0]])
    rgb, a = male_weg(ctx, "gerade", U2, V2, [("linie", (0.0, 0.0), (1.0, 0.0))], sl)
    out_rgb[sl] = rgb
    out_a[sl] = a
    belegt = np.zeros((M, M), dtype=bool)
    belegt[sl] = True
    rand_px = max(3, M // 170)
    for fname, ((u0, v0), quad, strips, fade) in sf.felder(ctx.wit).items():
        u0f, v0f = u0 / 512.0, v0 / 512.0
        poly = [((u0f + U / 4.0) * M, (v0f - V_ / 4.0) * M) for U, V_ in quad]
        maske = sf.polygon_maske(M, poly, rand_px)
        ys, xs = np.where(maske)
        sl = (slice(ys.min(), ys.max() + 1), slice(xs.min(), xs.max() + 1))
        Ug, Vg = np.meshgrid((jj[sl[1]] - u0f) * 4.0, (v0f - jj[sl[0]]) * 4.0)
        rgb, a = male_weg(ctx, fname, Ug, Vg, strips, sl, quad)
        if fade:
            halb, weich = fade[0] + 0.03, fade[1] + 0.05
            a = a * np.clip((halb - np.abs(Ug)) / weich + 0.5, 0, 1)
        m = maske[sl] & (a >= out_a[sl])
        if belegt[sl].any():
            m &= ~belegt[sl] | (a > out_a[sl] + 0.02)
        out_rgb[sl] = np.where(m[..., None], rgb, out_rgb[sl])
        out_a[sl] = np.where(m, a, out_a[sl])
        belegt[sl] |= maske[sl]
    # colour under zero alpha: the surface (or rut) colour, so mip levels keep the colour at the edges
    leer = out_a < 1.0 / 255.0
    if rez.get("flaeche"):
        out_rgb[leer] = ctx.belag(rez["flaeche"])[leer]
    else:
        out_rgb[leer] = ctx.belag(rez["spur"][2])[leer]
    return out_rgb, out_a
