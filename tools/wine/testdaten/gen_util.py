# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Helpers of the test data generator: files, textures, W3D meshes, the chunk container of maps.

Everything here computes its output; there is no third-party or EA content.
"""
import os
import random
import struct


def write_text(root, rel, text, crlf=True):
    """Writes a text file (Latin-1, CRLF like the game data) below root."""
    path = os.path.join(root, *rel.replace('\\', '/').split('/'))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if crlf:
        text = text.replace('\r\n', '\n').replace('\n', '\r\n')
    with open(path, 'wb') as f:
        f.write(text.encode('latin-1', 'replace'))
    return path


def write_bytes(root, rel, data):
    path = os.path.join(root, *rel.replace('\\', '/').split('/'))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)
    return path


# ---------------------------------------------------------------------------------------------
# Textures

def noise_pixels(size, base, seed, contrast=0.56):
    """Value noise over three octaves, tiling at the edge; returns BGR bytes."""
    rnd = random.Random(seed)
    layers = []
    for cells, weight in ((4, 0.55), (16, 0.3), (64, 0.15)):
        cells = min(cells, size)
        layers.append((cells, weight, [[rnd.random() for _ in range(cells)] for _ in range(cells)]))
    out = bytearray()
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
            shade = 1.0 - contrast / 2 + contrast * v
            r, g, b = (min(255, int(c * shade)) for c in base)
            out += bytes((b, g, r))
    return bytes(out)


def tga_bytes(width, height, pixels, bits=24, image_id=b''):
    """Uncompressed TGA with the origin at the bottom left (descriptor bit 5 clear, the layout the
    game's readers assume); pixels are rows from the top, BGR (24) or BGRA (32). image_id goes into
    the image ID field (terrain tiles larger than 64 pixels carry "ZHTS<s>" there)."""
    pixels = bytes(pixels)
    step = width * bits // 8
    rows = [pixels[i:i + step] for i in range(0, len(pixels), step)]
    header = struct.pack('<BBBHHBHHHHBB', len(image_id), 0, 2, 0, 0, 0, 0, 0, width, height, bits,
                         8 if bits == 32 else 0)
    return header + image_id + b''.join(reversed(rows))


def dds_bgra(size, levels):
    """An uncompressed DDS (32 bit, A8R8G8B8) of a square image; levels: BGRA bytes per mip level, largest first."""
    header = struct.pack('<7I', 124, 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000, size, size, size * 4, 0, len(levels))
    header += b'\0' * 44
    header += struct.pack('<8I', 32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    header += struct.pack('<5I', 0x1000 | 0x8 | 0x400000, 0, 0, 0, 0)
    return b'DDS ' + header + b''.join(levels)


def solid_tga(width, height, rgba):
    r, g, b, a = rgba
    return tga_bytes(width, height, bytes((b, g, r, a)) * (width * height), 32)


# ---------------------------------------------------------------------------------------------
# W3D meshes

def _chunk(kind, payload, wraps=False):
    return struct.pack('<II', kind, len(payload) | (0x80000000 if wraps else 0)) + payload


def _name(text):
    return text.encode('ascii')[:15].ljust(16, b'\0')


def box_mesh_w3d(name, parts, colour, texture=None, texel=20.0):
    """A mesh made of boxes, as a W3D file without hierarchy.

    parts: list of (cx, cy, cz, sx, sy, sz): centre and full size of each box; z is up and the
    lowest point of the model should be at z = 0. colour: (r, g, b) of the vertex material. With
    texture (a file name), the texture is projected along each face normal and repeats every texel
    world units, so it keeps the same density on boxes of any size; the colour tints it.
    """
    vertices, normals, triangles, uvs = [], [], [], []
    faces = [((1, 0, 0), ((1, -1, -1), (1, 1, -1), (1, 1, 1), (1, -1, 1))),
             ((-1, 0, 0), ((-1, 1, -1), (-1, -1, -1), (-1, -1, 1), (-1, 1, 1))),
             ((0, 1, 0), ((1, 1, -1), (-1, 1, -1), (-1, 1, 1), (1, 1, 1))),
             ((0, -1, 0), ((-1, -1, -1), (1, -1, -1), (1, -1, 1), (-1, -1, 1))),
             ((0, 0, 1), ((-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1))),
             ((0, 0, -1), ((-1, 1, -1), (1, 1, -1), (1, -1, -1), (-1, -1, -1)))]
    for cx, cy, cz, sx, sy, sz in parts:
        half = (sx / 2, sy / 2, sz / 2)
        for normal, corners in faces:
            first = len(vertices)
            for i, corner in enumerate(corners):
                p = (cx + corner[0] * half[0], cy + corner[1] * half[1], cz + corner[2] * half[2])
                vertices.append(p)
                normals.append(normal)
                a, b = [k for k in range(3) if not normal[k]]
                uvs.append((p[a] / texel, p[b] / texel))
            d = -(normal[0] * vertices[first][0] + normal[1] * vertices[first][1] + normal[2] * vertices[first][2])
            triangles.append((first, first + 1, first + 2, normal, d))
            triangles.append((first, first + 2, first + 3, normal, d))
    return mesh_w3d(name, vertices, normals, uvs, triangles, colour, texture)


def plane(p0, p1, p2):
    """Unit normal and distance term of the triangle plane, as W3D stores them per triangle."""
    e1 = [p1[k] - p0[k] for k in range(3)]
    e2 = [p2[k] - p0[k] for k in range(3)]
    n = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
    length = (n[0] ** 2 + n[1] ** 2 + n[2] ** 2) ** 0.5 or 1.0
    n = (n[0] / length, n[1] / length, n[2] / length)
    return n, -(n[0] * p0[0] + n[1] * p0[1] + n[2] * p0[2])


def ellipsoid_mesh_w3d(name, lo, hi, colour, specular=(0, 0, 0), shininess=1.0, rings=6, segments=10, texture=None):
    """A coarse ellipsoid filling the box lo..hi (corners (x, y, z)) with smooth normals, as a W3D file; few rings and
    segments, so lighting per vertex and per pixel differ visibly."""
    import math
    c = [(lo[k] + hi[k]) / 2 for k in range(3)]
    r = [(hi[k] - lo[k]) / 2 for k in range(3)]
    vertices, normals, uvs, triangles = [], [], [], []
    for i in range(rings + 1):
        theta = math.pi * i / rings
        for j in range(segments + 1):
            phi = 2 * math.pi * j / segments
            d = (math.sin(theta) * math.cos(phi), math.sin(theta) * math.sin(phi), -math.cos(theta))
            vertices.append(tuple(c[k] + r[k] * d[k] for k in range(3)))
            n = [d[k] / r[k] for k in range(3)]
            length = sum(x * x for x in n) ** 0.5
            normals.append(tuple(x / length for x in n))
            uvs.append((j / segments, i / rings))
    for i in range(rings):
        for j in range(segments):
            a = i * (segments + 1) + j
            b, cc, dd = a + 1, a + segments + 1, a + segments + 2
            for tri in ((a, b, dd), (a, dd, cc)):
                p = [vertices[k] for k in tri]
                if p[0] == p[1] or p[1] == p[2] or p[0] == p[2]:
                    continue
                n, dist = plane(*p)
                triangles.append(tri + (n, dist))
    return mesh_w3d(name, vertices, normals, uvs, triangles, colour, texture, specular, shininess)


def mesh_w3d(name, vertices, normals, uvs, triangles, colour, texture=None, specular=(0, 0, 0), shininess=1.0):
    """One mesh as a W3D file without hierarchy, with at most one texture.

    triangles: (a, b, c, plane normal, plane distance), counter-clockwise seen from the front; z is up.
    colour: (r, g, b) of the vertex material, it tints the texture. uvs: (u, v) per vertex with v
    counted from the bottom (the loader flips it). specular, shininess: highlight of the vertex material.
    """
    # the engine counts vertices and indices of a mesh with 16 bits and does not load larger meshes
    if len(vertices) > 0xFFFF or 3 * len(triangles) > 0xFFFF:
        raise ValueError('%s: %d vertices, %d triangles; the engine counts indices with 16 bits'
                         % (name, len(vertices), len(triangles)))
    lo = [min(v[k] for v in vertices) for k in range(3)]
    hi = [max(v[k] for v in vertices) for k in range(3)]
    vec = lambda v: struct.pack('<3f', *v)
    centre = [(lo[k] + hi[k]) / 2 for k in range(3)]
    radius = sum((hi[k] - lo[k]) ** 2 for k in range(3)) ** 0.5 / 2
    header = struct.pack('<II', (4 << 16) | 2, 0) + _name(name) + _name('')
    header += struct.pack('<IIIIiII', len(triangles), len(vertices), 1, 0, 0, 0, 0)
    header += struct.pack('<II', 7 if texture else 3, 1)
    header += vec(lo) + vec(hi) + vec(centre) + struct.pack('<f', radius)
    tris = b''.join(struct.pack('<IIII', a, b, c, 0) + vec(n) + struct.pack('<f', d) for a, b, c, n, d in triangles)
    rgb = lambda c: bytes(c) + b'\0'
    material = struct.pack('<I', 0) + rgb(colour) + rgb(colour) + rgb(specular) + rgb((0, 0, 0))
    material += struct.pack('<3f', shininess, 1.0, 0.0)
    # Shader: depth test less-equal, depth write, src one, dest zero, gradient modulate, texturing.
    shader = bytes((3, 1, 0, 0, 0, 1, 0, 1, 1 if texture else 0, 0, 0, 0, 0, 0, 0, 0))
    body = _chunk(0x1F, header)
    body += _chunk(0x02, b''.join(vec(v) for v in vertices))
    body += _chunk(0x03, b''.join(vec(n) for n in normals))
    body += _chunk(0x20, tris)
    body += _chunk(0x22, b''.join(struct.pack('<I', i) for i in range(len(vertices))))
    body += _chunk(0x28, struct.pack('<4I', 1, 1, 1, 1 if texture else 0))
    body += _chunk(0x29, shader)
    body += _chunk(0x2A, _chunk(0x2B, _chunk(0x2C, b'Body\0') + _chunk(0x2D, material), True), True)
    passes = _chunk(0x39, struct.pack('<I', 0)) + _chunk(0x3A, struct.pack('<I', 0))
    if texture:
        body += _chunk(0x30, _chunk(0x31, _chunk(0x32, texture.encode('ascii') + b'\0'), True), True)
        stage = _chunk(0x49, struct.pack('<I', 0)) + _chunk(0x4A, b''.join(struct.pack('<2f', *uv) for uv in uvs))
        passes += _chunk(0x48, stage, True)
    body += _chunk(0x38, passes, True)
    return _chunk(0x00, body, True)


# ---------------------------------------------------------------------------------------------
# Chunk container of .map and .scb files (Code/Tools/Editors/MapEditor/Model/Chunk.cpp)

class ChunkFile:
    """Writes the chunk format: name table first, then the chunks. Ids in order of first use."""

    def __init__(self):
        self.names = {}         # name -> id
        self.order = []         # names, newest first like the game writes them
        self.out = bytearray()
        self.open = []

    def _id(self, name):
        if name not in self.names:
            self.names[name] = len(self.names) + 1
            self.order.insert(0, name)
        return self.names[name]

    def begin(self, name, version):
        self.out += struct.pack('<IHi', self._id(name), version, 0xFFFF)
        self.open.append(len(self.out) - 4)

    def end(self):
        pos = self.open.pop()
        struct.pack_into('<I', self.out, pos, len(self.out) - pos - 4)

    def i32(self, v):
        self.out += struct.pack('<i', v)

    def u32(self, v):
        self.out += struct.pack('<I', v)

    def f32(self, v):
        self.out += struct.pack('<f', v)

    def u8(self, v):
        self.out += struct.pack('<B', v)

    def boolean(self, v):
        self.out += b'\1' if v else b'\0'

    def ascii(self, text):
        raw = text.encode('latin-1')
        self.out += struct.pack('<H', len(raw)) + raw

    def unicode(self, text):
        raw = text.encode('utf-16-le')
        self.out += struct.pack('<H', len(text)) + raw

    def name_key(self, name):
        self.out += struct.pack('<I', (self._id(name) << 8) | 3)

    def dict(self, pairs):
        """pairs: list of (key, value); bool, int, float, str (ascii) or ('u', text) for unicode."""
        self.out += struct.pack('<H', len(pairs))
        for key, value in pairs:
            kid = self._id(key) << 8
            if isinstance(value, bool):
                self.out += struct.pack('<I', kid | 0) + (b'\1' if value else b'\0')
            elif isinstance(value, int):
                self.out += struct.pack('<Ii', kid | 1, value)
            elif isinstance(value, float):
                self.out += struct.pack('<If', kid | 2, value)
            elif isinstance(value, str):
                self.out += struct.pack('<I', kid | 3)
                self.ascii(value)
            else:
                self.out += struct.pack('<I', kid | 4)
                self.unicode(value[1])

    def table(self):
        t = bytearray(b'CkMp' + struct.pack('<i', len(self.order)))
        for name in self.order:
            raw = name.encode('latin-1')
            t += struct.pack('<B', len(raw)) + raw + struct.pack('<I', self.names[name])
        return bytes(t)

    def file_bytes(self):
        assert not self.open
        return self.table() + bytes(self.out)


class Crc:
    """The CRC of Common/CRC.cpp (rotate left by one and add, byte by byte); used for MapCache.ini."""

    @staticmethod
    def of(data):
        crc = 0
        for b in data:
            hibit = 1 if crc & 0x80000000 else 0
            crc = ((crc << 1) & 0xFFFFFFFF) + b + hibit
            crc &= 0xFFFFFFFF
        return crc


def big_archive(entries):
    """BIGF archive (open EA format): entries is a list of (path with backslashes, bytes)."""
    table = b''
    names = [(name.replace('/', '\\').encode('latin-1') + b'\0', data) for name, data in entries]
    head = 16
    table_size = sum(8 + len(n) for n, _ in names)
    body = head + table_size
    offset = body
    for n, data in names:
        table += struct.pack('>II', offset, len(data)) + n
        offset += len(data)
    total = offset
    out = b'BIGF' + struct.pack('>III', total, len(names), body) + table
    return out + b''.join(d for _, d in names)
