# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Humvee aus einem KI-3D-Modell (Meshy) auf das EA-Gerüst AvHummer.w3d bringen -- TODO 62, Pilot.
#
# Blender ohne Oberfläche (5.2, Add-on io_mesh_w3d):
#   blender --background --factory-startup --python humvee_bauen.py -- <schritt> <arbeitsordner> [optionen]
#
# Schritte (jeder liest das Ergebnis des vorigen aus dem Arbeitsordner):
#   vorbereiten  EA-Modell (ea/avhummer.w3d) und Meshy-GLB laden, GLB auf EA-Maß bringen, in Teile schneiden
#                (Karosserie, Turm, vier Räder), Turm auf den TURRET-Drehpunkt und das Rohr auf MUZZLE01 legen,
#                Niedrigpoly-Teile bauen (Karosserie/Turm: Voxel-Remesh + Decimate, Räder und Rohr: Zylinder),
#                Teamfarben-Flächen HOUSECOLOR01 auf die neue Karosserie legen, UV-Atlas -> szene.blend
#   backen       je Teil aus dem feinen Modell backen: Grundfarbe, Umgebungsverdeckung, Normalen (fein und grob),
#                Abdeckung -> backen/<teil>_<pass>.exr
#   export       Texturen zusammensetzen (tools/assets/blender/humvee_textur.py rechnet außerhalb von Blender),
#                Teile unter den EA-Namen in das EA-Gerüst setzen, AVHUMMER.w3d schreiben
#   pruefen      exportierte Datei gegen das EA-Modell prüfen (Knochen auf 5 Nachkommastellen, Namen, Hierarchie)
#
# Die EA-Datei liegt nur im Arbeitsordner (entpackt mit big_werkzeug.py), nie im Repo.
import os
import sys
import json
import math
import bpy
import bmesh
import addon_utils
import numpy as np
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree

ARGV = sys.argv[sys.argv.index('--') + 1:]
SCHRITT, ARBEIT = ARGV[0], ARGV[1]
OPT = {}
for a in ARGV[2:]:
    k, _, v = a.partition('=')
    OPT[k.lstrip('-')] = v

# ------------------------------------------------------------------------------------------------
# Einstellungen für den Humvee (Meshy-Koordinaten: vorne = -x, oben = +z; EA: vorne = +x, links = +y)
# ------------------------------------------------------------------------------------------------
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import pfade  # noqa: E402
GLB = OPT.get('glb') or pfade.werkzeuge('meshy', 'Meshy_AI_Desert_Guardian_0925215852_texture.glb', hinweis='oder --glb=')
EA_W3D = os.path.join(ARBEIT, 'ea', 'avhummer.w3d')
# ZHCA-Präfix: die Engine färbt Pixel mit Alpha < 255 in der Teamfarbe um (W3DAssetManager, Recolor)
TEXTUR_NAME = OPT.get('textur', 'ZHCA_AvHummerKI')   # Name im Modell (.tga, 32 Bit unkomprimiert)
ATLAS = int(OPT.get('atlas', '1024'))
ZIEL_DR = {'CHASSIS': int(OPT.get('karosserie', '1900')), 'TURRET': int(OPT.get('turm', '340'))}
RAD_SEGMENTE = int(OPT.get('rad_segmente', '14'))
DACH_Z = 11.3246               # EA: Höhe des TURRET-Knochens = Dachoberkante
RAD_R_EA = 2.82                # EA: Radhalbmesser (Reifen z 0.08..5.72 um 2.9)
TURM_STAUCHUNG = 0.62          # Turm über dem Dach in z gestaucht (EA-Proportion: flacher Turm)
ROHR_DICKE = 2.0               # Rohrquerschnitt vergrößert (Lesbarkeit aus der RTS-Sicht)
RING_MESHY = (0.015, 0.058)    # Mitte des Turmrings in Meshy-Koordinaten (aus der Draufsicht gemessen)
TEILE = ('CHASSIS', 'TURRET', 'TIRE01', 'TIRE02', 'TIRE03', 'TIRE04', 'HOUSECOLOR01')
# Teile, die aus dem EA-Modell unverändert bleiben
EA_BLEIBT = ('TURRETUP01', 'HOUSECOLOR02', 'HOUSECOLOR03', 'MUZZLEFX01', 'MUZZLEFXUP01', 'HEADLIGHT01')


def log(*a):
    print('HUMVEE', *a, flush=True)


def leeren():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    for c in list(bpy.data.collections):
        bpy.data.collections.remove(c)
    for sammlung in (bpy.data.meshes, bpy.data.armatures, bpy.data.materials, bpy.data.images):
        for x in list(sammlung):
            if x.users == 0:
                sammlung.remove(x)


def w3d_laden(pfad):
    addon_utils.enable('io_mesh_w3d', default_set=True, persistent=False)
    bpy.ops.import_mesh.westwood_w3d(filepath=pfad)


def ecken(o, welt=True):
    n = len(o.data.vertices)
    co = np.empty(n * 3, dtype=np.float64)
    o.data.vertices.foreach_get('co', co)
    co = co.reshape(-1, 3)
    if welt:
        M = np.array(o.matrix_world)
        co = co @ M[:3, :3].T + M[:3, 3]
    return co


def ecken_setzen(o, co):
    o.data.vertices.foreach_set('co', co.astype(np.float32).ravel())
    o.data.update()


def dreiecke(o):
    return sum(len(p.vertices) - 2 for p in o.data.polygons)


def flaechen_behalten(o, maske):
    """Löscht alle Flächen, deren Maske False ist (und lose Ecken)."""
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bm.faces.ensure_lookup_table()
    weg = [f for f, m in zip(bm.faces, maske) if not m]
    bmesh.ops.delete(bm, geom=weg, context='FACES')
    lose = [v for v in bm.verts if not v.link_faces]
    bmesh.ops.delete(bm, geom=lose, context='VERTS')
    bm.to_mesh(o.data)
    bm.free()
    o.data.update()


def kopie(o, name):
    k = o.copy()
    k.data = o.data.copy()
    k.name = name
    k.data.name = name
    bpy.context.scene.collection.objects.link(k)
    return k


def aktiv(o, auswahl=()):
    bpy.ops.object.select_all(action='DESELECT')
    for s in auswahl:
        s.select_set(True)
    o.select_set(True)
    bpy.context.view_layer.objects.active = o


def ea_knochen():
    rig = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
    return rig, {b.name: (rig.matrix_world @ b.matrix_local) for b in rig.data.bones}


# ------------------------------------------------------------------------------------------------
# Schritt 1: vorbereiten
# ------------------------------------------------------------------------------------------------
def meshy_mass(co):
    """Radmitten und Halbmesser im Meshy-Modell messen, daraus die Abbildung auf das EA-Maß."""
    zmin = co[:, 2].min()
    mitten = {}
    for sx in (-1, 1):
        for sy in (-1, 1):
            q = co[(np.sign(co[:, 0]) == sx) & (np.sign(co[:, 1]) == sy)]
            tief = q[q[:, 2] < q[:, 2].min() + 0.01]
            xm = tief[:, 0].mean()
            ym = (tief[:, 1].min() + tief[:, 1].max()) / 2
            # Reifenoberkante: äußere Flanke auf der senkrechten Linie durch die Radmitte
            flanke = q[(np.abs(q[:, 0] - xm) < 0.012) & (np.abs(q[:, 1]) > abs(ym) + 0.02) & (q[:, 2] < zmin + 0.5)]
            oben = flanke[:, 2].max() if len(flanke) else zmin + 0.37
            mitten[(sx, sy)] = (xm, ym, zmin, oben)
    vorne = np.mean([mitten[(-1, s)][0] for s in (-1, 1)])
    hinten = np.mean([mitten[(1, s)][0] for s in (-1, 1)])
    spur = np.mean([abs(m[1]) for m in mitten.values()])
    # Halbmesser: die Reifenoberkante liegt im Radkasten und ist nicht sauber zu trennen -> aus der
    # Seitenansicht gemessen (Reifen z -0.524..-0.150), überschreibbar
    r = float(OPT.get('rad_meshy', '0.187'))
    return dict(vorne=vorne, hinten=hinten, spur=spur, r=r, zmin=zmin, mitten=mitten)


