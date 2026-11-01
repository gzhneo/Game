# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Saatdateien fuer W3DFuzz (tools/fuzz/engine/W3DFuzz.cpp): W3D-Modelle und DDS-Texturen, ganz aus eigener Vorschrift
# erzeugt, nichts aus EA-Daten.
#
#   python3 w3dseeds.py <Zielordner>
#
# W3D: ein Requisit (Netz ohne Hierarchie), ein Baum (Hierarchie, Netz, HLOD; tools/assets/blender/w3d_geruest.py), eine
# klassische Animation (Vektor-, Quaternion- und Sichtbarkeitskanal), zwei komprimierte Animationen (Zeitcode und
# adaptive Delta), eine Morph-Animation (Kanal auf die klassische Animation als Posen), ein Netz mit Skin (Einfluesse je
# Ecke) und AABB-Baum, ein Kollisionskasten und ein Null-Objekt.
# DDS: DXT1 und DXT5 mit voller Mip-Kette, BC7 mit DX10-Kopf, unkomprimiert A8R8G8B8 und ein Wuerfel-Textur-Kopf.
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'assets', 'blender'))
import w3d_geruest as g  # noqa: E402

ANIMATION, ANIMATION_HEADER, ANIMATION_CHANNEL, BIT_CHANNEL = 0x0200, 0x0201, 0x0202, 0x0203
COMPRESSED_ANIMATION, COMPRESSED_ANIMATION_HEADER, COMPRESSED_ANIMATION_CHANNEL, COMPRESSED_BIT_CHANNEL = \
    0x0280, 0x0281, 0x0282, 0x0283
MORPH_ANIMATION, MORPHANIM_HEADER, MORPHANIM_CHANNEL, MORPHANIM_POSENAME, MORPHANIM_KEYDATA, \
    MORPHANIM_PIVOTCHANNELDATA = 0x02C0, 0x02C1, 0x02C2, 0x02C3, 0x02C4, 0x02C5
VERTEX_INFLUENCES = 0x000E
AABBTREE, AABBTREE_HEADER, AABBTREE_POLYINDICES, AABBTREE_NODES = 0x0090, 0x0091, 0x0092, 0x0093
BOX, NULL_OBJECT = 0x0740, 0x0750
# compressed channels use the same types as the classic ones (hcanim.cpp add_channel)
ANIM_CHANNEL_X, ANIM_CHANNEL_Q = 0, 6


def wuerfel():
    ecken, normalen, uv, dreiecke = [], [], [], []
    seiten = [((1, 0, 0), (0, 1, 0), (0, 0, 1)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)), ((0, 1, 0), (0, 0, 1), (1, 0, 0)),
              ((0, -1, 0), (1, 0, 0), (0, 0, 1)), ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (0, 1, 0), (1, 0, 0))]
    for n, a, b in seiten:
        basis = len(ecken)
        for s, t in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            ecken.append(tuple(5.0 * (n[i] + s * a[i] + t * b[i]) for i in range(3)))
            normalen.append(tuple(float(v) for v in n))
            uv.append(((s + 1) / 2.0, (t + 1) / 2.0))
        dreiecke += [(basis, basis + 1, basis + 2), (basis, basis + 2, basis + 3)]
    return {'ecken': ecken, 'normalen': normalen, 'uv': uv, 'dreiecke': dreiecke}


def hierarchie(modell, knochen):
    return g.abschnitt(g.HIERARCHY, [
        g.abschnitt(g.HIERARCHY_HEADER, struct.pack('<I', g.HTREE_VERSION) + g.name16(modell) +
                    struct.pack('<I3f', len(knochen), 0.0, 0.0, 0.0)),
        g.abschnitt(g.PIVOTS, b''.join(g.pivot(*k) for k in knochen)),
    ])


