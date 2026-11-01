# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Testarchiv für den KI-3D-Humvee (TODO 62): Modelle AvHummer und AvHummer_D mit ihren Texturen in ein .big.
#
#   python humvee_packen.py <arbeitsordner> <ziel.big> [--groesse 512] [--textur ZHCA_AvHummerKI] [--dxt]
#
# Liest <arbeitsordner>\export\AVHUMMER.w3d (und AVHUMMER_D.w3d, falls vorhanden) sowie
# <arbeitsordner>\textur\<textur>.png (und <textur>D.png). Texturen mit ZHCA-Präfix werden als TGA 32 Bit
# unkomprimiert geschrieben wie die EA-Dateien zhca_*.tga (Kopf-Byte 17 = 8: Ursprung unten links, 8 Bit Alpha):
# die Engine färbt Pixel mit Alpha < 255 beim Laden in der Teamfarbe um (W3DAssetManager::Recolor_Texture,
# nur 16/32-Bit-Flächen) und darf sie deshalb nicht komprimieren. Andere Namen oder --dxt: DDS DXT1 mit Mips.
# Das Archiv ersetzt im Spiel Art\W3D\AvHummer.W3D und AvHummer_D.W3D (laden mit -mod <ordner des .big>;
# -mod lädt einen Ordner rekursiv -- nur ein Archiv je Ordner).
import argparse
import os
import struct
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from build_texture_tiers import build_mips, encode_dds, write_big  # noqa: E402


def tga32(rgba):
    """TGA 32 Bit unkomprimiert, Zeilen von unten nach oben, BGRA -- wie EA zhca_*.tga."""
    h, w = rgba.shape[:2]
    kopf = struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, w, h, 32, 8)
    daten = rgba[::-1, :, [2, 1, 0, 3]].tobytes()
    return kopf + daten


def textur_eintrag(pfad, name, groesse, dxt):
    bild = Image.open(pfad).convert('RGBA')
    if bild.size[0] != groesse:
        # Teamfarbe (Alpha) nicht verwischen: Farbe gefiltert, Alpha als Schwelle
        rgb = bild.convert('RGB').resize((groesse, groesse), Image.LANCZOS)
        a = bild.getchannel('A').resize((groesse, groesse), Image.BILINEAR).point(lambda v: 255 if v >= 128 else 0)
        bild = Image.merge('RGBA', (*rgb.split(), a))
    rgba = np.asarray(bild).copy()
    if dxt or not name.lower().startswith('zhc'):
        rgba[..., 3] = 255
        return 'Art\\Textures\\%s.dds' % name.lower(), encode_dds(build_mips(rgba, 'DXT1', 'none'), 'DXT1')
    return 'Art\\Textures\\%s.tga' % name.lower(), tga32(rgba)


ap = argparse.ArgumentParser()
ap.add_argument('arbeit')
ap.add_argument('ziel')
ap.add_argument('--groesse', type=int, default=512)
ap.add_argument('--textur', default='ZHCA_AvHummerKI')
ap.add_argument('--dxt', action='store_true')
a = ap.parse_args()

eintr = []
for modell, archivname, zusatz in (('AVHUMMER', 'AvHummer', ''), ('AVHUMMER_D', 'AvHummer_D', 'D')):
    w3d = os.path.join(a.arbeit, 'export', modell + '.w3d')
    png = os.path.join(a.arbeit, 'textur', a.textur + zusatz + '.png')
    if not os.path.exists(w3d):
        continue
    eintr.append(('Art\\W3D\\%s.W3D' % archivname, open(w3d, 'rb').read()))
    eintr.append(textur_eintrag(png, a.textur + zusatz, a.groesse, a.dxt))
os.makedirs(os.path.dirname(os.path.abspath(a.ziel)), exist_ok=True)
write_big(a.ziel, eintr)
print('geschrieben', a.ziel, [(n, len(d)) for n, d in eintr])
