#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Bildpruefung fuer Render-Tests.

Prueft jedes Bild eines Ordners fuer sich auf typische Darstellungsfehler -- ohne Referenzbild.
Das findet Fehler, die schon in einer Referenz stecken, und Fehler, fuer die es noch keine gibt.

Pruefungen (Kennung in Klammern):
  schwarz   grosse schwarze Flaeche in der Szene mit harter Kante (fehlender Gelaende-Nachschub,
            TODO Nr. 31). Der verhuellte Bereich ausserhalb der Karte ist ebenfalls schwarz, laeuft
            aber mit weichem Verlauf aus -- er wird an der Kante erkannt und nicht gemeldet.
  naht      scharfe senkrechte oder waagrechte Spruenge ueber grosse Teile des Bildes, einzeln sehr
            lang oder in festem Abstand wiederholt (Texturkoordinaten, Halbpixel-Versatz; die
            Striche im Hauptmenue lagen alle 10 Pixel)
  magenta   Flaeche in der Ersatzfarbe fuer fehlende Texturen
  schach    kleinteiliges Zweifarben-Schachbrett (Ersatztextur)
  leer      einfarbiges oder fast leeres Bild (Zeichnen abgebrochen, falscher Renderzustand)
  sprung    ein Bild weicht stark von beiden Nachbarn ab, die einander aehneln (Flackern,
            verlorene Objekte) -- nur ueber die Bildfolge eines Ordners, erst ab drei Bildern

Nicht Szene ist die 2D-Leiste unten -- nur die Leiste selbst, nicht die ganze Bildbreite: links und
rechts daneben ist 3D-Szene und wird mitgeprueft. Die Leiste ist so breit wie ein 4:3-Bild der
Bildhoehe und steht mittig (1280x720: x 160 bis 1117, 1024x768: volle Breite); die Vorgabe sind zwei
Rechtecke, der Radarteil links mit dem hoeheren Aufsatz und der Rest (DEFAULT_HUD). --hud X,Y,B,H
ersetzt die Vorgabe, mehrfach angebbar, in Pixeln oder Prozent des Bildes wie --ignore in
compare_shots.py. In der Leiste wird nur auf Magenta, Schachbrett und leeres Bild geprueft.

Jeder Befund nennt den Bildausschnitt (x, y, Breite, Hoehe in Pixeln). Mit --crops ORDNER wird er
zusaetzlich als vergroesserter Ausschnitt gespeichert (nie in den gepruften Ordner schreiben).

Aufruf:
    python tools/render/inspect_shots.py render-out/cam-zoom-g-x86
    python tools/render/inspect_shots.py ordner1 ordner2 --json
    python tools/render/inspect_shots.py bild.bmp --crops C:/tmp/ausschnitte
    python tools/render/inspect_shots.py ordner --only schwarz,naht --stats
    python tools/render/inspect_shots.py ordner --hud 12%,68%,76%,32%       # eigene Leistenmaske
    python tools/render/inspect_shots.py --selftest

Rueckgabewert: 0 = keine Befunde, 1 = mindestens ein Befund, 2 = Fehler (Datei nicht lesbar u. a.).

Nur Standardbibliothek; BMP-Lesen aus compare_shots.py. Gerechnet wird auf einem Raster von 8x8
Pixeln, damit ein 1280x720-Bild in rund einer Sekunde durch ist.