def klassische_animation(modell, frames=8):
    kopf = struct.pack('<I', g.version(4, 1)) + g.name16('ANIM') + g.name16(modell) + struct.pack('<II', frames, 15)
    x = struct.pack('<HHHHHH', 0, frames - 1, 1, ANIM_CHANNEL_X, 1, 0) + b''.join(struct.pack('<f', i * 0.5)
                                                                                     for i in range(frames))
    q = struct.pack('<HHHHHH', 0, frames - 1, 4, ANIM_CHANNEL_Q, 2, 0) + b''.join(struct.pack('<4f', 0, 0, 0, 1)
                                                                                     for _ in range(frames))
    sicht = struct.pack('<HHHHB', 0, frames - 1, 0, 1, 1) + bytes([0b10101010])
    return g.abschnitt(ANIMATION, [g.abschnitt(ANIMATION_HEADER, kopf), g.abschnitt(ANIMATION_CHANNEL, x),
                                   g.abschnitt(ANIMATION_CHANNEL, q), g.abschnitt(BIT_CHANNEL, sicht)])


def zeitcode_animation(modell, frames=10):
    kopf = struct.pack('<I', g.version(0, 1)) + g.name16('ZEIT') + g.name16(modell) + struct.pack('<IHH', frames, 15, 0)
    codes = [0, 4, 9]
    x = struct.pack('<IHBB', len(codes), 1, 1, ANIM_CHANNEL_X) + \
        b''.join(struct.pack('<If', c, c * 0.25) for c in codes)
    q = struct.pack('<IHBB', len(codes), 2, 4, ANIM_CHANNEL_Q) + \
        b''.join(struct.pack('<I4f', c | (0x80000000 if c == 4 else 0), 0, 0, 0, 1) for c in codes)
    sicht = struct.pack('<IHBB', 2, 1, 1, 1) + struct.pack('<II', 0, 5 | 0x80000000)
    return g.abschnitt(COMPRESSED_ANIMATION, [g.abschnitt(COMPRESSED_ANIMATION_HEADER, kopf),
                                              g.abschnitt(COMPRESSED_ANIMATION_CHANNEL, x),
                                              g.abschnitt(COMPRESSED_ANIMATION_CHANNEL, q),
                                              g.abschnitt(COMPRESSED_BIT_CHANNEL, sicht)])


def delta_animation(modell, frames=33):
    kopf = struct.pack('<I', g.version(0, 1)) + g.name16('DELTA') + g.name16(modell) + struct.pack('<IHH', frames, 15, 1)
    # Startwert(e), dann je 16 Frames und Komponente ein Block aus Filterindex und 8 Byte Deltas (4 Bit je Frame)
    bloecke = (frames + 15) // 16
    x = struct.pack('<IHBBf', frames, 1, 1, ANIM_CHANNEL_X, 0.5) + struct.pack('<f', 0.0) + \
        b''.join(bytes([i % 16]) + bytes(range(8)) for i in range(bloecke))
    q = struct.pack('<IHBBf', frames, 2, 4, ANIM_CHANNEL_Q, 0.1) + struct.pack('<4f', 0, 0, 0, 1) + \
        b''.join(bytes([i % 16]) + bytes(8) for i in range(bloecke * 4))
    return g.abschnitt(COMPRESSED_ANIMATION, [g.abschnitt(COMPRESSED_ANIMATION_HEADER, kopf),
                                              g.abschnitt(COMPRESSED_ANIMATION_CHANNEL, x),
                                              g.abschnitt(COMPRESSED_ANIMATION_CHANNEL, q)])


def morph_animation(modell, posen, knochenzahl, frames=12):
    """Ein Kanal, der die Frames der Animation <posen> (voller Name, etwa FUZZTREE.ANIM) als Posen nimmt."""
    kopf = struct.pack('<I', g.version(0, 1)) + g.name16('MORPH') + g.name16(modell) + struct.pack('<IfI', frames, 15.0, 1)
    schluessel = b''.join(struct.pack('<II', m, p) for m, p in ((0, 0), (4, 3), (8, 7)))
    kanal = g.abschnitt(MORPHANIM_CHANNEL, [g.abschnitt(MORPHANIM_POSENAME, posen.encode('ascii') + b'\0'),
                                            g.abschnitt(MORPHANIM_KEYDATA, schluessel)])
    return g.abschnitt(MORPH_ANIMATION, [g.abschnitt(MORPHANIM_HEADER, kopf), kanal,
                                         g.abschnitt(MORPHANIM_PIVOTCHANNELDATA, bytes(4 * knochenzahl))])


