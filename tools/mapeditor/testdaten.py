#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Builds a tiny game data folder for the map editor's 3D view without EA files.

The engine needs Data/INI/Terrain.ini and the terrain textures (Art/Terrain/*.tga, 64 pixels per
tile) to draw a map. This script writes both for the texture classes of the editor's demo map, with
textures computed from noise (no third-party assets), plus the files the engine start reads without
checking (the audio definitions), empty. The water gets its settings (Water.ini) and two noise
textures; the trees of the demo map (TreePine) get an object definition and a model computed here
(a coloured pyramid, Art/W3D/TestTree.w3d); one road type and one bridge type (Roads.ini) have a computed texture; two sound effects give the
object sound fields something to show. Enough for smoke tests of engine start, terrain,
water, objects and camera in the cloud environment (Wine); the look of real maps needs the game
data.

Aufruf: python tools/mapeditor/testdaten.py <Zielordner>
"""
import os
import random
import struct
import sys

# name in the map, tiles, base colour (R, G, B), terrain class
CLASSES = [
    ('GrassMediumType8', 16, (92, 124, 58), 'SWISS_1'),
    ('DirtRoadType1', 16, (128, 104, 74), 'SWISS_2'),
    ('CliffMediumType8', 16, (116, 112, 104), 'CLIFF'),
    ('SnowLargeType1', 36, (226, 230, 236), 'SNOW_1'),
    ('SandType1', 4, (196, 176, 128), 'DESERT_1'),
    # Not used by the demo map: the texture brush adds it.
    ('RockType1', 4, (134, 128, 118), 'ROCK'),
]
TILE = 64


def noise_texture(size, base, seed):
    """Value noise over three octaves, tiling at the image edge."""
    rnd = random.Random(seed)
    layers = []
    for cells, weight in ((4, 0.55), (16, 0.3), (64, 0.15)):
        grid = [[rnd.random() for _ in range(cells)] for _ in range(cells)]
        layers.append((cells, weight, grid))
    pixels = bytearray()
    for y in range(size):
        for x in range(size):
            v = 0.0
            for cells, weight, grid in layers:
                fx = x * cells / size
                fy = y * cells / size
                x0, y0 = int(fx) % cells, int(fy) % cells
                x1, y1 = (x0 + 1) % cells, (y0 + 1) % cells
                tx, ty = fx - int(fx), fy - int(fy)
                tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
                top = grid[y0][x0] * (1 - tx) + grid[y0][x1] * tx
                bottom = grid[y1][x0] * (1 - tx) + grid[y1][x1] * tx
                v += weight * (top * (1 - ty) + bottom * ty)
            shade = 0.72 + 0.56 * v
            r, g, b = (min(255, int(c * shade)) for c in base)
            pixels += bytes((b, g, r))  # TGA stores BGR
    return pixels


def write_tga(path, size, pixels, bits=24):
    header = struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, size, size, bits, 8 if bits == 32 else 0)
    with open(path, 'wb') as f:
        f.write(header)
        f.write(pixels)


def chunk(kind, payload, wraps=False):
    """W3D chunk: type, size (high bit set when the payload is made of chunks), payload."""
    return struct.pack('<II', kind, len(payload) | (0x80000000 if wraps else 0)) + payload


def w3d_name(text):
    return text.encode('ascii')[:15].ljust(16, b'\0')


def pyramid_w3d(name, height, radius, colour):
    """A four-sided pyramid as a W3D mesh without textures, lit through one vertex material."""
    apex = (0.0, 0.0, height)
    base = [(radius, 0.0, 0.0), (0.0, radius, 0.0), (-radius, 0.0, 0.0), (0.0, -radius, 0.0)]
    vertices, normals, triangles = [], [], []
    for i in range(4):
        a, b = base[i], base[(i + 1) % 4]
        u = [b[k] - a[k] for k in range(3)]
        v = [apex[k] - a[k] for k in range(3)]
        n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
        length = sum(c * c for c in n) ** 0.5
        n = [c / length for c in n]
        first = len(vertices)
        vertices += [a, b, apex]
        normals += [n, n, n]
        triangles.append((first, first + 1, first + 2, n, sum(n[k] * a[k] for k in range(3))))
    vec = lambda v: struct.pack('<3f', *v)
    header = struct.pack('<II', (4 << 16) | 2, 0) + w3d_name(name) + w3d_name('')
    header += struct.pack('<IIIIiII', len(triangles), len(vertices), 1, 0, 0, 0, 0)
    header += struct.pack('<II', 3, 1)
    header += vec((-radius, -radius, 0.0)) + vec((radius, radius, height))
    header += vec((0.0, 0.0, height * 0.5)) + struct.pack('<f', max(radius, height * 0.5) * 1.5)
    tris = b''.join(struct.pack('<IIII', a, b, c, 0) + vec(n) + struct.pack('<f', d)
                    for a, b, c, n, d in triangles)
    rgb = lambda c: bytes(c) + b'\0'
    material = struct.pack('<I', 0) + rgb(colour) + rgb(colour) + rgb((0, 0, 0)) + rgb((0, 0, 0))
    material += struct.pack('<3f', 1.0, 1.0, 0.0)
    # Shader: depth test less-equal, depth write, src one, dest zero, gradient modulate, no texture.
    shader = bytes((3, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0))
    body = chunk(0x1F, header)
    body += chunk(0x02, b''.join(vec(v) for v in vertices))
    body += chunk(0x03, b''.join(vec(n) for n in normals))
    body += chunk(0x20, tris)
    body += chunk(0x22, b''.join(struct.pack('<I', i) for i in range(len(vertices))))
    body += chunk(0x28, struct.pack('<4I', 1, 1, 1, 0))
    body += chunk(0x29, shader)
    body += chunk(0x2A, chunk(0x2B, chunk(0x2C, b'Tree\0') + chunk(0x2D, material), True), True)
    body += chunk(0x38, chunk(0x39, struct.pack('<I', 0)) + chunk(0x3A, struct.pack('<I', 0)), True)
    return chunk(0x00, body, True)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    root = sys.argv[1]
    ini_dir = os.path.join(root, 'Data', 'INI')
    art_dir = os.path.join(root, 'Art', 'Terrain')
    os.makedirs(ini_dir, exist_ok=True)
    os.makedirs(art_dir, exist_ok=True)
    lines = ['; Test data of tools/mapeditor/testdaten.py (no EA files).', '']
    for i, (name, tiles, base, cls) in enumerate(CLASSES):
        side = int(round(tiles ** 0.5))
        size = side * TILE
        texture = name.lower() + '.tga'
        write_tga(os.path.join(art_dir, texture), size, noise_texture(size, base, 1000 + i))
        lines += ['Terrain %s' % name, '  Texture = %s' % texture, '  Class = %s' % cls, 'End', '']
    with open(os.path.join(ini_dir, 'Terrain.ini'), 'w', newline='\r\n') as f:
        f.write('\n'.join(lines))
    # The few game values the terrain and the editor camera need.
    with open(os.path.join(ini_dir, 'GameData.ini'), 'w', newline='\r\n') as f:
        f.write('\n'.join(['; Test data of tools/mapeditor/testdaten.py.', 'GameData',
                           '  PartitionCellSize = 40.0', '  CameraPitch = 37.5', '  CameraYaw = 0.0',
                           '  MaxCameraHeight = 310.0', 'End', '']))
    # AudioManager::init, the particle systems and the 2D animations load these without checking;
    # the object folder must not be empty either.
    os.makedirs(os.path.join(ini_dir, 'Default'), exist_ok=True)
    os.makedirs(os.path.join(ini_dir, 'Object'), exist_ok=True)
    empty = ['AudioSettings.ini', 'MiscAudio.ini', 'Animation2D.ini']
    for name in ('Music.ini', 'SoundEffects.ini', 'Speech.ini', 'Voice.ini'):
        empty += ['Default/' + name] + ([] if name == 'SoundEffects.ini' else [name])
    # Two sound effects for the object sound fields (no audio files: the editor plays nothing).
    with open(os.path.join(ini_dir, 'SoundEffects.ini'), 'w', newline='\r\n') as f:
        f.write('\n'.join(['; Test data of tools/mapeditor/testdaten.py.',
                           'AudioEvent TestWind', '  Volume = 60', '  MinVolume = 20', '  Priority = LOW',
                           '  Type = WORLD SHROUDED EVERYONE', '  Control = LOOP', '  MinRange = 50', '  MaxRange = 400', 'End', '',
                           'AudioEvent TestBell', '  Volume = 80', '  Priority = HIGH', '  Type = WORLD EVERYONE',
                           '  MinRange = 20', '  MaxRange = 300', 'End', '']))
    for name in empty:
        with open(os.path.join(ini_dir, name), 'w', newline='\r\n') as f:
            f.write('; empty (tools/mapeditor/testdaten.py)\n')
    # One particle system of the game data: the particle editor changes it and puts it back.
    with open(os.path.join(ini_dir, 'ParticleSystem.ini'), 'w', newline='\r\n') as f:
        f.write('\n'.join(['; Test data of tools/mapeditor/testdaten.py.', 'ParticleSystem TestSmoke',
                           '  Priority = CONSTANT', '  IsOneShot = No', '  Shader = ALPHA', '  Type = PARTICLE',
                           '  ParticleName = noise0000.tga', '  Lifetime = 30.00 40.00', '  Size = 5.00 8.00',
                           '  Alpha1 = 0.50 0.50 0', '  Alpha2 = 0.00 0.00 40', '  Color1 = R:200 G:200 B:200 0',
                           '  BurstDelay = 2.00 2.00', '  BurstCount = 2.00 2.00', '  VelocityType = ORTHO',
                           '  VelOrthoZ = 0.50 1.00', '  VolumeType = SPHERE', '  VolSphereRadius = 5.00', 'End', '']))
    with open(os.path.join(ini_dir, 'Object', 'TestObjects.ini'), 'w', newline='\r\n') as f:
        f.write('\n'.join(['; Test data of tools/mapeditor/testdaten.py.', 'Object TreePine',
                           '  Draw = W3DModelDraw ModuleTag_01', '    DefaultConditionState',
                           '      Model = TestTree', '    End', '  End', '  KindOf = IMMOBILE',
                           # The real data sets it in Default/Object.ini; the template leaves it unset.
                           '  Scale = 1.0', '  Shadow = SHADOW_VOLUME', '  SoundAmbient = TestWind',
                           '  EditorSorting = SHRUBBERY', 'End', '',
                           # A second tree for the grove tool and a fence piece for the fence tool.
                           'Object TreeBirch', '  Draw = W3DModelDraw ModuleTag_01', '    DefaultConditionState',
                           '      Model = TestTree', '    End', '  End', '  KindOf = IMMOBILE', '  Scale = 0.7',
                           '  EditorSorting = SHRUBBERY', 'End', '',
                           'Object TestFence', '  Draw = W3DModelDraw ModuleTag_01', '    DefaultConditionState',
                           '      Model = TestFence', '    End', '  End', '  KindOf = IMMOBILE', '  Scale = 1.0',
                           '  EditorSorting = MISC_MAN_MADE', '  FenceWidth = 20.0', '  FenceXOffset = 5.0', 'End', '']))
    # Water: one setting per time of day and the standing-water values; the water object of the
    # engine reads all of them without checking.
    tex_dir = os.path.join(root, 'Art', 'Textures')
    os.makedirs(tex_dir, exist_ok=True)
    write_tga(os.path.join(tex_dir, 'testwater.tga'), 128, noise_texture(128, (52, 96, 128), 2000))
    write_tga(os.path.join(tex_dir, 'testsky.tga'), 128, noise_texture(128, (150, 180, 215), 2001))
    # Fixed names of the river drawing: noise and sparkles (multiplied, then added to the colour) and
    # the edge mask, whose colour is added too and whose alpha fades the river towards both banks.
    write_tga(os.path.join(tex_dir, 'noise0000.tga'), 64, noise_texture(64, (160, 160, 160), 2002))
    write_tga(os.path.join(tex_dir, 'watersurfacebubbles.tga'), 64, noise_texture(64, (24, 24, 24), 2003))
    # Fixed names of the terrain's cloud shadows and macro texture (both multiplied over the ground).
    write_tga(os.path.join(tex_dir, 'TSCloudMed.tga'), 128, noise_texture(128, (225, 225, 225), 2004))
    write_tga(os.path.join(tex_dir, 'TSNoiseUrb.tga'), 128, noise_texture(128, (215, 215, 215), 2005))
    ramp = [min(255, int(255 * 4 * min(x + 0.5, 63.5 - x) / 64)) for x in range(64)]
    edge = b''.join(bytes((0, 0, 0, a)) for a in ramp) * 64
    write_tga(os.path.join(tex_dir, 'twalphaedge.tga'), 64, edge, 32)
    water = ['; Test data of tools/mapeditor/testdaten.py.']
    for tod in ('MORNING', 'AFTERNOON', 'EVENING', 'NIGHT'):
        water += ['WaterSet %s' % tod, '  SkyTexture = testsky.tga', '  WaterTexture = testwater.tga',
                  '  Vertex00Color = R:255 G:255 B:255', '  Vertex10Color = R:255 G:255 B:255',
                  '  Vertex01Color = R:255 G:255 B:255', '  Vertex11Color = R:255 G:255 B:255',
                  '  DiffuseColor = R:255 G:255 B:255 A:255', '  TransparentDiffuseColor = R:255 G:255 B:255 A:160',
                  '  UScrollPerMS = 0.001', '  VScrollPerMS = 0.001', '  SkyTexelsPerUnit = 0.1',
                  '  WaterRepeatCount = 8', 'End', '']
    water += ['WaterTransparency', '  TransparentWaterDepth = 3.0', '  TransparentWaterMinOpacity = 0.6',
              '  StandingWaterColor = R:255 G:255 B:255', '  StandingWaterTexture = testwater.tga', 'End', '']
    with open(os.path.join(ini_dir, 'Water.ini'), 'w', newline='\r\n') as f:
        f.write('\n'.join(water))
    # One road type for the road tool: a grey band with lighter edges, computed here.
    road = bytearray()
    for y in range(128):
        for x in range(128):
            edge = y < 10 or y >= 118
            v = 150 if edge else 96 + (x * 7 + y * 3) % 9
            road += bytes((v, v, v))
    write_tga(os.path.join(tex_dir, 'testroad.tga'), 128, road)
    with open(os.path.join(ini_dir, 'Roads.ini'), 'w', newline='\r\n') as f:
        f.write('\n'.join(['; Test data of tools/mapeditor/testdaten.py.', 'Road TestRoad',
                           '  Texture = testroad.tga', '  RoadWidth = 30.0', '  RoadWidthInTexture = 0.8', 'End', '',
                           # A bridge type for the tool; its model lacks the bridge parts, so the terrain skips it.
                           'Bridge TestBridge', '  BridgeModelName = TestTree', '  Texture = testroad.tga',
                           '  BridgeScale = 1.0', 'End', '']))
    w3d_dir = os.path.join(root, 'Art', 'W3D')
    os.makedirs(w3d_dir, exist_ok=True)
    with open(os.path.join(w3d_dir, 'TestTree.w3d'), 'wb') as f:
        f.write(pyramid_w3d('TestTree', 24.0, 7.0, (40, 110, 45)))
    with open(os.path.join(w3d_dir, 'TestFence.w3d'), 'wb') as f:
        f.write(pyramid_w3d('TestFence', 8.0, 9.0, (120, 90, 60)))
    # A mesh mold as the old WorldBuilder reads them: a pyramid 30 high on a base 100 across.
    mold_dir = os.path.join(root, 'Data', 'Editor', 'Molds')
    os.makedirs(mold_dir, exist_ok=True)
    with open(os.path.join(mold_dir, 'TestPyramid.w3d'), 'wb') as f:
        f.write(pyramid_w3d('TestPyramid', 30.0, 50.0, (128, 128, 128)))
    print('%d Texturklassen und ein Testmodell nach %s' % (len(CLASSES), root))
    return 0


if __name__ == '__main__':
    sys.exit(main())
