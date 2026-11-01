# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# W3D-Modell untersuchen: Teile, Dreiecke, Eltern-Knochen, Material/Textur, Lage in Weltkoordinaten,
# Knochen mit Weltposition; optional Vorschaubilder (Workbench, Texturfarben).
#
# Aufruf (Blender ohne Oberfläche, Add-on io_mesh_w3d installiert):
#   blender --background --factory-startup --python w3d_info.py -- <modell.w3d> [--bilder <praefix>]
# Texturen sucht das Add-on neben der W3D-Datei (DDS/TGA mit dem Namen aus dem Modell).
import os
import sys
import math
import bpy
import addon_utils
import mathutils

argv = sys.argv[sys.argv.index('--') + 1:]
datei = argv[0]
praefix = argv[argv.index('--bilder') + 1] if '--bilder' in argv else None

addon_utils.enable('io_mesh_w3d', default_set=True, persistent=False)
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o, do_unlink=True)
bpy.ops.import_mesh.westwood_w3d(filepath=datei)

meshes = sorted((o for o in bpy.data.objects if o.type == 'MESH'), key=lambda o: o.name)
gesamt = 0
for o in meshes:
    n = sum(len(p.vertices) - 2 for p in o.data.polygons)
    gesamt += n
    pts = [o.matrix_world @ v.co for v in o.data.vertices]
    mn = [min(p[i] for p in pts) for i in range(3)]
    mx = [max(p[i] for p in pts) for i in range(3)]
    tex = []
    for m in o.data.materials:
        if m and m.node_tree:
            tex += [n_.image.name for n_ in m.node_tree.nodes if n_.type == 'TEX_IMAGE' and n_.image]
    print('MESH %-14s Dr %4d Ecken %4d Knochen %-12s versteckt %d Mat %s Tex %s' % (
        o.name, n, len(o.data.vertices), o.parent_bone or '-', o.hide_get(),
        [m.name + ':' + getattr(m, 'material_type', '?') for m in o.data.materials if m], tex))
    print('     Welt min (%.2f %.2f %.2f) max (%.2f %.2f %.2f)' % (*mn, *mx))
print('DREIECKE', gesamt)
for a in [o for o in bpy.data.objects if o.type == 'ARMATURE']:
    print('SKELETT', a.name, 'delta', tuple(round(v, 4) for v in a.delta_location),
          tuple(round(v, 4) for v in a.delta_rotation_quaternion))
    for b in a.data.bones:
        w = a.matrix_world @ b.matrix_local
        loc, rot, _ = w.decompose()
        print('  KNOCHEN %-14s Eltern %-12s Welt (%.4f %.4f %.4f) Drehung %s' % (
            b.name, b.parent.name if b.parent else '-', loc.x, loc.y, loc.z,
            tuple(round(math.degrees(e), 2) for e in rot.to_euler())))

if praefix:
    pts = [o.matrix_world @ mathutils.Vector(c) for o in meshes if not o.hide_get()
           and not o.name.startswith(('HEADLIGHT', 'MUZZLEFX')) for c in o.bound_box]
    mn = [min(p[i] for p in pts) for i in range(3)]
    mx = [max(p[i] for p in pts) for i in range(3)]
    for o in meshes:
        if o.name.startswith(('HEADLIGHT',)):
            o.hide_render = True
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_WORKBENCH'
    sc.display.shading.light = 'STUDIO'
    sc.display.shading.color_type = 'TEXTURE'
    sc.render.resolution_x, sc.render.resolution_y = 1000, 700
    mitte = mathutils.Vector([(a + b) / 2 for a, b in zip(mn, mx)])
    groesse = max(mx[i] - mn[i] for i in range(3))
    for name, az, el in (('vorne', 35, 25), ('hinten', 215, 25), ('oben', 0, 89), ('seite', 90, 3)):
        cam = bpy.data.objects.new('cam_' + name, bpy.data.cameras.new('cam_' + name))
        sc.collection.objects.link(cam)
        r = groesse * 1.9
        cam.location = mitte + mathutils.Vector((r * math.cos(math.radians(az)) * math.cos(math.radians(el)),
                                                 r * math.sin(math.radians(az)) * math.cos(math.radians(el)),
                                                 r * math.sin(math.radians(el))))
        cam.rotation_euler = (mitte - cam.location).to_track_quat('-Z', 'Y').to_euler()
        sc.camera = cam
        sc.render.filepath = '%s_%s.png' % (praefix, name)
        bpy.ops.render.render(write_still=True)
        print('BILD', sc.render.filepath)
