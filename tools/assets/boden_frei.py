#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour (Electronic Arts).
# Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Boden frei: Gelaendetexturen aus CC0-Fotoscans.

Ersetzt EAs Gelaende-TGAs (art/terrain/<name>.tga, Namen aus Terrain.ini) durch frei weitergebbare Fassungen aus
Poly-Haven- und ambientCG-Scans. Die Erzeugung (Befehl `bauen`) liest KEINE EA-Datei: Farbe, Helligkeitsspreizung,
Bewuchsanteil und Groesse jeder Textur stehen als eigene Werte in den Tabellen unten (TOENE, FAMILIEN, ZUORDNUNG).
EA-abhaengig sind nur die Schnittstellen: die Dateinamen (Terrain.ini verweist auf sie) und die Groesse in Kacheln
(die Karten speichern Kachelnummern je Textur).

Verfahren je Textur:
  1. Familie (Gras, Sand, Fels, ...) mit einem oder zwei Scans. Jeder Scan wird kachelnd ueber die Textur gelegt
     (`wdh` Wiederholungen je 4 Kacheln), der zweite um 90 Grad gedreht und versetzt, beide nach einer periodischen,
     weichen Rauschmaske plus Hoehenkarte gemischt -- nahtlos, weil alles ueber die Textur periodisch ist.
  2. Schattierung aus Umgebungsverdeckung und Normalen (Licht von links oben): die Scans sind entlichtet.
  3. Farbe: der Ton der Familie (eigener fester Wert, TOENE) ist die mittlere Farbe; die Helligkeitsstreuung wird
     mit einem Exponenten auf die Spreizung der Familie gebracht (`streu`, eigener fester Wert).
  4. Optional ein Bewuchs (zweite Familie mit eigenem Ton und Anteil), etwa braunes Gras auf grauem Fels oder Gras
     auf Sand; gemischt nach einer eigenen periodischen Fleckenmaske plus Hoehenkarten.
  5. Optional Einsprengsel (gerechnete Punkte: Kiesel im Sand, Punkte im Schnee) und Daempfung dunkler Scanflecken.
  6. Aus EINER Berechnung in der groessten Stufe (Kachel 64*4 Pixel) werden die Stufen s = 1, 2, 4 gemittelt;
     TGA wie EA (24 Bit, unkomprimiert), ab s = 2 mit Kennung "ZHTS<s>" im Bildkennungsfeld
     (WorldHeightMap::readTileScaleId). Wahlweise DDS (BC1/BC7 ueber texconv); die Engine liest Gelaende-
     kacheln als TGA oder DDS (DDS vor TGA, Kachelgroesse aus Breite und Kachelzahl).

Ausgabe: <aus>/s<s>/BodenFrei.big (Eintraege Art\\Terrain\\<name>.tga). Geladen als Mod (-mod <absoluter Ordner>):
Mods ueberlagern Grundarchive und Texturstufen, EAs Dateien bleiben unberuehrt.

Befehle:
  bauen       Texturen rechnen und BIG-Archive bauen (--stufen 1,2,4, --nur tmgras12a ..., --prozesse 4,
              --format tga|dds-bc1|dds-bc7)
  packen      nur die Archive aus den gerechneten PNGs (etwa in einem anderen Format)
  schaerfen   geschaerftes Paket aus den DDS im Repo (--ziel <eigener Ordner>, --staerke 0.5, --baender 4,
              --abfall 0.75): Unscharfmaske je Mip-Stufe, eingerechnet in die Grundstufe (schaerfe_mips)
  relief      Normalen, Glanz und Hoehe je Textur als Art/Terrain/<name>_n.dds (BC7, --stufe 2, --ziel), deckungsgleich
              zur Farbe (gleiche Lagen und Mischmasken); die Engine nutzt sie mit -d3d11Effects terrainnormal
  hoehe       Hoehe je Textur als Art/Terrain/<name>_h.dds (BC4, --stufe 1, --ziel), deckungsgleich zur Farbe; die Engine
              verschiebt damit das tessellierte Gelaende (-d3d11Effects terraintess)
  liste       Zuordnung, Groessen und geschaetzte Paketgroessen je Stufe und Format (ohne zu bauen)
  vergleich   Messwerkzeug: Bogen EA | frei je Textur (liest EA-Texturen, nur zum Pruefen)
  messen      Messwerkzeug: Kennzahlen der EA-Texturen (Mittel, Streuung, zwei Farbgruppen) -- Anhalt fuer die
              eigenen Werte, geht nicht in den Bau ein
  kandidaten  Vorschau mehrerer Scans in einer Familie (Kandidatenvergleich)
  nachweis    Tabelle Herkunft/Lizenz der verwendeten Scans fuer ASSET_CREDITS.md

Beispiel:
  python tools/assets/boden_detail.py laden polyhaven:sparse_grass ...      (Scans holen, samt meta.json)
  python tools/assets/boden_frei.py bauen --aus render-out/boden-detail3/frei
