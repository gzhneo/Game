# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Textures and models of the test data set, all computed here.

Terrain textures are noise over a base colour, models are boxes (interface pictures: gen_ui_art). Nothing is derived from third-party or EA images; the optional photo textures
come from gen_polyhaven.
"""
import gen_logic
import gen_ui
from gen_util import box_mesh_w3d, dds_bgra, ellipsoid_mesh_w3d, noise_pixels, tga_bytes

TILE = 64


def terrain_textures(seed=100):
    """rel path -> bytes, Art/Terrain/<name>.tga; a class of n x n tiles is n*64 pixels square."""
    out = {}
    for i, (name, tiles, base, cls) in enumerate(gen_ui.TERRAIN):
        size = tiles * TILE
        out['Art/Terrain/%s.tga' % name.lower()] = tga_bytes(size, size, noise_pixels(size, base, seed + i))
    return out


def other_textures(seed=200):
    """Water, sky, road, and the fixed names the terrain drawing loads."""
    out = {}
    tex = 'Art/Textures/'
    out[tex + 'testwater.tga'] = tga_bytes(128, 128, noise_pixels(128, (52, 96, 128), seed))
    out[tex + 'testsky.tga'] = tga_bytes(128, 128, noise_pixels(128, (150, 180, 215), seed + 1))
    out[tex + 'noise0000.tga'] = tga_bytes(64, 64, noise_pixels(64, (160, 160, 160), seed + 2))
    out[tex + 'watersurfacebubbles.tga'] = tga_bytes(64, 64, noise_pixels(64, (24, 24, 24), seed + 3))
    ramp = [min(255, int(255 * 4 * min(x + 0.5, 63.5 - x) / 64)) for x in range(64)]
    edge = b''.join(bytes((0, 0, 0, a)) for a in ramp) * 64
    out[tex + 'twalphaedge.tga'] = tga_bytes(64, 64, edge, 32)
    road = bytearray()
    for y in range(128):
        for x in range(128):
            v = 150 if (y < 10 or y >= 118) else 96 + (x * 7 + y * 3) % 9
            road += bytes((v, v, v))
    out[tex + 'exmask_g.tga'] = tga_bytes(64, 64, noise_pixels(64, (128, 128, 128), seed + 4))
    # fixed names the renderer loads; grey noise (the terrain shader multiplies with the first two)
    for k, name in enumerate(('TSNoiseUrb', 'TSCloudMed', 'EXScorch01', 'tsmoonlarg', 'exlaser', 'TBBib', 'TBRedBib')):
        out[tex + name.lower() + '.tga'] = tga_bytes(64, 64, noise_pixels(64, (128, 128, 128), seed + 10 + k))
    out[tex + 'testroad.tga'] = tga_bytes(128, 128, bytes(road))
    if TEAM_COLOUR:
        out[tex + TEAM_COLOUR_TEXTURE] = team_colour_texture()
    out.update(particle_textures(seed + 20))
    return out


def particle_textures(seed, size=64):
    """The particle textures of ParticleSystem.ini: testsmoke.tga, a soft round puff (white, the alpha falls off
    outwards, a little noise; ALPHA shader) and testring.tga, a bright ring on black (ADDITIVE shader: black adds
    nothing)."""
    import math
    import random
    rnd = random.Random(seed)
    half = (size - 1) / 2.0
    smoke, ring = bytearray(), bytearray()
    for y in range(size):
        for x in range(size):
            r = math.hypot(x - half, y - half) / half
            a = max(0.0, 1.0 - r) ** 1.5 * (0.8 + 0.2 * rnd.random())
            smoke += bytes((255, 255, 255, int(255 * a)))
            v = int(255 * math.exp(-((r - 0.7) / 0.12) ** 2)) if r <= 1.0 else 0
            ring += bytes((v, v, v))
    return {'Art/Textures/testsmoke.tga': tga_bytes(size, size, bytes(smoke), 32),
            'Art/Textures/testring.tga': tga_bytes(size, size, bytes(ring))}


def silent_wav(seconds=0.5, rate=8000):
    """8 bit mono PCM silence; the music track the engine wants to find."""
    import struct
    n = int(seconds * rate)
    data = b'\x80' * n
    fmt = struct.pack('<HHIIHH', 1, 1, rate, rate, 1, 8)
    return (b'RIFF' + struct.pack('<I', 4 + 8 + len(fmt) + 8 + len(data)) + b'WAVE' +
            b'fmt ' + struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', len(data)) + data)


STRUCTURES = ('Headquarters', 'PowerPlant', 'Barracks', 'Factory', 'Superweapon')

# --teamfarbe: the units (not the structures) carry a team colour texture; the engine blends the side's colour in
# where its alpha is low (W3DAssetManager, names starting with ZHCA).
TEAM_COLOUR = False
TEAM_COLOUR_TEXTURE = 'zhca_testunit.tga'

# --rund: the units are coarse ellipsoids with smooth normals and a white highlight, for the lighting per pixel; their
# texture has a normal map next to it (<name>_n.dds), for the normal maps of models lit per pixel
ROUND = False
ROUND_TEXTURE = 'testround.tga'


def round_textures(size=128):
    """rel path -> bytes: light grey colour texture and its normal map, a grid of round bumps (R, G = slope in image
    axes with G up, B = gloss 128, A = 0), with a full mip chain of box-filtered levels."""
    import math
    colour = noise_pixels(64, (200, 200, 200), 300, 0.2)
    cells = 8
    level = bytearray()
    for y in range(size):
        for x in range(size):
            fx = (x + 0.5) * cells / size % 1.0 - 0.5
            fy = (y + 0.5) * cells / size % 1.0 - 0.5
            r = math.hypot(fx, fy)
            nx = ny = 0.0
            if r < 0.4:
                s = math.sin(r / 0.4 * math.pi) * 0.8		# slope of a cosine bump
                nx, ny = s * fx / max(r, 1e-6), -s * fy / max(r, 1e-6)
            nz = math.sqrt(max(0.0, 1.0 - nx * nx - ny * ny))
            length = math.sqrt(nx * nx + ny * ny + nz * nz)
            rr = int(round(127.5 + 127.5 * nx / length))
            gg = int(round(127.5 + 127.5 * ny / length))
            level += bytes((128, gg, rr, 0))
    levels, current, n = [bytes(level)], level, size
    while n > 1:
        h = n // 2
        nxt = bytearray()
        for y in range(h):
            for x in range(h):
                for c in range(4):
                    nxt.append((current[((2 * y) * n + 2 * x) * 4 + c] + current[((2 * y) * n + 2 * x + 1) * 4 + c] +
                                current[((2 * y + 1) * n + 2 * x) * 4 + c] + current[((2 * y + 1) * n + 2 * x + 1) * 4 + c] + 2) // 4)
        levels.append(bytes(nxt))
        current, n = nxt, h
    return {'Art/Textures/' + ROUND_TEXTURE: tga_bytes(64, 64, colour),
            'Art/Textures/' + ROUND_TEXTURE.replace('.tga', '_n.dds'): dds_bgra(size, levels)}


def team_colour_texture(size=64):
    """Grey (alpha 255) with diagonal bands of a saturated red the engine turns to the side's hue: alpha 0, 128 and a
    soft edge. The engine keeps value and scales saturation (hue shift), so grey texels would stay grey; as BGRA."""
    pixels = bytearray()
    for y in range(size):
        for x in range(size):
            grey = 150 + ((x * 7 + y * 13) % 23)
            band = (x + y) % 32
            if band < 16:
                alpha = 0 if band < 8 else 128 if band < 12 else 255 - 16 * (16 - band)
                red = 120 + band * 8
                pixels += bytes((red // 5, red // 4, red, alpha))
            else:
                pixels += bytes((grey, grey, grey, 255))
    return tga_bytes(size, size, pixels, 32)


def models(textured=False):
    """rel path -> bytes, Art/W3D/<name>.w3d; one mesh per file, the mesh carries the file's name.

    textured: structures and units reference the photo textures of gen_polyhaven.MODEL."""
    out = {}
    for side, tmpl, disp, colour in gen_logic.SIDES:
        for part, boxes in gen_logic.MODEL_PARTS.items():
            if part == 'SupplyPile':
                continue
            name = gen_logic.model_name(side, part)
            texture = ('testbuilding.tga' if part in STRUCTURES else 'testunit.tga') if textured else None
            tint = colour
            if TEAM_COLOUR and part not in STRUCTURES:
                texture, tint = TEAM_COLOUR_TEXTURE, (255, 255, 255)	# white material: grey and team colour show as they are
            if ROUND and part not in STRUCTURES:
                lo = [min(b[k] - b[k + 3] / 2 for b in boxes) for k in range(3)]
                hi = [max(b[k] + b[k + 3] / 2 for b in boxes) for k in range(3)]
                out['Art/W3D/%s.w3d' % name] = ellipsoid_mesh_w3d(name, lo, hi, tint, (255, 255, 255), 24.0,
                                                                   texture=ROUND_TEXTURE)
                continue
            out['Art/W3D/%s.w3d' % name] = box_mesh_w3d(name, boxes, tint, texture)
    out['Art/W3D/TestSupplyPile.w3d'] = box_mesh_w3d('TestSupplyPile', gen_logic.MODEL_PARTS['SupplyPile'], (200, 170, 60))
    out['Art/W3D/TestRock.w3d'] = box_mesh_w3d('TestRock', [(0, 0, 6, 16, 14, 12), (3, 2, 12, 8, 8, 6)], (120, 116, 108))
    out['Art/W3D/TestRallyMarker.w3d'] = box_mesh_w3d('TestRallyMarker', [(0, 0, 4, 2, 2, 8)], (240, 240, 60))
    out['Art/W3D/TestDebris.w3d'] = box_mesh_w3d('TestDebris', [(0, 0, 1, 3, 3, 2)], (110, 110, 110))
    out['Art/W3D/TestMoveHint.w3d'] = box_mesh_w3d('TestMoveHint', [(0, 0, 0.5, 6, 6, 1)], (60, 220, 60))
    # placement cursor, fixed names in the engine
    out['Art/W3D/Locater01.w3d'] = box_mesh_w3d('Locater01', [(0, 0, 0.5, 8, 8, 1)], (60, 220, 60))
    out['Art/W3D/Locater02.w3d'] = box_mesh_w3d('Locater02', [(0, 0, 1, 2, 12, 2)], (60, 220, 60))
    return out