def vorbereiten():
    leeren()
    w3d_laden(EA_W3D)
    rig, kn = ea_knochen()
    tire = {n: kn[n].translation.copy() for n in ('TIRE01', 'TIRE02', 'TIRE03', 'TIRE04')}
    turret = kn['TURRET'].translation.copy()
    muzzle = kn['MUZZLE01'].translation.copy()
    log('EA TIRE01', tuple(tire['TIRE01']), 'TURRET', tuple(turret), 'MUZZLE01', tuple(muzzle))

    vorher = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=GLB)
    hp = [o for o in bpy.data.objects if o not in vorher and o.type == 'MESH'][0]
    hp.name = 'hp'
    co = ecken(hp)
    m = meshy_mass(co)
    radstand_ea = tire['TIRE01'].x - tire['TIRE03'].x
    sx = radstand_ea / (m['hinten'] - m['vorne'])
    sy = abs(tire['TIRE01'].y) / m['spur']
    zc = m['zmin'] + m['r']
    sz = (DACH_Z - tire['TIRE01'].z) / (OPT_DACH_MESHY - zc)
    x_off = tire['TIRE01'].x - sx * (-m['vorne'])
    log('MESHY Raeder vorne %.4f hinten %.4f Spur %.4f r %.4f zmin %.4f' % (m['vorne'], m['hinten'], m['spur'], m['r'], m['zmin']))
    log('MASS sx %.3f sy %.3f sz %.3f x_off %.3f (Rad r im EA-Mass x %.2f z %.2f, EA %.2f)' % (
        sx, sy, sz, x_off, m['r'] * sx, m['r'] * sz, RAD_R_EA))
    # Abbildung: um z gedreht (vorne +x), EA-Maß, Radmitten auf die TIRE-Knochen
    neu = np.empty_like(co)
    neu[:, 0] = x_off - sx * co[:, 0]
    neu[:, 1] = -sy * co[:, 1]
    neu[:, 2] = tire['TIRE01'].z + sz * (co[:, 2] - zc)
    hp.matrix_world = Matrix.Identity(4)
    ecken_setzen(hp, neu)
    co = neu

    # --- Flächen einteilen (Mittelpunkte) ---
    nf = len(hp.data.polygons)
    fc = np.empty(nf * 3); hp.data.polygons.foreach_get('center', fc); fc = fc.reshape(-1, 3)
    teil = np.zeros(nf, dtype=np.int8)                      # 0 Karosserie, 1 Turm, 2..5 Räder, 9 weg
    rx, rz = m['r'] * sx, m['r'] * sz
    for i, n in enumerate(('TIRE01', 'TIRE02', 'TIRE03', 'TIRE04')):
        t = tire[n]
        ell = ((fc[:, 0] - t.x) / rx) ** 2 + ((fc[:, 2] - t.z) / rz) ** 2
        seite = np.sign(fc[:, 1]) == np.sign(t.y)
        teil[(ell < 1.03 ** 2) & seite & (np.abs(fc[:, 1]) > abs(t.y) - 1.35) & (np.abs(fc[:, 1]) < abs(t.y) + 1.4)] = 2 + i
    ring_x = x_off - sx * RING_MESHY[0]
    ring_y = -sy * RING_MESHY[1]
    ueber = fc[:, 2] > DACH_Z + 0.05
    turm = ueber & (fc[:, 0] > -4.3) & (fc[:, 0] < 6.2) & (np.abs(fc[:, 1]) < 5.0)
    # Rohr (dünn, vor dem Turm über der Haube)
    rohr_f = ueber & (fc[:, 0] >= 6.2) & (np.abs(fc[:, 1]) < 0.8) & (fc[:, 2] > DACH_Z + 1.5)
    teil[turm | rohr_f] = 1
    # aufgeklappter Lukendeckel hinter dem Turm: dünne Platte, die der drehende Turm schneiden würde
    luke = ueber & (fc[:, 0] <= -4.3) & (fc[:, 0] > -8.0) & (fc[:, 2] > DACH_Z + 0.45) & (np.abs(fc[:, 1]) < 3.5)
    teil[luke] = 9
    log('FLAECHEN Karosserie %d Turm %d Raeder %s entfernt %d' % (
        (teil == 0).sum(), (teil == 1).sum(), [int((teil == k).sum()) for k in range(2, 6)], (teil == 9).sum()))

    teile_hp = {}
    for k, name in [(0, 'hp_CHASSIS'), (1, 'hp_TURRET'), (2, 'hp_TIRE01'), (3, 'hp_TIRE02'), (4, 'hp_TIRE03'), (5, 'hp_TIRE04')]:
        o = kopie(hp, name)
        flaechen_behalten(o, teil == k)
        teile_hp[name] = o
        # UV-Nähte zuschweißen (das GLB trennt die Ecken an jeder Naht): sonst liefert die Krümmung (Pointiness)
        # an jeder Naht eine helle Linie; die UV je Ecke bleiben erhalten
        bm = bmesh.new(); bm.from_mesh(o.data)
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=float(OPT.get('schweissen', '0.004')))
        bm.to_mesh(o.data); bm.free()
        if OPT.get('hp_normalen', '0') == '1':
            bm = bmesh.new(); bm.from_mesh(o.data)
            bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
            bm.to_mesh(o.data); bm.free()
    bpy.data.objects.remove(hp, do_unlink=True)

    # --- Räder: um die Knochenmitte rund machen und auf den EA-Halbmesser bringen ---
    for n in ('TIRE01', 'TIRE02', 'TIRE03', 'TIRE04'):
        o = teile_hp['hp_' + n]
        c = ecken(o)
        t = tire[n]
        aussen = c[np.abs(c[:, 1]) > abs(t.y)]
        c[:, 0] = t.x + (c[:, 0] - t.x) * RAD_R_EA / rx
        c[:, 2] = t.z + (c[:, 2] - t.z) * RAD_R_EA / rz
        yb = np.abs(c[:, 1])
        mitte_y = (np.percentile(yb, 1) + np.percentile(yb, 99.5)) / 2
        c[:, 1] += np.sign(t.y) * (abs(t.y) - mitte_y)
        ecken_setzen(o, c)
    rad_y = [np.percentile(np.abs(ecken(teile_hp['hp_TIRE01'])[:, 1]), q) for q in (2, 99.5)]
    log('RAD Breite |y| %.2f..%.2f' % tuple(rad_y))

    # --- Turm: Ring auf den Drehpunkt, flacher, Rohr auf die Mündung ---
    o = teile_hp['hp_TURRET']
    c = ecken(o)
    c[:, 0] += turret.x - ring_x
    scheibe = (np.hypot(c[:, 0] - turret.x, c[:, 1] - ring_y) < 2.9) & (c[:, 2] < DACH_Z + 0.55)
    c[scheibe, 1] -= ring_y                       # nur die Ringscheibe mittig, Schilde bleiben symmetrisch
    c[:, 2] = DACH_Z + (c[:, 2] - DACH_Z) * TURM_STAUCHUNG
    # Rohr: dünner Teil vor dem Verschluss (x > Wurzel), wird verlängert/verkürzt, geschert und dicker
    kand = (np.abs(c[:, 1] - muzzle.y) < 0.9) & (c[:, 2] > DACH_Z + 1.0)
    xs = c[kand, 0]
    # Wurzel: ab hier nach vorn ist der Querschnitt klein (Halbhöhe < 0.45)
    wurzel = None
    for x in np.arange(xs.max(), xs.min(), -0.1):
        s = c[kand & (c[:, 0] > x - 0.1) & (c[:, 0] <= x)]
        if len(s) and (s[:, 2].max() - s[:, 2].min() > 0.9 or np.ptp(s[:, 1]) > 0.9):
            wurzel = x
            break
    rohr = kand & (c[:, 0] > wurzel)
    ya, za = np.median(c[rohr, 1]), np.median(c[rohr, 2])
    spitze = c[rohr, 0].max()
    u = (c[rohr, 0] - wurzel) / (spitze - wurzel)
    c[rohr, 0] = wurzel + u * (muzzle.x - wurzel)
    c[rohr, 1] = ya + (c[rohr, 1] - ya) * ROHR_DICKE + u * (muzzle.y - ya)
    c[rohr, 2] = za + (c[rohr, 2] - za) * ROHR_DICKE + u * (muzzle.z - za)
    ecken_setzen(o, c)
    rohr_r = float(np.median(np.hypot(c[rohr, 1] - (ya + u * (muzzle.y - ya)), c[rohr, 2] - (za + u * (muzzle.z - za)))))
    log('TURM Ring x %.2f y %.2f -> Drehpunkt %.2f; Rohr Wurzel x %.2f Achse y %.2f z %.2f Spitze %.2f -> %.2f r %.3f' % (
        ring_x, ring_y, turret.x, wurzel, ya, za, spitze, muzzle.x, rohr_r))
    rohr_info = dict(wurzel=[float(wurzel), float(ya), float(za)], spitze=[muzzle.x, muzzle.y, muzzle.z], r=rohr_r * 1.15)

    # --- Dachflicken unter dem alten Ringplatz (der Turm sitzt jetzt weiter hinten) ---
    dach = teile_hp['hp_CHASSIS']
    flicken = dach_flicken(dach, ring_x, ring_y)

    # --- Niedrigpoly ---
    lp = {}
    lp['CHASSIS'] = niedrig(teile_hp['hp_CHASSIS'], 'lp_CHASSIS', ZIEL_DR['CHASSIS'], voxel=float(OPT.get('voxel_karosserie', '0')), extra=[flicken])
    t_ohne_rohr = kopie(teile_hp['hp_TURRET'], 'tmp_turm')
    cc = ecken(t_ohne_rohr)
    fcx = np.empty(len(t_ohne_rohr.data.polygons) * 3); t_ohne_rohr.data.polygons.foreach_get('center', fcx)
    fcx = fcx.reshape(-1, 3)
    fnz = np.empty(len(t_ohne_rohr.data.polygons) * 3); t_ohne_rohr.data.polygons.foreach_get('normal', fnz)
    fnz = fnz.reshape(-1, 3)
    # Ringscheibe: flacher Zylinder um den Drehpunkt; als Drehkörper gebaut (dreht sauber, rund), nicht reduziert
    rr = np.hypot(fcx[:, 0] - turret.x, fcx[:, 1] - turret.y)
    deckel = (rr > 1.6) & (rr < 2.6) & (fnz[:, 2] > 0.9) & (fcx[:, 2] < DACH_Z + 0.8)
    scheibe_z = float(np.median(fcx[deckel, 2])) if deckel.any() else DACH_Z + 0.25
    scheibe_r = float(OPT.get('ring_r', '2.75'))
    im_ring = (rr < scheibe_r + 0.2) & (fcx[:, 2] < scheibe_z + 0.08)
    log('TURM Ringscheibe r %.2f Oberkante z %.3f (%.3f ueber dem Dach), %d Flaechen' % (
        scheibe_r, scheibe_z, scheibe_z - DACH_Z, im_ring.sum()))
    flaechen_behalten(t_ohne_rohr, ~(((fcx[:, 0] > wurzel + 0.15) & (np.abs(fcx[:, 1] - ya) < 1.5) &
                                      (fcx[:, 2] > DACH_Z + 0.6)) | im_ring))
    lp['TURRET'] = niedrig(t_ohne_rohr, 'lp_TURRET', ZIEL_DR['TURRET'], voxel=float(OPT.get('voxel_turm', '0')))
    bpy.data.objects.remove(t_ohne_rohr, do_unlink=True)
    rohr_lp = zylinder('lp_rohr', Vector(rohr_info['wurzel']) - Vector((0.3, 0, 0)), Vector(rohr_info['spitze']),
                       rohr_info['r'], 6)
    ring_lp = zylinder('lp_ring', Vector((turret.x, turret.y, DACH_Z - 0.1)), Vector((turret.x, turret.y, scheibe_z)),
                       scheibe_r, int(OPT.get('ring_segmente', '16')))
    verbinden(lp['TURRET'], rohr_lp, ring_lp)
    lp['TIRE01'] = rad('TIRE01', tire['TIRE01'], rad_y)
    lp['HOUSECOLOR01'] = teamfarbe_auflegen(lp['CHASSIS'])
    # erst UV (auf dem zusammenhängenden Netz, sonst zerfällt der Atlas in Splitter), dann harte Kanten trennen
    uv_atlas(lp)
    for n, o in lp.items():
        kanten_trennen(o, float(OPT.get('kantenwinkel', '65')))
        log('NIEDRIG %-12s %5d Dreiecke' % (n, dreiecke(o)))
    # die anderen Räder: Kopie von TIRE01 samt UV, verschoben bzw. gespiegelt (Knochen symmetrisch)
    for n in ('TIRE02', 'TIRE03', 'TIRE04'):
        o = kopie(lp['TIRE01'], 'lp_' + n)
        d = tire[n] - tire['TIRE01']
        spiegeln = tire[n].y * tire['TIRE01'].y < 0
        bm = bmesh.new(); bm.from_mesh(o.data)
        for v in bm.verts:
            v.co = Vector((v.co.x + d.x, -v.co.y if spiegeln else v.co.y, v.co.z + d.z))
        if spiegeln:
            bmesh.ops.reverse_faces(bm, faces=bm.faces)
        bm.to_mesh(o.data); bm.free()
        o.data.update()
        lp[n] = o
    # Material für die feinen Teile: Original und Emission (Grundfarbe) als Variante
    hp_materialien(list(teile_hp.values()) + [flicken])
    info = dict(rohr=rohr_info, sx=sx, sy=sy, sz=sz, x_off=x_off, rad_y=rad_y, scheibe_z=scheibe_z,
                tire01=list(tire['TIRE01']))
    json.dump(info, open(os.path.join(ARBEIT, 'info.json'), 'w'), indent=1)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ARBEIT, 'szene.blend'))
    log('GESPEICHERT szene.blend')