def skin_netz(modell):
    """Netz mit Geometrietyp SKIN (Ecken haengen einzeln an Knochen) und AABB-Baum ueber die Dreiecke."""
    geo = g._geo(wuerfel(), 'fuzzskin.tga')
    teil = g.netz('SKIN', modell, geo, beidseitig=False, alphatest=False)
    # Kopf patchen: Attribute = W3D_MESH_FLAG_GEOMETRY_TYPE_SKIN (0x00020000)
    inhalt = bytearray(teil[8:])
    kopf_daten = 8
    inhalt[kopf_daten + 4:kopf_daten + 8] = struct.pack('<I', 0x00020000)
    n = len(geo['ecken'])
    einfluss = b''.join(struct.pack('<H6x', 1 + (i % 2)) for i in range(n))
    m = len(geo['dreiecke'])
    knoten = struct.pack('<3f3fII', -5, -5, -5, 5, 5, 5, 0x80000000 | 1, 1) + \
        struct.pack('<3f3fII', -5, -5, -5, 5, 5, 5, 0x80000000, m // 2) + \
        struct.pack('<3f3fII', -5, -5, -5, 5, 5, 5, 0x80000000 | (m // 2), m - m // 2)
    # erster Knoten ist innerer Knoten mit Kindern 1 und 2
    knoten = struct.pack('<3f3fII', -5, -5, -5, 5, 5, 5, 1, 2) + knoten[32:]
    baum = g.abschnitt(AABBTREE, [
        g.abschnitt(AABBTREE_HEADER, struct.pack('<II6I', 3, m, 0, 0, 0, 0, 0, 0)),
        g.abschnitt(AABBTREE_POLYINDICES, struct.pack('<%dI' % m, *range(m))),
        g.abschnitt(AABBTREE_NODES, knoten),
    ])
    inhalt += g.abschnitt(VERTEX_INFLUENCES, einfluss) + baum
    return struct.pack('<II', g.MESH, len(inhalt) | 0x80000000) + bytes(inhalt)


def kasten(name):
    voll = name.encode('ascii')
    return g.abschnitt(BOX, struct.pack('<II', g.version(1, 0), 0x10) + voll + b'\0' * (32 - len(voll)) +
                       bytes([255, 0, 0, 0]) + struct.pack('<6f', 0, 0, 5, 3, 4, 5))


def null_objekt(name):
    voll = name.encode('ascii')
    return g.abschnitt(NULL_OBJECT, struct.pack('<IIII', g.version(1, 0), 0, 0, 0) + voll + b'\0' * (32 - len(voll)))


def w3d_dateien():
    knochen = [(g.WURZEL, -1, (0.0, 0.0, 0.0)), ('BAUM', 0, (0.0, 0.0, 0.0)), (g.FEUER_KNOCHEN, 0, (0.0, 0.0, 9.0))]
    baum = g.baum('FUZZTREE', wuerfel(), 'fuzztree.tga')
    return {
        'prop.w3d': g.requisit('FUZZPROP', wuerfel(), 'fuzzprop.tga'),
        'tree.w3d': baum,
        'anim.w3d': hierarchie('FUZZTREE', knochen) + klassische_animation('FUZZTREE'),
        'canim.w3d': hierarchie('FUZZTREE', knochen) + zeitcode_animation('FUZZTREE') + delta_animation('FUZZTREE'),
        'treeanim.w3d': baum + klassische_animation('FUZZTREE') + zeitcode_animation('FUZZTREE'),
        'morph.w3d': baum + klassische_animation('FUZZTREE') + morph_animation('FUZZTREE', 'FUZZTREE.ANIM', len(knochen)),
        'skin.w3d': hierarchie('FUZZSKIN', knochen) + skin_netz('FUZZSKIN'),
        'box.w3d': kasten('FUZZBOX.BOX') + null_objekt('FUZZNULL'),
    }


# --- DDS ------------------------------------------------------------------------------------------
DDSD_CAPS, DDSD_HEIGHT, DDSD_WIDTH, DDSD_PIXELFORMAT, DDSD_MIPMAPCOUNT, DDSD_LINEARSIZE = \
    0x1, 0x2, 0x4, 0x1000, 0x20000, 0x80000
DDPF_ALPHAPIXELS, DDPF_FOURCC, DDPF_RGB = 0x1, 0x4, 0x40
DDSCAPS_COMPLEX, DDSCAPS_TEXTURE, DDSCAPS_MIPMAP = 0x8, 0x1000, 0x400000
DDSCAPS2_CUBEMAP_ALL = 0x200 | 0xFC00


def dds(breite, hoehe, stufen, pixelformat, daten, caps2=0, dx10=None):
    kopf = struct.pack('<7I', 124, DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_MIPMAPCOUNT |
                       DDSD_LINEARSIZE, hoehe, breite, len(daten[0]), 0, stufen)
    kopf += b'\0' * 44 + pixelformat
    kopf += struct.pack('<5I', DDSCAPS_COMPLEX | DDSCAPS_TEXTURE | DDSCAPS_MIPMAP, caps2, 0, 0, 0)
    assert len(kopf) == 124
    return b'DDS ' + kopf + (dx10 or b'') + b''.join(daten)


def fourcc(code):
    return struct.pack('<II4s5I', 32, DDPF_FOURCC, code, 0, 0, 0, 0, 0)


def rgba_format():
    return struct.pack('<II4s5I', 32, DDPF_RGB | DDPF_ALPHAPIXELS, b'\0\0\0\0', 32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000)


def kette(breite, hoehe, block_bytes, block=True, bpp=4):
    out, w, h = [], breite, hoehe
    i = 0
    while True:
        if block:
            n = max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * block_bytes
        else:
            n = w * h * bpp
        out.append(bytes((j * 37 + i * 11) & 0xFF for j in range(n)))
        if w == 1 and h == 1:
            return out
        w, h, i = max(1, w // 2), max(1, h // 2), i + 1


def dds_dateien():
    dxt1 = kette(64, 32, 8)
    dxt5 = kette(32, 32, 16)
    bc7 = kette(64, 64, 16)
    rgba = kette(16, 8, 0, block=False)
    wuerfel_daten = kette(8, 8, 8) * 6
    dx10 = struct.pack('<5I', 98, 3, 0, 1, 0)       # DXGI_FORMAT_BC7_UNORM, TEXTURE2D, ein Element
    return {
        'dxt1.dds': dds(64, 32, len(dxt1), fourcc(b'DXT1'), dxt1),
        'dxt5.dds': dds(32, 32, len(dxt5), fourcc(b'DXT5'), dxt5),
        'bc7.dds': dds(64, 64, len(bc7), fourcc(b'DX10'), bc7, dx10=dx10),
        'rgba.dds': dds(16, 8, len(rgba), rgba_format(), rgba),
        'cube.dds': dds(8, 8, 4, fourcc(b'DXT1'), wuerfel_daten, caps2=DDSCAPS2_CUBEMAP_ALL),
    }


def main():
    if len(sys.argv) != 2:
        print('Aufruf: w3dseeds.py <Zielordner>')
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    for name, daten in list(w3d_dateien().items()) + list(dds_dateien().items()):
        with open(os.path.join(sys.argv[1], name), 'wb') as f:
            f.write(daten)
    return 0


if __name__ == '__main__':
    sys.exit(main())
