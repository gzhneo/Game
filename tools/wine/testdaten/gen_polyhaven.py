# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Photo textures and models from Poly Haven (CC0 1.0) for the test data set, optional.

Downloads through the public API (api.polyhaven.com) into a cache outside the repository and checks
every file against the MD5 the API lists. Textures become TGA: terrain at 256 pixels per tile (image ID
"ZHTS4"), box model textures at 512 x 512. Rock and supply pile become Poly Haven models, read from the
glTF files and written as W3D; structures, vehicles and the soldier are built in Blender
(blender_modelle.py) with Poly Haven textures. Needs Pillow and network access on the first run; later runs use the
cache (GZH_POLYHAVEN_CACHE, default ~/.cache/gzh-testdaten/polyhaven).
"""
import hashlib
import json
import math
import os
import shutil
import struct
import subprocess
import urllib.request

import gen_logic
import gen_ui
from gen_util import mesh_w3d, plane, tga_bytes

API = 'https://api.polyhaven.com'
# the CDN answers 403 to the default urllib agent
HEADERS = {'User-Agent': 'gzh-neo-testdaten'}
TILE_SCALE = 4
TERRAIN = {
    'TestGrass': 'rocky_terrain_02',
    'TestDirt': 'dry_ground_rocks',
    'TestRock': 'aerial_rocks_02',
    'TestSand': 'coast_sand_01',
}
# texture file -> asset; the names are what gen_assets.models() puts into the meshes
MODEL = {
    'testbuilding.tga': 'corrugated_iron',
    'testunit.tga': 'brushed_concrete',
}
MODEL_SIZE = 512
MODEL_BRIGHTNESS = 200
# W3D model -> (width in world units, parts); a part is (model asset, x, y, z in metres, turn about the
# vertical axis in degrees). The whole model is scaled so its larger horizontal extent is the width.
# A model must stay below 21846 triangles (see gen_util.mesh_w3d).
PROPS = {
    'TestRock': (18.0, [('moon_rock_03', 0, 0, 0, 0)]),
    'TestSupplyPile': (28.0, [('wooden_crate_02', -0.3, 0, 0, 0), ('wooden_crate_02', 0.3, 0, 0, 0),
                              ('wooden_crate_02', 0, 0.05, 0.45, 90),
                              ('barrel_03', 0.95, -0.3, 0, 0), ('barrel_03', 0.9, 0.42, 0, 30)]),
}
# material of blender_modelle.py -> texture asset; the structures, vehicles and the soldier are built there
BUILT_TEXTURES = {
    'paint': 'green_metal_rust',
    'roof': 'painted_metal_shutter',
    'wall': 'brushed_concrete',
    'base': 'concrete_tiles',
    'metal': 'metal_plate_02',
    'track': 'rubberized_track',
    'wood': 'brown_planks_05',
    'cloth': 'denim_fabric',
}
# ground of the loading screen picture
SCENE_GROUND = 'rocky_terrain_02'
BUILT_PARTS = ('Headquarters', 'PowerPlant', 'Barracks', 'Factory', 'Tank', 'Dozer', 'Collector', 'Rifleman')
PROP_TEXTURE_SIZE = 1024


def _get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS), timeout=300) as r:
        return r.read()


def cache_dir():
    return os.environ.get('GZH_POLYHAVEN_CACHE') or os.path.join(
        os.path.expanduser('~'), '.cache', 'gzh-testdaten', 'polyhaven')


def _cached(path, url, md5):
    """Downloads url to path unless a verified copy is there already."""
    meta = path + '.md5'
    if os.path.exists(path) and os.path.exists(meta) and open(meta).read().strip() == md5:
        return path
    data = _get(url)
    if hashlib.md5(data).hexdigest() != md5:
        raise IOError('MD5 mismatch for %s' % url)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)
    with open(meta, 'w') as f:
        f.write(md5 + '\n')
    return path


def _files(asset):
    return json.loads(_get('%s/files/%s' % (API, asset)))


def diffuse(asset, res):
    """Path of the cached diffuse JPG of a texture asset at resolution res ('1k', '2k', ...)."""
    path = os.path.join(cache_dir(), '%s_diff_%s.jpg' % (asset, res))
    if os.path.exists(path + '.md5'):
        return path
    entry = _files(asset)['Diffuse'][res]['jpg']
    return _cached(path, entry['url'], entry['md5'])


def gltf(asset, res):
    """Path of the cached glTF of a model asset; its buffer and diffuse texture lie next to it
    (normal and roughness maps are left out, the engine draws only the colour)."""
    folder = os.path.join(cache_dir(), 'models', '%s_%s' % (asset, res))
    path = os.path.join(folder, '%s_%s.gltf' % (asset, res))
    if os.path.exists(path + '.md5'):
        return path
    entry = _files(asset)['gltf'][res]['gltf']
    for rel, inc in entry['include'].items():
        if rel.endswith('.bin') or '_diff_' in rel:
            _cached(os.path.join(folder, *rel.split('/')), inc['url'], inc['md5'])
    return _cached(path, entry['url'], entry['md5'])


def _tileable_resize(image, size):
    """Scales a seamless texture to size x size without seams: wraps a margin around it first, so the
    filter at the border sees the opposite edge instead of a clamped one."""
    from PIL import Image
    w, h = image.size
    m = w // 8
    padded = Image.new('RGB', (w + 2 * m, h + 2 * m))
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            padded.paste(image, (m + dx * w, m + dy * h))
    m2 = round(m * size / w)
    big = padded.resize((size + 2 * m2, size + 2 * m2), Image.LANCZOS)
    return big.crop((m2, m2, m2 + size, m2 + size))


def _tga(asset, res, size, brightness, image_id=b'', stretch=False):
    """brightness: mean grey value the texture is scaled to. stretch: spread the contrast first, so a
    faint pattern stays visible once a model colour tints it."""
    from PIL import Image, ImageOps, ImageStat
    with Image.open(diffuse(asset, res)) as image:
        image = _tileable_resize(image.convert('RGB'), size)
    if stretch:
        image = ImageOps.autocontrast(image, cutoff=1)
    gain = brightness / max(1.0, ImageStat.Stat(image.convert('L')).mean[0])
    image = image.point(lambda v: min(255, round(v * gain)))
    return tga_bytes(size, size, image.tobytes('raw', 'BGR'), 24, image_id)


def terrain_textures(res):
    """rel path -> bytes, replaces gen_assets.terrain_textures(); same names and tile counts, each scan
    scaled to the brightness of the class's base colour."""
    out = {}
    for name, tiles, base, cls in gen_ui.TERRAIN:
        size = tiles * 64 * TILE_SCALE
        grey = 0.299 * base[0] + 0.587 * base[1] + 0.114 * base[2]
        out['Art/Terrain/%s.tga' % name.lower()] = _tga(TERRAIN[name], res, size, grey, b'ZHTS%d' % TILE_SCALE)
    return out