Braucht numpy und Pillow.
"""
import argparse
import io
import json
import os
import subprocess
import sys
import tempfile
import zlib

import numpy as np
from PIL import Image

HIER = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HIER))
sys.path.insert(0, HIER)
import boden_detail as bd  # noqa: E402  (Bildhilfen; der Bau ruft dort nichts auf, was EA-Daten liest)

FARBE = 0.6			# Anteil der Farbabweichung des Scans, der erhalten bleibt
MAX_S = 4			# groesste Stufe: Kacheln zu 64*4 Pixeln; daraus werden die kleineren gemittelt
# texconv aus DirectXTex: unter GZH_WERKZEUGE\directxtex, sonst aus dem Suchpfad
TEXCONV = bd.pfade.werkzeuge("directxtex", "texconv.exe", pflicht=False) or "texconv.exe"

# -------------------------------------------------------------------------------------------------
# Eigene Werte (keine EA-Daten): Toene, Familien, Zuordnung
# -------------------------------------------------------------------------------------------------
# Toene: mittlere Farbe einer Textur (sRGB 0..255, Mittel der Pixelwerte je Kanal). Eigene Palette, je Familie
# 1 bis 16 Toene, dazu die Bewuchstoene; gesetzt am Bild.
TOENE = {
    "acker_mittel_beige": (152, 132, 108),
    "asphalt_mittel_grau": (100, 108, 104),
    "asphalt_mittel_grau_2": (124, 128, 124),
    "asphalt_mittel_grau_3": (132, 132, 136),
    "asphalt_mittel_grau_4": (144, 148, 148),
    "asphalt_mittel_oliv": (112, 112, 100),
    "asphalt_mittel_oliv_2": (116, 116, 104),
    "beton_hell_grau": (180, 176, 164),
    "beton_hell_grau_2": (176, 176, 176),
    "beton_hell_grau_3": (176, 180, 176),
    "beton_hell_grau_4": (200, 196, 188),
    "beton_hell_grau_5": (200, 196, 188),
    "beton_mittel_grau": (148, 152, 144),
    "beton_mittel_grau_2": (152, 156, 152),
    "beton_mittel_grau_3": (156, 156, 156),
    "beton_mittel_oliv": (152, 148, 132),
    "bewuchs_braun": (112, 90, 54),
    "bewuchs_gelbgruen": (150, 146, 66),
    "bewuchs_gruen": (92, 116, 48),
    "bewuchs_oliv": (128, 120, 60),
    "bewuchs_saat": (104, 132, 44),
    "bewuchs_schnee": (228, 228, 228),
    "bewuchs_stein": (172, 170, 162),
    "bewuchs_stein_hell": (214, 212, 204),
    "erde_hell_hell_beige": (176, 156, 136),
    "erde_hell_mittel_grau": (148, 148, 148),
    "fels_dunkel_beige": (84, 68, 76),
    "fels_hell_beige": (184, 156, 108),
    "fels_hell_beige_2": (188, 160, 112),
    "fels_hell_grau": (188, 188, 180),
    "fels_hell_oliv": (172, 168, 152),
    "fels_hell_oliv_2": (212, 208, 184),
    "fels_mittel_beige": (152, 144, 112),
    "fels_mittel_beige_2": (164, 136, 104),
    "fels_mittel_beige_3": (148, 128, 128),
    "fels_mittel_beige_4": (176, 148, 100),
    "fels_mittel_beige_5": (184, 156, 100),
    "fels_mittel_blau": (116, 132, 144),
    "fels_mittel_grau": (116, 116, 120),
    "fels_mittel_grau_10": (152, 160, 164),
    "fels_mittel_grau_2": (124, 124, 120),
    "fels_mittel_grau_3": (132, 132, 124),
    "fels_mittel_grau_4": (132, 132, 132),
    "fels_mittel_grau_5": (136, 140, 128),
    "fels_mittel_grau_6": (136, 140, 132),
    "fels_mittel_grau_7": (136, 136, 140),
    "fels_mittel_grau_8": (144, 140, 132),
    "fels_mittel_grau_9": (144, 144, 140),
    "fels_mittel_oliv": (104, 116, 88),
    "fels_mittel_oliv_2": (152, 156, 128),
    "geroell_hell_beige": (200, 168, 116),
    "geroell_hell_grau": (188, 188, 188),
    "geroell_mittel_beige": (164, 148, 120),
    "geroell_mittel_blau": (84, 116, 136),
    "geroell_mittel_grau": (128, 136, 132),
    "geroell_mittel_grau_2": (136, 136, 136),
    "geroell_mittel_grau_3": (144, 144, 144),
    "geroell_mittel_oliv": (108, 112, 96),
    "geroell_mittel_oliv_2": (112, 116, 104),
    "gift_dunkel_rot": (96, 64, 56),
    "gift_hell_gelb": (180, 168, 80),
    "gift_mittel_rot": (176, 120, 92),
    "gras_dunkel_gruen": (80, 92, 44),
    "gras_dunkel_gruen_2": (76, 92, 32),
    "gras_dunkel_gruen_3": (96, 108, 24),
    "gras_dunkel_gruen_4": (84, 108, 48),
    "gras_dunkel_gruen_5": (80, 108, 68),
    "gras_dunkel_gruen_6": (96, 96, 52),
    "gras_fein_mittel_gruen": (144, 148, 80),
    "gras_mittel_gelb": (128, 124, 68),
    "gras_mittel_gruen": (104, 112, 44),
    "gras_mittel_gruen_10": (116, 116, 76),
    "gras_mittel_gruen_11": (128, 132, 80),
    "gras_mittel_gruen_12": (124, 140, 84),
    "gras_mittel_gruen_13": (116, 160, 84),
    "gras_mittel_gruen_14": (144, 148, 68),
    "gras_mittel_gruen_15": (136, 152, 88),
    "gras_mittel_gruen_2": (100, 108, 56),
    "gras_mittel_gruen_3": (100, 124, 44),
    "gras_mittel_gruen_4": (104, 120, 68),
    "gras_mittel_gruen_5": (100, 116, 72),
    "gras_mittel_gruen_6": (108, 120, 72),
    "gras_mittel_gruen_7": (112, 132, 60),
    "gras_mittel_gruen_8": (124, 124, 60),
    "gras_mittel_gruen_9": (116, 132, 56),
    "gras_mittel_oliv": (104, 108, 72),
    "gras_mittel_oliv_2": (144, 144, 104),
    "holz_mittel_beige": (160, 152, 136),
    "kies_dunkel_grau": (88, 88, 80),
    "kies_dunkel_oliv": (76, 80, 64),
    "kies_dunkel_rot": (116, 84, 60),
    "kies_dunkel_rot_2": (120, 88, 60),
    "kies_hell_grau": (184, 184, 180),
    "kies_mittel_beige": (132, 104, 80),
    "kies_mittel_beige_2": (136, 128, 120),
    "kies_mittel_beige_3": (168, 132, 100),
    "kies_mittel_blau": (80, 128, 176),
    "kies_mittel_blau_2": (112, 152, 176),
    "markierung_weiss": (226, 224, 214),
    "pflaster_mittel_beige": (148, 120, 88),
    "pflaster_mittel_beige_2": (140, 124, 108),
    "pflaster_mittel_beige_3": (140, 128, 108),
    "pflaster_mittel_beige_4": (152, 144, 116),
    "platten_hell_beige": (188, 168, 128),
    "platten_hell_grau": (168, 164, 152),
    "platten_hell_rot": (216, 156, 128),
    "platten_mittel_beige": (152, 132, 112),
    "rissboden_hell_beige": (208, 180, 120),
    "rissboden_hell_beige_2": (228, 208, 160),
    "rissboden_hell_gelb": (236, 192, 96),
    "rissboden_hell_grau": (160, 160, 160),
    "rissboden_hell_grau_2": (236, 236, 236),
    "sand_hell_beige": (176, 156, 132),
    "sand_hell_beige_2": (188, 164, 112),
    "sand_hell_beige_3": (216, 184, 128),
    "sand_hell_beige_4": (220, 184, 124),
    "sand_hell_beige_5": (216, 184, 128),
    "sand_hell_beige_6": (220, 184, 128),
    "sand_hell_beige_7": (236, 212, 144),
    "sand_hell_beige_8": (248, 232, 196),
    "sand_hell_beige_9": (248, 232, 196),
    "sand_hell_gelb": (200, 156, 76),
    "sand_hell_gelb_2": (200, 156, 80),
    "sand_hell_gelb_3": (212, 168, 96),
    "sand_hell_grau": (236, 240, 228),
    "sand_mittel_gelb": (156, 124, 64),
    "sand_stein_hell_beige": (188, 160, 104),
    "sand_stein_hell_beige_10": (232, 212, 152),
    "sand_stein_hell_beige_11": (228, 216, 160),
    "sand_stein_hell_beige_12": (236, 216, 172),
    "sand_stein_hell_beige_13": (228, 212, 180),
    "sand_stein_hell_beige_14": (244, 228, 188),
    "sand_stein_hell_beige_15": (248, 232, 196),
    "sand_stein_hell_beige_2": (204, 172, 116),
    "sand_stein_hell_beige_3": (192, 172, 136),
    "sand_stein_hell_beige_4": (204, 176, 124),
    "sand_stein_hell_beige_5": (212, 184, 116),
    "sand_stein_hell_beige_6": (200, 184, 144),
    "sand_stein_hell_beige_7": (216, 184, 132),
    "sand_stein_hell_beige_8": (224, 188, 128),
    "sand_stein_hell_beige_9": (236, 200, 124),
    "sand_stein_hell_blau": (156, 176, 184),
    "sand_stein_hell_gelb": (212, 160, 68),
    "sand_stein_hell_gelb_2": (204, 164, 80),
    "sand_stein_hell_gelb_3": (204, 164, 88),
    "sand_stein_hell_gelb_4": (220, 176, 80),
    "sand_stein_hell_gelb_5": (224, 188, 116),
    "sand_stein_hell_gelb_6": (228, 188, 112),
    "sand_stein_mittel_gelb": (168, 144, 84),
    "sand_stein_mittel_oliv": (156, 152, 140),
    "schnee_hell_grau": (208, 208, 192),
    "schnee_hell_grau_2": (212, 220, 224),
    "schnee_hell_grau_3": (224, 224, 224),
    "schnee_hell_grau_4": (224, 224, 224),
    "schnee_hell_grau_5": (228, 228, 228),
    "schnee_hell_grau_6": (228, 232, 236),
    "schnee_hell_grau_7": (236, 236, 236),
    "schnee_hell_grau_8": (240, 240, 240),
    "schnitzel_mittel_beige": (144, 132, 120),
    "schnitzel_mittel_beige_2": (148, 132, 116),
    "trockengras_dunkel_beige": (88, 76, 52),
    "trockengras_dunkel_beige_2": (108, 92, 68),
    "trockengras_dunkel_gelb": (96, 88, 52),
    "trockengras_dunkel_oliv": (80, 92, 64),
    "trockengras_hell_beige": (196, 172, 112),
    "trockengras_hell_beige_2": (212, 188, 144),
    "trockengras_mittel_beige": (112, 104, 76),
    "trockengras_mittel_beige_2": (108, 104, 88),
    "trockengras_mittel_beige_3": (152, 132, 100),
    "trockengras_mittel_gelb": (144, 132, 56),
    "trockengras_mittel_gelb_2": (172, 152, 84),
    "trockengras_mittel_grau": (140, 148, 136),
    "trockengras_mittel_gruen": (136, 132, 88),
    "trockengras_mittel_oliv": (128, 132, 100),
}

# Familien: Scans (Quelle, Id) und Parameter.
#   wdh     Wiederholungen eines Scans je 256 SD-Pixel (4 Kacheln, rund 80 Welteinheiten). 1 = ein 2-m-Scan
#           ueber eine 4x4-Textur -- so gross wie die Fotos der EA-Texturen (Kiesel 3 bis 5 Pixel).
#   anteil  Anteil des zweiten Scans in der Mischung (0..1), maske Frequenz der Mischmaske je Textur,
#   hoehe   Gewicht der Hoehenkarten in der Mischung, weich Breite des Uebergangs
#   ao, rel Schattierung (Exponent der Umgebungsverdeckung, der Reliefschattierung)
#   glatt   Anteil der grossen Flecken des Scans, der entfernt wird (Vorgabe 0,7)
#   streu   Helligkeitsspreizung: Standardabweichung der Helligkeit (0..1) in SD -- eigene feste Wahl je Familie
#   flecken Frequenz der Fleckenmaske je 4 Kacheln, wenn die Familie als Bewuchs auf einer anderen liegt
#   farbe   Anteil der Farbabweichung des Scans (Vorgabe FARBE); kleiner fuer Flaechen ohne Farbspiel (Asphalt)
#   sprenkel  Gewicht einer feinen Rauschmaske (4-fache Frequenz) im Bewuchs: zerteilt die Flecken in Sprenkel
#   flecken_weg  dunkle Flecken mittlerer Groesse des Scans aufhellen (0..1)
#   punkte  Einsprengsel: Liste aus {dichte (je SD-Kachel), r (SD-Pixel), hell (Faktor), relief}
FAMILIEN = {
    # Pilot (Tournament Lake)
    "gras": {"scans": [("polyhaven", "sparse_grass"), ("polyhaven", "forrest_ground_01")],
             "wdh": 1, "anteil": 0.45, "maske": 2, "hoehe": 0.6, "weich": 0.12, "ao": 0.6, "rel": 0.3, "streu": 0.13,
             "flecken": 10, "sprenkel": 0.2},
    "gras_fein": {"scans": [("polyhaven", "grass_ground"), ("polyhaven", "leafy_grass")],
                  "wdh": 2, "anteil": 0.35, "maske": 2, "hoehe": 0.6, "weich": 0.12, "ao": 0.5, "rel": 0.25,
                  "streu": 0.09},
    "trockengras": {"scans": [("polyhaven", "withered_grass"), ("polyhaven", "grass_ground")],
                    "wdh": 1, "anteil": 0.35, "maske": 2, "hoehe": 0.6, "weich": 0.12, "ao": 0.6, "rel": 0.35,
                    "streu": 0.13, "flecken": 10, "sprenkel": 0.2},
    "kies": {"scans": [("polyhaven", "pebble_ground_01"), ("polyhaven", "rocky_trail")],
             "wdh": 1, "anteil": 0.4, "maske": 3, "hoehe": 1.0, "weich": 0.10, "ao": 0.8, "rel": 0.5, "streu": 0.14,
             "farbe": 0.4},
    "erde_hell": {"scans": [("polyhaven", "dirt_floor"), ("polyhaven", "dry_ground_rocks")],
                  "wdh": 1, "anteil": 0.35, "maske": 2, "hoehe": 0.8, "weich": 0.12, "ao": 0.7, "rel": 0.45,
                  "streu": 0.09},
    # Geroell am Boden (Pilot-Fels); auch Bewuchs aus Steinen auf Sand oder Gras
    "fels_boden": {"scans": [("polyhaven", "rock_ground"), ("polyhaven", "rocks_ground_03")],
                   "wdh": 1, "anteil": 0.4, "maske": 2, "hoehe": 1.0, "weich": 0.10, "ao": 0.8, "rel": 0.6,
                   "glatt": 0.2, "streu": 0.15, "flecken": 14, "sprenkel": 0.15},
    # Fels und Klippen: Felswand-Scans mit grossen Formen
    "fels": {"scans": [("polyhaven", "rock_face_03"), ("polyhaven", "rock_face")],
             "wdh": 1, "anteil": 0.4, "maske": 2, "hoehe": 1.0, "weich": 0.10, "ao": 1.0, "rel": 0.8,
             "glatt": 0.0, "streu": 0.19, "farbe": 0.3},
    "sand": {"scans": [("polyhaven", "sand_02"), ("polyhaven", "dense_sand")],
             "wdh": 1, "anteil": 0.4, "maske": 2, "hoehe": 0.6, "weich": 0.12, "ao": 0.5, "rel": 0.4, "streu": 0.06,
             "punkte": [{"dichte": 4.0, "r": 1.4, "hell": 1.6, "relief": True}, {"dichte": 6.0, "r": 0.8, "hell": 0.6}]},
    "sand_stein": {"scans": [("polyhaven", "gravelly_sand"), ("polyhaven", "dirt_floor")],
                   "wdh": 1, "anteil": 0.4, "maske": 2, "hoehe": 0.8, "weich": 0.12, "ao": 0.7, "rel": 0.45,
                   "streu": 0.09},
    # ein Scan zweimal (der zweite gedreht und versetzt): bricht die Wiederholung in grossen Texturen
    "schnee": {"scans": [("ambientcg", "Snow006"), ("ambientcg", "Snow006")],
               "wdh": 1, "anteil": 0.5, "maske": 2, "hoehe": 0.6, "weich": 0.12, "ao": 0.5, "rel": 0.5, "streu": 0.045,
               "flecken": 6, "sprenkel": 0.15, "farbe": 0.4, "punkte": [{"dichte": 12.0, "r": 0.8, "hell": 0.45}]},
    "asphalt": {"scans": [("polyhaven", "asphalt_05"), ("polyhaven", "asphalt_floor")],
                "wdh": 1, "anteil": 0.4, "maske": 2, "hoehe": 0.4, "weich": 0.12, "ao": 0.5, "rel": 0.3,
                "glatt": 0.9, "streu": 0.035, "farbe": 0.3},
    "beton": {"scans": [("polyhaven", "concrete_floor_01"), ("polyhaven", "concrete_floor_01")],
              "wdh": 1, "anteil": 0.5, "maske": 2, "hoehe": 0.4, "weich": 0.12, "ao": 0.5, "rel": 0.3,
              "glatt": 0.8, "streu": 0.06, "farbe": 0.3},
    "pflaster": {"scans": [("polyhaven", "cobblestone_05")],
                 "wdh": 4, "anteil": 0.0, "maske": 2, "hoehe": 0.4, "weich": 0.12, "ao": 0.8, "rel": 0.6,
                 "glatt": 0.3, "streu": 0.20},
    "platten": {"scans": [("polyhaven", "stone_tiles_02")],
                "wdh": 2, "anteil": 0.0, "maske": 2, "hoehe": 0.4, "weich": 0.12, "ao": 0.8, "rel": 0.6,
                "glatt": 0.3, "streu": 0.14},
    "rissboden": {"scans": [("polyhaven", "dry_ground_01"), ("polyhaven", "dry_ground_01")],
                  "wdh": 1, "anteil": 0.5, "maske": 2, "hoehe": 0.6, "weich": 0.12, "ao": 0.8, "rel": 0.5,
                  "streu": 0.12, "flecken_weg": 1.0},
    # Stege: Planken waagrecht wie der Scan
    "holz": {"scans": [("polyhaven", "weathered_brown_planks")],
             "wdh": 2, "anteil": 0.0, "maske": 2, "hoehe": 0.4, "weich": 0.12, "ao": 0.8, "rel": 0.5,
             "glatt": 0.3, "streu": 0.12},
    "schnitzel": {"scans": [("polyhaven", "wood_chips"), ("polyhaven", "wood_chips")],
                  "wdh": 1, "anteil": 0.5, "maske": 2, "hoehe": 0.6, "weich": 0.12, "ao": 0.8, "rel": 0.5,
                  "streu": 0.18},
    # Acker: gepfluegter Boden (farm_furrows zeigt Fahrspuren, die sich sichtbar wiederholen); die Pflanzenreihen legt
# REIHEN als Bewuchs darueber
    "acker": {"scans": [("polyhaven", "farm_soil"), ("polyhaven", "farm_soil")],
              "wdh": 1, "anteil": 0.4, "maske": 2, "hoehe": 0.6, "weich": 0.12, "ao": 0.8, "rel": 0.5,
              "streu": 0.12},
    # Verseuchter Boden: Sand mit Steinen in eigenen Giftfarben, dazu dunkle Krusten als Einsprengsel
    "gift": {"scans": [("polyhaven", "gravelly_sand"), ("polyhaven", "burned_ground_01")],
             "wdh": 1, "anteil": 0.4, "maske": 3, "hoehe": 0.8, "weich": 0.10, "ao": 0.8, "rel": 0.5,
             "streu": 0.16, "farbe": 0.4,
             "punkte": [{"dichte": 10.0, "r": 1.2, "hell": 0.35}, {"dichte": 8.0, "r": 0.8, "hell": 1.4}]},
    "geroell": {"scans": [("polyhaven", "gravel_stones"), ("polyhaven", "rocks_ground_01")],
                "wdh": 1, "anteil": 0.4, "maske": 3, "hoehe": 1.0, "weich": 0.10, "ao": 1.0, "rel": 0.8,
                "glatt": 0.2, "streu": 0.25},
}

# Zuordnung TGA (ohne .tga) -> (Familie, Kacheln, Ton[, (Bewuchsfamilie, Ton, Anteil)]).
# Kacheln = Kantenlaenge in Kacheln zu 64 SD-Pixeln (Schnittstelle: die Karten speichern Kachelnummern).
ZUORDNUNG = {
    "tgcobble01": ("platten", 4, "platten_mittel_beige"),
    "tggrass02": ("gras", 10, "gras_mittel_gruen_9"),
    "tggrcsand01": ("sand_stein", 2, "sand_stein_mittel_gelb"),
    "tlcliff01a": ("fels", 6, "fels_mittel_beige_3"),
    "tlcliff01b": ("fels", 6, "fels_hell_oliv"),
    "tlcliff02a": ("fels", 6, "fels_mittel_beige_3"),
    "tlcliff03a": ("gras", 6, "gras_mittel_oliv"),
    "tlcliff03b": ("fels", 6, "fels_mittel_beige"),
    "tlcliff04a": ("fels", 6, "fels_dunkel_beige"),
    "tlcliff05a": ("fels", 6, "fels_mittel_oliv"),
    "tlcliff06a": ("fels", 6, "fels_mittel_grau_8", ("gras", "bewuchs_gruen", 0.20)),
    "tlcliff06b": ("gras", 6, "gras_mittel_oliv_2"),
    "tlcliff07a": ("fels", 6, "fels_mittel_grau_3"),
    "tlcliff08a": ("fels", 6, "fels_mittel_grau_5"),
    "tlcliff09a": ("fels", 6, "fels_mittel_beige_4"),
    "tlcliff10a": ("fels", 4, "fels_hell_beige"),
    "tlcliff11a": ("fels", 10, "fels_mittel_beige_5"),
    "tlcliff12a": ("fels", 10, "fels_mittel_beige_4"),
    "tlcliff13a": ("fels", 6, "fels_mittel_beige"),
    "tlcobble02": ("pflaster", 4, "pflaster_mittel_beige_4"),
    "tlcobble03": ("pflaster", 4, "pflaster_mittel_beige_2"),
    "tlcobble04": ("pflaster", 10, "pflaster_mittel_beige"),
    "tlgras01a": ("gras", 6, "gras_mittel_gruen_11"),
    "tlgras01b": ("gras", 6, "gras_mittel_gruen", ("schnee", "bewuchs_schnee", 0.35)),
    "tlgras02a": ("trockengras", 6, "trockengras_mittel_gelb"),
    "tlsand01a": ("sand", 6, "sand_hell_beige"),
    "tlsand02a": ("sand", 6, "sand_hell_beige_7"),
    "tlsand02b": ("sand_stein", 6, "sand_stein_hell_beige_9", ("trockengras", "bewuchs_oliv", 0.25)),
    "tlsand03a": ("sand", 6, "sand_hell_beige_4"),
    "tlsand03b": ("sand_stein", 6, "sand_stein_hell_beige_8", ("trockengras", "bewuchs_oliv", 0.30)),
    "tlsand03c": ("sand", 6, "sand_hell_beige_7"),
    "tlsand03d": ("sand", 6, "sand_hell_beige_5"),
    "tlsand03e": ("sand", 6, "sand_hell_beige_4"),
    "tlsand03f": ("sand", 6, "sand_hell_beige_4"),
    "tlsand03g": ("sand_stein", 6, "sand_stein_hell_beige_8", ("trockengras", "bewuchs_oliv", 0.30)),
    "tlsand05a": ("sand", 6, "sand_hell_gelb_3"),
    "tlsand06a": ("sand_stein", 6, "sand_stein_hell_gelb_6", ("trockengras", "bewuchs_oliv", 0.30)),
    "tlsnow01a": ("schnee", 6, "schnee_hell_grau_8"),
    "tmcliff01a": ("fels", 4, "fels_mittel_grau_6", ("gras", "bewuchs_gruen", 0.15)),
    "tmcliff02a": ("fels", 4, "fels_mittel_grau_8"),
    "tmcliff03a": ("fels", 4, "fels_mittel_grau_3"),
    "tmcliff04a": ("fels", 4, "fels_mittel_grau_7", ("gras", "bewuchs_gruen", 0.10)),
    "tmcliff05a": ("fels", 4, "fels_mittel_grau_4"),
    "tmcliff05b": ("fels", 4, "fels_mittel_beige"),
    "tmcliff06a": ("fels", 4, "fels_mittel_grau_9"),
    "tmcliff06b": ("fels", 4, "fels_hell_oliv", ("trockengras", "bewuchs_oliv", 0.30)),
    "tmcliff06c": ("fels", 4, "fels_hell_oliv_2", ("trockengras", "bewuchs_oliv", 0.30)),
    "tmcliff06d": ("fels", 4, "fels_hell_grau"),
    "tmcliff07a": ("fels", 4, "fels_mittel_grau_2"),
    "tmcliff08a": ("fels", 4, "fels_mittel_grau_4"),
    "tmcliff08b": ("fels", 4, "fels_mittel_beige"),
    "tmcliff09a": ("fels", 4, "fels_mittel_grau_9"),
    "tmcliff09b": ("fels", 4, "fels_mittel_oliv_2"),
    "tmcliff10a": ("fels", 4, "fels_mittel_grau"),
    "tmcliff10b": ("fels", 4, "fels_mittel_beige"),
    "tmcliff11a": ("fels", 4, "fels_mittel_oliv"),
    "tmcliff12a": ("fels", 4, "fels_mittel_grau", ("trockengras", "bewuchs_braun", 0.33)),
    "tmcliff13a": ("fels", 4, "fels_mittel_grau_10", ("gras", "bewuchs_oliv", 0.15)),
    "tmcliff13b": ("fels", 4, "fels_mittel_oliv_2"),
    "tmcliff14a": ("fels", 4, "fels_mittel_oliv_2", ("gras", "bewuchs_gruen", 0.25)),
    "tmcliff15a": ("fels", 4, "fels_mittel_grau_3"),
    "tmcliff15b": ("fels", 4, "fels_mittel_beige"),
    "tmcliff15c": ("fels", 4, "fels_hell_grau"),
    "tmcliff16a": ("fels", 4, "fels_mittel_grau_6"),
    "tmcliff17a": ("fels", 4, "fels_mittel_beige"),
    "tmcliff17b": ("fels", 4, "fels_mittel_beige_3"),
    "tmcliff17c": ("fels", 4, "fels_hell_oliv"),
    "tmcliff18a": ("fels", 4, "fels_mittel_beige_2"),
    "tmcliff19a": ("fels", 4, "fels_mittel_beige_5"),
    "tmcliff20a": ("fels", 4, "fels_hell_beige_2"),
    "tmcliff21a": ("fels", 4, "fels_mittel_blau", ("gras", "bewuchs_gelbgruen", 0.20)),
    "tmcliff22a": ("fels", 4, "fels_hell_grau"),
    "tmcliff23a": ("fels", 4, "fels_mittel_grau_2"),
    "tmcliff24b": ("fels", 4, "fels_hell_oliv"),
    "tmconc01a": ("beton", 4, "beton_mittel_grau_2"),
    "tmconc01b": ("beton", 4, "beton_hell_grau_2"),
    "tmcrops01a": ("acker", 4, "acker_mittel_beige", ("gras", "bewuchs_saat", 0.40)),
    "tmdirt01a": ("kies", 4, "kies_dunkel_grau"),
    "tmdirt02a": ("kies", 4, "kies_dunkel_oliv"),
    "tmdirt03a": ("erde_hell", 4, "erde_hell_hell_beige"),
    "tmdirt04a": ("kies", 4, "kies_hell_grau"),
    "tmdirt05a": ("kies", 4, "kies_mittel_beige_2"),
    "tmdirt06a": ("erde_hell", 4, "erde_hell_mittel_grau"),
    "tmdirt06b": ("sand_stein", 4, "sand_stein_hell_beige_10"),
    "tmdirt06d": ("kies", 4, "kies_hell_grau"),
    "tmdirt07a": ("rissboden", 4, "rissboden_hell_grau"),
    "tmdirt07b": ("rissboden", 4, "rissboden_hell_beige_2"),
    "tmdirt07c": ("sand_stein", 4, "sand_stein_hell_beige_12", ("trockengras", "bewuchs_oliv", 0.25)),
    "tmdirt07d": ("rissboden", 4, "rissboden_hell_beige"),
    "tmdirt07e": ("rissboden", 4, "rissboden_hell_gelb"),
    "tmdirt08a": ("sand_stein", 4, "sand_stein_hell_beige_2"),
    "tmdirt09a": ("sand_stein", 4, "sand_stein_hell_beige_3"),
    "tmdirt10a": ("sand_stein", 4, "sand_stein_hell_beige"),
    "tmdirt11a": ("sand_stein", 4, "sand_stein_hell_beige_4"),
    "tmdirt12a": ("sand_stein", 4, "sand_stein_hell_gelb_3"),
    "tmdirt13a": ("sand_stein", 2, "sand_stein_hell_gelb_2"),
    "tmdirt14a": ("kies", 4, "kies_mittel_beige_2"),
    "tmdirt15a": ("rissboden", 4, "rissboden_hell_grau_2"),
    "tmgras02a": ("gras_fein", 4, "gras_fein_mittel_gruen"),
    "tmgras03a": ("gras", 4, "gras_mittel_oliv_2"),
    "tmgras04a": ("gras", 4, "gras_mittel_oliv"),
    "tmgras04b": ("trockengras", 4, "trockengras_mittel_gelb_2"),
    "tmgras05a": ("trockengras", 4, "trockengras_mittel_beige_2"),
    "tmgras06a": ("trockengras", 4, "trockengras_mittel_beige_3"),
    "tmgras07a": ("trockengras", 4, "trockengras_mittel_gruen"),
    "tmgras08a": ("gras", 4, "gras_mittel_gruen_3"),
    "tmgras09a": ("gras", 4, "gras_dunkel_gruen_2"),
    "tmgras10a": ("trockengras", 4, "trockengras_dunkel_beige_2"),
    "tmgras11a": ("gras", 4, "gras_mittel_gruen_2"),
    "tmgras11b": ("gras", 4, "gras_mittel_gruen_14"),
    "tmgras12a": ("gras", 4, "gras_mittel_gruen_8"),
    "tmgras12b": ("trockengras", 4, "trockengras_mittel_beige"),
    "tmgras13a": ("gras", 4, "gras_mittel_gruen_10"),
    "tmgras13b": ("trockengras", 4, "trockengras_dunkel_beige"),
    "tmgras14a": ("gras", 4, "gras_mittel_gruen_4"),
    "tmgras14b": ("trockengras", 4, "trockengras_dunkel_gelb"),
    "tmgras14c": ("gras", 4, "gras_mittel_gruen_15"),
    "tmgras15a": ("gras", 4, "gras_dunkel_gruen_5"),
    "tmgras16a": ("gras", 4, "gras_mittel_gruen_12", ("fels_boden", "bewuchs_stein", 0.10)),
    "tmgras17a": ("trockengras", 4, "trockengras_mittel_beige", ("fels_boden", "bewuchs_stein_hell", 0.20)),
    "tmgras18a": ("gras", 4, "gras_mittel_gruen_10"),
    "tmgras19a": ("gras", 4, "gras_mittel_gruen_13"),
    "tmgras20a": ("trockengras", 4, "trockengras_mittel_grau"),
    "tmgras21a": ("gras", 4, "gras_mittel_gruen_5", ("fels_boden", "bewuchs_stein_hell", 0.20)),
    "tmgras22a": ("gras", 4, "gras_mittel_gelb"),
    "tmgras23a": ("gras", 4, "gras_mittel_gruen_11"),
    "tmgras24a": ("trockengras", 4, "trockengras_dunkel_oliv", ("fels_boden", "bewuchs_stein", 0.35)),
    "tmgras24b": ("trockengras", 4, "trockengras_mittel_gelb", ("fels_boden", "bewuchs_stein_hell", 0.35)),
    "tmgras25a": ("gras", 4, "gras_mittel_gruen_6", ("fels_boden", "bewuchs_stein", 0.20)),
    "tmgras26a": ("trockengras", 4, "trockengras_mittel_oliv"),
    "tmgras27a": ("gras", 4, "gras_mittel_gruen_12"),
    "tmgras28a": ("gras", 4, "gras_mittel_gruen_4", ("fels_boden", "bewuchs_stein", 0.20)),
    "tmgras28b": ("trockengras", 4, "trockengras_hell_beige_2"),
    "tmgras28c": ("gras", 4, "gras_mittel_gruen_8", ("fels_boden", "bewuchs_stein", 0.30)),
    "tmgras29a": ("trockengras", 4, "trockengras_mittel_beige_3"),
    "tmgras30a": ("trockengras", 4, "trockengras_hell_beige"),
    "tmgras31a": ("gras", 4, "gras_mittel_gruen_2"),
    "tmgras32a": ("gras", 4, "gras_mittel_gruen_3"),
    "tmgras33a": ("trockengras", 4, "trockengras_mittel_gelb_2"),
    "tmgras34a": ("gras", 4, "gras_mittel_gruen_7"),
    "tmgras35a": ("gras", 4, "gras_mittel_gruen_14"),
    "tmgras36a": ("trockengras", 4, "trockengras_hell_beige"),
    "tmgras37a": ("gras", 4, "gras_mittel_gruen"),
    "tmgras37b": ("gras", 4, "gras_dunkel_gruen_3"),
    "tmgras37c": ("gras", 4, "gras_dunkel_gruen", ("fels_boden", "bewuchs_stein_hell", 0.15)),
    "tmgras37d": ("trockengras", 4, "trockengras_dunkel_gelb"),
    "tmgras37e": ("gras", 4, "gras_mittel_gruen_14"),
    "tmgras37f": ("gras", 4, "gras_mittel_gruen_9"),
    "tmgras37g": ("gras", 4, "gras_dunkel_gruen_6"),
    "tmgras38": ("gras", 4, "gras_dunkel_gruen_2"),
    "tmrock01a": ("geroell", 4, "geroell_hell_beige"),
    "tmrock01b": ("geroell", 4, "geroell_hell_grau"),
    "tmrock01c": ("geroell", 4, "geroell_mittel_grau_2"),
    "tmsand01a": ("sand_stein", 4, "sand_stein_mittel_oliv"),
    "tmsand02a": ("sand", 4, "sand_hell_beige_2"),
    "tmsand03a": ("sand_stein", 4, "sand_stein_hell_beige_4"),
    "tmsand03b": ("sand_stein", 4, "sand_stein_hell_beige_10"),
    "tmsand04a": ("sand_stein", 4, "sand_stein_hell_beige_11"),
    "tmsand04b": ("sand", 4, "sand_hell_beige_6"),
    "tmsand04c": ("sand_stein", 4, "sand_stein_hell_beige_7"),
    "tmsand04d": ("sand_stein", 4, "sand_stein_hell_beige_7", ("trockengras", "bewuchs_oliv", 0.25)),
    "tmsand04e": ("sand_stein", 4, "sand_stein_hell_beige_14", ("gras", "bewuchs_gelbgruen", 0.30)),
    "tmsand07a": ("sand_stein", 4, "sand_stein_hell_gelb_4", ("trockengras", "bewuchs_oliv", 0.40)),
    "tmsand08a": ("sand_stein", 4, "sand_stein_hell_beige_7", ("trockengras", "bewuchs_oliv", 0.25)),
    "tmsand09a": ("sand_stein", 4, "sand_stein_hell_gelb_3"),
    "tmsand09b": ("kies", 4, "kies_mittel_blau"),
    "tmsand09c": ("kies", 4, "kies_mittel_blau_2"),
    "tmsand09d": ("sand_stein", 4, "sand_stein_hell_blau"),
    "tmsand09e": ("sand", 4, "sand_hell_grau"),
    "tmsand09f": ("sand_stein", 4, "sand_stein_hell_beige"),
    "tmsand09g": ("sand_stein", 4, "sand_stein_hell_gelb", ("schnee", "bewuchs_schnee", 0.35)),
    "tmsand09h": ("sand_stein", 4, "sand_stein_hell_gelb_4", ("schnee", "bewuchs_schnee", 0.50)),
    "tmsand09i": ("sand_stein", 4, "sand_stein_hell_gelb_5", ("schnee", "bewuchs_schnee", 0.60)),
    "tmsand10a": ("sand_stein", 4, "sand_stein_hell_beige_8", ("gras", "bewuchs_gruen", 0.10)),
    "tmsand10b": ("sand_stein", 4, "sand_stein_hell_beige_5"),
    "tmsand11a": ("sand_stein", 4, "sand_stein_hell_gelb_2"),
    "tmsand12a": ("sand_stein", 4, "sand_stein_hell_beige_5", ("trockengras", "bewuchs_oliv", 0.30)),
    "tmsand13a": ("sand", 4, "sand_hell_beige_9"),
    "tmsand13b": ("sand", 4, "sand_hell_beige_8"),
    "tmsand13c": ("sand_stein", 4, "sand_stein_hell_beige_14"),
    "tmsand13d": ("sand_stein", 4, "sand_stein_hell_beige_13"),
    "tmsand13e": ("sand_stein", 4, "sand_stein_hell_beige_13"),
    "tmsand13f": ("sand_stein", 4, "sand_stein_hell_beige_6", ("gras", "bewuchs_gelbgruen", 0.35)),
    "tmsand13g": ("sand_stein", 4, "sand_stein_hell_beige_15", ("gras", "bewuchs_gelbgruen", 0.20)),
    "tmsand13h": ("sand_stein", 4, "sand_stein_hell_beige_15", ("gras", "bewuchs_gelbgruen", 0.30)),
    "tmsand14": ("sand", 4, "sand_hell_grau"),
    "tmsnow01a": ("schnee", 4, "schnee_hell_grau_3"),
    "tmsnow02a": ("schnee", 4, "schnee_hell_grau_7"),
    "tmsnow03a": ("schnee", 4, "schnee_hell_grau", ("trockengras", "bewuchs_gelbgruen", 0.35)),
    "tmsnow03b": ("schnee", 4, "schnee_hell_grau_4", ("trockengras", "bewuchs_gelbgruen", 0.20)),
    "tmsnow03c": ("schnee", 4, "schnee_hell_grau_5", ("trockengras", "bewuchs_gelbgruen", 0.10)),
    "trtasflt": ("asphalt", 1, "asphalt_mittel_grau_3"),
    "trtasflt2": ("asphalt", 1, "asphalt_mittel_grau_4"),
    "trwood1": ("holz", 1, "holz_mittel_beige"),
    "txasph01a": ("asphalt", 2, "asphalt_mittel_oliv"),
    "txasph02a": ("asphalt", 2, "asphalt_mittel_grau_2"),
    "txasph03a": ("asphalt", 2, "asphalt_mittel_grau"),
    "txcobb01a": ("pflaster", 2, "pflaster_mittel_beige_3"),
    "txconc01a": ("beton", 2, "beton_mittel_grau"),
    "txconc02a": ("beton", 2, "beton_mittel_grau_3"),
    "txconc03a": ("beton", 2, "beton_hell_grau_3"),
    "txconc04a": ("beton", 2, "beton_mittel_oliv"),
    "txconc05a": ("beton", 2, "beton_hell_grau"),
    "txconc06a": ("beton", 2, "beton_hell_grau_4"),
    "txconc07a": ("beton", 2, "beton_hell_grau_5"),
    "txdirt01a": ("kies", 2, "kies_mittel_beige"),
    "txdirt02a": ("kies", 2, "kies_dunkel_rot_2"),
    "txdirt03a": ("kies", 2, "kies_dunkel_rot"),
    "txgras01a": ("gras", 2, "gras_dunkel_gruen_4"),
    "txpklt01a": ("asphalt", 2, "asphalt_mittel_oliv_2"),
    "txpklt01b": ("asphalt", 2, "asphalt_mittel_oliv_2"),
    "txpklt01c": ("asphalt", 2, "asphalt_mittel_oliv_2"),
    "txpklt01d": ("asphalt", 2, "asphalt_mittel_oliv_2"),
    "txrock01a": ("geroell", 2, "geroell_mittel_grau"),
    "txrock02a": ("geroell", 2, "geroell_mittel_grau_3"),
    "txrock03a": ("geroell", 2, "geroell_mittel_oliv"),
    "txrock03b": ("geroell", 2, "geroell_mittel_blau"),
    "txrock04a": ("geroell", 2, "geroell_mittel_oliv_2"),
    "txrock05a": ("platten", 2, "platten_hell_beige"),
    "txrock05b": ("platten", 2, "platten_hell_rot"),
    "txrock06a": ("geroell", 2, "geroell_mittel_beige"),
    "txrock07a": ("platten", 2, "platten_hell_grau"),
    "txsand01a": ("kies", 2, "kies_mittel_beige_3"),
    "txsand01b": ("kies", 2, "kies_mittel_beige"),
    "txsand01c": ("kies", 2, "kies_dunkel_rot"),
    "txsand02a": ("sand", 2, "sand_hell_beige"),
    "txsand03a": ("sand", 2, "sand_hell_beige"),
    "txsand04a": ("sand", 2, "sand_hell_gelb_2"),
    "txsand04b": ("sand", 2, "sand_hell_gelb"),
    "txsand04c": ("sand", 2, "sand_mittel_gelb"),
    "txsand05a": ("sand", 2, "sand_hell_beige_3"),
    "txsand07a": ("gift", 2, "gift_mittel_rot"),
    "txsand08a": ("gift", 2, "gift_hell_gelb"),
    "txsand09a": ("gift", 2, "gift_dunkel_rot"),
    "txsnow01a": ("schnee", 2, "schnee_hell_grau_3", ("trockengras", "bewuchs_oliv", 0.12)),
    "txsnow02a": ("schnee", 2, "schnee_hell_grau_2"),
    "txsnow03a": ("schnee", 2, "schnee_hell_grau_6", ("trockengras", "bewuchs_oliv", 0.12)),
    "txsnow04a": ("schnee", 2, "schnee_hell_grau_7"),
    "txsnow05a": ("schnee", 2, "schnee_hell_grau_7"),
    "txwood01a": ("schnitzel", 2, "schnitzel_mittel_beige"),
    "txwood01b": ("schnitzel", 2, "schnitzel_mittel_beige_2"),
}


# -------------------------------------------------------------------------------------------------
# Scans und Mischung
# -------------------------------------------------------------------------------------------------
_CACHE = {}


def scan_karten(quelle, aid, download, n):
    """Farbe (RGB 0..1), Hoehe (0..1, Mittel 0,5), Umgebungsverdeckung und Relief eines Scans, auf n x n Pixel."""
    key = (quelle, aid, n)
    if key in _CACHE:
        return _CACHE[key]
    d = os.path.join(download, quelle, aid)
    farbe = bd.lies(os.path.join(d, "farbe.png"))
    groesse = farbe.shape[0]

    def auf(x):
        if x.shape[0] != groesse:
            x = np.asarray(Image.fromarray(x.astype(np.float32), mode="F").resize((groesse, groesse), Image.BILINEAR),
                           dtype=np.float64)
        return x

    def klein(x):
        if x.ndim == 3:
            return np.stack([klein(x[..., c]) for c in range(x.shape[2])], axis=-1)
        im = Image.fromarray(x.astype(np.float32), mode="F")
        return np.asarray(im.resize((n, n), Image.LANCZOS), dtype=np.float64)

    schatten = np.ones(farbe.shape[:2])
    if os.path.exists(os.path.join(d, "ao.png")):
        ao = auf(bd.lies(os.path.join(d, "ao.png"), grau=True))
        schatten = schatten * np.clip(ao, 0.05, 1.0)
    rel = np.ones(farbe.shape[:2])
    if os.path.exists(os.path.join(d, "normal.png")):
        nm = bd.lies(os.path.join(d, "normal.png")) * 2.0 - 1.0
        if nm.shape[0] != groesse:
            nm = np.stack([auf(nm[..., c]) for c in range(3)], axis=-1)
        nm = nm / np.maximum(np.linalg.norm(nm, axis=2, keepdims=True), 1e-6)
        licht = np.array([-0.45, 0.45, 0.77])
        licht = licht / np.linalg.norm(licht)
        rel = np.clip((nm * licht).sum(axis=2), 0.0, 1.0) / licht[2]
    hoehe = np.full(farbe.shape[:2], 0.5)
    if os.path.exists(os.path.join(d, "hoehe.png")):
        hoehe = auf(bd.lies(os.path.join(d, "hoehe.png"), grau=True))
        hoehe = (hoehe - hoehe.mean()) / max(hoehe.std(), 1e-6) * 0.15 + 0.5
    out = (np.clip(klein(farbe), 0, 1), klein(hoehe), np.clip(klein(schatten), 0.05, 1), np.clip(klein(rel), 0.05, 2))
    _CACHE[key] = out
    return out


def scan_relief(quelle, aid, download, n):
    """Relief eines Scans auf n x n Pixel: (x, y, glanz) mit x, y der Normalen (OpenGL: y zeigt im Bild nach oben,
    x = -dh/dx) und glanz = 1 - Rauheit (0 ohne Rauheitskarte). Ohne Normalenkarte flach."""
    key = ("relief", quelle, aid, n)
    if key in _CACHE:
        return _CACHE[key]
    d = os.path.join(download, quelle, aid)

    def klein(x):
        im = Image.fromarray(x.astype(np.float32), mode="F")
        return np.asarray(im.resize((n, n), Image.LANCZOS), dtype=np.float64)

    out = np.zeros((n, n, 3))
    if os.path.exists(os.path.join(d, "normal.png")):
        nm = bd.lies(os.path.join(d, "normal.png")) * 2.0 - 1.0
        nm = nm / np.maximum(np.linalg.norm(nm, axis=2, keepdims=True), 1e-6)
        out[..., 0] = klein(nm[..., 0])
        out[..., 1] = klein(nm[..., 1])
    if os.path.exists(os.path.join(d, "rauheit.png")):
        out[..., 2] = np.clip(1.0 - klein(bd.lies(os.path.join(d, "rauheit.png"), grau=True)), 0.0, 1.0)
    _CACHE[key] = out
    return out


def rauschen_periodisch(n, frequenz, saat):
    """Weiches, periodisches Rauschen (Mittel 0, Streuung 1) ueber n x n, Strukturen ~ n/frequenz."""
    rng = np.random.default_rng(saat)
    x = rng.standard_normal((n, n))
    x = bd.gauss_periodisch(x, n / (frequenz * 4.0))
    return (x - x.mean()) / max(x.std(), 1e-9)


def lege_resize(x, n_x, n_y):
    return np.asarray(Image.fromarray(x.astype(np.float32), mode="F").resize((n_x, n_y), Image.LANCZOS), dtype=np.float64)


def lege(karte, wdh_x, n_x, n_y, drehen, versatz):
    """Scan-Karte (n x n) mit wdh Wiederholungen quer ueber eine n_x x n_y-Textur legen."""
    k = np.rot90(karte, 1, axes=(0, 1)) if drehen else karte
    k = np.roll(k, (k.shape[0] * versatz[0]) // 4, axis=0)
    k = np.roll(k, (k.shape[1] * versatz[1]) // 4, axis=1)
    reps_y = max(1, int(round(wdh_x * n_y / n_x)))
    t = np.tile(k, (reps_y, wdh_x) + (1,) * (k.ndim - 2))
    if t.shape[0] != n_y or t.shape[1] != n_x:
        if t.ndim == 3:
            return np.stack([lege_resize(t[..., c], n_x, n_y) for c in range(t.shape[2])], axis=-1)
        return lege_resize(t, n_x, n_y)
    return t


def normiert(farbe, glatt):
    """Farbe einer Lage als Faktor um 1 je Kanal: grosse Flecken des Scans gedaempft (periodischer Tiefpass im
    Logarithmus, Anteil `glatt` entfernt), Mittel je Kanal 1. Beide Lagen haben so dieselbe mittlere Farbe; die
    Mischung wechselt nur die Struktur, nicht die Farbe (sonst entstehen Tarnflecken)."""
    lg = np.log(np.maximum(farbe, 1e-3))
    n = farbe.shape[0]
    for c in range(3):
        lg[..., c] -= glatt * bd.gauss_periodisch(lg[..., c], n / 10.0)
    f = np.exp(lg)
    return f / f.reshape(-1, 3).mean(axis=0)


def dunkle_flecken_weg(farbe, staerke):
    """Dunkle Flecken mittlerer Groesse (Flecken, Schatten von Einzelsteinen) aufhellen, feine Linien (Risse) und
    grosse Formen bleiben: Bandpass der Log-Helligkeit zwischen n/150 und n/8, nur der dunkle Teil, Anteil `staerke`."""
    n = farbe.shape[0]
    lg = np.log(np.maximum(lum601(farbe), 1e-3))
    band = bd.gauss_periodisch(lg, n / 150.0) - bd.gauss_periodisch(lg, n / 8.0)
    return np.clip(farbe * np.exp(-staerke * np.minimum(band, 0.0))[..., None], 0.0, 1.0)


def einsprengsel(W, kacheln, arten, saat):
    """Faktorbild (W x W) mit verstreuten Punkten: je Art Dichte (Punkte je SD-Kachel), Radius (SD-Pixel), Faktor
    der Helligkeit im Punkt und wahlweise Relief (Licht links oben, Schatten rechts unten). Periodisch, eigene
    Zufallszahlen."""
    rng = np.random.default_rng(saat)
    log_f = np.zeros((W, W))
    # unregelmaessige Raender: feines periodisches Rauschen verschiebt die Schwelle je Punkt
    rand = bd.gauss_periodisch(rng.standard_normal((W, W)), 0.6 * MAX_S)
    rand = rand / max(rand.std(), 1e-9)
    for art in arten:
        anzahl = int(round(art["dichte"] * kacheln * kacheln))
        sigma = art["r"] * MAX_S
        for teil in range(3):					# drei Groessen je Art
            groesse = sigma * (0.7, 1.0, 1.4)[teil]
            impulse = np.zeros((W, W))
            ys = rng.integers(0, W, anzahl // 3 + 1)
            xs = rng.integers(0, W, anzahl // 3 + 1)
            np.add.at(impulse, (ys, xs), rng.uniform(0.6, 1.0, len(ys)))
            g = bd.gauss_periodisch(impulse, groesse) * (2 * np.pi * groesse ** 2)
            # Scheibe mit weichem Rand von rund einem UHD-Pixel statt Gauss-Glocke
            fleck = np.clip((g + 0.08 * rand - 0.3) / 0.12, 0.0, 1.0)
            log_f += np.log(art["hell"]) * fleck
            if art.get("relief"):
                d = max(1, int(round(groesse * 0.8)))
                schatten = np.roll(np.roll(fleck, d, axis=0), d, axis=1) * (1.0 - fleck)
                log_f += np.log(0.72) * schatten
    return np.exp(log_f)


def maske_mit_anteil(roh, anteil, weich):
    """Weiche Maske 0..1 aus einem Feld `roh`, Schwelle so gesucht, dass das Mittel `anteil` ist."""
    lo, hi = float(roh.min()) - 1.0, float(roh.max()) + 1.0
    s = max(weich * 0.25, 1e-3)
    for _ in range(40):
        mid = 0.5 * (lo + hi)
        w = 1.0 / (1.0 + np.exp(-np.clip((roh - mid) / s, -60, 60)))
        if w.mean() > anteil:
            lo = mid
        else:
            hi = mid
    return w


# Parkplatzmarkierung: eigene Geometrie in handelsueblichen Massen (Stellplatz 2,5 x 5 m, eine Kachel ~ 5 m),
# nicht aus den EA-Bildern. Je TGA die Seite der Randlinie; die Stelllinien stehen senkrecht darauf, um einen halben
# Stellplatz gegen den Texturrand versetzt (so schliessen gleich ausgerichtete Kacheln nahtlos an).
# Linienbild aus einem CC0-Scan (ambientCG RoadLines003, Farbe und Deckkraft einer abgefahrenen weissen Linie).
KACHEL_M = 5.0
LINIEN = {"txpklt01a": "oben", "txpklt01b": "rechts", "txpklt01c": "unten", "txpklt01d": "links"}
LINIE = {"scan": ("ambientcg", "RoadLines003"), "breite_m": 0.15, "stand_m": 2.5, "laenge_m": 4.8, "rand_m": 0.12,
         "ton": "markierung_weiss", "glanz": 0.3}

# Acker: Pflanzenreihen als gerechnete Streifenmaske (eigene Zahlen): Reihen je 4 Kacheln, Breite der Reihe als Anteil
# der Periode, Welligkeit; der Bewuchs aus ZUORDNUNG folgt dieser Maske statt der Fleckenmaske.
REIHEN = {"tmcrops01a": {"reihen": 10, "breite": 0.5, "wellig": 0.04}}


def linien_bild(name, W, kacheln, download):
    """(Farbe W x W x 3, Deckkraft W x W) der Parkplatzlinien fuer `name` in der groessten Stufe."""
    seite = LINIEN[name]
    d = os.path.join(download, *LINIE["scan"])
    farbe = bd.lies(os.path.join(d, "farbe.png"))
    deck = bd.lies(os.path.join(d, "deckkraft.png"), grau=True)
    px_m = W / (kacheln * KACHEL_M)
    breite = max(2, int(round(LINIE["breite_m"] * px_m)))
    # Streifen (laenge senkrecht im Scan) auf Linienbreite bringen; Laenge im selben Massstab
    sh, sw = deck.shape
    lang = max(4, int(round(sh * breite / sw)))
    streifen_f = np.stack([lege_resize(farbe[..., c], breite, lang) for c in range(3)], axis=-1)
    streifen_d = lege_resize(deck, breite, lang)
    out_f = np.zeros((W, W, 3))
    out_d = np.zeros((W, W))

    def setze(x0, y0, laenge, senkrecht, versatz):
        """Linie ab (x0, y0) mit `laenge` Pixeln; senkrecht = entlang y."""
        idx = (np.arange(laenge) + versatz) % lang
        f = streifen_f[idx]			# laenge x breite x 3
        dd = streifen_d[idx]
        if not senkrecht:
            f = np.transpose(f, (1, 0, 2))
            dd = dd.T
        hh, ww = dd.shape
        ys = (np.arange(hh) + y0) % W
        xs = (np.arange(ww) + x0) % W
        alt = out_d[np.ix_(ys, xs)]
        neu = np.maximum(alt, dd)
        out_f[np.ix_(ys, xs)] = np.where((dd > alt)[..., None], f, out_f[np.ix_(ys, xs)])
        out_d[np.ix_(ys, xs)] = neu

    rand = int(round(LINIE["rand_m"] * px_m))
    stand = LINIE["stand_m"] * px_m
    laenge = int(round(LINIE["laenge_m"] * px_m))
    n = int(round(W / stand))
    rng = np.random.default_rng(saat_fuer(name) + 31)
    # Randlinie entlang der Seite, ueber die ganze Breite
    if seite in ("oben", "unten"):
        y = rand if seite == "oben" else W - rand - breite
        setze(0, y, W, False, int(rng.integers(lang)))
        for i in range(n):
            x = int(round((i + 0.5) * stand)) - breite // 2
            y0 = y + breite if seite == "oben" else y - laenge
            setze(x, y0, laenge, True, int(rng.integers(lang)))
    else:
        x = rand if seite == "links" else W - rand - breite
        setze(x, 0, W, True, int(rng.integers(lang)))
        for i in range(n):
            y = int(round((i + 0.5) * stand)) - breite // 2
            x0 = x + breite if seite == "links" else x - laenge
            setze(x0, y, laenge, False, int(rng.integers(lang)))
    return out_f, out_d


def reihen_maske(name, W, kacheln, saat):
    """Weiche Streifenmaske (0..1) der Pflanzenreihen, waagrecht, periodisch ueber die Textur."""
    p = REIHEN[name]
    n = max(1, int(round(p["reihen"] * kacheln / 4.0)))
    y = np.arange(W)[:, None] / W
    welle = p["wellig"] * rauschen_periodisch(W, 2, saat + 41) / n
    phase = (y + welle) * n % 1.0
    abstand = np.abs(phase - 0.5) * 2.0		# 0 in der Reihenmitte, 1 zwischen den Reihen
    roh = (p["breite"] - abstand) + 0.12 * rauschen_periodisch(W, 6 * kacheln, saat + 43)
    return 1.0 / (1.0 + np.exp(-roh / 0.04))


def familie_bild(fam, W, H, kacheln, download, saat, scans=None, relief=False):
    """(Faktorbild h x w x 3 mit Mittel je Kanal etwa 1, Hoehe h x w) einer Familie in der groessten Stufe.
    relief: statt des Faktorbilds das Relief (x, y, glanz wie scan_relief), gelegt und gemischt wie die Farbe --
    gleiche Lagen, Drehung, Versatz und Mischmaske, damit Relief und Farbe deckungsgleich sind."""
    p = FAMILIEN[fam]
    wdh0 = max(1, int(round(p["wdh"] * kacheln / 4.0)))
    lagen = []
    for i, (quelle, aid) in enumerate(scans or p["scans"]):
        # die zweite Lage eine Wiederholung weniger: in grossen Texturen (6 x 6 Kacheln) wiederholen sich die Lagen
        # dann nicht im selben Takt
        wdh = wdh0 if i == 0 or wdh0 < 2 else wdh0 - 1
        n = int(np.ceil(W / wdh))
        farbe, hoehe, ao, rel = scan_karten(quelle, aid, download, n)
        if relief:
            dreh = i % 2 == 1
            vers = (i + saat % 3, 2 * i + (saat // 3) % 3)
            r = lege(scan_relief(quelle, aid, download, n), wdh, W, H, dreh, vers)
            if dreh:
                # np.rot90 dreht das Bild gegen den Uhrzeigersinn; die Normale dreht mit: (x, y) -> (-y, x)
                r = np.stack([-r[..., 1], r[..., 0], r[..., 2]], axis=-1)
            lagen.append((r, lege(hoehe, wdh, W, H, dreh, vers)))
            continue
        schatt = np.power(ao, p["ao"]) * np.power(rel, p["rel"])
        farbe = farbe * schatt[..., None]
        if p.get("flecken_weg"):
            farbe = dunkle_flecken_weg(farbe, p["flecken_weg"])
        farbe = normiert(farbe, p.get("glatt", 0.7))
        dreh = i % 2 == 1
        vers = (i + saat % 3, 2 * i + (saat // 3) % 3)
        lagen.append((lege(farbe, wdh, W, H, dreh, vers), lege(hoehe, wdh, W, H, dreh, vers)))
    if len(lagen) == 1:
        return lagen[0]
    maske = rauschen_periodisch(max(W, H), p["maske"], saat)
    if W != H:
        maske = lege_resize(maske, W, H)
    roh = maske * 0.25 + (lagen[1][1] - lagen[0][1]) * p["hoehe"]
    w = maske_mit_anteil(roh, p["anteil"], p["weich"])
    f = lagen[0][0] * (1 - w[..., None]) + lagen[1][0] * w[..., None]
    h = lagen[0][1] * (1 - w) + lagen[1][1] * w
    return f, h


def lum601(x):
    return 0.299 * x[..., 0] + 0.587 * x[..., 1] + 0.114 * x[..., 2]


def faerben(faktor, ton, streu, farbe=FARBE):
    """Faktorbild auf mittlere Farbe `ton` (0..1 je Kanal) und Helligkeitsstreuung `streu` (in SD) bringen.
    Im Logarithmus: Helligkeit des Faktors hoch k, Farbabweichung zu FARBE gedaempft."""
    ton = np.asarray(ton, dtype=np.float64)
    lg_l = np.log(np.maximum(lum601(faktor), 1e-4))[..., None]
    lg_c = np.log(np.maximum(faktor, 1e-4)) - lg_l
    lo, hi = 0.2, 3.0
    out = None
    for _ in range(25):
        k = 0.5 * (lo + hi)
        out = np.exp(lg_l * k + lg_c * farbe)
        out = ton * out / bd.box(out, MAX_S).reshape(-1, 3).mean(axis=0)
        if lum601(bd.box(np.clip(out, 0, 1), MAX_S)).std() < streu:
            lo = k
        else:
            hi = k
    for _ in range(3):					# Klemmung verschiebt das Mittel etwas: nachziehen
        out = np.clip(out, 0.0, 1.0)
        out = out * (ton / np.maximum(bd.box(out, MAX_S).reshape(-1, 3).mean(axis=0), 1e-6))
    return np.clip(out, 0.0, 1.0), k


def ton_rgb(name):
    return np.asarray(TOENE[name], dtype=np.float64) / 255.0


def saat_fuer(name):
    return zlib.crc32(name.encode("ascii")) % 100000


def textur(name, download, scans=None, relief=False):
    """Ersatztextur (UHD, float 0..1) aus den eigenen Werten der Zuordnung. Liest keine EA-Datei.
    relief: statt der Farbe das Relief (x, y, glanz) und die gemischte Hoehe, deckungsgleich zur Farbe
    (Rueckgabe bild, info, hoehe); Einsprengsel bleiben dort flach, Linien flach mit LINIE["glanz"]."""
    e = ZUORDNUNG[name]
    fam, kacheln, ton = e[0], e[1], e[2]
    W = H = 64 * kacheln * MAX_S
    saat = saat_fuer(name)
    f, h = familie_bild(fam, W, H, kacheln, download, saat, scans, relief)
    if relief:
        bild, k = f, 0.0
    else:
        bild, k = faerben(f, ton_rgb(ton), FAMILIEN[fam]["streu"], FAMILIEN[fam].get("farbe", FARBE))
    info = {"familie": fam, "ton": ton, "kacheln": kacheln, "k": round(k, 3)}
    if len(e) > 3 and e[3]:
        bfam, bton, banteil = e[3]
        pb = FAMILIEN[bfam]
        fb, hb = familie_bild(bfam, W, H, kacheln, download, saat + 7, relief=relief)
        if relief:
            bb, kb = fb, 0.0
        else:
            bb, kb = faerben(fb, ton_rgb(bton), pb["streu"], pb.get("farbe", FARBE))
        # Flecken je 4 Kacheln wie die Familie sie vorgibt; die Hoehenkarten zerfransen die Raender
        maske = rauschen_periodisch(W, pb.get("flecken", 6) * kacheln / 4.0, saat + 13)
        fein = rauschen_periodisch(W, pb.get("flecken", 6) * kacheln, saat + 17)
        roh = maske * 0.25 + fein * pb.get("sprenkel", 0.0) + (hb - h) * 1.0
        w = maske_mit_anteil(roh, banteil, 0.15)
        if name in REIHEN:
            w = reihen_maske(name, W, kacheln, saat) * np.clip(0.9 + (hb - h) + 0.1 * fein, 0.0, 1.0)
        bild = bild * (1 - w[..., None]) + bb * w[..., None]
        info.update({"bewuchs": bfam, "bewuchs_ton": bton, "anteil": banteil, "k_bewuchs": round(kb, 3)})
        if relief:
            h = h * (1 - w) + hb * w
    if relief:
        if name in LINIEN:
            ld = linien_bild(name, W, kacheln, download)[1][..., None]
            bild = bild * (1 - ld) + np.array([0.0, 0.0, LINIE["glanz"]]) * ld
        return bild, info, h
    if FAMILIEN[fam].get("punkte"):
        mittel = bild.reshape(-1, 3).mean(axis=0)
        bild = np.clip(bild * einsprengsel(W, kacheln, FAMILIEN[fam]["punkte"], saat + 23)[..., None], 0.0, 1.0)
        for _ in range(3):				# Mittel wie vor den Punkten
            bild = np.clip(bild * (mittel / np.maximum(bild.reshape(-1, 3).mean(axis=0), 1e-6)), 0.0, 1.0)
    if name in LINIEN:
        lf, ld = linien_bild(name, W, kacheln, download)
        # Linienfarbe: Helligkeitsverlauf des Scans um den eigenen Ton
        l = 0.299 * lf[..., 0] + 0.587 * lf[..., 1] + 0.114 * lf[..., 2]
        lm = l[ld > 0.5].mean() if (ld > 0.5).any() else 1.0
        farbe_l = np.clip(ton_rgb(LINIE["ton"]) * (l / max(lm, 1e-6))[..., None], 0.0, 1.0)
        bild = bild * (1 - ld[..., None]) + farbe_l * ld[..., None]
        info["linien"] = LINIEN[name]
    return bild, info


# -------------------------------------------------------------------------------------------------
# Ausgabe
# -------------------------------------------------------------------------------------------------
def tga_bytes(rgb8, s):
    buf = io.BytesIO()
    kw = {"id_section": b"ZHTS" + str(s).encode("ascii")} if s > 1 else {}
    Image.fromarray(np.ascontiguousarray(rgb8), "RGB").save(buf, format="TGA", **kw)
    return buf.getvalue()


def dds_bytes(rgb8, fmt, tmp, s):
    """DDS ohne Mip-Kette (die Engine baut den Atlas und seine Mips selbst) ueber texconv; gleiche Pixelgroesse wie
    die TGA, Eintrag Art/Terrain/<name>.dds (im Archiv mit Rueckstrich)."""
    src = os.path.join(tmp, "k.png")
    Image.fromarray(np.ascontiguousarray(rgb8), "RGB" if rgb8.shape[2] == 3 else "RGBA").save(src)
    subprocess.run([TEXCONV, "-nologo", "-y", "-m", "1", "-f", fmt, "-o", tmp, src], check=True,
                   stdout=subprocess.DEVNULL)
    with open(os.path.join(tmp, "k.dds"), "rb") as f:
        daten = bytearray(f.read())
    # Kachelgroesse wie bei der TGA (Kennung ZHTS<s>), hier im ersten Wort von dwReserved1 (Versatz 32);
    # texconv laesst den Bereich leer
    if daten[32:76] != bytes(44):
        raise SystemExit("DDS: dwReserved1 belegt")
    daten[32:37] = b"ZHTS" + str(s).encode("ascii")
    return bytes(daten)


def ziel_namen(a):
    namen = [n.lower().replace(".tga", "") for n in (a.nur or [])] or sorted(ZUORDNUNG)
    for n in namen:
        if n not in ZUORDNUNG:
            raise SystemExit("nicht in ZUORDNUNG: " + n)
    return namen


def _rechne(arg):
    """Eine Textur rechnen und als PNG (groesste Stufe und SD-Vorschau) ablegen; Arbeitsschritt fuer den Pool."""
    name, aus, download = arg
    bild, info = textur(name, download)
    b8 = np.clip(np.rint(bild * 255.0), 0, 255).astype(np.uint8)
    Image.fromarray(b8).save(os.path.join(aus, "png", "%s_s%d.png" % (name, MAX_S)))
    sd = bd.box(bild, MAX_S)
    Image.fromarray(np.clip(np.rint(sd * 255.0), 0, 255).astype(np.uint8)).save(
        os.path.join(aus, "png", "%s_s1.png" % name))
    info["mittel"] = [round(float(v) * 255, 1) for v in sd.reshape(-1, 3).mean(0)]
    info["streu_sd"] = round(float(lum601(sd).std()), 4)
    with open(os.path.join(aus, "png", name + ".json"), "w", encoding="utf-8") as f:
        json.dump(info, f)
    return name, info


def cmd_bauen(a):
    namen = ziel_namen(a)
    os.makedirs(os.path.join(a.aus, "png"), exist_ok=True)
    arbeit = [(n, a.aus, a.download) for n in namen]
    if a.prozesse > 1:
        import multiprocessing
        with multiprocessing.Pool(a.prozesse) as pool:
            ergebnisse = pool.imap_unordered(_rechne, arbeit)
            for name, info in ergebnisse:
                zeige(name, info)
    else:
        for arg in arbeit:
            zeige(*_rechne(arg))
    if not a.ohne_paket:
        cmd_packen(a)


def zeige(name, info):
    print("%-14s %-12s %-24s %2d Kacheln  k %.2f%s" % (
        name, info["familie"], info["ton"], info["kacheln"], info["k"],
        ("  + %s %s %.2f" % (info["bewuchs"], info["bewuchs_ton"], info["anteil"]) if "bewuchs" in info else "")),
        flush=True)


def cmd_packen(a):
    """Mod-Archive je Stufe aus den gerechneten PNGs (alle Texturen der Zuordnung, die gerechnet vorliegen)."""
    import build_texture_tiers as btt
    stufen = [int(x) for x in a.stufen.split(",")]
    eintraege = {s: [] for s in stufen}
    werte = {}
    tmp = tempfile.mkdtemp(prefix="bodenfrei_")
    endung = ".tga" if a.format == "tga" else ".dds"
    for name in sorted(ZUORDNUNG):
        png = os.path.join(a.aus, "png", "%s_s%d.png" % (name, MAX_S))
        if not os.path.exists(png):
            continue
        bild = np.asarray(Image.open(png).convert("RGB"), dtype=np.float64) / 255.0
        with open(os.path.join(a.aus, "png", name + ".json"), encoding="utf-8") as f:
            werte[name] = json.load(f)
        for s in stufen:
            f = MAX_S // s
            st = bd.box(bild, f) if f > 1 else bild
            b8 = np.clip(np.rint(st * 255.0), 0, 255).astype(np.uint8)
            if a.format == "tga":
                daten = tga_bytes(b8, s)
            else:
                daten = dds_bytes(b8, {"dds-bc1": "BC1_UNORM", "dds-bc7": "BC7_UNORM"}[a.format], tmp, s)
            eintraege[s].append(("Art\\Terrain\\" + name + endung, daten))
    for s in stufen:
        d = os.path.join(a.ziel or a.aus, "s%d" % s)
        os.makedirs(d, exist_ok=True)
        datei = os.path.join(d, "BodenFrei.big" if a.format == "tga" else "BodenFrei_%s.big" % a.format)
        btt.write_big(datei, eintraege[s])
        print("s%d: %s (%.1f MB, %d Texturen)" % (s, datei, os.path.getsize(datei) / 1e6, len(eintraege[s])))
    with open(os.path.join(a.aus, "werte.json"), "w", encoding="utf-8") as f:
        json.dump(werte, f, indent=1)


# -------------------------------------------------------------------------------------------------
# Relief: Normalen, Glanz und Hoehe je Textur (TODO 82 Stufe 1)
# -------------------------------------------------------------------------------------------------
# Datei Art/Terrain/<name>_n.dds neben der Farbtextur, BC7, gleiche Kachelkennung ZHTS<s>. Kanaele (RGBA):
#   R, G  Normale x, y in Bildachsen (OpenGL: G zeigt im Bild nach oben), Byte = 128 + 127 * Wert (128 = flach)
#   B     Glanz = 1 - Rauheit des Scans (0 = matt), weichgezeichnet (RELIEF_GLANZ_WEICH)
#   A     eingebackenes Licht: wie stark die Farbtextur auf das feste Licht von links oben (scan_karten) reagiert,
#         Steigung s der Log-Helligkeit gegen dot(Normale, LICHT_GEBACKEN) / LICHT_GEBACKEN.z, Byte = 255 * s / 4
#         (konstant je Textur). Der Shader rechnet dieses Licht heraus, bevor er mit der Sonne der Karte neu beleuchtet.
# x und y sind je Textur mittelwertfrei: die Beleuchtung aendert die mittlere Helligkeit nicht.
# BC7 mit vier unabhaengigen Kanaelen kostet Genauigkeit (mittlerer Fehler der Normalen rund 7 Stufen mit Hoehe
# in A, 3,9 ohne Hoehe und mit weichem Glanz, BC5 nur Normalen 2,3; Kies tmdirt05a). Die Hoehe fuer eine spaetere
# Tessellation gehoert deshalb in eine eigene Datei; der Glanz aendert sich ohnehin nur grossflaechig.
RELIEF_GLANZ_WEICH = 4.0		# Gauss-Sigma des Glanzes in Pixeln der Ausgabestufe
LICHT_GEBACKEN = (-0.45, 0.45, 0.77)	# Lichtrichtung der Schattierung in scan_karten (x rechts, y oben im Bild)


def gebackenes_licht(rgba, farbe):
    """Steigung der hochpassgefilterten Log-Helligkeit der Farbe gegen den Lichtterm der Normalen (gleiche Groesse)."""
    nx = (rgba[..., 0].astype(np.float64) - 128.0) / 127.0
    ny = (rgba[..., 1].astype(np.float64) - 128.0) / 127.0
    term = (LICHT_GEBACKEN[0] * nx + LICHT_GEBACKEN[1] * ny) / LICHT_GEBACKEN[2]
    lg = np.log(np.maximum(lum601(farbe), 1e-3))

    def hp(x):
        return x - bd.gauss_periodisch(x, 6.0)
    x, y = hp(term).ravel(), hp(lg).ravel()
    return max(0.0, float((x * y).sum() / max((x * x).sum(), 1e-9)))


def relief_rgba(name, download, s):
    r, _, h = textur(name, download, relief=True)
    r = r.copy()
    r[..., 0] -= r[..., 0].mean()
    r[..., 1] -= r[..., 1].mean()
    f = MAX_S // s
    if f > 1:
        r = bd.box(r, f)
    glanz = bd.gauss_periodisch(r[..., 2], RELIEF_GLANZ_WEICH)
    rgba = np.empty(r.shape[:2] + (4,), dtype=np.uint8)
    rgba[..., 0] = np.clip(np.rint(128.0 + 127.0 * r[..., 0]), 1, 255)
    rgba[..., 1] = np.clip(np.rint(128.0 + 127.0 * r[..., 1]), 1, 255)
    rgba[..., 2] = np.clip(np.rint(255.0 * glanz), 0, 255)
    rgba[..., 3] = 0
    werte = {"neigung": round(float(np.sqrt(r[..., 0] ** 2 + r[..., 1] ** 2).mean()), 4),
             "glanz": round(float(glanz.mean()), 3), "glanz_p95": round(float(np.percentile(glanz, 95)), 3)}
    return rgba, werte


def _rechne_relief(arg):
    name, ziel, aus, download, s = arg
    rgba, werte = relief_rgba(name, download, s)
    tmp = tempfile.mkdtemp(prefix="bodenrelief_")
    # eingebackenes Licht gegen die Farbtextur daneben (die ausgelieferte, geschaerfte Fassung)
    farbe_dds = os.path.join(ziel, name + ".dds")
    if os.path.exists(farbe_dds):
        farbe, _, fs = _dds_lesen(farbe_dds, tmp)
        if farbe.shape[:2] == rgba.shape[:2]:
            steigung = gebackenes_licht(rgba, farbe)
            rgba[..., 3] = int(np.clip(np.rint(255.0 * steigung / 4.0), 0, 255))
            werte["gebacken"] = round(steigung, 3)
    with open(os.path.join(ziel, name + "_n.dds"), "wb") as f:
        f.write(dds_bytes(rgba, "BC7_UNORM", tmp, s))
    if aus:
        # Vorschau: Relief im Licht von links oben (wie die Schattierung der Farbe), SD
        n = rgba[..., :2].astype(np.float64) - 128.0
        licht = np.clip(0.5 + (-0.45 * n[..., 0] + 0.45 * n[..., 1]) / 127.0 * 2.0, 0, 1)
        sd = bd.box(licht, s) if s > 1 else licht
        Image.fromarray(np.clip(np.rint(sd * 255.0), 0, 255).astype(np.uint8)).save(
            os.path.join(aus, name + "_relief_s1.png"))
    return name, werte


def cmd_relief(a):
    """Relief-Texturen (<name>_n.dds) fuer alle Texturen der Zuordnung oder --nur, Stufe --stufe."""
    namen = ziel_namen(a)
    os.makedirs(a.ziel, exist_ok=True)
    vorschau = os.path.join(a.aus, "relief")
    os.makedirs(vorschau, exist_ok=True)
    arbeit = [(n, a.ziel, vorschau, a.download, a.stufe) for n in namen]
    alle = {}
    if a.prozesse > 1:
        import multiprocessing
        with multiprocessing.Pool(a.prozesse) as pool:
            for name, werte in pool.imap_unordered(_rechne_relief, arbeit):
                alle[name] = werte
                print("%-14s %s" % (name, werte), flush=True)
    else:
        for arg in arbeit:
            name, werte = _rechne_relief(arg)
            alle[name] = werte
            print("%-14s %s" % (name, werte), flush=True)
    with open(os.path.join(vorschau, "relief_werte.json"), "w", encoding="utf-8") as f:
        json.dump(dict(sorted(alle.items())), f, indent=1)


# -------------------------------------------------------------------------------------------------
# Hoehe fuer die Tessellation (TODO 82 Stufe 2)
# -------------------------------------------------------------------------------------------------
# Datei Art/Terrain/<name>_h.dds neben der Farbtextur, BC4 (ein Kanal), Kachelkennung ZHTS<s> wie die Farbe. Byte =
# 255 * (Hoehe - Mittel + 0,5): je Textur mittelwertfrei um 128, die Engine zieht 0,5 ab und skaliert mit der Amplitude
# (TT_AMPLITUDE, Welteinheiten je Hoeheneinheit). Die Hoehe ist die der Relief-Rechnung (textur(..., relief=True)):
# gleiche Lagen, Drehungen und Mischmasken wie Farbe und Normalen. Stufe 1 reicht (32 Pixel je Zelle, 0,3 Welteinheiten
# je Pixel; der Tessellator teilt eine Zelle hoechstens in 16 Stuecke); leicht weichgezeichnet gegen Flimmern beim
# Abtasten im Domain-Shader.
HOEHE_WEICH = 1.0		# Gauss-Sigma der Hoehe in Pixeln der Ausgabestufe


def hoehe_bytes(name, download, s):
    _, _, h = textur(name, download, relief=True)
    f = MAX_S // s
    if f > 1:
        h = bd.box(h, f)
    h = bd.gauss_periodisch(h, HOEHE_WEICH)
    h = h - h.mean() + 0.5
    werte = {"streu": round(float(h.std()), 4), "min": round(float(h.min()), 3), "max": round(float(h.max()), 3),
             "geklemmt": round(float(((h < 0.0) | (h > 1.0)).mean()), 5)}
    return np.clip(np.rint(255.0 * h), 0, 255).astype(np.uint8), werte


def _rechne_hoehe(arg):
    name, ziel, aus, download, s = arg
    h8, werte = hoehe_bytes(name, download, s)
    tmp = tempfile.mkdtemp(prefix="bodenhoehe_")
    rgb = np.repeat(h8[..., None], 3, axis=2)
    with open(os.path.join(ziel, name + "_h.dds"), "wb") as f:
        f.write(dds_bytes(rgb, "BC4_UNORM", tmp, s))
    if aus:
        Image.fromarray(h8).save(os.path.join(aus, name + "_hoehe_s%d.png" % s))
    return name, werte


def cmd_hoehe(a):
    """Hoehen-Texturen (<name>_h.dds) fuer alle Texturen der Zuordnung oder --nur, Stufe --stufe."""
    namen = ziel_namen(a)
    os.makedirs(a.ziel, exist_ok=True)
    vorschau = os.path.join(a.aus, "hoehe")
    os.makedirs(vorschau, exist_ok=True)
    arbeit = [(n, a.ziel, vorschau, a.download, a.stufe) for n in namen]
    alle = {}
    if a.prozesse > 1:
        import multiprocessing
        with multiprocessing.Pool(a.prozesse) as pool:
            for name, werte in pool.imap_unordered(_rechne_hoehe, arbeit):
                alle[name] = werte
                print("%-14s %s" % (name, werte), flush=True)
    else:
        for arg in arbeit:
            name, werte = _rechne_hoehe(arg)
            alle[name] = werte
            print("%-14s %s" % (name, werte), flush=True)
    with open(os.path.join(vorschau, "hoehe_werte.json"), "w", encoding="utf-8") as f:
        json.dump(dict(sorted(alle.items())), f, indent=1)


def cmd_archiv(a):
    """Archiv aus den DDS-Dateien im Repo (Data/FreierBoden/Art/Terrain), zum Laden mit -mod."""
    import build_texture_tiers as btt
    quelle = os.path.join(a.ordner, "Art", "Terrain")
    eintraege = []
    for name in sorted(os.listdir(quelle)):
        if name.lower().endswith(".dds"):
            with open(os.path.join(quelle, name), "rb") as f:
                eintraege.append(("Art"+chr(92)+"Terrain"+chr(92) + name, f.read()))
    os.makedirs(os.path.dirname(os.path.abspath(a.ziel)), exist_ok=True)
    btt.write_big(a.ziel, eintraege)
    print("%s (%.1f MB, %d Texturen)" % (a.ziel, os.path.getsize(a.ziel) / 1e6, len(eintraege)))


# -------------------------------------------------------------------------------------------------
# Schaerfen je Mip-Stufe (TODO 81)
# -------------------------------------------------------------------------------------------------
def _hoch2(x):
    """Bilinear auf doppelte Groesse, periodischer Rand; Pixelmitten wie bei der 2x2-Mittelung (Gewichte 3/4, 1/4)."""
    for achse in (0, 1):
        vor = np.roll(x, 1, axis=achse)
        nach = np.roll(x, -1, axis=achse)
        gerade = 0.75 * x + 0.25 * vor
        ungerade = 0.75 * x + 0.25 * nach
        form = list(x.shape)
        form[achse] *= 2
        neu = np.empty(form, dtype=x.dtype)
        if achse == 0:
            neu[0::2], neu[1::2] = gerade, ungerade
        else:
            neu[:, 0::2], neu[:, 1::2] = gerade, ungerade
        x = neu
    return x


def schaerfe_mips(bild, staerke, baender=4, abfall=0.75):
    """Unscharfmaske je Mip-Stufe, eingerechnet in die Grundstufe (float 0..1, RGB, kachelnd).

    Die Engine baut den Gelaendeatlas und seine Mips beim Laden selbst (2x2-Mittelung, TerrainTex.cpp,
    Generate_Mip_Levels mit FILTER_BOX); Mips aus der DDS liest sie nicht. Deshalb wirkt das Schaerfen ueber die
    Grundstufe: Laplace-Pyramide mit derselben 2x2-Mittelung, Band k (die feinste Oktave der Mip-Stufe k) wird um
    staerke * abfall**k verstaerkt. Die Mittelung loescht die feineren Baender, also sieht jede Mip-Stufe k ihre
    eigene feinste Oktave am staerksten angehoben -- eine Unscharfmaske je Stufe ohne Laufzeitkosten. Nur die
    Helligkeit wird geschaerft (keine Farbsaeume), der Mittelwert bleibt gleich (die Baender sind mittelwertfrei).
    """
    if staerke <= 0.0:
        return bild
    lum = lum601(bild)
    h, w = lum.shape
    stufen = [lum]
    for _ in range(baender):
        g = stufen[-1]
        if g.shape[0] < 2 or g.shape[1] < 2 or g.shape[0] % 2 or g.shape[1] % 2:
            break
        stufen.append(bd.box(g, 2))
    delta = np.zeros_like(lum)
    for k in range(len(stufen) - 1):
        band = stufen[k] - _hoch2(stufen[k + 1])
        for _ in range(k):
            band = _hoch2(band)
        delta += staerke * (abfall ** k) * band[:h, :w]
    return np.clip(bild + delta[..., None], 0.0, 1.0)


def _dds_lesen(pfad, tmp):
    """DDS ueber texconv nach PNG entpacken; liefert (float RGB 0..1, Format fuer texconv, Kachelkennung s)."""
    with open(pfad, "rb") as f:
        kopf = f.read(148)
    s = int(kopf[36:37]) if kopf[32:36] == b"ZHTS" else 1
    if kopf[84:88] == b"DX10":
        dxgi = int.from_bytes(kopf[128:132], "little")
        fmt = {98: "BC7_UNORM", 99: "BC7_UNORM_SRGB", 71: "BC1_UNORM", 28: "R8G8B8A8_UNORM"}.get(dxgi)
    else:
        fmt = {b"DXT1": "BC1_UNORM"}.get(kopf[84:88])
    if fmt is None:
        raise SystemExit("%s: Format nicht unterstuetzt" % pfad)
    subprocess.run([TEXCONV, "-nologo", "-y", "-ft", "png", "-o", tmp, pfad], check=True, stdout=subprocess.DEVNULL)
    png = os.path.join(tmp, os.path.splitext(os.path.basename(pfad))[0] + ".png")
    bild = np.asarray(Image.open(png).convert("RGB"), dtype=np.float64) / 255.0
    os.remove(png)
    return bild, fmt, s


def cmd_schaerfen(a):
    """Geschaerftes Paket aus den DDS-Dateien im Repo: entpacken, je Mip-Stufe schaerfen, im selben Format neu packen.
    Schreibt nur nach --ziel (Ordner Art/Terrain und BodenFrei.big); die Quelle bleibt unberuehrt."""
    import build_texture_tiers as btt
    quelle = os.path.join(a.ordner, "Art", "Terrain")
    ziel = os.path.abspath(a.ziel)
    ordner = os.path.abspath(a.ordner).lower()
    if os.path.commonpath([ziel.lower(), ordner]) == ordner:
        raise SystemExit("--ziel liegt in der Quelle; bitte einen eigenen Ordner angeben")
    os.makedirs(os.path.join(ziel, "Art", "Terrain"), exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="bodenscharf_")
    # nur die Farbtexturen; das Relief (<name>_n.dds) wird nicht geschaerft
    namen = sorted(n for n in os.listdir(quelle) if n.lower().endswith(".dds") and not n.lower().endswith("_n.dds"))
    if a.nur:
        namen = [n for n in namen if n[:-4].lower() in {x.lower() for x in a.nur}]
    eintraege = []
    for name in namen:
        bild, fmt, s = _dds_lesen(os.path.join(quelle, name), tmp)
        scharf = schaerfe_mips(bild, a.staerke, a.baender, a.abfall)
        b8 = np.clip(np.rint(scharf * 255.0), 0, 255).astype(np.uint8)
        daten = dds_bytes(b8, fmt, tmp, s)
        with open(os.path.join(ziel, "Art", "Terrain", name), "wb") as f:
            f.write(daten)
        eintraege.append(("Art" + chr(92) + "Terrain" + chr(92) + name, daten))
        print("%-16s %s s%d  Streuung %.4f -> %.4f" % (name, fmt, s, lum601(bild).std(), lum601(scharf).std()),
              flush=True)
    datei = os.path.join(ziel, "BodenFrei.big")
    btt.write_big(datei, eintraege)
    print("%s (%.1f MB, %d Texturen, Staerke %.2f, %d Baender, Abfall %.2f)" % (
        datei, os.path.getsize(datei) / 1e6, len(eintraege), a.staerke, a.baender, a.abfall))


def groesse_schaetzen(kacheln, s, fmt):
    px = (64 * kacheln * s) ** 2
    return {"tga": 18 + 5 + px * 3, "dds-bc1": 128 + px // 2, "dds-bc7": 148 + px}[fmt]


def cmd_liste(a):
    fam_n = {}
    for name in sorted(ZUORDNUNG):
        e = ZUORDNUNG[name]
        fam_n[e[0]] = fam_n.get(e[0], 0) + 1
    print(len(ZUORDNUNG), "Texturen:", ", ".join("%s %d" % kv for kv in sorted(fam_n.items())))
    for fmt in ("tga", "dds-bc1", "dds-bc7"):
        print(fmt, "  ".join("s%d %.1f MB" % (s, sum(groesse_schaetzen(e[1], s, fmt) for e in ZUORDNUNG.values()) / 1e6)
                             for s in (1, 2, 4)))


# -------------------------------------------------------------------------------------------------
# Messwerkzeuge (lesen EA-Texturen; nur zum Vergleichen, nicht fuer den Bau)
# -------------------------------------------------------------------------------------------------
_EA = []


def ea_textur(tga):
    import texture_inventory as inv
    if not _EA:
        _EA.extend(inv.effective_files(root)[0] for root in (bd.root_zh(), bd.root_g()))
    for files in _EA:
        key = "art\\terrain\\" + tga
        if key in files:
            im = Image.open(io.BytesIO(files[key][1].read(key)))
            im.load()
            return np.asarray(im.convert("RGB"), dtype=np.float64) / 255.0
    return None


def zwei_gruppen(rgb):
    """Zwei Farbgruppen (k-Mittel auf Farbart und Helligkeit): (Mittel 1, Mittel 2, Anteil 2, Abstand der Farbart)."""
    x = rgb.reshape(-1, 3)
    l = np.maximum(lum601(x), 1e-3)[:, None]
    merk = np.concatenate([x / l, 0.5 * l], axis=1)
    c = merk[np.argsort(merk[:, 3])[[len(merk) // 5, 4 * len(merk) // 5]]]
    for _ in range(30):
        d = ((merk[:, None, :] - c[None]) ** 2).sum(-1)
        z = d.argmin(1)
        for j in range(2):
            if (z == j).any():
                c[j] = merk[z == j].mean(0)
    m1, m2 = x[z == 0].mean(0) if (z == 0).any() else x.mean(0), x[z == 1].mean(0) if (z == 1).any() else x.mean(0)
    abst = float(np.linalg.norm(c[0, :3] - c[1, :3]))
    return m1, m2, float((z == 1).mean()), abst


def cmd_messen(a):
    import texture_inventory as inv
    alle = {}
    for root in (bd.root_g(), bd.root_zh()):
        _, ini = bd.terrain_ini(root)
        alle.update(ini)
    zuo = {}
    for z in open(bd.ZIEL_INI, encoding="latin-1"):
        z = z.split(";")[0].strip()
        if "=" in z:
            k, v = [s.strip() for s in z.split("=", 1)]
            zuo[k.lower()] = v
    erg = {}
    for tga in sorted(alle):
        ea = ea_textur(tga)
        if ea is None:
            continue
        sd = ea
        m1, m2, a2, abst = zwei_gruppen(sd[::2, ::2])
        erg[tga[:-4]] = {"material": zuo.get(tga, "-"), "klasse": alle[tga][0], "kacheln": sd.shape[1] // 64,
                         "mittel": [round(float(v) * 255, 1) for v in sd.reshape(-1, 3).mean(0)],
                         "streu": round(float(lum601(sd).std()), 4),
                         "gruppe1": [round(float(v) * 255) for v in m1], "gruppe2": [round(float(v) * 255) for v in m2],
                         "anteil2": round(a2, 3), "abstand": round(abst, 3)}
        print(tga, erg[tga[:-4]], flush=True)
    os.makedirs(os.path.dirname(a.ausgabe), exist_ok=True)
    with open(a.ausgabe, "w", encoding="utf-8") as f:
        json.dump(erg, f, indent=1)


def naht(x):
    """Sprung ueber den Kachelrand (umlaufend) im Verhaeltnis zum mittleren Nachbarsprung innen; 1 = nahtlos."""
    l = lum601(x.astype(np.float64))
    innen = 0.5 * (np.abs(np.diff(l, axis=0)).mean() + np.abs(np.diff(l, axis=1)).mean())
    rand = 0.5 * (np.abs(l[0] - l[-1]).mean() + np.abs(l[:, 0] - l[:, -1]).mean())
    return float(rand / max(innen, 1e-9))


def wiederholung(x):
    """Groesste Autokorrelation der Helligkeit (Hochpass) bei Versaetzen ab 1/8 der Texturbreite: Mass fuer ein
    Muster, das sich innerhalb einer Textur wiederholt (0 = keins, 1 = exakte Wiederholung)."""
    l = lum601(x.astype(np.float64))
    l = l - bd.gauss_periodisch(l, l.shape[0] / 16.0)
    f = np.fft.fft2(l - l.mean())
    ak = np.real(np.fft.ifft2(f * np.conj(f)))
    ak /= max(ak[0, 0], 1e-9)
    n = l.shape[0]
    yy, xx = np.meshgrid(np.minimum(np.arange(n), n - np.arange(n)), np.minimum(np.arange(n), n - np.arange(n)),
                         indexing="ij")
    return float(ak[np.maximum(yy, xx) >= n // 8].max())


def cmd_vergleich(a):
    namen = ziel_namen(a)
    zeilen = []
    werte = []
    for name in namen:
        ea = ea_textur(name + ".tga")
        frei = np.asarray(Image.open(os.path.join(a.aus, "png", "%s_s1.png" % name)).convert("RGB"))
        e8 = (ea * 255).astype(np.uint8)
        if e8.shape != frei.shape:
            frei = np.asarray(Image.fromarray(frei).resize((e8.shape[1], e8.shape[0]), Image.LANCZOS))
        z = max(1, 256 // e8.shape[0])
        rep = lambda x: np.tile(x, (2 * z, 2 * z, 1))[:512, :512]
        paar = np.concatenate([rep(e8), np.full((min(512, e8.shape[0] * 2 * z), 8, 3), 30, np.uint8), rep(frei)], axis=1)
        zeilen.append(paar)
        zeilen.append(np.full((8, paar.shape[1], 3), 30, np.uint8))
        le, lf = float(lum601(ea).mean() * 255), float(lum601(frei / 255.0).mean() * 255)
        werte.append((name, le, lf))
        print("%-14s Helligkeit EA %6.1f frei %6.1f  (%+5.1f)  Naht EA %.2f frei %.2f  Wiederholung EA %.2f frei %.2f" % (
            name, le, lf, lf - le, naht(e8), naht(frei), wiederholung(e8), wiederholung(frei)))
    breite = max(z.shape[1] for z in zeilen)
    zeilen = [np.pad(z, ((0, 0), (0, breite - z.shape[1]), (0, 0)), constant_values=30) for z in zeilen]
    blatt = 12
    for i in range(0, len(zeilen), 2 * blatt):
        bild = np.concatenate(zeilen[i:i + 2 * blatt], axis=0)
        pfad = os.path.join(a.aus, "vergleich_sd_%02d.png" % (i // (2 * blatt)))
        Image.fromarray(bild).save(pfad)
        print(pfad)


def cmd_kandidaten(a):
    """Je Kandidat-Scan eine Vorschau der Familie mit nur diesem Scan, gefaerbt mit dem Ton, neben der EA-Textur."""
    os.makedirs(a.aus, exist_ok=True)
    for name in a.tga:
        name = name.lower().replace(".tga", "")
        ea = ea_textur(name + ".tga")
        spalten = [np.tile((ea * 255).astype(np.uint8), (2, 2, 1))]
        teile = [np.asarray(Image.fromarray(spalten[0]).resize((256, 256), Image.LANCZOS))]
        nah = []
        # Vorschau ohne festen Ton: mittlere Farbe der EA-Textur (nur in dieser Vorschau, nicht im Bau)
        TOENE["_ea"] = tuple(float(v) * 255 for v in ea.reshape(-1, 3).mean(0))
        for sc in a.scans:
            q, i = sc.split(":")
            ZUORDNUNG[name] = (a.familie, ea.shape[1] // 64, a.ton or "_ea")
            bild, _ = textur(name, a.download, scans=[(q, i)] + ([] if a.einzeln else FAMILIEN[a.familie]["scans"][1:]))
            sd = (bd.box(bild, MAX_S) * 255).astype(np.uint8)
            teile.append(np.asarray(Image.fromarray(np.tile(sd, (2, 2, 1))).resize((256, 256), Image.LANCZOS)))
            nah.append((bild[:256, :256] * 255).astype(np.uint8))
            print(name, sc)
        reihe = np.concatenate([np.pad(t, ((0, 4), (0, 4), (0, 0)), constant_values=30) for t in teile], axis=1)
        ea_nah = np.asarray(Image.fromarray((ea[:64, :64] * 255).astype(np.uint8)).resize((256, 256), Image.LANCZOS))
        reihe2 = np.concatenate([np.pad(t, ((0, 4), (0, 4), (0, 0)), constant_values=30) for t in [ea_nah] + nah], axis=1)
        Image.fromarray(np.concatenate([reihe, reihe2], axis=0)).save(os.path.join(a.aus, "k_%s_%s.png" % (a.familie, name)))
    print("Spalten: EA, " + ", ".join(a.scans))


def cmd_nachweis(a):
    gesehen = set()
    print("| Familie | Scan | Quelle | Autoren | Masse | Lizenz |")
    print("|---|---|---|---|---|---|")
    liste = [(fam, sc) for fam, p in FAMILIEN.items() for sc in dict.fromkeys(p["scans"])]
    liste.append(("parkplatz (Linien)", LINIE["scan"]))
    for fam, (quelle, aid) in liste:
        if True:
            with open(os.path.join(a.download, quelle, aid, "meta.json"), encoding="utf-8") as f:
                m = json.load(f)
            masse = m.get("masse_mm") or [0, 0]
            masse = ("%.1f x %.1f m" % (masse[0] / 1000.0, masse[1] / 1000.0)) if masse[0] else "k. A."
            autoren = ", ".join(m["autoren"])
            print("| %s | %s (`%s`)%s | %s, <%s> | %s | %s | CC0 1.0 |" % (
                fam, m["name"], aid, "" if aid not in gesehen else " (s. o.)", m["quelle"], m["seite"], autoren, masse))
            gesehen.add(aid)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--download", default=bd.DOWNLOAD, required=bd.DOWNLOAD is None)
    ap.add_argument("--aus", default=os.path.join(REPO, "render-out", "boden-detail3", "frei"))
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("bauen")
    s.add_argument("--stufen", default="1,2,4")
    s.add_argument("--nur", nargs="*")
    s.add_argument("--format", choices=("tga", "dds-bc1", "dds-bc7"), default="tga")
    s.add_argument("--prozesse", type=int, default=1)
    s.add_argument("--ziel", help="Ordner der Archive (Vorgabe --aus)")
    s.add_argument("--ohne-paket", action="store_true", help="nur rechnen (PNG), keine Archive")
    s = sub.add_parser("packen", help="Archive aus bereits gerechneten PNGs")
    s.add_argument("--ziel", help="Ordner der Archive (Vorgabe --aus); DDS getrennt von TGA ablegen")
    s.add_argument("--stufen", default="1,2,4")
    s.add_argument("--format", choices=("tga", "dds-bc1", "dds-bc7"), default="tga")
    s = sub.add_parser("archiv", help="Archiv aus den DDS-Dateien im Repo (Data/FreierBoden)")
    s.add_argument("--ordner", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Data", "FreierBoden"))
    s.add_argument("--ziel", required=True, help="Zieldatei .big")
    s = sub.add_parser("schaerfen", help="geschaerftes Paket (je Mip-Stufe) aus den DDS-Dateien im Repo")
    s.add_argument("--ordner", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Data", "FreierBoden"))
    s.add_argument("--ziel", required=True, help="eigener Ordner (nicht in --ordner)")
    s.add_argument("--staerke", type=float, default=0.5, help="Verstaerkung der feinsten Oktave (0 = aus)")
    s.add_argument("--baender", type=int, default=4, help="Anzahl geschaerfter Mip-Stufen")
    s.add_argument("--abfall", type=float, default=0.75, help="Faktor der Staerke je groebere Stufe")
    s.add_argument("--nur", nargs="*")
    s = sub.add_parser("relief", help="Normalen, Glanz und Hoehe je Textur als <name>_n.dds (BC7)")
    s.add_argument("--ziel", default=os.path.join(REPO, "Data", "FreierBoden", "Art", "Terrain"))
    s.add_argument("--stufe", type=int, default=2, help="Kachelstufe s (1, 2, 4); das Farbpaket ist HD (2)")
    s.add_argument("--nur", nargs="*")
    s.add_argument("--prozesse", type=int, default=1)
    s = sub.add_parser("hoehe", help="Hoehe je Textur als <name>_h.dds (BC4) fuer die Tessellation")
    s.add_argument("--ziel", default=os.path.join(REPO, "Data", "FreierBoden", "Art", "Terrain"))
    s.add_argument("--stufe", type=int, default=1, help="Kachelstufe s (1, 2, 4); 1 reicht fuer die Verschiebung")
    s.add_argument("--nur", nargs="*")
    s.add_argument("--prozesse", type=int, default=1)
    sub.add_parser("liste")
    s = sub.add_parser("vergleich")
    s.add_argument("--nur", nargs="*")
    s = sub.add_parser("messen")
    s.add_argument("--ausgabe", default=os.path.join(REPO, "render-out", "boden-detail3", "messung.json"))
    s = sub.add_parser("kandidaten")
    s.add_argument("--familie", required=True)
    s.add_argument("--ton")
    s.add_argument("--einzeln", action="store_true", help="nur der Kandidat, ohne den zweiten Scan der Familie")
    s.add_argument("--tga", nargs="+", required=True)
    s.add_argument("--scans", nargs="+", required=True)
    sub.add_parser("nachweis")
    a = ap.parse_args()
    {"bauen": cmd_bauen, "packen": cmd_packen, "liste": cmd_liste, "vergleich": cmd_vergleich, "messen": cmd_messen,
     "kandidaten": cmd_kandidaten, "nachweis": cmd_nachweis, "archiv": cmd_archiv, "schaerfen": cmd_schaerfen,
     "relief": cmd_relief, "hoehe": cmd_hoehe}[a.cmd](a)


if __name__ == "__main__":
    main()
