# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Builds the structures, vehicles and the soldier of the test data set in Blender.

    blender -b --factory-startup -P blender_modelle.py -- <config.json>

The models are made from primitives in world units (z up, front +x, base at z = 0, footprints as the
boxes in gen_logic.MODEL_PARTS). Each part gets materials from the photo textures named in the config
(box projection in object space, so no texture coordinates are needed for them), then one texture
layout (smart UV project) onto which Cycles bakes the base colour times ambient occlusion, once per
side: the team colour parts (roofs, turret, cabs, load bed, uniform) take the side colour. Output per
part: <out>/<part>.gltf with its buffer, <out>/<part>_<side>.png and a cameo picture with transparent
background <out>/<part>_<side>_cameo.png (256 x 192); with "preview" also <out>/<part>_<side>_preview.png.
With "scene" the loading screen picture <out>/scene.png is rendered from the built models.

Config: {"out": dir, "textures": {material: jpg}, "sides": {side: [r, g, b]}, "parts": [names],
"preview": bool, "scene": {"ground": jpg, "width": n, "height": n}}
"""
import json
import math
import os
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Vector

CFG = json.load(open(sys.argv[sys.argv.index('--') + 1], encoding='utf-8'))

# material -> texture key in CFG["textures"] (None: flat colour), colour, desaturate, world units per
# texture repeat, team colour
MATERIALS = {
    'paint': ('paint', (1.0, 1.0, 1.0), False, 12.0, False),
    'team': ('paint', (1.6, 1.6, 1.6), True, 12.0, True),
    'roof': ('roof', (1.25, 1.25, 1.25), True, 10.0, True),
    'wall': ('wall', (1.05, 1.0, 0.95), False, 16.0, False),
    'base': ('base', (0.9, 0.9, 0.9), False, 20.0, False),
    'metal': ('metal', (0.8, 0.8, 0.8), False, 6.0, False),
    'track': ('track', (0.22, 0.22, 0.22), True, 4.0, False),
    'wood': ('wood', (1.0, 1.0, 1.0), False, 6.0, False),
    'sandbag': ('cloth', (1.25, 1.1, 0.8), True, 3.0, False),
    'cloth': ('cloth', (1.5, 1.5, 1.5), True, 2.0, True),
    'glass': (None, (0.08, 0.11, 0.14), False, 1.0, False),
    'skin': (None, (0.75, 0.55, 0.42), False, 1.0, False),
    'dark': (None, (0.05, 0.05, 0.05), False, 1.0, False),
}


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def make_materials():
    team_nodes = []
    mats = {}
    for name, (key, colour, desat, repeat, team) in MATERIALS.items():
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        nt = m.node_tree
        bsdf = nt.nodes['Principled BSDF']
        bsdf.inputs['Roughness'].default_value = 0.8
        tint = nt.nodes.new('ShaderNodeVectorMath')
        tint.operation = 'MULTIPLY'
        tint.inputs[1].default_value = colour
        if key and key in CFG['textures']:
            coord = nt.nodes.new('ShaderNodeTexCoord')
            mapping = nt.nodes.new('ShaderNodeMapping')
            mapping.inputs['Scale'].default_value = (1.0 / repeat,) * 3
            tex = nt.nodes.new('ShaderNodeTexImage')
            tex.image = bpy.data.images.load(CFG['textures'][key], check_existing=True)
            tex.projection = 'BOX'
            tex.projection_blend = 0.3
            nt.links.new(coord.outputs['Object'], mapping.inputs['Vector'])
            nt.links.new(mapping.outputs['Vector'], tex.inputs['Vector'])
            src = tex.outputs['Color']
            if desat:
                hsv = nt.nodes.new('ShaderNodeHueSaturation')
                hsv.inputs['Saturation'].default_value = 0.0
                nt.links.new(src, hsv.inputs['Color'])
                src = hsv.outputs['Color']
            nt.links.new(src, tint.inputs[0])
        else:
            tint.inputs[0].default_value = (1.0, 1.0, 1.0)
        nt.links.new(tint.outputs['Vector'], bsdf.inputs['Base Color'])
        if team:
            team_nodes.append((tint, colour))
        mats[name] = m
    return mats, team_nodes


class Builder:
    """Collects the pieces of one model; every piece carries one material."""

    def __init__(self, mats):
        self.mats = mats
        self.objs = []

    def _add(self, obj, mat):
        obj.data.materials.append(self.mats[mat])
        self.objs.append(obj)
        return obj

    def box(self, centre, size, mat, bevel=0.0):
        bpy.ops.mesh.primitive_cube_add(size=1, location=centre)
        obj = bpy.context.active_object
        obj.scale = size
        bpy.ops.object.transform_apply(scale=True)
        if bevel:
            mod = obj.modifiers.new('bevel', 'BEVEL')
            mod.width = bevel
            mod.segments = 1
            bpy.ops.object.modifier_apply(modifier=mod.name)
        return self._add(obj, mat)

    def cyl(self, centre, radius, depth, mat, axis='z', verts=16, radius2=None, fill='NGON'):
        rot = {'z': (0, 0, 0), 'x': (0, math.pi / 2, 0), 'y': (math.pi / 2, 0, 0)}[axis]
        if radius2 is None:
            bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=radius, depth=depth, location=centre,
                                                rotation=rot, end_fill_type=fill)
        else:
            bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=radius, radius2=radius2, depth=depth,
                                            location=centre, rotation=rot, end_fill_type=fill)
        return self._add(bpy.context.active_object, mat)

    def sphere(self, centre, radius, mat, scale=(1, 1, 1), segments=12, rings=8, top_half=False):
        bpy.ops.mesh.primitive_uv_sphere_add(segments=segments, ring_count=rings, radius=radius, location=centre)
        obj = bpy.context.active_object
        if top_half:
            bm = bmesh.new()
            bm.from_mesh(obj.data)
            bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.co.z < -1e-4], context='VERTS')
            bm.to_mesh(obj.data)
            bm.free()
        obj.scale = scale
        bpy.ops.object.transform_apply(scale=True)
        return self._add(obj, mat)

    def prism(self, profile, y0, y1, mat, axis='y'):
        """Extrudes a convex profile: axis 'y' takes (x, z) points along y, axis 'x' takes (y, z) points
        along x."""
        mesh = bpy.data.meshes.new('prism')
        bm = bmesh.new()
        if axis == 'y':
            front = [bm.verts.new((a, y0, b)) for a, b in profile]
            back = [bm.verts.new((a, y1, b)) for a, b in profile]
        else:
            front = [bm.verts.new((y0, a, b)) for a, b in profile]
            back = [bm.verts.new((y1, a, b)) for a, b in profile]
        bm.faces.new(front)
        bm.faces.new(list(reversed(back)))
        n = len(profile)
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((front[i], back[i], back[j], front[j]))
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        bm.to_mesh(mesh)
        bm.free()
        obj = bpy.data.objects.new('prism', mesh)
        bpy.context.collection.objects.link(obj)
        return self._add(obj, mat)

    def finish(self, name):
        bpy.ops.object.select_all(action='DESELECT')
        for o in self.objs:
            o.select_set(True)
        bpy.context.view_layer.objects.active = self.objs[0]
        bpy.ops.object.join()
        obj = bpy.context.active_object
        obj.name = name
        bpy.ops.object.mode_set(mode='EDIT')
        bpy.ops.mesh.select_all(action='SELECT')
        bpy.ops.mesh.remove_doubles(threshold=0.001)
        bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.004)
        # fixed rules, the exporter's own triangulation of n-gons differs between runs
        bpy.ops.mesh.quads_convert_to_tris(quad_method='FIXED', ngon_method='CLIP')
        bpy.ops.object.mode_set(mode='OBJECT')
        # round parts (wheels, dish, tower) smooth, edges of boxes and the octagonal turret hard
        bpy.ops.object.shade_auto_smooth(angle=math.radians(35))
        return obj


def sandbags(b, x, y, length, along='x', rows=2):
    for r in range(rows):
        n = max(1, int(length / 2.2))
        for i in range(n):
            t = (i - (n - 1) / 2) * 2.2 + (1.1 if r % 2 else 0)
            if abs(t) > length / 2:
                continue
            c = (x + t, y, 0.45 + r * 0.8) if along == 'x' else (x, y + t, 0.45 + r * 0.8)
            s = (2.1, 1.1, 0.8) if along == 'x' else (1.1, 2.1, 0.8)
            b.box(c, s, 'sandbag', bevel=0.25)


def windows(b, x0, x1, y, z, h, n, axis='x'):
    step = (x1 - x0) / n
    for i in range(n):
        c = x0 + step * (i + 0.5)
        if axis == 'x':
            b.box((c, y, z), (step * 0.55, 0.3, h), 'glass')
        else:
            b.box((y, c, z), (0.3, step * 0.55, h), 'glass')


# --- structures ---------------------------------------------------------------------------------

def headquarters(b):
    b.box((0, 0, 0.6), (56, 56, 1.2), 'base', bevel=0.3)
    b.box((0, 0, 7.2), (34, 34, 12), 'wall', bevel=0.4)
    b.box((0, 0, 13.9), (36, 36, 1.4), 'roof', bevel=0.3)
    b.box((-3, 0, 19.6), (16, 16, 10), 'wall', bevel=0.3)
    b.box((-3, 0, 25.2), (17.5, 17.5, 1.2), 'roof', bevel=0.2)
    b.box((17.4, 0, 4.6), (1.2, 10, 7), 'metal')
    for side in (-1, 1):
        windows(b, -14, 14, side * 17.1, 10, 2, 5)
        windows(b, -14, 14, side * 17.1, 10, 2, 5, axis='y')
    windows(b, -10, 4, 5.1, 21, 1.6, 3, axis='y')
    b.cyl((-3, 0, 29), 0.5, 6, 'metal', verts=8)
    b.sphere((-3, 0, 32.4), 3.4, 'metal', scale=(1, 1, 0.35), top_half=True)
    b.cyl((10, -10, 18), 0.25, 8, 'metal', verts=6)
    b.box((11.6, -10, 21), (3.2, 0.15, 2), 'team')
    for x, y in ((24, 24), (-24, 24), (24, -24), (-24, -24)):
        sandbags(b, x, y, 7, 'x')
        sandbags(b, x - math.copysign(3, x), y - math.copysign(3, y), 5, 'y')


def power_plant(b):
    b.box((0, 0, 0.6), (34, 34, 1.2), 'base', bevel=0.3)
    b.cyl((-6, -5, 11.2), 8, 20, 'wall', verts=24, radius2=5.8, fill='NOTHING')
    b.cyl((-6, -5, 19.5), 5.9, 3, 'dark', verts=24, fill='NGON')
    b.cyl((-6, -5, 21.25), 6.0, 0.5, 'roof', verts=24, radius2=5.8, fill='NOTHING')
    b.box((8, 6, 5.2), (14, 18, 8), 'wall', bevel=0.3)
    b.prism([(1, 9.2), (15, 9.2), (8, 12.2)], -3, 15, 'roof')
    windows(b, 0, 15, 15.2, 6.5, 2, 4, axis='y')
    for i in range(3):
        b.box((-8 + i * 4, 11, 2.7), (3, 3, 3), 'metal', bevel=0.2)
    b.cyl((1, -5, 6), 0.9, 10, 'metal', axis='x', verts=10)
    b.cyl((8, 0, 4), 0.9, 12, 'metal', axis='y', verts=10)
    b.cyl((13, -10, 9), 0.9, 16, 'metal', verts=10)


def barracks(b):
    b.box((0, 0, 0.6), (44, 30, 1.2), 'base', bevel=0.3)
    r = 9
    half = [(r * math.cos(a), 1.2 + r * math.sin(a)) for a in [math.pi * i / 12 for i in range(13)]]
    b.prism(half, -17, 17, 'roof', axis='x')
    for x in (-17.05, 17.05):
        b.prism([(y, z) for y, z in half], x - 0.3, x + 0.3, 'wall', axis='x')
    b.box((17.4, 0, 4), (0.4, 4.5, 5.6), 'wood')
    b.box((-17.4, 0, 4), (0.4, 3, 3), 'glass')
    b.cyl((-10, 5, 10), 0.5, 4, 'metal', verts=8)
    sandbags(b, 20.5, 6, 8, 'y')
    sandbags(b, 20.5, -6, 4, 'y')
    sandbags(b, 0, -13.5, 16, 'x')


def factory(b):
    b.box((0, 0, 0.6), (52, 40, 1.2), 'base', bevel=0.3)
    b.box((-2, 0, 7.6), (40, 32, 13), 'wall', bevel=0.4)
    for i in range(4):
        x0 = -22 + i * 10
        b.prism([(x0, 14), (x0 + 10, 14), (x0 + 10, 18.5)], -16.5, 16.5, 'roof')
        b.box((x0 + 9.9, 0, 16.2), (0.2, 31, 4), 'glass')
    b.box((18.2, 0, 6), (0.6, 14, 10), 'metal')
    b.box((18.3, 0, 11.5), (0.6, 15, 1), 'team')
    b.cyl((-16, 12, 16), 1.8, 30, 'metal', verts=12)
    b.cyl((-16, 12, 30.5), 2.1, 1, 'dark', verts=12)
    for side in (-1, 1):
        windows(b, -20, 16, side * 16.1, 10, 2.5, 6)
    b.box((22, 15, 2.2), (6, 6, 2), 'wood', bevel=0.2)
    b.box((22, 15, 4.2), (4, 4, 2), 'wood', bevel=0.2)


# --- vehicles and soldier -------------------------------------------------------------------------

def tracks(b, length, width, height, y, z=None):
    z = height / 2 if z is None else z
    for s in (-1, 1):
        b.box((0, s * y, z), (length, width, height), 'track', bevel=min(width, height) * 0.35)
        for i in range(5):
            x = (i - 2) * (length - 3) / 4
            b.cyl((x, s * (y + width / 2 + 0.05), z), height * 0.32, 0.2, 'metal', axis='y', verts=10)


def tank(b):
    tracks(b, 20, 2.6, 3.0, 4.6)
    b.prism([(-9.5, 2.2), (8.5, 2.2), (10, 3.5), (7, 5.6), (-9, 5.6), (-10, 4.2)], -3.6, 3.6, 'paint')
    b.box((-8.8, 0, 4.6), (1.2, 7.6, 1.6), 'metal')
    b.cyl((-0.5, 0, 6.8), 4.0, 2.4, 'team', verts=8, radius2=3.4)
    b.cyl((-1.5, -1.6, 8.3), 0.9, 0.6, 'paint', verts=10)
    b.cyl((8.5, 0, 7), 0.42, 11, 'metal', axis='x', verts=10)
    b.cyl((14, 0, 7), 0.62, 1.2, 'dark', axis='x', verts=10)
    b.box((-4.6, 1.8, 7), (1.2, 2.2, 1.6), 'paint')


def dozer(b):
    tracks(b, 13, 2.4, 3.2, 3.6)
    b.box((-1.2, 0, 3.8), (10, 5.4, 2.4), 'paint', bevel=0.3)
    b.box((-2.4, 0, 6.6), (4.6, 4.6, 3.4), 'team', bevel=0.25)
    b.box((-0.05, 0, 6.8), (0.3, 3.8, 1.8), 'glass')
    for s in (-1, 1):
        b.box((-2.4, s * 2.33, 6.9), (3.4, 0.2, 1.6), 'glass')
        b.box((5.4, s * 3.4, 2.6), (4.4, 0.6, 0.8), 'metal')
    b.prism([(7.4, 0.2), (8.4, 0.2), (8.6, 4.4), (7.6, 4.0)], -5.2, 5.2, 'metal')
    b.cyl((-4.6, 1.6, 9.2), 0.25, 2.2, 'dark', verts=6)


def collector(b):
    b.box((0, 0, 2.2), (17, 4.4, 1.0), 'metal')
    for x in (-6, -2.6, 6):
        for s in (-1, 1):
            b.cyl((x, s * 3.3, 1.6), 1.6, 1.2, 'track', axis='y', verts=14)
    b.box((6.4, 0, 4.9), (4.4, 7.4, 4.6), 'team', bevel=0.4)
    b.box((8.65, 0, 5.8), (0.2, 6.2, 1.8), 'glass')
    for s in (-1, 1):
        b.box((6.6, s * 3.72, 5.8), (2.8, 0.1, 1.6), 'glass')
    b.box((-3.2, 0, 3.1), (10.6, 8, 0.6), 'team')
    for s in (-1, 1):
        b.box((-3.2, s * 3.9, 4.2), (10.6, 0.3, 1.6), 'wood')
    b.box((-8.4, 0, 4.2), (0.3, 8, 1.6), 'wood')
    for x, y, h in ((-6, -1.8, 2.6), (-6, 1.8, 2.6), (-2.6, -1.8, 2.2), (-2.6, 1.8, 2.6), (0.6, 0, 2.2)):
        b.box((x, y, 3.4 + h / 2), (3, 3, h), 'wood', bevel=0.12)


def rifleman(b):
    for s in (-1, 1):
        b.box((0, s * 0.45, 2.6), (0.8, 0.7, 5.2), 'cloth')
        b.box((0.25, s * 0.45, 0.3), (1.2, 0.75, 0.6), 'dark')
    b.box((0, 0, 7.0), (1.2, 2.2, 3.8), 'cloth')
    b.box((0.4, 0, 5.4), (0.4, 2.3, 0.4), 'dark')
    b.box((0.1, -1.4, 7.0), (0.7, 0.6, 3.2), 'cloth')
    b.box((0.9, 1.25, 7.3), (2.0, 0.6, 0.7), 'cloth')
    b.sphere((0, 0, 9.7), 0.8, 'skin', segments=10, rings=6)
    b.sphere((0, 0, 10.05), 1.0, 'paint', scale=(1, 1, 0.75), segments=10, rings=6, top_half=True)
    b.box((1.6, 0.8, 7.5), (4.2, 0.3, 0.45), 'dark')


PARTS = {
    'Headquarters': (headquarters, 1024),
    'PowerPlant': (power_plant, 1024),
    'Barracks': (barracks, 1024),
    'Factory': (factory, 1024),
    'Tank': (tank, 512),
    'Dozer': (dozer, 512),
    'Collector': (collector, 512),
    'Rifleman': (rifleman, 256),
}


def bake(obj, size, team_nodes, mats, out, part):
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.render.bake.margin = 4
    targets = {}
    for kind in ('colour', 'ao'):
        img = bpy.data.images.new('%s_%s' % (part, kind), size, size, alpha=False, float_buffer=True)
        targets[kind] = img
    def target(img):
        for m in mats.values():
            nt = m.node_tree
            node = nt.nodes.get('bake') or nt.nodes.new('ShaderNodeTexImage')
            node.name = 'bake'
            node.image = img
            nt.nodes.active = node
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    scene.cycles.samples = 24
    target(targets['ao'])
    bpy.ops.object.bake(type='AO', margin=4, use_clear=True)
    ao = np.empty(size * size * 4, dtype=np.float32)
    targets['ao'].pixels.foreach_get(ao)
    scene.cycles.samples = 4
    results = []
    for side, rgb in CFG['sides'].items():
        lin = [(c / 255.0) ** 2.2 for c in rgb]
        for node, colour in team_nodes:
            node.inputs[1].default_value = [colour[k] * lin[k] for k in range(3)]
        target(targets['colour'])
        bpy.ops.object.bake(type='DIFFUSE', pass_filter={'COLOR'}, margin=4, use_clear=True)
        col = np.empty(size * size * 4, dtype=np.float32)
        targets['colour'].pixels.foreach_get(col)
        shade = 0.45 + 0.55 * ao[0::4]
        for k in range(3):
            # the bake is linear, the PNG holds sRGB
            col[k::4] = np.clip(col[k::4] * shade, 0.0, 1.0) ** (1 / 2.2)
        col[3::4] = 1.0
        res = bpy.data.images.new('%s_%s_out' % (part, side), size, size, alpha=False)
        res.pixels.foreach_set(col)
        res.filepath_raw = os.path.join(out, '%s_%s.png' % (part, side))
        res.file_format = 'PNG'
        res.save()
        results.append((side, res))
    for side, res in results:
        view(obj, res, os.path.join(out, '%s_%s_cameo.png' % (part, side)), 256, 192, True)
        if CFG.get('preview'):
            view(obj, res, os.path.join(out, '%s_%s_preview.png' % (part, side)), 320, 320, False)


def textured(image):
    m = bpy.data.materials.new('textured')
    m.use_nodes = True
    tex = m.node_tree.nodes.new('ShaderNodeTexImage')
    tex.image = image
    bsdf = m.node_tree.nodes['Principled BSDF']
    bsdf.inputs['Roughness'].default_value = 0.8
    m.node_tree.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    return m


def add_sun(energy=3.0, angle=(40, 10, 30)):
    sun = bpy.data.objects.new('sun', bpy.data.lights.new('sun', 'SUN'))
    sun.rotation_euler = [math.radians(a) for a in angle]
    sun.data.energy = energy
    bpy.context.collection.objects.link(sun)
    return sun


def view(obj, image, path, width, height, transparent):
    """Renders the model with its baked texture from an RTS-like view (orthographic, from the front
    right), framed to fill the picture."""
    scene = bpy.context.scene
    saved = list(obj.data.materials)
    obj.data.materials.clear()
    obj.data.materials.append(textured(image))
    cam_data = bpy.data.cameras.new('cam')
    cam_data.type = 'ORTHO'
    cam = bpy.data.objects.new('cam', cam_data)
    bpy.context.collection.objects.link(cam)
    cam.rotation_euler = (math.radians(52), 0, math.radians(38))
    # frame the projected bounding box: camera axes from the rotation, extents along them
    rot = cam.rotation_euler.to_matrix()
    right, up, back = rot.col[0], rot.col[1], rot.col[2]
    corners = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    xs = [c.dot(right) for c in corners]
    ys = [c.dot(up) for c in corners]
    aspect = width / height
    # the box corners stick out past the silhouette, so the frame can be a little tighter than the box
    cam_data.ortho_scale = max(max(xs) - min(xs), (max(ys) - min(ys)) * aspect) * 0.94
    mid = right * ((max(xs) + min(xs)) / 2) + up * ((max(ys) + min(ys)) / 2)
    cam.location = mid + back * (max(obj.dimensions) * 4)
    cam_data.clip_end = max(obj.dimensions) * 10
    scene.camera = cam
    sun = add_sun()
    scene.world = scene.world or bpy.data.worlds.new('w')
    scene.world.color = (0.35, 0.35, 0.38)
    scene.render.film_transparent = transparent
    scene.render.resolution_x, scene.render.resolution_y = width, height
    scene.cycles.samples = 16
    scene.render.image_settings.color_mode = 'RGBA' if transparent else 'RGB'
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    obj.data.materials.clear()
    for m in saved:
        obj.data.materials.append(m)
    bpy.data.objects.remove(cam)
    bpy.data.objects.remove(sun)


# loading screen: (part, side, x, y, turn in degrees); the first side's base with the other side's
# tanks coming in from the right
SCENE = [
    ('Headquarters', 0, -10, 20, 0), ('Factory', 0, -70, -25, 0), ('Barracks', 0, -60, 45, 90),
    ('PowerPlant', 0, 40, 60, 0), ('Collector', 0, 0, -30, 200), ('Dozer', 0, -30, -45, 160),
    ('Tank', 0, 22, -18, 20), ('Tank', 0, 30, -36, 10),
    ('Tank', 1, 70, -40, 165), ('Tank', 1, 82, -18, 175), ('Tank', 1, 88, -58, 150),
    ('Rifleman', 0, 12, -45, 0), ('Rifleman', 0, 15, -50, 10), ('Rifleman', 0, 9, -52, 350),
    ('Rifleman', 1, 58, -55, 170), ('Rifleman', 1, 62, -60, 180), ('Rifleman', 1, 55, -62, 175),
]


def load_scene(out, cfg):
    """Renders the loading screen picture from the built models, <out>/scene.png."""
    reset_scene()
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    sides = list(CFG['sides'])
    images = {}
    for part, side, x, y, turn in SCENE:
        bpy.ops.import_scene.gltf(filepath=os.path.join(out, part + '.gltf'))
        obj = bpy.context.selected_objects[0]
        key = '%s_%s' % (part, sides[side])
        if key not in images:
            images[key] = textured(bpy.data.images.load(os.path.join(out, key + '.png')))
        obj.data.materials.clear()
        obj.data.materials.append(images[key])
        obj.location = (x, y, 0)
        obj.rotation_euler = (0, 0, math.radians(turn))
    bpy.ops.mesh.primitive_plane_add(size=4000, location=(0, 0, 0))
    ground = bpy.context.active_object
    m = bpy.data.materials.new('ground')
    m.use_nodes = True
    nt = m.node_tree
    coord = nt.nodes.new('ShaderNodeTexCoord')
    mapping = nt.nodes.new('ShaderNodeMapping')
    mapping.inputs['Scale'].default_value = (1 / 40.0,) * 3
    tex = nt.nodes.new('ShaderNodeTexImage')
    tex.image = bpy.data.images.load(cfg['ground'])
    nt.links.new(coord.outputs['Object'], mapping.inputs['Vector'])
    nt.links.new(mapping.outputs['Vector'], tex.inputs['Vector'])
    nt.links.new(tex.outputs['Color'], nt.nodes['Principled BSDF'].inputs['Base Color'])
    ground.data.materials.append(m)
    cam_data = bpy.data.cameras.new('cam')
    cam_data.lens = 40
    cam = bpy.data.objects.new('cam', cam_data)
    bpy.context.collection.objects.link(cam)
    cam.location = (50, -175, 70)
    cam.rotation_euler = (math.radians(68), 0, math.radians(16))
    cam_data.clip_end = 3000
    scene.camera = cam
    scene.world = bpy.data.worlds.new('w')
    add_sun(4.0, (55, 0, 60))
    scene.world.color = (0.55, 0.62, 0.75)
    scene.render.resolution_x, scene.render.resolution_y = cfg['width'], cfg['height']
    scene.cycles.samples = 48
    scene.cycles.use_denoising = True
    scene.render.image_settings.color_mode = 'RGB'
    scene.render.filepath = os.path.join(out, 'scene.png')
    bpy.ops.render.render(write_still=True)


def main():
    out = CFG['out']
    os.makedirs(out, exist_ok=True)
    for part in CFG['parts']:
        reset_scene()
        mats, team_nodes = make_materials()
        build, size = PARTS[part]
        b = Builder(mats)
        build(b)
        obj = b.finish(part)
        bake(obj, size, team_nodes, mats, out, part)
        bpy.ops.object.select_all(action='DESELECT')
        obj.select_set(True)
        bpy.ops.export_scene.gltf(filepath=os.path.join(out, part + '.gltf'), export_format='GLTF_SEPARATE',
                                  use_selection=True, export_materials='NONE', export_apply=True)
        print('GZH built %s: %d triangles' % (part, sum(len(p.vertices) - 2 for p in obj.data.polygons)))
    if CFG.get('scene'):
        load_scene(out, CFG['scene'])


main()