def model_textures(res):
    """rel path -> bytes, Art/Textures/<file> for the textured models; bright, so the side colour shows."""
    return {'Art/Textures/' + name: _tga(asset, res, MODEL_SIZE, MODEL_BRIGHTNESS, stretch=True)
            for name, asset in MODEL.items()}


def _node_matrix(node):
    """4 x 4 row-major matrix of a glTF node (matrix, or translation, rotation, scale)."""
    if 'matrix' in node:
        m = node['matrix']
        return [[m[c * 4 + r] for c in range(4)] for r in range(4)]
    x, y, z, w = node.get('rotation', (0, 0, 0, 1))
    scale = node.get('scale', (1, 1, 1))
    move = node.get('translation', (0, 0, 0))
    rot = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
           [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
           [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    return [[rot[i][0] * scale[0], rot[i][1] * scale[1], rot[i][2] * scale[2], move[i]] for i in range(3)] + [[0, 0, 0, 1]]


def _mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def read_gltf(path):
    """Triangle lists of the default scene with the node transforms applied, in glTF axes (y up,
    metres): list of (positions, normals, uvs, indices). Only what the Poly Haven models use:
    non-interleaved or strided float attributes and 8/16/32 bit indices."""
    g = json.load(open(path, encoding='utf-8'))
    folder = os.path.dirname(path)
    buffers = [open(os.path.join(folder, b['uri']), 'rb').read() for b in g['buffers']]

    def accessor(i):
        a = g['accessors'][i]
        view = g['bufferViews'][a['bufferView']]
        fmt = {5126: 'f', 5125: 'I', 5123: 'H', 5121: 'B'}[a['componentType']]
        n = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
        stride = view.get('byteStride') or struct.calcsize('<%d%s' % (n, fmt))
        start = view.get('byteOffset', 0) + a.get('byteOffset', 0)
        data = buffers[view['buffer']]
        return [struct.unpack_from('<%d%s' % (n, fmt), data, start + k * stride) for k in range(a['count'])]

    out = []

    def visit(index, parent):
        node = g['nodes'][index]
        m = _mul(parent, _node_matrix(node))
        for prim in g['meshes'][node['mesh']]['primitives'] if 'mesh' in node else ():
            att = prim['attributes']
            pos = [tuple(sum(m[r][k] * p[k] for k in range(3)) + m[r][3] for r in range(3)) for p in accessor(att['POSITION'])]
            nor = []
            for n in accessor(att['NORMAL']):
                v = [sum(m[r][k] * n[k] for k in range(3)) for r in range(3)]
                length = math.sqrt(sum(c * c for c in v)) or 1.0
                nor.append(tuple(c / length for c in v))
            uv = accessor(att['TEXCOORD_0'])
            idx = [i[0] for i in accessor(prim['indices'])]
            out.append((pos, nor, uv, idx))
        for child in node.get('children', ()):
            visit(child, m)

    identity = [[float(i == j) for j in range(4)] for i in range(4)]
    for index in g['scenes'][g.get('scene', 0)]['nodes']:
        visit(index, identity)
    return out


def _assemble(items, width=None):
    """Vertices, normals, uvs and triangles (W3D axes) of glTF files placed together.

    items: (path, atlas slot, slot count, x, y, z, turn about the vertical axis in degrees). With width the
    result is centred, its larger horizontal extent scaled to width and its base sunk a little (uneven
    bottoms would float above the ground otherwise); without, the coordinates stay as they are."""
    vertices, normals, uvs, faces = [], [], [], []
    for path, slot, slots, x, y, z, turn in items:
        c, s_ = math.cos(math.radians(turn)), math.sin(math.radians(turn))
        for pos, nor, uv, idx in read_gltf(path):
            first = len(vertices)
            for p, n, t in zip(pos, nor, uv):
                # glTF is y up; turn (x, y, z) into (x, -z, y), then about z
                px, py, pz = p[0], -p[2], p[1]
                nx, ny, nz = n[0], -n[2], n[1]
                vertices.append((c * px - s_ * py + x, s_ * px + c * py + y, pz + z))
                normals.append((c * nx - s_ * ny, s_ * nx + c * ny, nz))
                uvs.append(((slot + t[0]) / slots, 1.0 - t[1]))
            faces += [(first + idx[k], first + idx[k + 1], first + idx[k + 2]) for k in range(0, len(idx), 3)]
    # Blender writes the same triangles in varying order; a fixed order (smallest index first, winding
    # kept, then sorted) gives the same file on every run
    faces = sorted(min((f, f[1:] + f[:1], f[2:] + f[:2])) for f in faces)
    if width:
        lo = [min(v[k] for v in vertices) for k in range(3)]
        hi = [max(v[k] for v in vertices) for k in range(3)]
        f = width / max(hi[0] - lo[0], hi[1] - lo[1])
        cx, cy = (lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2
        sink = 0.03 * (hi[2] - lo[2])
        vertices = [((v[0] - cx) * f, (v[1] - cy) * f, (v[2] - lo[2] - sink) * f) for v in vertices]
    triangles = [(a, b, c_) + plane(vertices[a], vertices[b], vertices[c_]) for a, b, c_ in faces]
    return vertices, normals, uvs, triangles


def _prop(model, width, parts, res, colour, texture):
    """W3D bytes of one model built from Poly Haven models (see PROPS) and its texture atlas as TGA bytes.
    Several assets get their colour maps side by side (all Poly Haven UVs lie within 0..1)."""
    from PIL import Image
    assets = sorted({p[0] for p in parts})
    slots = 1
    while slots < len(assets):
        slots *= 2
    items = [(gltf(asset, res), assets.index(asset), slots, x, y, z, turn) for asset, x, y, z, turn in parts]
    w3d = mesh_w3d(model, *_assemble(items, width), colour, texture)
    size = PROP_TEXTURE_SIZE
    atlas = Image.new('RGB', (size * slots, size))
    for i, asset in enumerate(assets):
        jpg = os.path.join(os.path.dirname(gltf(asset, res)), 'textures', '%s_diff_%s.jpg' % (asset, res))
        with Image.open(jpg) as image:
            atlas.paste(image.convert('RGB').resize((size, size), Image.LANCZOS), (i * size, 0))
    return w3d, tga_bytes(atlas.width, size, atlas.tobytes('raw', 'BGR'))


def prop_models(res):
    """rel path -> bytes: the models of PROPS as Art/W3D/<model>.w3d with their textures in Art/Textures;
    they replace the boxes of the same names."""
    out = {}
    for model, (width, parts) in PROPS.items():
        texture = model.lower() + '.tga'
        out['Art/W3D/%s.w3d' % model], out['Art/Textures/' + texture] = _prop(
            model, width, parts, res, (235, 235, 235), texture)
    return out


def _blender():
    return os.environ.get('BLENDER') or shutil.which('blender')


def built_dir(res):
    """Folder of the Blender build (blender_modelle.py): per part the glTF, the baked textures and the
    cameo pictures per side, and the loading screen picture scene.png. Blender runs once per script,
    texture set and side colours; the result stays in the cache. None without Blender."""
    blender = _blender()
    if not blender:
        print('Blender nicht gefunden (BLENDER oder PATH): Gebaeude, Fahrzeuge und Einheiten bleiben Quader')
        return None
    script = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'blender_modelle.py')
    sides = {side: list(colour) for side, tmpl, disp, colour in gen_logic.SIDES}
    key = hashlib.sha1(open(script, 'rb').read() + json.dumps([BUILT_TEXTURES, BUILT_PARTS, sides, res, SCENE_GROUND],
                                                               sort_keys=True).encode()).hexdigest()[:16]
    folder = os.path.join(cache_dir(), 'built', key)
    if not os.path.exists(os.path.join(folder, 'fertig')):
        os.makedirs(folder, exist_ok=True)
        config = {'out': folder, 'sides': sides, 'parts': list(BUILT_PARTS),
                  'textures': {k: diffuse(v, res) for k, v in BUILT_TEXTURES.items()},
                  'scene': {'ground': diffuse(SCENE_GROUND, res), 'width': 1024, 'height': 768}}
        path = os.path.join(folder, 'config.json')
        json.dump(config, open(path, 'w', encoding='utf-8'), indent=1)
        print('Blender baut %d Modelle und das Ladebild (einmalig, rund zwei Minuten) ...' % len(BUILT_PARTS))
        p = subprocess.run([blender, '-b', '--factory-startup', '-P', script, '--', path],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        missing = [x for x in BUILT_PARTS if not os.path.exists(os.path.join(folder, x + '.gltf'))]
        if not os.path.exists(os.path.join(folder, 'scene.png')):
            missing.append('scene.png')
        if p.returncode != 0 or missing:
            raise RuntimeError('Blender fehlgeschlagen (Rueckgabe %d, fehlt: %s):\n%s'
                               % (p.returncode, ', '.join(missing), p.stdout[-3000:]))
        open(os.path.join(folder, 'fertig'), 'w').close()
    return folder


def built_models(folder):
    """rel path -> bytes: structures, vehicles and soldier of the Blender build in folder (built_dir), one
    W3D model and one baked texture per side; they replace the boxes."""
    from PIL import Image
    sides = [side for side, tmpl, disp, colour in gen_logic.SIDES]
    out = {}
    for part in BUILT_PARTS:
        mesh = _assemble([(os.path.join(folder, part + '.gltf'), 0, 1, 0, 0, 0, 0)])
        for side in sides:
            model = gen_logic.model_name(side, part)
            texture = model.lower() + '.tga'
            out['Art/W3D/%s.w3d' % model] = mesh_w3d(model, *mesh, (235, 235, 235), texture)
            with Image.open(os.path.join(folder, '%s_%s.png' % (part, side))) as image:
                image = image.convert('RGB')
            out['Art/Textures/' + texture] = tga_bytes(image.width, image.height, image.tobytes('raw', 'BGR'))
    return out


def credits():
    """Text listing every asset used: name, authors, address, licence."""
    lines = ['Poly Haven textures and models in this data set (CC0 1.0, https://polyhaven.com/license)', '']
    props = {p[0] for width, parts in PROPS.values() for p in parts}
    built = set(BUILT_TEXTURES.values()) | {SCENE_GROUND} if _blender() else set()
    for asset in sorted(set(TERRAIN.values()) | set(MODEL.values()) | props | built):
        info = json.loads(_get('%s/info/%s' % (API, asset)))
        authors = ', '.join(sorted(info.get('authors', {})))
        lines.append('%s -- "%s" by %s -- https://polyhaven.com/a/%s' % (asset, info.get('name', asset), authors, asset))
    return '\n'.join(lines) + '\n'