Eichung (2026-09-18, Generals x86, 1280x720; mit der Leistenmaske wiederholt): render-out/cam-zoom-g-x86
Bilder 720 bis 960 melden "schwarz", die uebrigen Bilder dieser Fahrt, cam-quer-g-x86 (schwarze Ecken
ausserhalb der Karte) und alle ref-*-Ordner melden nichts. Neu dazu, weil die Ecken neben der Leiste
jetzt Szene sind: cam-rundum-g-x86 Bild 360 (schwarzes Dreieck unten rechts, fehlendes nahes Gelaende
vor der Korrektur zu TODO 31 (a); t31-rundum zeigt dort Gelaende). Nachtkarten koennen eigene
Schwellen brauchen (--black).
"""
import argparse
import glob
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_shots  # noqa: E402

CHECKS = ("schwarz", "naht", "magenta", "schach", "leer", "sprung")

BLOCK = 8                   # Rasterweite in Pixeln
# 2D-Leiste unten (2026-09-18 gemessen an cam-quer-g-x86, cam-rand-g-x86, cam-rundum-zh-x86 und
# cam-zoom-g-x86-4zu3: Pixel, die ueber alle 30 Bilder einer Fahrt gleich bleiben). Die Leiste ist
# ein 4:3-Bild der Bildhoehe, mittig: 1280x720 x 160 bis 1117, 1024x768 x 0 bis 1023. Oberkante
# Radarteil mit Aufsatz 494 von 720 (68,6 %) bzw. 528 von 768 (68,8 %), bis 22,1 % der Leistenbreite;
# rechts davon hoechstens 519 von 720 (72,1 %) bzw. 553 von 768 (72,0 %), die Knoepfe oben rechts.
# Je Rechteck: x und Breite in Prozent der Leistenbreite ab ihrem linken Rand, y und Hoehe in Prozent
# der Bildhoehe; je rund einen halben Prozentpunkt Rand, damit nichts von der Leiste in die Szene ragt.
DEFAULT_HUD = ((-0.5, 68.0, 23.0, 32.0),    # Radar mit Aufsatz
               (22.5, 71.5, 78.0, 28.5))    # Mitte und rechter Teil bis zu den Knoepfen
HUD_ASPECT = 4.0 / 3.0      # Seitenverhaeltnis der Leiste
BLACK_LEVEL = 12            # Pixel mit max(R,G,B) <= BLACK_LEVEL gelten als schwarz
BLACK_BLOCK_SHARE = 0.90    # Block ist schwarz, wenn so viele seiner Pixel schwarz sind
BLACK_MIN_AREA = 0.01       # Mindestgroesse einer schwarzen Flaeche (Anteil der Szene)
DIM_LEVEL = 40              # "gedaempft": ueber BLACK_LEVEL, hoechstens DIM_LEVEL (max-Kanal)
SOFT_EDGE_MAX = 0.20        # Anteil gedaempfter Pixel am Rand (zwei Blockreihen breit), bis zu dem
                            # die Kante als hart gilt. Geeicht: fehlendes Gelaende 0,02 bis 0,13 je
                            # Bild (mind. eine Flaeche), Bereich ausserhalb der Karte ab 0,25
SEAM_JUMP = 28              # Helligkeitssprung zwischen zwei Nachbarpixeln, der als Sprung zaehlt
SEAM_SINGLE = 0.85          # eine einzelne Naht muss so viel der Szenenhoehe/-breite abdecken
SEAM_REPEAT = 0.30          # wiederholte Naehte: Abdeckung je Linie
SEAM_REPEAT_MIN = 4         # ... und mindestens so viele Linien in festem Abstand
MAGENTA_MIN_BLOCKS = 4      # zusammenhaengende Magenta-Bloecke fuer einen Befund
CHECKER_MIN_BLOCKS = 4
EMPTY_SHARE = 0.97          # Anteil der Bloecke mit fast derselben Farbe fuer "leer"
JUMP_RATIO = 3.0            # Abweichung zu den Nachbarn / Abweichung der Nachbarn untereinander
JUMP_MIN = 20.0             # ... und mindestens diese mittlere Abweichung (0..255)


# --- Vorverarbeitung --------------------------------------------------------------------------------
def hud_boxes(hud, w, h):
    """Rechtecke der 2D-Leiste in Pixeln, (x0, y0, x1, y1) mit x1/y1 exklusiv.
    hud: None fuer die Vorgabe (DEFAULT_HUD, an der Leistenbreite ausgerichtet) oder eine Liste von
    compare_shots.Rect (Pixel oder Prozent des Bildes)."""
    boxes = []
    if hud is None:
        bar_w = min(w, h * HUD_ASPECT)
        left = (w - bar_w) / 2.0
        for x, y, bw_, bh_ in DEFAULT_HUD:
            x0 = int(left + x / 100.0 * bar_w)
            x1 = int(round(left + (x + bw_) / 100.0 * bar_w + 0.4999))
            y0 = int(y / 100.0 * h)
            y1 = int(round((y + bh_) / 100.0 * h + 0.4999))
            x0, y0, x1, y1 = max(0, x0), max(0, y0), min(w, x1), min(h, y1)
            if x1 > x0 and y1 > y0:
                boxes.append((x0, y0, x1, y1))
    else:
        for r in hud:
            box = r.resolve(w, h)
            if box is not None:
                boxes.append(box)
    return boxes


def _free_runs(cuts, n):
    """Abschnitte (a, b) von 0..n, die von keinem der Intervalle in cuts bedeckt sind."""
    out = []
    pos = 0
    for a, b in sorted(cuts):
        if a > pos:
            out.append((pos, a))
        pos = max(pos, b)
    if pos < n:
        out.append((pos, n))
    return out


class Prepared:
    """Helligkeit (max-Kanal und Luma) je Pixel, Blockstatistik und Maske der 2D-Leiste."""

    def __init__(self, img, hud, black_level):
        self.img = img
        w, h = img.width, img.height
        self.w, self.h = w, h
        self.bw, self.bh = w // BLOCK, h // BLOCK
        # 2D-Leiste: pixelgenau fuer die Nahtpruefung, blockweise fuer die uebrigen. Ein Block gehoert
        # zur Leiste, sobald er sie beruehrt -- sonst zaehlte ein halb bedeckter Block mit dunklem
        # Leistenrand als Szene.
        self.hud = hud_boxes(hud, w, h)
        self.bmask = [[False] * self.bw for _ in range(self.bh)]
        for x0, y0, x1, y1 in self.hud:
            for by in range(y0 // BLOCK, min(self.bh, (y1 - 1) // BLOCK + 1)):
                for bx in range(x0 // BLOCK, min(self.bw, (x1 - 1) // BLOCK + 1)):
                    self.bmask[by][bx] = True
        self.scene_blocks = [(bx, by) for by in range(self.bh) for bx in range(self.bw)
                             if not self.bmask[by][bx]]
        black_table = bytes(1 if v <= black_level else 0 for v in range(256))
        dim_table = bytes(1 if black_level < v <= DIM_LEVEL else 0 for v in range(256))
        self.mx = []        # je Zeile: bytes mit max(R,G,B)
        self.luma = []      # je Zeile: bytes mit Luma
        bgr = img.bgr
        for y in range(h):
            s = y * w * 3
            b, g, r = bgr[s:s + w * 3:3], bgr[s + 1:s + w * 3:3], bgr[s + 2:s + w * 3:3]
            self.mx.append(bytes(map(max, b, g, r)))
            self.luma.append(bytes(map(_luma, r, g, b)))
        # Blockstatistik: Summe max-Kanal, Anzahl schwarzer und gedaempfter Pixel
        bw, bh = self.bw, self.bh
        self.bsum = [[0] * bw for _ in range(bh)]
        self.bblack = [[0] * bw for _ in range(bh)]
        self.bdim = [[0] * bw for _ in range(bh)]
        for y in range(bh * BLOCK):
            by = y // BLOCK
            row = self.mx[y]
            mask = row.translate(black_table)
            dmask = row.translate(dim_table)
            srow, krow, drow = self.bsum[by], self.bblack[by], self.bdim[by]
            for bx in range(bw):
                a = bx * BLOCK
                srow[bx] += sum(row[a:a + BLOCK])
                krow[bx] += sum(mask[a:a + BLOCK])
                drow[bx] += sum(dmask[a:a + BLOCK])

    def block_pixels(self):
        return BLOCK * BLOCK

    def row_runs(self, y):
        """Szenenabschnitte (x0, x1) der Zeile y, x1 exklusiv."""
        return _free_runs([(x0, x1) for x0, y0, x1, y1 in self.hud if y0 <= y < y1], self.w)

    def col_runs(self, x):
        """Szenenabschnitte (y0, y1) der Spalte x, y1 exklusiv."""
        return _free_runs([(y0, y1) for x0, y0, x1, y1 in self.hud if x0 <= x < x1], self.h)


def _luma(r, g, b):
    return (r * 77 + g * 150 + b * 29) >> 8


def rect_of_blocks(bx0, by0, bx1, by1):
    """Blockrechteck (inklusive) -> Pixelrechteck (x, y, w, h)."""
    return (bx0 * BLOCK, by0 * BLOCK, (bx1 - bx0 + 1) * BLOCK, (by1 - by0 + 1) * BLOCK)


def components(cells, bw, bh):
    """4-zusammenhaengende Komponenten einer Menge von (bx, by)."""
    seen = set()
    out = []
    for start in cells:
        if start in seen:
            continue
        stack = [start]
        seen.add(start)
        comp = []
        while stack:
            c = stack.pop()
            comp.append(c)
            x, y = c
            for n in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if n in cells and n not in seen:
                    seen.add(n)
                    stack.append(n)
        out.append(comp)
    return out


# --- Pruefungen -------------------------------------------------------------------------------------
def check_black(p, stats):
    npix = p.block_pixels()
    black = set()
    for bx, by in p.scene_blocks:              # Bloecke der Leiste sind nie schwarze Szene
        if p.bblack[by][bx] >= BLACK_BLOCK_SHARE * npix:
            black.add((bx, by))
    scene_blocks = len(p.scene_blocks) or 1
    findings = []
    for comp in components(black, p.bw, p.bh):
        area = len(comp) / scene_blocks
        if area < BLACK_MIN_AREA:
            continue
        # Rand: zwei Blockreihen um die Flaeche. Ausserhalb der Karte laeuft das Bild mit einem
        # Verlauf ins Schwarze aus -- dort ist der Rand voller gedaempfter Pixel. Fehlt Gelaende,
        # grenzt Schwarz hart an Gras, Strasse oder Haus; gedaempfte Pixel sind selten.
        # (Silhouetten vor dem Kartenrand sind ebenfalls hart; der Verlauf daneben ueberwiegt.)
        # Bloecke der Leiste zaehlen nicht zum Rand: ihre Kante ist immer hart.
        cs = set(comp)
        ring = set()
        frontier = cs
        for _ in range(2):
            nxt = set()
            for x, y in frontier:
                for n in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                    if (0 <= n[0] < p.bw and 0 <= n[1] < p.bh and not p.bmask[n[1]][n[0]]
                            and n not in cs and n not in ring):
                        nxt.add(n)
            ring |= nxt
            frontier = nxt
        dim = nonblack = 0
        for x, y in ring:
            dim += p.bdim[y][x]
            nonblack += npix - p.bblack[y][x]
        soft = dim / nonblack if nonblack else 1.0
        xs = [c[0] for c in comp]
        ys = [c[1] for c in comp]
        rect = rect_of_blocks(min(xs), min(ys), max(xs), max(ys))
        info = {"flaeche": round(area, 4), "weicher_rand": round(soft, 3), "ausschnitt": rect}
        if stats is not None:
            stats.append(("schwarz", info))
        if soft <= SOFT_EDGE_MAX:
            findings.append({
                "pruefung": "schwarz",
                "text": f"schwarze Flaeche im Bild, {area * 100:.1f} % der Szene, harte Kante "
                        f"({soft * 100:.0f} % gedaempfte Randpixel)",
                "ausschnitt": list(rect),
                "werte": info,
            })
    return findings


def _seam_profile(segments, n):
    """Naht-Profile ueber eine Achse. Liefert (hoch, runter, linie), je Position i der Anteil der
    Linien mit
      hoch/runter: isolierter Stufe zwischen i und i+1 (Nachbarschritte klein), nach Richtung;
      linie:       duenner Linie genau auf Pixel i (zwei gegenlaeufige Spruenge, aussen ruhig).
    segments: Liste von (Anfang, Folge) -- Szenenabschnitte der Zeilen (senkrechte Naehte) bzw.
    Spalten (waagrechte Naehte); die 2D-Leiste teilt eine Linie in mehrere Abschnitte.
    Der Anteil bezieht sich auf die Linien, die Position i als Szene sehen, mindestens aber auf die
    Haelfte der meisten -- sonst reichten neben der Leiste schon wenige Linien fuer eine Naht."""
    up = [0] * n
    down = [0] * n
    spike = [0] * n
    count = [0] * n
    j, half = SEAM_JUMP, SEAM_JUMP / 2
    for off, line in segments:
        m = len(line)
        if m < 4:
            continue
        d = [line[i + 1] - line[i] for i in range(m - 1)]
        a = [abs(v) for v in d]
        for i in range(1, m - 2):
            count[off + i] += 1
            ai = a[i]
            if ai >= j and a[i - 1] < ai / 2 and a[i + 1] < ai / 2:
                if d[i] > 0:
                    up[off + i] += 1
                else:
                    down[off + i] += 1
            elif (ai >= j and a[i - 1] >= j and (d[i] > 0) != (d[i - 1] > 0)
                  and (i < 2 or a[i - 2] < half) and a[i + 1] < half):
                spike[off + i] += 1
    floor = max(1, max(count) / 2)
    norm = [max(c, floor) for c in count]
    return ([c / k for c, k in zip(up, norm)], [c / k for c, k in zip(down, norm)],
            [c / k for c, k in zip(spike, norm)])


def _single_seams(up, down):
    """Lange Einzelnaht: eine Stufe ueber fast die ganze Szene, ohne Gegenkante in bis zu 6 Pixeln
    Abstand. Mit Gegenkante ist es ein schmaler Balken -- der Rahmen eines Menues o. ae."""
    out = []
    n = len(up)
    for prof, other in ((up, down), (down, up)):
        for i, v in enumerate(prof):
            if v < SEAM_SINGLE:
                continue
            near = max(other[max(0, i - 6):min(n, i + 7)])
            if near >= 0.4 * v:
                continue
            out.append((i, v))
    return out


def _is_thin_bar(p, axis, i, samples=64):
    """True, wenn die Stufe bei i nur der Rand eines schmalen hellen oder dunklen Balkens ist (Rahmenlinie
    eines Menues, weich skaliert: mehrere Schritte statt einer Gegenkante): die Helligkeit acht bis
    vier Pixel vor der Stufe entspricht der vier bis acht Pixel danach. Bei einer echten Naht liegen die
    beiden Seiten mindestens einen Sprung auseinander."""
    n = p.w if axis == "senkrecht" else p.h
    lines = p.h if axis == "senkrecht" else p.w
    if i - 8 < 0 or i + 9 >= n:
        return False
    step = max(1, lines // samples)
    same = tested = 0
    for k in range(0, lines, step):
        if axis == "senkrecht":
            row = p.luma[k]
            before, after = row[i - 8:i - 3], row[i + 5:i + 10]
        else:
            before = bytes(p.luma[y][k] for y in range(i - 8, i - 3))
            after = bytes(p.luma[y][k] for y in range(i + 5, i + 10))
        tested += 1
        if abs(sum(before) / len(before) - sum(after) / len(after)) < SEAM_JUMP * 0.75:
            same += 1
    return tested > 0 and same >= 0.6 * tested


def check_seams(p, stats):
    findings = []
    # senkrechte Naehte: ueber die Zeilen der Szene (jede zweite Zeile genuegt), ohne die Leiste
    rows = []
    for y in range(0, p.h, 2):
        row = p.luma[y]
        rows += [(a, row[a:b]) for a, b in p.row_runs(y)]
    prof_v = _seam_profile(rows, p.w)
    # waagrechte Naehte: ueber die Spalten der Szene (jede zweite Spalte)
    cols = []
    for x in range(0, p.w, 2):
        for a, b in p.col_runs(x):
            cols.append((a, bytes(p.luma[y][x] for y in range(a, b))))
    prof_h = _seam_profile(cols, p.h)

    def col_extent(x0, x1):
        """Unterste Szenenzeile ueber den Spalten x0..x1 -- fuer den gemeldeten Ausschnitt."""
        return max((b for x in range(x0, min(p.w, x1 + 1)) for _, b in p.col_runs(x)), default=p.h)
    for axis, (up, down, spike) in (("senkrecht", prof_v), ("waagrecht", prof_h)):
        combined = [u + d_ + s for u, d_, s in zip(up, down, spike)]
        peaks = [i for i, v in enumerate(combined) if v >= SEAM_REPEAT]
        single = [(i, v) for i, v in _single_seams(up, down) if not _is_thin_bar(p, axis, i)]
        if stats is not None:
            stats.append(("naht", {"achse": axis,
                                   "max": [round(v, 3) for v in sorted(combined, reverse=True)[:3]],
                                   "linien_ueber_wiederholschwelle": len(peaks),
                                   "einzelnaehte": len(single)}))
        for i, v in single:
            rect = (i, 0, 2, col_extent(i, i + 1)) if axis == "senkrecht" else (0, i, p.w, 2)
            findings.append({"pruefung": "naht",
                             "text": f"{axis}e Naht bei {i + 1}, ueber {v * 100:.0f} % der Szene",
                             "ausschnitt": list(rect), "werte": {"abdeckung": round(v, 3)}})
        run = _periodic_run(peaks)
        if len(run) >= SEAM_REPEAT_MIN:
            step = run[1] - run[0]
            if axis == "senkrecht":
                rect = (run[0], 0, run[-1] - run[0] + 2, col_extent(run[0], run[-1] + 1))
            else:
                rect = (0, run[0], p.w, run[-1] - run[0] + 2)
            findings.append({"pruefung": "naht",
                             "text": f"{len(run)} {axis}e Naehte im Abstand {step} Pixel",
                             "ausschnitt": list(rect),
                             "werte": {"linien": len(run), "abstand": step}})
    return findings


def _periodic_run(positions):
    """Laengste Folge von Positionen mit gleichem Abstand (Abstand >= 3, Toleranz 1 Pixel)."""
    best = []
    pos = sorted(set(positions))
    for a in range(len(pos)):
        for b in range(a + 1, len(pos)):
            step = pos[b] - pos[a]
            if step < 3:
                continue
            run = [pos[a], pos[b]]
            nxt = pos[b] + step
            while True:
                hit = [q for q in pos if abs(q - nxt) <= 1]
                if not hit:
                    break
                run.append(hit[0])
                nxt = hit[0] + step
            if len(run) > len(best):
                best = run
        if len(best) >= SEAM_REPEAT_MIN and len(best) > len(pos) // 2:
            break
    return best


def _block_colors(p):
    """Mittlere Farbe je Block (B, G, R)."""
    w = p.w
    bgr = p.img.bgr
    out = [[None] * p.bw for _ in range(p.bh)]
    for by in range(p.bh):
        for bx in range(p.bw):
            sb = sg = sr = 0
            for y in range(by * BLOCK, by * BLOCK + BLOCK, 2):
                s = (y * w + bx * BLOCK) * 3
                seg = bgr[s:s + BLOCK * 3]
                sb += sum(seg[0::3])
                sg += sum(seg[1::3])
                sr += sum(seg[2::3])
            n = BLOCK * BLOCK // 2
            out[by][bx] = (sb / n, sg / n, sr / n)
    return out


def _is_magenta(c):
    b, g, r = c
    return r >= 180 and b >= 180 and g <= 80


def check_magenta(p, colors, stats):
    cells = {(bx, by) for by in range(p.bh) for bx in range(p.bw) if _is_magenta(colors[by][bx])}
    findings = []
    for comp in components(cells, p.bw, p.bh):
        if len(comp) < MAGENTA_MIN_BLOCKS:
            continue
        xs = [c[0] for c in comp]
        ys = [c[1] for c in comp]
        rect = rect_of_blocks(min(xs), min(ys), max(xs), max(ys))
        findings.append({"pruefung": "magenta",
                         "text": f"Ersatzfarbe Magenta, {len(comp)} Bloecke",
                         "ausschnitt": list(rect), "werte": {"bloecke": len(comp)}})
    if stats is not None:
        stats.append(("magenta", {"bloecke": len(cells)}))
    return findings


def _checker_block(p, bx, by):
    """Block zeigt ein Zweifarben-Schachbrett mit Feldern von 1 bis 4 Pixeln."""
    x0, y0 = bx * BLOCK, by * BLOCK
    vals = [p.luma[y][x0:x0 + BLOCK] for y in range(y0, y0 + BLOCK)]
    flat = [v for row in vals for v in row]
    lo, hi = min(flat), max(flat)
    if hi - lo < 60:
        return False
    mid = (lo + hi) / 2
    if any(abs(v - mid) < (hi - lo) * 0.3 for v in flat):
        return False                      # Zwischenwerte: kein reines Zweifarbenmuster
    bits = [[1 if v > mid else 0 for v in row] for row in vals]
    for cell in (1, 2, 4):
        ok = True
        for y in range(BLOCK):
            for x in range(BLOCK):
                if bits[y][x] != (bits[0][0] ^ (((x // cell) + (y // cell)) & 1)):
                    ok = False
                    break
            if not ok:
                break
        if ok:
            return True
    return False


def check_checker(p, stats):
    cells = {(bx, by) for by in range(p.bh) for bx in range(p.bw) if _checker_block(p, bx, by)}
    findings = []
    for comp in components(cells, p.bw, p.bh):
        if len(comp) < CHECKER_MIN_BLOCKS:
            continue
        xs = [c[0] for c in comp]
        ys = [c[1] for c in comp]
        rect = rect_of_blocks(min(xs), min(ys), max(xs), max(ys))
        findings.append({"pruefung": "schach", "text": f"Schachbrettmuster, {len(comp)} Bloecke",
                         "ausschnitt": list(rect), "werte": {"bloecke": len(comp)}})
    if stats is not None:
        stats.append(("schach", {"bloecke": len(cells)}))
    return findings


def check_empty(p, colors, stats):
    blocks = [c for row in colors for c in row]
    buckets = {}
    for b, g, r in blocks:
        key = (int(b) // 16, int(g) // 16, int(r) // 16)
        buckets[key] = buckets.get(key, 0) + 1
    share = max(buckets.values()) / len(blocks)
    if stats is not None:
        stats.append(("leer", {"groesste_farbe": round(share, 3)}))
    if share >= EMPTY_SHARE:
        return [{"pruefung": "leer", "text": f"fast einfarbiges Bild ({share * 100:.0f} % eine Farbe)",
                 "ausschnitt": [0, 0, p.w, p.h], "werte": {"anteil": round(share, 3)}}]
    return []


def thumb(p):
    """Verkleinerte Helligkeit der Szene (je Block ein Wert, ohne Leiste) fuer den Folgenvergleich."""
    n = p.block_pixels()
    return [p.bsum[by][bx] / n for bx, by in p.scene_blocks]


def mean_diff(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


# --- Ablauf -----------------------------------------------------------------------------------------
def inspect_image(path, args, stats=None):
    img = compare_shots.read_bmp(path)
    p = Prepared(img, args.hud, args.black)
    only = args.only
    findings = []
    colors = None
    if "schwarz" in only:
        findings += check_black(p, stats)
    if "naht" in only:
        findings += check_seams(p, stats)
    if "magenta" in only or "leer" in only:
        colors = _block_colors(p)
    if "magenta" in only:
        findings += check_magenta(p, colors, stats)
    if "schach" in only:
        findings += check_checker(p, stats)
    if "leer" in only:
        findings += check_empty(p, colors, stats)
    return p, findings


def check_sequence(entries):
    """entries: Liste (pfad, thumb) in Bildfolge. Liefert {pfad: [befund]}."""
    out = {}
    for i in range(1, len(entries) - 1):
        (pa, ta), (pb, tb), (pc, tc) = entries[i - 1], entries[i], entries[i + 1]
        if len(ta) != len(tb) or len(tb) != len(tc):
            continue
        d_ab, d_bc, d_ac = mean_diff(ta, tb), mean_diff(tb, tc), mean_diff(ta, tc)
        near = min(d_ab, d_bc)
        if near >= JUMP_MIN and near >= JUMP_RATIO * max(d_ac, 1.0):
            out.setdefault(pb, []).append({
                "pruefung": "sprung",
                "text": f"Bild weicht von beiden Nachbarn ab ({d_ab:.1f}/{d_bc:.1f}), "
                        f"die Nachbarn untereinander nur {d_ac:.1f}",
                "ausschnitt": None,
                "werte": {"zum_vorigen": round(d_ab, 2), "zum_naechsten": round(d_bc, 2),
                          "nachbarn": round(d_ac, 2)}})
    return out


def save_crop(img, rect, path, scale=2):
    """Ausschnitt (mit etwas Rand) vergroessert als BMP speichern."""
    x, y, w, h = rect
    pad = 16
    x0, y0 = max(0, x - pad), max(0, y - pad)
    x1, y1 = min(img.width, x + w + pad), min(img.height, y + h + pad)
    # grosse Ausschnitte nicht vergroessern
    if (x1 - x0) * scale > 1600 or (y1 - y0) * scale > 1200:
        scale = 1
    cw, ch = (x1 - x0) * scale, (y1 - y0) * scale
    out = bytearray(cw * ch * 3)
    for yy in range(ch):
        sy = y0 + yy // scale
        for xx in range(cw):
            sx = x0 + xx // scale
            s = (sy * img.width + sx) * 3
            d = (yy * cw + xx) * 3
            out[d:d + 3] = img.bgr[s:s + 3]
    compare_shots.write_bmp(path, compare_shots.Image(cw, ch, out))


def collect(inputs):
    """Liefert Liste von (gruppe, [pfade]) -- je Ordner eine Gruppe in Bildfolge."""
    groups = []
    for inp in inputs:
        if os.path.isdir(inp):
            files = sorted(glob.glob(os.path.join(inp, "shot_*.bmp")))
            groups.append((inp, files))
        elif os.path.isfile(inp):
            groups.append((os.path.dirname(inp) or ".", [inp]))
        else:
            raise FileNotFoundError(f"{inp}: weder Ordner noch Bilddatei")
    return groups


def run(args):
    groups = collect(args.inputs)
    report = []
    total = 0
    errors = 0
    crop_root = os.path.abspath(args.crops) if args.crops else None
    for group, files in groups:
        if crop_root and os.path.abspath(group) == crop_root:
            raise ValueError("--crops darf nicht der gepruefte Ordner sein")
        seq = []
        results = []
        for path in files:
            stats = [] if args.stats else None
            try:
                p, findings = inspect_image(path, args, stats)
            except (OSError, ValueError) as e:
                errors += 1
                results.append({"bild": path, "fehler": str(e), "befunde": []})
                continue
            seq.append((path, thumb(p)))
            results.append({"bild": path, "breite": p.w, "hoehe": p.h, "befunde": findings,
                            "_img": p.img, "_stats": stats})
        if "sprung" in args.only and len(seq) >= 3:
            jumps = check_sequence(seq)
            for r in results:
                r["befunde"] += jumps.get(r["bild"], [])
        for r in results:
            if crop_root and r["befunde"]:
                base = os.path.splitext(os.path.basename(r["bild"]))[0]
                sub = os.path.join(crop_root, os.path.basename(os.path.normpath(group)))
                for k, f in enumerate(r["befunde"]):
                    if f.get("ausschnitt"):
                        cp = os.path.join(sub, f"{base}_{f['pruefung']}_{k}.bmp")
                        save_crop(r["_img"], f["ausschnitt"], cp)
                        f["ausschnitt_datei"] = cp
            total += len(r["befunde"])
            report.append(r)
    if args.json:
        clean = []
        for r in report:
            c = {k: v for k, v in r.items() if not k.startswith("_")}
            if args.stats and r.get("_stats") is not None:
                c["statistik"] = [{"pruefung": n, **v} for n, v in r["_stats"]]
            clean.append(c)
        json.dump({"befunde": total, "fehler": errors, "bilder": clean}, sys.stdout, indent=1,
                  ensure_ascii=False)
        print()
    else:
        for r in report:
            if "fehler" in r:
                print(f"FEHLER  {r['bild']}: {r['fehler']}")
                continue
            if r["befunde"]:
                print(f"BEFUND  {r['bild']}")
                for f in r["befunde"]:
                    rect = f.get("ausschnitt")
                    where = f"  [x={rect[0]} y={rect[1]} {rect[2]}x{rect[3]}]" if rect else ""
                    print(f"        {f['pruefung']:8s} {f['text']}{where}")
                    if f.get("ausschnitt_datei"):
                        print(f"                 Ausschnitt: {f['ausschnitt_datei']}")
            elif not args.quiet:
                print(f"ok      {r['bild']}")
            if args.stats and r.get("_stats"):
                for name, v in r["_stats"]:
                    print(f"        [{name}] {v}")
        n_img = len(report)
        n_bad = sum(1 for r in report if r["befunde"])
        print(f"{n_img} Bilder, {n_bad} mit Befund, {total} Befunde, {errors} Fehler")
    if errors:
        return 2
    return 1 if total else 0


# --- Selbsttest -------------------------------------------------------------------------------------
def _in_bar(width, height, x, y):
    """Pixel der kuenstlichen 2D-Leiste: 4:3-breit und mittig wie im Spiel, links der hoehere
    Radarteil. Liegt knapp innerhalb der Vorgabemaske DEFAULT_HUD."""
    bar_w = min(width, height * HUD_ASPECT)
    left = (width - bar_w) / 2.0
    if not left + 2 <= x < left + bar_w - 2:
        return False
    top = 0.69 if x < left + 0.22 * bar_w else 0.725
    return y >= height * top


def _synthetic(width=640, height=360, seed=1):
    """Gelaendeaehnliches Bild: weiches Wertrauschen in Gruen/Braun plus feines Rauschen,
    unten mittig eine dunkle 2D-Leiste mit einem schwarzen Feld (die Leiste ist keine Szene),
    links und rechts daneben Gelaende."""
    import random
    rnd = random.Random(seed)
    cell = 40
    gw, gh = width // cell + 2, height // cell + 2
    grid = [[rnd.random() for _ in range(gw)] for _ in range(gh)]
    out = bytearray(width * height * 3)
    hud_top = int(height * 0.725)
    for y in range(height):
        fy = y / cell
        iy = int(fy)
        ty = fy - iy
        for x in range(width):
            fx = x / cell
            ix = int(fx)
            tx = fx - ix
            v = (grid[iy][ix] * (1 - tx) * (1 - ty) + grid[iy][ix + 1] * tx * (1 - ty)
                 + grid[iy + 1][ix] * (1 - tx) * ty + grid[iy + 1][ix + 1] * tx * ty)
            n = rnd.randint(-6, 6)
            g = int(70 + 80 * v) + n
            r = int(50 + 60 * v) + n
            b = int(30 + 40 * v) + n
            if _in_bar(width, height, x, y):
                b, g, r = 60, 45, 20
                if width * 0.3 < x < width * 0.45 and y > hud_top + 10:
                    b = g = r = 0      # schwarzes Feld in der Leiste (Minikarte o. ae.)
            d = (y * width + x) * 3
            out[d], out[d + 1], out[d + 2] = max(0, min(255, b)), max(0, min(255, g)), max(0, min(255, r))
    return compare_shots.Image(width, height, out)


def _copy(img):
    return compare_shots.Image(img.width, img.height, bytearray(img.bgr))


def _fill(img, x0, y0, x1, y1, bgr):
    for y in range(y0, y1):
        for x in range(x0, x1):
            d = (y * img.width + x) * 3
            img.bgr[d:d + 3] = bytes(bgr)


def _soft_corner(img, radius, cx=0, cy=0):
    """Weicher schwarzer Verlauf um die Ecke (cx, cy) (wie ausserhalb der Karte). Die Leiste bleibt."""
    for y in range(img.height):
        for x in range(img.width):
            if _in_bar(img.width, img.height, x, y):
                continue
            dist = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
            if dist < radius * 1.8:
                f = 0.0 if dist < radius else (dist - radius) / (radius * 0.8)
                d = (y * img.width + x) * 3
                for k in range(3):
                    img.bgr[d + k] = int(img.bgr[d + k] * f)


def selftest():
    ok = True
    base = _synthetic()
    cases = []
    cases.append(("unversehrt", base, set()))
    img = _copy(base)
    _fill(img, 200, 60, 360, 180, (0, 0, 0))
    _fill(img, 260, 100, 280, 150, (90, 140, 170))   # "Haus" im Schwarzen
    cases.append(("schwarzes Rechteck", img, {"schwarz"}))
    img = _copy(base)
    _soft_corner(img, 140)
    cases.append(("weicher Rand ausserhalb der Karte", img, set()))
    img = _copy(base)
    for y in range(img.height):                          # die ganze Leiste schwarz
        for x in range(img.width):
            if _in_bar(img.width, img.height, x, y):
                _fill(img, x, y, x + 1, y + 1, (0, 0, 0))
    cases.append(("schwarz nur in der 2D-Leiste", img, set()))
    img = _copy(base)
    _fill(img, 0, 230, 72, 360, (0, 0, 0))              # Ecke links neben der Leiste, harte Kante
    cases.append(("schwarze Ecke neben der Leiste, harte Kante", img, {"schwarz"}))
    img = _copy(base)
    _soft_corner(img, 90, 0, 359)                        # Ecke links unten, weicher Verlauf
    cases.append(("weicher Rand in der Ecke neben der Leiste", img, set()))
    img = _copy(base)
    for x in range(40, 600, 10):                         # Menue-Striche alle 10 Pixel
        for y in range(0, 280):
            d = (y * img.width + x) * 3
            img.bgr[d:d + 3] = bytes(max(0, v - 60) for v in img.bgr[d:d + 3])
    cases.append(("Naehte alle 10 Pixel", img, {"naht"}))
    img = _copy(base)                                    # Versatz: rechte Haelfte heller
    for y in range(img.height):
        for x in range(320, 640):
            d = (y * img.width + x) * 3
            img.bgr[d:d + 3] = bytes(min(255, v + 50) for v in img.bgr[d:d + 3])
    cases.append(("einzelne senkrechte Naht", img, {"naht"}))
    img = _copy(base)                                    # weich skalierte Rahmenlinie: kein Naht-Fehler
    for y in range(img.height):
        for x, add in ((300, 50), (301, 130), (302, 100), (303, 20)):
            d = (y * img.width + x) * 3
            img.bgr[d:d + 3] = bytes(min(255, v + add) for v in img.bgr[d:d + 3])
    cases.append(("weich skalierte Rahmenlinie", img, set()))
    img = _copy(base)
    _fill(img, 400, 40, 440, 80, (255, 0, 255))
    cases.append(("Magenta-Flaeche", img, {"magenta"}))
    img = _copy(base)
    for y in range(40, 88):
        for x in range(100, 164):
            v = 255 if ((x // 2 + y // 2) & 1) else 0
            d = (y * img.width + x) * 3
            img.bgr[d:d + 3] = bytes((v, v, v))
    cases.append(("Schachbrett", img, {"schach"}))
    img = compare_shots.Image(640, 360, bytearray(b"\x20\x20\x20" * 640 * 360))
    cases.append(("einfarbig", img, {"leer"}))

    args = argparse.Namespace(hud=None, black=BLACK_LEVEL, only=set(CHECKS))
    with tempfile.TemporaryDirectory() as tmp:
        for name, img, expected in cases:
            path = os.path.join(tmp, "shot.bmp")
            compare_shots.write_bmp(path, img)
            _, findings = inspect_image(path, args)
            got = {f["pruefung"] for f in findings}
            good = got == expected
            ok &= good
            print(f"{'ok    ' if good else 'FEHLER'} {name}: erwartet {sorted(expected) or '-'}, "
                  f"gefunden {sorted(got) or '-'}")
        # Folge: mittleres Bild weicht ab
        seq_dir = os.path.join(tmp, "folge")
        os.makedirs(seq_dir)
        flick = _copy(base)
        _fill(flick, 100, 20, 540, 260, (200, 200, 200))
        for i, im in enumerate((base, base, flick, base)):
            compare_shots.write_bmp(os.path.join(seq_dir, f"shot_{i:06d}.bmp"), im)
        entries = []
        for f in sorted(glob.glob(os.path.join(seq_dir, "shot_*.bmp"))):
            p, _ = inspect_image(f, argparse.Namespace(hud=None, black=BLACK_LEVEL, only=set()))
            entries.append((f, thumb(p)))
        jumps = check_sequence(entries)
        good = list(jumps) == [entries[2][0]]
        ok &= good
        print(f"{'ok    ' if good else 'FEHLER'} Sprung in der Bildfolge: "
              f"{'Bild 2 gemeldet' if good else sorted(os.path.basename(k) for k in jumps)}")
    print("Selbsttest", "bestanden" if ok else "FEHLGESCHLAGEN")
    return 0 if ok else 1


def _hud_rect(spec):
    try:
        return compare_shots.Rect(spec)
    except ValueError as e:
        raise argparse.ArgumentTypeError(str(e).replace("--ignore", "--hud"))


def main():
    ap = argparse.ArgumentParser(description="Bildpruefung fuer Render-Tests (ohne Referenz)")
    ap.add_argument("inputs", nargs="*", help="Ordner mit shot_*.bmp oder einzelne Bilder")
    ap.add_argument("--json", action="store_true", help="Ergebnis als JSON auf stdout")
    ap.add_argument("--crops", help="Ordner fuer vergroesserte Ausschnitte der Befunde")
    ap.add_argument("--only", help="nur diese Pruefungen, kommagetrennt: " + ",".join(CHECKS))
    ap.add_argument("--hud", action="append", type=_hud_rect, metavar="X,Y,B,H", default=None,
                    help="Rechteck der 2D-Leiste in Pixeln oder Prozent des Bildes (z. B. "
                         "12%%,68%%,76%%,32%%), mehrfach angebbar; ersetzt die Vorgabe (4:3-breite "
                         "Leiste mittig, Radarteil und Rest)")
    ap.add_argument("--black", type=int, default=BLACK_LEVEL,
                    help="Pixel mit max(R,G,B) bis zu diesem Wert gelten als schwarz (Vorgabe 12)")
    ap.add_argument("--stats", action="store_true", help="Messwerte je Pruefung ausgeben (Eichung)")
    ap.add_argument("--quiet", action="store_true", help="unauffaellige Bilder nicht auflisten")
    ap.add_argument("--selftest", action="store_true", help="Selbsttest mit kuenstlich beschaedigten Bildern")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not args.inputs:
        ap.error("Ordner oder Bild angeben (oder --selftest)")
    if args.only:
        wanted = {s.strip() for s in args.only.split(",") if s.strip()}
        unknown = wanted - set(CHECKS)
        if unknown:
            ap.error("unbekannte Pruefung: " + ",".join(sorted(unknown)))
        args.only = wanted
    else:
        args.only = set(CHECKS)
    try:
        return run(args)
    except (OSError, ValueError) as e:
        print(f"FEHLER: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
