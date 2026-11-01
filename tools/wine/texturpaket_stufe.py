#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Stufe UHD aus den Wine-Testdaten fuer den Test des Texturpakets (tools/wine/texturpaket_test.sh).

Liest die Texturen (Art\\Textures und Art\\Terrain, TGA) aus den Archiven eines Testdatenordners und schreibt sie
viermal so gross (Pixel vervierfacht, also beim Halbieren mit Box-Filter wieder genau das Original) nach
<Datenordner>\\Texturstufen\\UHD\\TexturesUHD.big. Gelaendekacheln bekommen die Kennung ZHTS4 wie die Stufen des
Projekts. Eine Textur (Vorgabe testwater, die Wassertextur der Testkarte) kommt zusaetzlich als DXT1-DDS mit
voller Mip-Kette in die Stufe: das Paket nimmt dann diese Fassung, und der Test prueft auch den DDS-Weg der Engine
(und die Himmelskachelung nach der Originalgroesse, W3DWater).

    python3 tools/wine/texturpaket_stufe.py <Datenordner> [--dds testwater] [--mod <Ordner>]

Mit --mod kommt dieselbe Textur zusaetzlich in Originalgroesse als lose DXT1-DDS nach <Ordner>\\Art\\Textures: ein Mod,
der eine Textur des Pakets ersetzt (die Engine nimmt dann die Datei des Mods unverkleinert, Zeile TEXTURPAKET ersetzt).
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'assets'))
import texture_inventory as inv  # noqa: E402
import build_texture_tiers as tiers  # noqa: E402

FACTOR = 4


def tga(rgba, ident=b''):
    h, w = rgba.shape[:2]
    bgra = rgba[::-1, :, [2, 1, 0, 3]]		# untere Zeile zuerst, BGRA
    head = struct.pack('<BBBHHBHHHHBB', len(ident), 0, 2, 0, 0, 0, 0, 0, w, h, 32, 8)
    return head + ident + tiers.np.ascontiguousarray(bgra).tobytes()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('data')
    ap.add_argument('--dds', default='testwater', help='Textur, die als DXT1-DDS in die Stufe kommt')
    ap.add_argument('--mod', help='Ordner fuer einen Mod mit dieser Textur in Originalgroesse')
    a = ap.parse_args()
    if not tiers.HAVE_IMAGING:
        print('numpy und Pillow fehlen')
        return 2
    np = tiers.np
    files, _ = inv.effective_files(a.data)
    entries = []
    for name in sorted(files):
        if not name.endswith('.tga') or not (name.startswith('art\\textures\\') or name.startswith('art\\terrain\\')):
            continue
        rgba = tiers.decode(files[name][1].read(name), name)
        big = np.repeat(np.repeat(rgba, FACTOR, axis=0), FACTOR, axis=1)
        stem = name.rsplit('\\', 1)[-1][:-4]
        if name.startswith('art\\textures\\') and stem == a.dds:
            if a.mod:
                mod = os.path.join(a.mod, 'Art', 'Textures')
                os.makedirs(mod, exist_ok=True)
                with open(os.path.join(mod, stem + '.dds'), 'wb') as f:
                    f.write(tiers.encode_dds(tiers.build_mips(rgba, 'DXT1', 'none'), 'DXT1'))
                print(f'Mod: {stem}.dds {rgba.shape[1]}x{rgba.shape[0]} nach {mod}')
            levels = tiers.build_mips(big, 'DXT1', 'none')
            entries.append((name[:-4] + '.dds', tiers.encode_dds(levels, 'DXT1')))
            continue
        entries.append((name, tga(big, b'ZHTS4' if name.startswith('art\\terrain\\') else b'')))
    out = os.path.join(a.data, 'Texturstufen', 'UHD')
    os.makedirs(out, exist_ok=True)
    tiers.write_big(os.path.join(out, 'TexturesUHD.big'), entries)
    print(f'Stufe UHD: {len(entries)} Texturen nach {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