OPT_DACH_MESHY = float(OPT.get('dach_meshy', '0.239'))   # Dachoberkante im Meshy-Modell (Höhenprofil)


def dach_flicken(dach, ring_x, ring_y):
    """Flache Platte auf Dachhöhe über der Fläche, die vorher unter der Turmscheibe lag; Farbe: Dach daneben."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = Vector((ring_x + v.co.x * 6.0, ring_y + v.co.y * 6.0, DACH_Z - 0.19 + v.co.z * 0.5))
    me = bpy.data.meshes.new('hp_flicken')
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new('hp_flicken', me)
    bpy.context.scene.collection.objects.link(o)
    # UV: alle Ecken auf die UV einer Dachfläche neben dem Ring (gleiches Material wie das Meshy-Modell)
    fc = np.empty(len(dach.data.polygons) * 3); dach.data.polygons.foreach_get('center', fc); fc = fc.reshape(-1, 3)
    nz = np.empty(len(dach.data.polygons) * 3); dach.data.polygons.foreach_get('normal', nz); nz = nz.reshape(-1, 3)
    kand = np.where((np.abs(fc[:, 2] - DACH_Z) < 0.15) & (nz[:, 2] > 0.95) & (np.abs(fc[:, 1]) < 3.5) &
                    (np.abs(fc[:, 0] - ring_x) > 3.2) & (np.abs(fc[:, 0] - ring_x) < 5.5))[0]
    uvl = dach.data.uv_layers.active.data
    # je Kandidat die UV-Mitte und die Farbe dort; genommen wird die Fläche mit der typischsten Dachfarbe
    # (Median über alle Kandidaten) -- eine beliebige UV kann auf einen Schmutzfleck zeigen
    bild = [n.image for n in dach.data.materials[0].node_tree.nodes
            if n.type == 'TEX_IMAGE' and n.image and n.image.colorspace_settings.name == 'sRGB'][0]
    bw, bh = bild.size
    px = np.empty(bw * bh * 4, dtype=np.float32)
    bild.pixels.foreach_get(px)
    px = px.reshape(bh, bw, 4)
    mitten, farben = [], []
    for i in kand:
        p = dach.data.polygons[int(i)]
        u = np.mean([uvl[li].uv[:] for li in p.loop_indices], axis=0)
        mitten.append(u)
        farben.append(px[min(bh - 1, int(u[1] % 1 * bh)), min(bw - 1, int(u[0] % 1 * bw)), :3])
    if mitten:
        farben = np.array(farben)
        typisch = np.argmin(np.linalg.norm(farben - np.median(farben, axis=0), axis=1))
        uv_mitte = np.array(mitten[typisch])
    else:
        uv_mitte = np.array([0.5, 0.5])
    me.uv_layers.new(name='UVMap')
    for l in me.uv_layers[0].data:
        l.uv = tuple(uv_mitte)
    me.materials.append(dach.data.materials[0])
    log('FLICKEN Dach-UV', tuple(np.round(uv_mitte, 4)), 'aus', len(kand), 'Flaechen')
    return o


def niedrig(quelle, name, ziel, voxel, extra=()):
    o = kopie(quelle, name)
    for e in extra:
        k = kopie(e, name + '_x')
        aktiv(o, [k])
        bpy.ops.object.join()
    o.data.materials.clear()
    for uv in list(o.data.uv_layers):
        o.data.uv_layers.remove(uv)
    aktiv(o)
    if voxel:
        o.data.remesh_voxel_size = voxel
        o.data.remesh_voxel_adaptivity = 0.0
        bpy.ops.object.voxel_remesh()
    else:
        # Nähte der UV-Inseln schließen (das GLB trennt Ecken an jeder Naht), dann direkt reduzieren
        bm = bmesh.new(); bm.from_mesh(o.data)
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=float(OPT.get('schweissen', '0.004')))
        # kleine lose Teile (Schrauben, Griffe, Splitter) weg: sie werden beim Reduzieren zu Zacken
        klein = int(OPT.get('klein', '150'))
        bm.faces.ensure_lookup_table()
        gesehen, weg = set(), []
        for f in bm.faces:
            if f in gesehen:
                continue
            stapel, komp = [f], []
            gesehen.add(f)
            while stapel:
                x = stapel.pop()
                komp.append(x)
                for e in x.edges:
                    for g in e.link_faces:
                        if g not in gesehen:
                            gesehen.add(g)
                            stapel.append(g)
            if len(komp) < klein:
                weg += komp
        bmesh.ops.delete(bm, geom=weg, context='FACES')
        # ebene Flächen vorab zusammenfassen, damit die Dreiecke in Kanten und Formen gehen, nicht ins Rauschen
        winkel = float(OPT.get('planar', '2.0'))
        if winkel > 0:
            bmesh.ops.dissolve_limit(bm, angle_limit=math.radians(winkel), verts=bm.verts, edges=bm.edges)
            bmesh.ops.triangulate(bm, faces=bm.faces, quad_method='BEAUTY', ngon_method='BEAUTY')
        log('  %s: %d kleine Flaechen entfernt, nach Planar %d Dreiecke' % (name, len(weg), len(bm.faces)))
        bm.to_mesh(o.data); bm.free()
    n0 = dreiecke(o)
    # Decimate erreicht das Ziel bei so großen Sprüngen nicht in einem Zug -> wiederholen
    for _ in range(8):
        n = dreiecke(o)
        if n <= ziel * 1.03:
            break
        mod = o.modifiers.new('dez', 'DECIMATE')
        mod.decimate_type = 'COLLAPSE'
        mod.use_collapse_triangulate = True
        mod.ratio = max(0.02, ziel / n)
        r = bpy.ops.object.modifier_apply(modifier=mod.name)
        log('  Decimate %s: %d -> %d (%s, Nutzer %d)' % (name, n, dreiecke(o), r, o.data.users))
    # kleine lose Reste weg
    bm = bmesh.new(); bm.from_mesh(o.data)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    # Flächen nach außen richten (das KI-Netz ist nicht überall gleich orientiert; die Engine blendet Rückseiten aus)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(o.data); bm.free()
    log('REMESH %s: Voxel %.2f -> %d Dreiecke -> %d' % (name, voxel, n0, dreiecke(o)))
    return o


def zylinder(name, a, b, r, seg):
    bm = bmesh.new()
    achse = (b - a)
    laenge = achse.length
    rot = achse.to_track_quat('Z', 'Y').to_matrix().to_4x4()
    ringe = []
    for z in (0.0, laenge):
        ring = []
        for i in range(seg):
            w = 2 * math.pi * i / seg
            ring.append(bm.verts.new(a + rot @ Vector((r * math.cos(w), r * math.sin(w), z))))
        ringe.append(ring)
    for i in range(seg):
        j = (i + 1) % seg
        bm.faces.new((ringe[0][i], ringe[0][j], ringe[1][j], ringe[1][i]))
    bm.faces.new(list(reversed(ringe[0])))
    bm.faces.new(ringe[1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    return o


def verbinden(ziel, *andere):
    aktiv(ziel, andere)
    bpy.ops.object.join()


def rad(name, mitte, rad_y):
    """Rad als Drehkörper um die y-Achse durch den Knochen: Nabe, Flanke, Fase, Lauffläche."""
    innen, aussen = rad_y
    b = (aussen - innen) / 2
    # Profil (Halbmesser, y relativ zur Radmitte nach außen)
    prof = [(0.0, b - 0.25), (1.45, b - 0.25), (2.45, b), (RAD_R_EA, b - 0.35), (RAD_R_EA, -b + 0.35),
            (2.45, -b), (0.0, -b)]
    s = 1 if mitte.y > 0 else -1
    bm = bmesh.new()
    ringe = []
    for r, y in prof:
        ring = []
        for i in range(RAD_SEGMENTE):
            w = 2 * math.pi * (i + 0.5) / RAD_SEGMENTE
            ring.append(bm.verts.new((mitte.x + r * math.cos(w), mitte.y + s * y, mitte.z + r * math.sin(w))))
        ringe.append(ring)
    for k in range(len(ringe) - 1):
        for i in range(RAD_SEGMENTE):
            j = (i + 1) % RAD_SEGMENTE
            a, bb, c, d = ringe[k][i], ringe[k][j], ringe[k + 1][j], ringe[k + 1][i]
            if prof[k][0] == 0.0:
                continue
            bm.faces.new((a, bb, c, d))
    # Deckel: Mitte außen (Ring 0 ist r=0 -> zu einem Punkt verschmelzen)
    for idx, nxt in ((0, 1), (len(prof) - 1, len(prof) - 2)):
        c = bm.verts.new((mitte.x, mitte.y + s * prof[idx][1], mitte.z))
        for i in range(RAD_SEGMENTE):
            j = (i + 1) % RAD_SEGMENTE
            bm.faces.new((c, ringe[nxt][i], ringe[nxt][j]))
        for v in ringe[idx]:
            bm.verts.remove(v)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    me = bpy.data.meshes.new('lp_' + name)
    bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new('lp_' + name, me)
    bpy.context.scene.collection.objects.link(o)
    return o


def teamfarbe_auflegen(karosserie):
    """EA-Teamfarbenflächen HOUSECOLOR01 (einmal unterteilt) auf die neue Karosserie projizieren."""
    ea = bpy.data.objects['HOUSECOLOR01']
    o = kopie(ea, 'lp_HOUSECOLOR01')
    o.parent = None
    o.matrix_world = ea.matrix_world.copy()
    aktiv(o)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bm = bmesh.new(); bm.from_mesh(o.data)
    # Stoßstangen-Balken weg (vorn und hinten, tief, nach vorn/hinten gerichtet): an der neuen, schwarzen
    # Meshy-Stoßstange liegen sie nicht auf; Haube, Schweller und Heckstreifen bleiben
    weg = [f for f in bm.faces if abs(f.normal.x) > 0.7 and f.calc_center_median().z < 5.5]
    bmesh.ops.delete(bm, geom=weg, context='FACES')
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    bmesh.ops.subdivide_edges(bm, edges=bm.edges, cuts=1, use_grid_fill=True)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    # EA-UV zeigen auf gemalte Details des EA-Modells (Griff, Punkte) -> alle Ecken auf eine rein weiße Stelle
    # von Housecolor2 (Pixel 176/56): die Streifen werden gleichmäßig in der Teamfarbe
    uv = bm.loops.layers.uv.active
    for f in bm.faces:
        for l in f.loops:
            l[uv].uv = (176.5 / 256, 1 - 56.5 / 256)
    bm.to_mesh(o.data); bm.free()
    log('TEAMFARBE: %d Stossstangen-Flaechen entfernt' % len(weg))
    bvh = BVHTree.FromObject(karosserie, bpy.context.evaluated_depsgraph_get())
    weit = 0
    for v in o.data.vertices:
        loc, nrm, idx, d = bvh.find_nearest(v.co)
        if loc is None:
            continue
        if d > 1.5:
            weit += 1
        v.co = loc + nrm * 0.07
    o.data.update()
    log('TEAMFARBE HOUSECOLOR01: %d Ecken projiziert, %d davon weiter als 1.5 entfernt' % (len(o.data.vertices), weit))
    return o


def kanten_trennen(o, winkel):
    bm = bmesh.new(); bm.from_mesh(o.data)
    scharf = [e for e in bm.edges if len(e.link_faces) == 2 and
              e.link_faces[0].normal.angle(e.link_faces[1].normal, 0) > math.radians(winkel)]
    bmesh.ops.split_edges(bm, edges=scharf)
    bm.to_mesh(o.data); bm.free()
    for p in o.data.polygons:
        p.use_smooth = True
    o.data.update()


ACHSEN = [Vector(a) for a in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1))]


def uv_kasten(o):
    """Kastenprojektion nach geglätteten Normalen: Das reduzierte Netz ist verrauscht, Smart-UV zerlegt es in
    tausende Splitter. Hier bekommt jede Fläche eine der sechs Achsen nach ihrer über die Nachbarschaft
    geglätteten Normalen; zusammenhängende Flächen einer Achse bilden eine Insel, orthografisch projiziert."""
    bm = bmesh.new(); bm.from_mesh(o.data)
    bm.faces.ensure_lookup_table()
    n = len(bm.faces)
    nrm = np.array([f.normal * f.calc_area() for f in bm.faces])
    nachbarn = [[g.index for e in f.edges for g in e.link_faces if g is not f] for f in bm.faces]
    for _ in range(int(OPT.get('uv_glaetten', '3'))):
        neu = nrm.copy()
        for i, nb in enumerate(nachbarn):
            for j in nb:
                neu[i] += nrm[j]
        nrm = neu
    achse = np.argmax(nrm @ np.array([tuple(a) for a in ACHSEN]).T, axis=1)
    for _ in range(2):          # einzelne Ausreißer an die Mehrheit der Nachbarn
        for i, nb in enumerate(nachbarn):
            if nb and all(achse[j] != achse[i] for j in nb):
                werte, anz = np.unique([achse[j] for j in nb], return_counts=True)
                achse[i] = werte[np.argmax(anz)]
    uv = bm.loops.layers.uv.verify()
    for f in bm.faces:
        a = achse[f.index]
        for l in f.loops:
            x, y, z = l.vert.co
            u, v = {0: (-y, z), 1: (y, z), 2: (x, z), 3: (-x, z), 4: (x, y), 5: (x, -y)}[int(a)]
            # Unterseite sieht man aus der RTS-Sicht nie -> wenig Texturfläche
            f_ = float(OPT.get('uv_unten', '0.3')) if a == 5 else 1.0
            l[uv].uv = (u * f_, v * f_)
    bm.to_mesh(o.data); bm.free()
    o.data.update()
    werte, anz = np.unique(achse, return_counts=True)
    log('UV %s: Flaechen je Achse %s' % (o.name, dict(zip(werte.tolist(), anz.tolist()))))


def uv_atlas(lp):
    """Ein gemeinsamer Atlas: Smart-UV über alle Niedrigpoly-Teile (gleiche Dichte), Turm und Rad dichter,
    alle vier Räder auf dieselben UV (TIRE01 wird gebacken), Teamfarbenflächen behalten die EA-UV."""
    teile = [lp[n] for n in ('CHASSIS', 'TURRET', 'TIRE01')]
    for o in teile:
        uv_kasten(o)
    # Dichte je Teil (Faktor auf die UV-Größe)
    for o, f in ((lp['TURRET'], float(OPT.get('dichte_turm', '1.5'))), (lp['TIRE01'], float(OPT.get('dichte_rad', '1.3')))):
        uv = o.data.uv_layers[0].data
        for l in uv:
            l.uv = (l.uv[0] * f, l.uv[1] * f)
    aktiv(teile[0], teile[1:])
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.select_all(action='SELECT')
    bpy.ops.uv.pack_islands(rotate=True, margin=float(OPT.get('uv_rand', '0.006')), shape_method='CONCAVE')
    bpy.ops.object.mode_set(mode='OBJECT')
    # Teamfarbe: EA-UV (Housecolor2) bleibt, liegt schon in der Kopie


def hp_materialien(objekte):
    """Meshy-Material: Emission aus der Grundfarbe als Variante 'emit' (fürs Backen der Grundfarbe)."""
    orig = objekte[0].data.materials[0]
    emit = orig.copy()
    emit.name = 'hp_emit'
    emit.use_fake_user = True
    nt = emit.node_tree
    tex = [n for n in nt.nodes if n.type == 'TEX_IMAGE' and n.image and n.image.colorspace_settings.name == 'sRGB'][0]
    out = [n for n in nt.nodes if n.type == 'OUTPUT_MATERIAL'][0]
    em = nt.nodes.new('ShaderNodeEmission')
    nt.links.new(tex.outputs['Color'], em.inputs['Color'])
    nt.links.new(em.outputs['Emission'], out.inputs['Surface'])
    orig.name = 'hp_orig'


# ------------------------------------------------------------------------------------------------
# Schritt 2: backen
# ------------------------------------------------------------------------------------------------
BAKE_TEILE = {'CHASSIS': ['hp_CHASSIS', 'hp_flicken'], 'TURRET': ['hp_TURRET'], 'TIRE01': ['hp_TIRE01']}


def backen():
    bpy.ops.wm.open_mainfile(filepath=os.path.join(ARBEIT, 'szene.blend'))
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'
    sc.render.bake.margin = 0
    sc.render.bake.use_selected_to_active = True
    sc.render.bake.cage_extrusion = float(OPT.get('cage', '0.35'))
    sc.render.bake.max_ray_distance = float(OPT.get('strahl', '1.2'))
    if sc.world is None:
        sc.world = bpy.data.worlds.new('welt')
    sc.world.light_settings.distance = float(OPT.get('ao_weite', '4.0'))
    ordner = os.path.join(ARBEIT, 'backen')
    os.makedirs(ordner, exist_ok=True)
    nur = OPT.get('nur')
    alle_hp = [o for o in bpy.data.objects if o.name.startswith('hp_')]
    for teil, quellen in BAKE_TEILE.items():
        if nur and teil not in nur.split(','):
            continue
        lp = bpy.data.objects['lp_' + teil]
        hps = [bpy.data.objects[q] for q in quellen]
        mat = bpy.data.materials.new('bake_' + teil)
        mat.use_nodes = True
        node = mat.node_tree.nodes.new('ShaderNodeTexImage')
        mat.node_tree.nodes.active = node
        lp.data.materials.clear()
        lp.data.materials.append(mat)
        lp.visible_diffuse = lp.visible_glossy = lp.visible_shadow = lp.visible_transmission = False
        lp.visible_volume_scatter = False
        for pas in ('farbe', 'ao', 'normal_fein', 'normal_grob', 'abdeckung', 'kanten', 'position'):
            if OPT.get('paesse') and pas not in OPT['paesse'].split(','):
                continue
            img = bpy.data.images.new('%s_%s' % (teil, pas), ATLAS, ATLAS, alpha=True, float_buffer=True)
            img.colorspace_settings.name = 'Non-Color'
            img.pixels.foreach_set(np.zeros(ATLAS * ATLAS * 4, dtype=np.float32))
            node.image = img
            for o in alle_hp:
                o.hide_render = False
            for o in bpy.data.objects:
                if o.name.startswith('lp_'):
                    o.hide_render = False
            if pas == 'farbe':
                for o in alle_hp:
                    o.data.materials[0] = bpy.data.materials['hp_emit']
                sc.cycles.samples = int(OPT.get('proben_farbe', '16'))
                aktiv(lp, hps); sc.render.bake.use_selected_to_active = True
                bpy.ops.object.bake(type='EMIT')
            elif pas == 'ao':
                # Umgebungsverdeckung auf dem groben Modell selbst (alle groben Teile sichtbar, feines Modell aus):
                # vom feinen Modell aus verdeckt das grobe (liegt darüber) bzw. zeigen Innenflächen falsch.
                # Feine Verdeckung steckt schon in der Meshy-Farbe.
                # der Turm dreht sich: er soll keinen festen Schatten in die Karosserie backen
                grob = [o for o in bpy.data.objects if o.name.startswith('lp_') and 'HOUSECOLOR' not in o.name
                        and (teil == 'TURRET' or o.name != 'lp_TURRET')]
                for o in bpy.data.objects:          # auch das EA-Modell (liegt am selben Ort) ausblenden
                    if o.type == 'MESH':
                        o.hide_render = o not in grob
                for o in grob:
                    o.hide_render = False
                    o.visible_diffuse = o.visible_glossy = o.visible_shadow = True
                sc.cycles.samples = int(OPT.get('proben_ao', '128'))
                aktiv(lp); sc.render.bake.use_selected_to_active = False
                bpy.ops.object.bake(type='AO', use_selected_to_active=False)
                for o in grob:
                    o.visible_diffuse = o.visible_glossy = o.visible_shadow = False
            elif pas == 'normal_fein':
                for o in alle_hp:
                    o.data.materials[0] = bpy.data.materials['hp_orig']
                sc.cycles.samples = 4
                sc.render.bake.normal_space = 'OBJECT'
                aktiv(lp, hps); sc.render.bake.use_selected_to_active = True
                bpy.ops.object.bake(type='NORMAL', normal_space='OBJECT')
            elif pas == 'normal_grob':
                sc.cycles.samples = 1
                aktiv(lp); sc.render.bake.use_selected_to_active = False
                bpy.ops.object.bake(type='NORMAL', normal_space='OBJECT', use_selected_to_active=False)
            elif pas == 'kanten':
                # Krümmung des feinen Modells (Cycles-Pointiness je Ecke): Kanten hell, Fugen und Vertiefungen dunkel
                for o in alle_hp:
                    o.data.materials[0] = hp_material_kanten()
                sc.cycles.samples = 4
                aktiv(lp, hps); sc.render.bake.use_selected_to_active = True
                bpy.ops.object.bake(type='EMIT')
            elif pas == 'position':
                # Weltlage je Texel (für Teamfarbenflächen, Felgen, Brandspuren): (Lage + 50) / 100
                pm = bpy.data.materials.new('position'); pm.use_nodes = True
                nt = pm.node_tree
                geo = nt.nodes.new('ShaderNodeNewGeometry')
                add = nt.nodes.new('ShaderNodeVectorMath'); add.operation = 'ADD'
                add.inputs[1].default_value = (50, 50, 50)
                mul = nt.nodes.new('ShaderNodeVectorMath'); mul.operation = 'SCALE'
                mul.inputs['Scale'].default_value = 0.01
                e = nt.nodes.new('ShaderNodeEmission')
                nt.links.new(geo.outputs['Position'], add.inputs[0])
                nt.links.new(add.outputs['Vector'], mul.inputs[0])
                nt.links.new(mul.outputs['Vector'], e.inputs['Color'])
                nt.links.new(e.outputs['Emission'], nt.nodes['Material Output'].inputs['Surface'])
                n2 = nt.nodes.new('ShaderNodeTexImage'); n2.image = img; nt.nodes.active = n2
                lp.data.materials[0] = pm
                sc.cycles.samples = 1
                aktiv(lp); sc.render.bake.use_selected_to_active = False
                bpy.ops.object.bake(type='EMIT', use_selected_to_active=False)
                lp.data.materials[0] = mat
            elif pas == 'abdeckung':
                em = bpy.data.materials.new('weiss'); em.use_nodes = True
                nt = em.node_tree
                e = nt.nodes.new('ShaderNodeEmission'); e.inputs['Color'].default_value = (1, 1, 1, 1)
                nt.links.new(e.outputs['Emission'], nt.nodes['Material Output'].inputs['Surface'])
                n2 = nt.nodes.new('ShaderNodeTexImage'); n2.image = img; nt.nodes.active = n2
                lp.data.materials[0] = em
                sc.cycles.samples = 1
                aktiv(lp); sc.render.bake.use_selected_to_active = False
                bpy.ops.object.bake(type='EMIT', use_selected_to_active=False)
                lp.data.materials[0] = mat
            img.filepath_raw = os.path.join(ordner, '%s_%s.exr' % (teil, pas))
            img.file_format = 'OPEN_EXR'
            img.save()
            log('GEBACKEN', teil, pas)


# ------------------------------------------------------------------------------------------------
# Schritt 3: Textur zusammensetzen (Farbe x Umgebungsverdeckung x Relief, Farbangleichung an EA)
# ------------------------------------------------------------------------------------------------
def bild_lesen(pfad):
    img = bpy.data.images.load(pfad)
    img.colorspace_settings.name = 'Non-Color'
    w, h = img.size
    a = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(a)
    bpy.data.images.remove(img)
    return a.reshape(h, w, 4)


def bild_schreiben(pfad, rgb):
    h, w = rgb.shape[:2]
    img = bpy.data.images.new('aus', w, h, alpha=False)
    img.colorspace_settings.name = 'Non-Color'
    a = np.ones((h, w, 4), dtype=np.float32)
    a[..., :3] = np.clip(rgb, 0, 1)
    img.pixels.foreach_set(a.ravel())
    img.filepath_raw = pfad
    img.file_format = 'PNG'
    img.save()
    bpy.data.images.remove(img)


def lin_zu_srgb(x):
    x = np.clip(x, 0, 1)
    return np.where(x <= 0.0031308, 12.92 * x, 1.055 * np.power(x, 1 / 2.4) - 0.055)


def auffuellen(rgb, maske, schritte=24):
    """Farbe über die Inselränder hinaus fortsetzen (gegen Säume in Mip-Stufen)."""
    rgb = rgb.copy()
    m = maske.astype(np.float32)
    for _ in range(schritte):
        summe = np.zeros_like(rgb)
        anz = np.zeros(m.shape, dtype=np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            summe += np.roll(np.roll(rgb * m[..., None], dy, 0), dx, 1)
            anz += np.roll(np.roll(m, dy, 0), dx, 1)
        neu = (anz > 0) & (m == 0)
        rgb[neu] = summe[neu] / anz[neu][:, None]
        m[neu] = 1
    rest = m == 0
    rgb[rest] = rgb[~rest].mean(axis=0)
    return rgb


def helligkeit(rgb):
    return rgb @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)


def hp_material_kanten():
    m = bpy.data.materials.get('hp_kanten')
    if m:
        return m
    m = bpy.data.materials.new('hp_kanten'); m.use_nodes = True
    nt = m.node_tree
    geo = nt.nodes.new('ShaderNodeNewGeometry')
    e = nt.nodes.new('ShaderNodeEmission')
    nt.links.new(geo.outputs['Pointiness'], e.inputs['Color'])
    nt.links.new(e.outputs['Emission'], nt.nodes['Material Output'].inputs['Surface'])
    return m


def unschaerf(x, r):
    """Kastenfilter zweimal (etwa Gauß), getrennt nach Achsen, Rand wiederholt."""
    for _ in range(2):
        k = 2 * r + 1
        pad = [(r + 1, r), (0, 0)] + [(0, 0)] * (x.ndim - 2)
        c = np.cumsum(np.pad(x, pad, mode='edge'), axis=0)
        x = (c[k:] - c[:-k]) / k
        pad = [(0, 0), (r + 1, r)] + [(0, 0)] * (x.ndim - 2)
        c = np.cumsum(np.pad(x, pad, mode='edge'), axis=1)
        x = (c[:, k:] - c[:, :-k]) / k
    return x


def verkleinern(a, f):
    if f == 1:
        return a
    h, w = a.shape[:2]
    return a.reshape(h // f, f, w // f, f, *a.shape[2:]).mean(axis=(1, 3))


def hsv_zu_rgb(h, s, v):
    import colorsys
    return np.array(colorsys.hsv_to_rgb(h, s, v), dtype=np.float32)


def wertrauschen(p, zelle, saat):
    """Glattes 3D-Wertrauschen an den Weltlagen p (N x 3): Zufallswerte am Gitter, trilinear."""
    q = p / zelle
    i = np.floor(q).astype(np.int64)
    t = q - i
    t = t * t * (3 - 2 * t)

    def h(ix, iy, iz):
        n = (ix * 73856093) ^ (iy * 19349663) ^ (iz * 83492791) ^ (saat * 2654435761)
        n = (n ^ (n >> 13)) * 1274126177
        return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0

    out = 0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (t[:, 0] if dx else 1 - t[:, 0]) * (t[:, 1] if dy else 1 - t[:, 1]) * (t[:, 2] if dz else 1 - t[:, 2])
                out = out + w * h(i[:, 0] + dx, i[:, 1] + dy, i[:, 2] + dz)
    return out


def textur():
    """Farbtextur zusammensetzen (Nachbesserung 2026-09-26): Farbe x Verdeckung x Relief x Krümmung, lokaler
    Kontrast, Farbton zum hellen Sand der EA-Fahrzeuge, helle Felgen, Teamfarbe großflächig über die
    ZHCA-Mechanik der Engine (Alpha < 255 = Teamfarbe, W3DAssetManager Recolor), dazu die beschädigte Fassung."""
    ordner = os.path.join(ARBEIT, 'backen')
    aus = os.path.join(ARBEIT, 'textur')
    os.makedirs(aus, exist_ok=True)
    info = json.load(open(os.path.join(ARBEIT, 'info.json')))
    f_ = ATLAS // int(OPT.get('ausgabe', '512'))
    N = ATLAS // f_
    farbe = np.zeros((N, N, 3), np.float32); ao = np.ones((N, N), np.float32)
    nf = np.zeros((N, N, 3), np.float32); ng = np.zeros((N, N, 3), np.float32)
    kr = np.zeros((N, N), np.float32); pos = np.zeros((N, N, 3), np.float32)
    teil_id = np.zeros((N, N), np.int8)
    treffer_alle = np.zeros((N, N), bool)
    for k, teil in enumerate(BAKE_TEILE, 1):
        lies = lambda pas: verkleinern(bild_lesen(os.path.join(ordner, '%s_%s.exr' % (teil, pas))), f_)
        cov = lies('abdeckung')[..., 0] > 0.5
        f = lies('farbe')
        treffer = f[..., 3] > 0.5
        farbe[cov] = f[cov, :3]
        ao[cov] = lies('ao')[cov, 0]
        nf[cov] = lies('normal_fein')[cov, :3] * 2 - 1
        ng[cov] = lies('normal_grob')[cov, :3] * 2 - 1
        leer = cov & ~treffer
        nf[leer] = ng[leer]
        kk = lies('kanten')[..., 0]
        ok = cov & treffer
        if ok.any():
            m = np.median(kk[ok])
            spanne = np.percentile(kk[ok], 95) - np.percentile(kk[ok], 5)
            kr[ok] = np.clip((kk[ok] - m) / max(spanne, 1e-4) * 2, -1, 1)
        pos[cov] = lies('position')[cov, :3] * 100 - 50
        teil_id[cov] = k
        treffer_alle |= ok
        log('TEIL %s: Abdeckung %d Pixel (%dx%d), ohne Treffer %d' % (teil, cov.sum(), N, N, leer.sum()))
    maske = teil_id > 0
    kar, turm, rad_ = teil_id == 1, teil_id == 2, teil_id == 3
    srgb = lin_zu_srgb(farbe)
    albedo_l = helligkeit(srgb)
    nfz = nf / np.maximum(np.linalg.norm(nf, axis=2, keepdims=True), 1e-4)
    ngz = ng / np.maximum(np.linalg.norm(ng, axis=2, keepdims=True), 1e-4)
    relief = 1 + float(OPT.get('relief_oben', '0.45')) * (nfz[..., 2] - ngz[..., 2]) \
        - float(OPT.get('relief_kante', '0.7')) * (1 - np.clip((nfz * ngz).sum(2), -1, 1))
    relief = np.clip(relief, 0.45, 1.35)
    ao_f = 1 - float(OPT.get('ao_staerke', '0.8')) * (1 - np.clip(ao, 0, 1))
    kant = 1 + float(OPT.get('kruemmung', '0.3')) * kr
    rgb = srgb * (ao_f * relief * kant)[..., None]
    # lokaler Kontrast (Unscharfmaskierung der Helligkeit, Radius ~ 2 Texel bei 512)
    voll = auffuellen(np.clip(rgb, 0, 1), maske & treffer_alle, 12)
    l = helligkeit(voll)
    lb = unschaerf(l, max(1, int(OPT.get('scharf_r', '2'))))
    rgb = rgb * np.clip(1 + float(OPT.get('scharf', '1.2')) * (l - lb) / np.maximum(lb, 0.05), 0.5, 1.6)[..., None]
    # Farbton und Helligkeit: Lack (sichtbare Karosserie, helle Hälfte) auf hellen Sand wie EA, leicht wärmer
    l = helligkeit(rgb)
    sicht = kar & treffer_alle & (ngz[..., 2] > -0.2)
    lack = sicht & (l > np.percentile(l[sicht], 50))
    ziel = hsv_zu_rgb(float(OPT.get('ton', '0.125')), float(OPT.get('saettigung', '0.19')), float(OPT.get('wert', '0.80')))
    ist = rgb[lack].mean(axis=0)
    rgb = rgb * (ziel / ist)[None, None, :]
    log('FARBE Lack vorher %s -> Ziel %s (EA-Lack 0.785 0.768 0.701)' % (np.round(ist, 3), np.round(ziel, 3)))
    # Felgen hell (wie EA): Außenseite der Radscheibe innerhalb der Nabe
    t = info['tire01']
    r_achse = np.hypot(pos[..., 0] - t[0], pos[..., 2] - t[2])
    felge = rad_ & (r_achse < float(OPT.get('felge_r', '1.6'))) & (pos[..., 1] > t[1] + 0.2)
    if felge.any():
        lf = helligkeit(rgb)[felge]
        detail = np.clip(0.6 + 0.5 * (lf / max(float(lf.mean()), 1e-3) - 1), 0.35, 1.15)
        rgb[felge] = ziel[None, :] * float(OPT.get('felge_hell', '0.95')) * detail[:, None]
    log('FELGE %d Texel' % felge.sum())
    rgb = np.clip(rgb, 0, 1)
    # Teamfarbe: Haube, Türen/Seiten, Turmschilde (ohne Scheiben, Waffe, Schwarzes)
    hell = albedo_l > float(OPT.get('team_min_hell', '0.3'))
    haube = kar & (pos[..., 0] > 8.4) & (ngz[..., 2] > 0.5) & (np.abs(pos[..., 1]) < 6.2) & (pos[..., 2] > 6.0)
    seite = kar & (np.abs(ngz[..., 1]) > 0.75) & (pos[..., 0] > -4.8) & (pos[..., 0] < 6.9) & \
        (pos[..., 2] > 5.2) & (pos[..., 2] < 9.9)
    schilde = turm & (pos[..., 2] > info['scheibe_z'] + 0.3) & (pos[..., 0] < info['rohr']['wurzel'][0] - 0.3)
    team = (haube | seite | schilde) & hell & treffer_alle
    team = unschaerf(team.astype(np.float32), 1) > 0.5          # Einzelpixel und Ausfransen glätten
    team &= maske
    log('TEAM Haube %d Seiten %d Schilde %d -> %d Texel (%.1f %% von Karosserie und Turm)' % (
        (haube & hell).sum(), (seite & hell).sum(), (schilde & hell).sum(), team.sum(),
        100.0 * team.sum() / max(1, (kar | turm).sum())))

    def ausgeben(name, farbe_rgb, team_m, team_v, vorschau_farbe=(0.15, 0.35, 1.0)):
        rgba = np.zeros((N, N, 4), np.float32)
        rgba[..., :3] = farbe_rgb
        rgba[..., 3] = 1
        rgba[team_m, 0] = team_v[team_m]
        rgba[team_m, 1] = 0
        rgba[team_m, 2] = 0
        rgba[team_m, 3] = 0
        rgba = auffuellen(rgba, maske, 24)
        rgba[maske & ~team_m, 3] = 1
        schreibe_rgba(os.path.join(aus, name + '.png'), rgba)
        # Vorschau mit Teamfarbe (so färbt die Engine: Farbton und Sättigung der Teamfarbe, Helligkeit der Textur)
        import colorsys
        th, ts, _ = colorsys.rgb_to_hsv(*vorschau_farbe)
        v = rgba[..., 3] < 1
        vor = rgba[..., :3].copy()
        vor[v] = hsv_zu_rgb(th, ts, 1.0)[None, :] * rgba[..., 0][v][:, None]
        bild_schreiben(os.path.join(aus, name + '_vorschau.png'), vor)
        log('TEXTUR', name, N, 'x', N)

    team_v = np.clip(helligkeit(rgb) * float(OPT.get('team_hell', '1.1')), 0.12, 1.0)
    ausgeben(TEXTUR_NAME, rgb, team, team_v)
    # beschädigte Fassung: dunkler, entsättigt, Ruß in Flecken (3D-Rauschen über die Weltlage, oben stärker)
    l = helligkeit(rgb)
    grau = l[..., None] + (rgb - l[..., None]) * 0.5
    dunkel = grau * float(OPT.get('d_dunkel', '0.78'))
    p = pos.reshape(-1, 3)
    n1 = wertrauschen(p, 3.0, 1) * 0.65 + wertrauschen(p, 1.1, 2) * 0.35
    russ = np.clip((n1.reshape(N, N) - float(OPT.get('russ_schwelle', '0.5'))) / 0.18, 0, 1) + 0.15 * np.clip(ngz[..., 2], 0, 1)
    russ = (np.clip(russ, 0, 1) * maske).astype(np.float32)
    braun = np.array([0.20, 0.13, 0.08], np.float32)
    schwarz = np.array([0.045, 0.04, 0.035], np.float32)
    rand = np.clip(1 - np.abs(russ - 0.35) / 0.2, 0, 1)[..., None]      # angesengter Rand um die Rußflecken
    d_rgb = dunkel * (1 - rand * 0.4) + braun * rand * 0.4
    d_rgb = d_rgb * (1 - russ[..., None] * 0.9) + schwarz * russ[..., None] * 0.9
    ausgeben(TEXTUR_NAME + 'D', d_rgb, team & (russ < 0.5), np.clip(team_v * 0.6 * (1 - russ), 0.08, 1.0))
    bild_schreiben(os.path.join(aus, 'k_kruemmung.png'), np.repeat(((kr + 1) / 2)[..., None], 3, 2))
    bild_schreiben(os.path.join(aus, 'k_relief.png'), np.repeat((relief / 1.35)[..., None], 3, 2))


def schreibe_rgba(pfad, rgba):
    h, w = rgba.shape[:2]
    img = bpy.data.images.new('aus', w, h, alpha=True)
    img.colorspace_settings.name = 'Non-Color'
    img.alpha_mode = 'STRAIGHT'
    img.pixels.foreach_set(np.clip(rgba, 0, 1).astype(np.float32).ravel())
    img.filepath_raw = pfad
    img.file_format = 'PNG'
    img.save()
    bpy.data.images.remove(img)


# ------------------------------------------------------------------------------------------------
# Schritt 4: export (Textur liegt fertig als PNG vor)
# ------------------------------------------------------------------------------------------------
FASSUNG = OPT.get('fassung', '').upper()        # '' = AvHummer, 'D' = AvHummer_D (beschädigt)
MODELL = 'AVHUMMER' + ('_' + FASSUNG if FASSUNG else '')


def ea_datei():
    return os.path.join(ARBEIT, 'ea', MODELL.lower() + '.w3d')


def export():
    bpy.ops.wm.open_mainfile(filepath=os.path.join(ARBEIT, 'szene.blend'))
    # Lage der Teile im normalen EA-Modell: die neuen Netze liegen relativ zu diesen Knochen (bei der beschädigten
    # Fassung sitzen TURRET/MUZZLE01 3,5 Hundertstel tiefer -- der Turm folgt so seinem Knochen, nicht der Welt)
    lage = {n: bpy.data.objects[n].matrix_world.copy() for n in TEILE}
    if FASSUNG:
        # EA-Gerüst der Fassung laden statt des normalen
        for o in list(bpy.data.objects):
            if not o.name.startswith(('lp_', 'hp_', 'tmp_')):
                bpy.data.objects.remove(o, do_unlink=True)
        for c in list(bpy.data.collections):
            bpy.data.collections.remove(c)
        for sammlung in (bpy.data.meshes, bpy.data.armatures):
            for x in list(sammlung):
                if x.users == 0:
                    sammlung.remove(x)
        w3d_laden(ea_datei())
    textur_name = TEXTUR_NAME + FASSUNG
    textur_png = os.path.join(ARBEIT, 'textur', textur_name + '.png')
    img = bpy.data.images.load(textur_png)
    img.name = textur_name + '.tga'
    ea_mat = bpy.data.objects['CHASSIS'].data.materials[0]
    mat = ea_mat.copy()
    mat.name = 'AvHummerKI'
    for n in mat.node_tree.nodes:
        if n.type == 'TEX_IMAGE':
            n.image = img
    img.filepath_raw = textur_name + '.tga'      # das Add-on schreibt den Dateinamen ins Modell
    for name in TEILE:
        ea = bpy.data.objects[name]
        lp = bpy.data.objects['lp_' + name]
        alt = ea.data
        me = lp.data.copy()
        bm = bmesh.new(); bm.from_mesh(me)
        bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
        bm.to_mesh(me); bm.free()
        # Weltkoordinaten -> lokale Koordinaten des EA-Teils (am Knochen)
        inv = lage[name].inverted()
        me.transform(inv)
        me.name = name
        for prop in ('object_type', 'casts_shadow', 'two_sided', 'sort_level', 'userText'):
            try:
                setattr(me, prop, getattr(alt, prop))
            except AttributeError:
                pass
        me.materials.clear()
        if name.startswith('HOUSECOLOR'):
            me.materials.append(alt.materials[0])
        else:
            me.materials.append(mat)
        ea.data = me
        bpy.data.meshes.remove(alt)
        log('TEIL %-12s %5d Dreiecke %5d Ecken' % (name, dreiecke(ea), len(me.vertices)))
    # alles außer dem EA-Gerüst entfernen (das Add-on exportiert jedes Mesh der Szene)
    for o in list(bpy.data.objects):
        if o.name.startswith(('lp_', 'hp_', 'tmp_')) or o.type not in ('MESH', 'ARMATURE'):
            bpy.data.objects.remove(o, do_unlink=True)
    for c in bpy.data.collections:
        if c.name.upper() != MODELL and len(c.objects):
            log('SAMMLUNG uebrig', c.name, [o.name for o in c.objects])
    gesamt = sum(dreiecke(o) for o in bpy.data.objects if o.type == 'MESH')
    log('DREIECKE gesamt', gesamt)
    ziel = os.path.join(ARBEIT, 'export', MODELL + '.w3d')
    os.makedirs(os.path.dirname(ziel), exist_ok=True)
    # Texturnamen wie im EA-Modell (.tga; die Engine lädt die gleichnamige .dds). Das Add-on schreibt den Dateinamen
    # des Bildes -- für die übernommenen EA-Teile sonst ohne Endung.
    for bild in bpy.data.images:
        if bild.name in ('Render Result', 'Viewer Node'):
            continue
        stamm = os.path.splitext(os.path.basename(bild.filepath_raw or bild.name))[0] or bild.name.split('.')[0]
        # ohne '//': unter Windows hielte os.path.basename '//x.tga' für einen UNC-Pfad und lieferte ''
        bild.filepath_raw = stamm + '.tga'
        bild.name = stamm + '.tga'
    addon_utils.enable('io_mesh_w3d', default_set=True, persistent=False)
    bpy.ops.export_mesh.westwood_w3d(filepath=ziel, file_format='W3D', export_mode='HM', use_existing_skeleton=False)
    log('EXPORT', ziel, os.path.getsize(ziel), 'Bytes')
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ARBEIT, 'export%s.blend' % FASSUNG.lower()))


# ------------------------------------------------------------------------------------------------
# Schritt 4: prüfen
# ------------------------------------------------------------------------------------------------
def lesen(pfad):
    leeren()
    w3d_laden(pfad)
    knochen, meshes = {}, {}
    for o in bpy.data.objects:
        if o.type == 'ARMATURE':
            for b in o.data.bones:
                knochen[b.name] = ([round(v, 5) for row in b.matrix_local for v in row],
                                   b.parent.name if b.parent else None)
        elif o.type == 'MESH':
            tex = [n.image.name for m in o.data.materials if m and m.node_tree
                   for n in m.node_tree.nodes if n.type == 'TEX_IMAGE' and n.image]
            meshes[o.name] = (o.parent_bone, dreiecke(o), tex, o.hide_get())
    return knochen, meshes


def pruefen():
    k1, m1 = lesen(ea_datei())
    k2, m2 = lesen(os.path.join(ARBEIT, 'export', MODELL + '.w3d'))
    print('PRUEFUNG', MODELL)
    abw = [n for n in k1 if k1[n] != k2.get(n)]
    print('PRUEFUNG Knochen', len(k1), 'EA /', len(k2), 'neu:', 'gleich (Lage 5 Nachkommastellen, Eltern)'
          if not abw and set(k1) == set(k2) else 'ABWEICHEND %s' % abw)
    print('PRUEFUNG Teile', 'gleiche Namen' if set(m1) == set(m2) else 'ABWEICHEND %s / %s' % (sorted(m1), sorted(m2)))
    for n in sorted(m1):
        print('  %-14s EA Knochen %-12s %4d Dr %s | neu %s' % (n, m1[n][0], m1[n][1], m1[n][2], m2.get(n)))


def ansicht():
    """Kontrollbilder: --satz=hp (feine Teile, Textur), lp (Niedrigpoly, Farbe je Teil), ea (EA-Modell),
    export (fertiges W3D mit Textur); dazu --blend=<datei> (Vorgabe szene.blend)."""
    satz = OPT.get('satz', 'lp')
    if satz in ('ea', 'export'):
        leeren()
        w3d_laden(ea_datei() if satz == 'ea' else os.path.join(ARBEIT, 'export', MODELL + '.w3d'))
        weg = ('HEADLIGHT', 'TURRETUP', 'MUZZLEFXUP', 'HOUSECOLOR03') + (() if OPT.get('feuer') else ('MUZZLEFX',))
        zeigen = [o for o in bpy.data.objects if o.type == 'MESH' and not o.name.startswith(weg)]
    else:
        bpy.ops.wm.open_mainfile(filepath=os.path.join(ARBEIT, OPT.get('blend', 'szene.blend')))
        zeigen = [o for o in bpy.data.objects if o.type == 'MESH' and o.name.startswith(satz + '_')]
        if OPT.get('mit_ea'):
            zeigen += [bpy.data.objects[n] for n in ('TURRETUP01', 'HOUSECOLOR02', 'MUZZLEFX01')]
    for o in bpy.data.objects:
        o.hide_render = o not in zeigen
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_WORKBENCH'
    sc.display.shading.light = 'STUDIO'
    sc.display.shading.color_type = 'OBJECT' if satz == 'lp' else 'TEXTURE'
    sc.display.shading.show_cavity = False
    if satz == 'lp':
        farben = {'CHASSIS': (0.8, 0.75, 0.6), 'TURRET': (0.4, 0.6, 0.9), 'HOUSECOLOR01': (0.9, 0.1, 0.1)}
        for o in zeigen:
            o.color = (*farben.get(o.name[3:], (0.3, 0.3, 0.3)), 1)
    sc.render.resolution_x, sc.render.resolution_y = 1200, 800
    ziel = OPT.get('bilder', os.path.join(ARBEIT, 'ansicht', satz))
    os.makedirs(os.path.dirname(ziel), exist_ok=True)
    mitte = Vector((1.0, 0.0, 6.5))
    for name, az, el, r, ortho in (('vorne', 35, 25, 50, 0), ('hinten', 215, 25, 50, 0), ('seite', -90, 0, 60, 30),
                                   ('oben', 0, 90, 60, 30), ('rts', 150, 55, 70, 0)):
        cam = bpy.data.objects.new('c' + name, bpy.data.cameras.new('c' + name))
        sc.collection.objects.link(cam)
        if ortho:
            cam.data.type = 'ORTHO'
            cam.data.ortho_scale = ortho
        cam.data.lens = 50
        cam.location = mitte + Vector((r * math.cos(math.radians(az)) * math.cos(math.radians(el)),
                                       r * math.sin(math.radians(az)) * math.cos(math.radians(el)),
                                       r * math.sin(math.radians(el))))
        cam.rotation_euler = (mitte - cam.location).to_track_quat('-Z', 'Y' if el < 89 else 'X').to_euler()
        sc.camera = cam
        sc.render.filepath = '%s_%s.png' % (ziel, name)
        bpy.ops.render.render(write_still=True)
        log('BILD', sc.render.filepath)


{'vorbereiten': vorbereiten, 'backen': backen, 'textur': textur, 'export': export, 'pruefen': pruefen, 'ansicht': ansicht}[SCHRITT]()
