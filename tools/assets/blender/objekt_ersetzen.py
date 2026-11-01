# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Unbewegliche Requisiten (Felsen, Deko) aus CC0-Scans.
# Verallgemeinert den Humvee-Weg (humvee_bauen.py) für Modelle ohne bewegliche Teile. Liest keine EA-Datei: Lage und
# Größe der Teile sowie die Zielfarbe stehen als Zahlen im Auftrag, das W3D-Gerüst schreibt w3d_geruest.py.
#
# Blender ohne Oberfläche (5.2, Add-on io_mesh_w3d):
#   blender --background --factory-startup --python objekt_ersetzen.py -- <schritt> <auftrag.json> <objekt> [--k=v ...]
#
# Der Auftrag (tools/assets/blender/natur_objekte.json) nennt je Objekt den Modellnamen der INI, die Scan-Datei und je
# Scan-Teil die Lage im Modell ("lage": Mitte, Winkel der Hauptachse, Maßstab; berechnet mit natur_anordnen.py);
# Arbeitsordner <arbeit>/<objekt>/.
#
# Schritte (jeder liest das Ergebnis des vorigen):
#   vorbereiten  Scan laden; je Teil drehen (Hauptachse der Grundfläche), gleichmäßig skalieren, an seine Mitte setzen
#                und etwas in den Boden versenken; Niedrigpoly (Decimate) mit UV-Atlas
#                -> szene.blend
#   backen       Grundfarbe (Emission), Umgebungsverdeckung (vom Scan, mit Bodenebene), Normalen fein/grob, Abdeckung
#   textur       Farbe x Verdeckung x Relief (Licht von oben), Farbe und Kontrast je Kanal auf die Zielwerte des
#                Auftrags ("farbziel") angeglichen, auf die Ausgabegröße verkleinert -> textur/<textur>.png (+ Fassung)
#   export       Niedrigpoly als Geometrieträger über das Add-on (export/<MODELL>_roh.w3d), Normalen an Nähten
#                gemittelt, dann mit eigenem Gerüst neu geschrieben (w3d_geruest.requisit) -> export/<MODELL>.w3d;
#                Fassungen (etwa _D) mit --fassung=<schluessel>
#   ansicht      Kontrollbilder neu (Workbench, Textur deckend); mit --ea=<datei> daneben das EA-Modell zum Vergleich
# Prüfen ohne Blender: python natur_bauen.py pruefen <objekt> (Sollwerte der Vorschrift)
import json
import math
import os
import sys

import addon_utils
import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

ARGV = sys.argv[sys.argv.index('--') + 1:]
SCHRITT, AUFTRAG_DATEI, NAME = ARGV[0], ARGV[1], ARGV[2]
OPT = {}
for a in ARGV[3:]:
    k, _, v = a.partition('=')
    OPT[k.lstrip('-')] = v
AUFTRAG = json.load(open(AUFTRAG_DATEI, encoding='utf-8'))
CFG = dict(AUFTRAG.get('vorgaben', {}))
CFG.update(AUFTRAG['objekte'][NAME])
CFG.update({k: (type(CFG[k])(v) if k in CFG and not isinstance(CFG[k], (list, dict)) else v) for k, v in OPT.items()})
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import pfade  # noqa: E402
ARBEIT_WURZEL = pfade.unter_werkzeuge(CFG.get('arbeit', 'cc0/arbeit'))
ARBEIT = os.path.join(ARBEIT_WURZEL, NAME)
MODELLE = pfade.unter_werkzeuge(CFG.get('modelle', 'cc0/modelle'))
AUSGABE = int(CFG.get('groesse', 256))
# Texturstufen: {"sd": 256, "hd": 1024, "uhd": 2048}; ohne Angabe nur SD = groesse.
STUFEN = {k: int(v) for k, v in (CFG.get('stufen') or {'sd': AUSGABE}).items()}
# Backgröße: mindestens 1024, sonst die größte Stufe; die Stufen werden daraus verkleinert
ATLAS = int(CFG.get('atlas', max([1024] + list(STUFEN.values()))))


def log(*a):
    print('OBJEKT', NAME, *a, flush=True)


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


def dreiecke(o):
    return sum(len(p.vertices) - 2 for p in o.data.polygons)


def aktiv(o, auswahl=()):
    bpy.ops.object.select_all(action='DESELECT')
    for s in auswahl:
        s.select_set(True)
    o.select_set(True)
    bpy.context.view_layer.objects.active = o


def kopie(o, name):
    k = o.copy()
    k.data = o.data.copy()
    k.name = name
    k.data.name = name
    bpy.context.scene.collection.objects.link(k)
    return k


def modellname(fassung=None):
    return CFG['fassungen'][fassung]['modell'] if fassung else CFG['modell']


# ------------------------------------------------------------------------------------------------
# Schritt 1: vorbereiten
# ------------------------------------------------------------------------------------------------
def grundflaeche(pts):
    """Mitte, Winkel der Hauptachse und Ausdehnung (Länge, Breite) der Grundfläche."""
    xy = pts[:, :2]
    m = (xy.min(0) + xy.max(0)) / 2
    c = np.cov((xy - xy.mean(0)).T)
    w, v = np.linalg.eigh(c)
    achse = v[:, np.argmax(w)]
    winkel = math.atan2(achse[1], achse[0])
    r = np.array([[math.cos(-winkel), -math.sin(-winkel)], [math.sin(-winkel), math.cos(-winkel)]])
    q = (xy - m) @ r.T
    mq = (q.min(0) + q.max(0)) / 2
    m = m + mq @ r            # Mitte der gedrehten Hülle
    return m, winkel, np.ptp(q[:, 0]), np.ptp(q[:, 1])


def einpassen(lage, obj, zu):
    """Scan-Teil obj gleichmäßig skaliert in seine Lage bringen: Hauptachse der Grundfläche auf den Winkel, Mitte der
    Grundfläche auf die Mitte, Unterkante um `versenken` x Höhe unter Gelände (Gelände ist nicht eben; lieber versenkt
    als schwebend). lage = {"mitte": [x, y], "winkel": Grad, "massstab": Spieleinheiten je Meter des Scans} aus dem
    Auftrag (natur_anordnen.py)."""
    q = ecken(obj)
    m_z, w_z = np.array(lage['mitte'], dtype=np.float64), math.radians(float(lage['winkel']) + float(zu.get('drehung', 0)))
    s = float(lage['massstab'])
    m_s, w_s, l_s, b_s = grundflaeche(q)
    versenken = float(zu.get('versenken', CFG.get('versenken', 0.15)))
    M = Matrix.Translation(Vector((m_z[0], m_z[1], 0))) @ Matrix.Rotation(w_z, 4, 'Z') @         Matrix.Diagonal(Vector((s, s, s, 1))) @ Matrix.Rotation(-w_s, 4, 'Z') @ Matrix.Translation(Vector((-m_s[0], -m_s[1], 0)))
    obj.data.transform(M @ obj.matrix_world)
    obj.matrix_world = Matrix.Identity(4)
    obj.data.update()
    neu = ecken(obj)
    hoehe = np.ptp(neu[:, 2])
    obj.data.transform(Matrix.Translation(Vector((0, 0, -versenken * hoehe - neu[:, 2].min()))))
    obj.data.update()
    neu = ecken(obj)
    log('EINPASSEN %s: Massstab %.2f Winkel %.0f | Scan L %.3f B %.3f H %.3f m | neu L %.2f B %.2f z %.2f..%.2f' % (
        obj.name, s, math.degrees(w_z), l_s, b_s, np.ptp(q[:, 2]), l_s * s, b_s * s, neu[:, 2].min(), neu[:, 2].max()))


def niedrig(hp, name, ziel):
    """Scan-Teil reduzieren: verschweißen, Decimate in mehreren Zügen, Flächen nach außen richten."""
    o = kopie(hp, name)
    o.data.materials.clear()
    for uv in list(o.data.uv_layers):
        o.data.uv_layers.remove(uv)
    bm = bmesh.new(); bm.from_mesh(o.data)
    groesse = max(o.dimensions)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=groesse * 1e-4)
    planar = float(CFG.get('planar', 0))
    if planar > 0:
        # ebene Flächen vorab zusammenfassen (Gefertigtes wie Fässer): die Dreiecke gehen in Kanten und Rundungen
        bmesh.ops.dissolve_limit(bm, angle_limit=math.radians(planar), verts=bm.verts, edges=bm.edges)
        bmesh.ops.triangulate(bm, faces=bm.faces, quad_method='BEAUTY', ngon_method='BEAUTY')
    bm.to_mesh(o.data); bm.free()
    aktiv(o)
    for _ in range(8):
        n = dreiecke(o)
        if n <= ziel * 1.03:
            break
        mod = o.modifiers.new('dez', 'DECIMATE')
        mod.decimate_type = 'COLLAPSE'
        mod.use_collapse_triangulate = True
        mod.ratio = max(0.01, ziel / n)
        bpy.ops.object.modifier_apply(modifier=mod.name)
    bm = bmesh.new(); bm.from_mesh(o.data)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(o.data); bm.free()
    log('NIEDRIG %s: %d Dreiecke' % (name, dreiecke(o)))
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


def uv_naehte_trennen(me):
    """Kanten an UV-Nähten trennen, so dass jede Ecke genau eine UV hat. Das Add-on trennt sonst selbst, aber grob
    (ganze Dreiecke an jeder Naht -- aus 820 Ecken wurden 2.500)."""
    bm = bmesh.new(); bm.from_mesh(me)
    uv = bm.loops.layers.uv.active
    naht = []
    for e in bm.edges:
        if len(e.link_loops) != 2:
            continue
        l1, l2 = e.link_loops
        # l1 läuft v0->v1, l2 in Gegenrichtung: gleiche Ecke = l1 mit l2.link_loop_next
        a1, b1 = l1[uv].uv, l1.link_loop_next[uv].uv
        a2, b2 = l2.link_loop_next[uv].uv, l2[uv].uv
        if (a1 - a2).length > 1e-6 or (b1 - b2).length > 1e-6:
            naht.append(e)
    bmesh.ops.split_edges(bm, edges=naht)
    bm.to_mesh(me); bm.free()
    me.update()
    return len(naht)


def uv_atlas(lp):
    aktiv(lp)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(float(CFG.get('uv_winkel', 85))),
                             island_margin=float(CFG.get('uv_rand', 0.003)), area_weight=0.0)
    bpy.ops.object.mode_set(mode='OBJECT')
    unten_verkleinern(lp, float(CFG.get('uv_unten', 0.25)))
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.select_all(action='SELECT')
    bpy.ops.uv.pack_islands(rotate=True, margin=float(CFG.get('uv_rand', 0.003)), shape_method='CONCAVE')
    bpy.ops.object.mode_set(mode='OBJECT')


def unten_verkleinern(o, faktor):
    """UV-Inseln, die überwiegend im Boden liegen (flächengewichtete Mitte unter z 0 -- der Fels ist versenkt), auf `faktor`
    ihrer Größe bringen: Sie sind nie zu sehen und kosteten in Runde 1 gut die Hälfte des Atlas (UV-Nutzung der
    sichtbaren Fläche 43 %, Texeldichte unter der des EA-Felsens)."""
    if faktor >= 1:
        return
    bm = bmesh.new(); bm.from_mesh(o.data)
    uv = bm.loops.layers.uv.active
    bm.faces.ensure_lookup_table()
    eltern = list(range(len(bm.faces)))

    def w(i):
        while eltern[i] != i:
            eltern[i] = eltern[eltern[i]]
            i = eltern[i]
        return i
    for e in bm.edges:
        if len(e.link_loops) != 2:
            continue
        l1, l2 = e.link_loops
        if (l1[uv].uv - l2.link_loop_next[uv].uv).length < 1e-6 and (l1.link_loop_next[uv].uv - l2[uv].uv).length < 1e-6:
            a, b = w(l1.face.index), w(l2.face.index)
            if a != b:
                eltern[b] = a
    inseln = {}
    for f in bm.faces:
        inseln.setdefault(w(f.index), []).append(f)
    n = 0
    for fl in inseln.values():
        flaeche = sum(f.calc_area() for f in fl)
        if sum(f.calc_center_median().z * f.calc_area() for f in fl) / max(flaeche, 1e-9) > 0.0:
            continue
        loops = [l for f in fl for l in f.loops]
        m = sum((l[uv].uv for l in loops), Vector((0, 0))) / len(loops)
        for l in loops:
            l[uv].uv = m + (l[uv].uv - m) * faktor
        n += 1
    bm.to_mesh(o.data); bm.free()
    log('UV %d von %d Inseln im Boden auf %.2f verkleinert' % (n, len(inseln), faktor))


def material_emission(mat):
    """Kopie des Scan-Materials, die die Grundfarbe als Emission ausgibt (Backen der Farbe)."""
    emit = mat.copy()
    emit.name = mat.name + '_emit'
    emit.use_fake_user = True
    nt = emit.node_tree
    bsdf = [n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED'][0]
    out = [n for n in nt.nodes if n.type == 'OUTPUT_MATERIAL'][0]
    em = nt.nodes.new('ShaderNodeEmission')
    eingang = bsdf.inputs['Base Color']
    if eingang.is_linked:
        nt.links.new(eingang.links[0].from_socket, em.inputs['Color'])
    else:
        em.inputs['Color'].default_value = eingang.default_value
    nt.links.new(em.outputs['Emission'], out.inputs['Surface'])
    return emit


def vorbereiten():
    leeren()
    os.makedirs(ARBEIT, exist_ok=True)
    vorher = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=os.path.join(MODELLE, CFG['quelle']))
    scan = {o.name: o for o in bpy.data.objects if o not in vorher and o.type == 'MESH'}
    for o in bpy.data.objects:
        if o not in vorher and o.type != 'MESH':
            bpy.data.objects.remove(o, do_unlink=True)
    hp_teile, lp_teile = [], []
    flaechen = []
    for i, zu in enumerate(CFG['zuordnung']):
        quelle = scan[zu['quelle']]
        hp = kopie(quelle, 'hp_%d' % i)
        hp.parent = None
        hp.matrix_world = quelle.matrix_world.copy()
        einpassen(zu['lage'], hp, zu)
        hp_teile.append(hp)
        flaechen.append(sum(p.area for p in hp.data.polygons))
    for o in scan.values():
        bpy.data.objects.remove(o, do_unlink=True)
    budget = int(CFG.get('dreiecke', 800))
    for i, hp in enumerate(hp_teile):
        lp_teile.append(niedrig(hp, 'lp_%d' % i, max(40, int(budget * flaechen[i] / sum(flaechen)))))
    lp = lp_teile[0]
    if len(lp_teile) > 1:
        aktiv(lp, lp_teile[1:])
        bpy.ops.object.join()
    lp.name = lp.data.name = 'lp'
    kanten_trennen(lp, float(CFG.get('harte_kante', 70)))
    uv_atlas(lp)
    for hp in hp_teile:
        for m in list(hp.data.materials):
            if m and not bpy.data.materials.get(m.name + '_emit'):
                material_emission(m)
    log('LOWPOLY %d Dreiecke, %d Ecken' % (dreiecke(lp), len(lp.data.vertices)))
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ARBEIT, 'szene.blend'))


# ------------------------------------------------------------------------------------------------
# Schritt 2: backen
# ------------------------------------------------------------------------------------------------
def backen():
    bpy.ops.wm.open_mainfile(filepath=os.path.join(ARBEIT, 'szene.blend'))
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'
    lp = bpy.data.objects['lp']
    hps = [o for o in bpy.data.objects if o.name.startswith('hp_')]
    groesse = max(lp.dimensions)
    sc.render.bake.margin = 0
    sc.render.bake.cage_extrusion = float(CFG.get('cage', 0.03)) * groesse
    sc.render.bake.max_ray_distance = float(CFG.get('strahl', 0.08)) * groesse
    if sc.world is None:
        sc.world = bpy.data.worlds.new('welt')
    sc.world.light_settings.distance = float(CFG.get('ao_weite', 0.25)) * groesse
    # Bodenebene: der Fels steht im Gelände, seine Unterkante soll im Schatten liegen
    bpy.ops.mesh.primitive_plane_add(size=groesse * 4, location=(0, 0, 0))
    boden = bpy.context.active_object
    boden.name = 'boden'
    ordner = os.path.join(ARBEIT, 'backen')
    os.makedirs(ordner, exist_ok=True)
    mat = bpy.data.materials.new('bake'); mat.use_nodes = True
    node = mat.node_tree.nodes.new('ShaderNodeTexImage')
    mat.node_tree.nodes.active = node
    lp.data.materials.clear()
    lp.data.materials.append(mat)
    for o in bpy.data.objects:
        if o.type == 'MESH' and o not in hps and o is not lp and o is not boden:
            o.hide_render = True
    orig = {o.name: [m for m in o.data.materials] for o in hps}
    for pas in ('farbe', 'ao', 'normal_fein', 'normal_grob', 'abdeckung'):
        if CFG.get('paesse') and pas not in str(CFG['paesse']).split(','):
            continue
        img = bpy.data.images.new('%s' % pas, ATLAS, ATLAS, alpha=True, float_buffer=True)
        img.colorspace_settings.name = 'Non-Color'
        img.pixels.foreach_set(np.zeros(ATLAS * ATLAS * 4, dtype=np.float32))
        node.image = img
        lp.hide_render = False
        lp.visible_camera = True
        for o in hps:
            for i, m in enumerate(orig[o.name]):
                o.data.materials[i] = m
        boden.hide_render = pas != 'ao'
        if pas == 'farbe':
            for o in hps:
                for i, m in enumerate(orig[o.name]):
                    o.data.materials[i] = bpy.data.materials[m.name + '_emit']
            sc.cycles.samples = int(CFG.get('proben_farbe', 16))
            aktiv(lp, hps)
            bpy.ops.object.bake(type='EMIT', use_selected_to_active=True)
        elif pas == 'ao':
            # vom Scan aus: das Niedrigpoly darf nicht verdecken
            lp.visible_diffuse = lp.visible_glossy = lp.visible_shadow = lp.visible_transmission = False
            sc.cycles.samples = int(CFG.get('proben_ao', 128))
            aktiv(lp, hps)
            bpy.ops.object.bake(type='AO', use_selected_to_active=True)
        elif pas == 'normal_fein':
            sc.cycles.samples = 4
            aktiv(lp, hps)
            bpy.ops.object.bake(type='NORMAL', normal_space='OBJECT', use_selected_to_active=True)
        elif pas == 'normal_grob':
            sc.cycles.samples = 1
            aktiv(lp)
            bpy.ops.object.bake(type='NORMAL', normal_space='OBJECT', use_selected_to_active=False)
        elif pas == 'abdeckung':
            em = bpy.data.materials.new('weiss'); em.use_nodes = True
            nt = em.node_tree
            e = nt.nodes.new('ShaderNodeEmission'); e.inputs['Color'].default_value = (1, 1, 1, 1)
            nt.links.new(e.outputs['Emission'], nt.nodes['Material Output'].inputs['Surface'])
            n2 = nt.nodes.new('ShaderNodeTexImage'); n2.image = img; nt.nodes.active = n2
            lp.data.materials[0] = em
            sc.cycles.samples = 1
            aktiv(lp)
            bpy.ops.object.bake(type='EMIT', use_selected_to_active=False)
            lp.data.materials[0] = mat
        img.filepath_raw = os.path.join(ordner, '%s.exr' % pas)
        img.file_format = 'OPEN_EXR'
        img.save()
        log('GEBACKEN', pas)


# ------------------------------------------------------------------------------------------------
# Schritt 3: Textur
# ------------------------------------------------------------------------------------------------
def bild_lesen(pfad, farbraum='Non-Color'):
    img = bpy.data.images.load(pfad)
    img.colorspace_settings.name = farbraum
    w, h = img.size
    a = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(a)
    bpy.data.images.remove(img)
    return a.reshape(h, w, 4)


def bild_schreiben(pfad, rgba):
    h, w = rgba.shape[:2]
    if rgba.shape[2] == 3:
        rgba = np.concatenate([rgba, np.ones((h, w, 1), np.float32)], 2)
    img = bpy.data.images.new('aus', w, h, alpha=True)
    img.colorspace_settings.name = 'Non-Color'
    img.alpha_mode = 'STRAIGHT'
    img.pixels.foreach_set(np.clip(rgba, 0, 1).astype(np.float32).ravel())
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
    if rest.any() and (~rest).any():
        rgb[rest] = rgb[~rest].mean(axis=0)
    return rgb


def unschaerf(x, r):
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


def helligkeit(rgb):
    return rgb @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)


def textur():
    ordner = os.path.join(ARBEIT, 'backen')
    aus = os.path.join(ARBEIT, 'textur')
    os.makedirs(aus, exist_ok=True)
    lies = lambda pas: bild_lesen(os.path.join(ordner, pas + '.exr'))
    cov = lies('abdeckung')[..., 0] > 0.5
    f = lies('farbe')
    treffer = cov & (f[..., 3] > 0.5)
    srgb = lin_zu_srgb(f[..., :3])
    ao = np.clip(lies('ao')[..., 0], 0, 1)
    nf = lies('normal_fein')[..., :3] * 2 - 1
    ng = lies('normal_grob')[..., :3] * 2 - 1
    nf[~treffer] = ng[~treffer]
    nf /= np.maximum(np.linalg.norm(nf, axis=2, keepdims=True), 1e-4)
    ng /= np.maximum(np.linalg.norm(ng, axis=2, keepdims=True), 1e-4)
    relief = 1 + float(CFG.get('relief_oben', 0.35)) * (nf[..., 2] - ng[..., 2]) \
        - float(CFG.get('relief_kante', 0.5)) * (1 - np.clip((nf * ng).sum(2), -1, 1))
    relief = np.clip(relief, 0.5, 1.3)
    ao_f = 1 - float(CFG.get('ao_staerke', 0.7)) * (1 - ao)
    rgb = srgb * (ao_f * relief)[..., None]
    rgb = auffuellen(rgb, treffer, 8)
    # Farbangleichung je Kanal an feste Zielwerte (sRGB 0..1): Mittelwert voll, Streuung auf kontrast x Ziel
    m_z = np.array(CFG['farbziel']['mittel'], dtype=np.float32)
    s_z = np.array(CFG['farbziel']['streuung'], dtype=np.float32)
    wahl = treffer & (ng[..., 2] > -0.3)          # was man von oben/seitlich sieht
    m, s = rgb[wahl].mean(0), rgb[wahl].std(0)
    ziel_s = s_z * float(CFG.get('kontrast', 1.0))
    anteil = float(CFG.get('farbe_angleichen', 1.0))
    # Kanalweise, aber die Helligkeitsstruktur des Scans bleibt: Faktor je Kanal aus der Streuung, Mittel verschoben
    k = np.where(s > 1e-4, ziel_s / np.maximum(s, 1e-4), 1.0)
    angeglichen = (rgb - m) * k + m_z
    rgb = anteil * angeglichen + (1 - anteil) * rgb
    log('FARBE Scan Mittel %s Streuung %s -> Ziel Mittel %s Streuung %s' % (
        np.round(m, 3), np.round(s, 3), np.round(m_z, 3), np.round(s_z, 3)))
    rgb = np.clip(rgb, 0, 1)
    voll = auffuellen(rgb, cov, 48)
    name = CFG['textur']
    for stufe, groesse in sorted(STUFEN.items(), key=lambda x: x[1]):
        rgb = verkleinern(voll, ATLAS // groesse)
        weich = float(CFG.get('weich', 0))
        if weich > 0:
            rgb = (1 - min(weich, 1)) * rgb + min(weich, 1) * unschaerf(rgb, 1)
        endung = '' if stufe == 'sd' else '_' + stufe
        bild_schreiben(os.path.join(aus, name + endung + '.png'), rgb)
        log('TEXTUR', name + endung, rgb.shape[1], 'x', rgb.shape[0], 'Mittel', np.round(rgb.reshape(-1, 3).mean(0), 3))
        # Fassungen (beschädigt): dunkler, entsättigt, Ruß
        for schluessel, fa in CFG.get('fassungen', {}).items():
            l = helligkeit(rgb)[..., None]
            d = (l + (rgb - l) * float(fa.get('saettigung', 0.6))) * float(fa.get('dunkel', 0.7))
            bild_schreiben(os.path.join(aus, fa['textur'] + endung + '.png'), np.clip(d, 0, 1))
            log('TEXTUR', fa['textur'] + endung, '(Fassung %s)' % schluessel)


# ------------------------------------------------------------------------------------------------
# Schritt 4: export
# ------------------------------------------------------------------------------------------------
def export():
    fassung = OPT.get('fassung')
    bpy.ops.wm.open_mainfile(filepath=os.path.join(ARBEIT, 'szene.blend'))
    lp = bpy.data.objects['lp']
    textur_name = CFG['fassungen'][fassung]['textur'] if fassung else CFG['textur']
    modell = modellname(fassung)
    # Geometrieträger: das Niedrigpoly allein in einer leeren Szene, Netz und Material nach dem Modell benannt
    me = lp.data.copy()
    me.transform(lp.matrix_world)
    naehte = uv_naehte_trennen(me)
    log('UV-NAEHTE %d Kanten getrennt -> %d Ecken' % (naehte, len(me.vertices)))
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    img = bpy.data.images.load(os.path.join(ARBEIT, 'textur', textur_name + '.png'))
    img.name = textur_name + '.tga'
    img.filepath_raw = textur_name + '.tga'
    mat = bpy.data.materials.new(textur_name)
    mat.use_nodes = True
    tex = mat.node_tree.nodes.new('ShaderNodeTexImage')
    tex.image = img
    mat.node_tree.links.new(tex.outputs['Color'], mat.node_tree.nodes['Principled BSDF'].inputs['Base Color'])
    me.materials.clear()
    me.materials.append(mat)
    me.name = modell.upper()
    obj = bpy.data.objects.new(modell.upper(), me)
    bpy.context.scene.collection.objects.link(obj)
    roh = os.path.join(ARBEIT, 'export', modell.upper() + '_roh.w3d')
    ziel = os.path.join(ARBEIT, 'export', modell.upper() + '.w3d')
    os.makedirs(os.path.dirname(ziel), exist_ok=True)
    addon_utils.enable('io_mesh_w3d', default_set=True, persistent=False)
    bpy.ops.export_mesh.westwood_w3d(filepath=roh, file_format='W3D', export_mode='M', use_existing_skeleton=False)
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import w3d_chunks
    import w3d_geruest
    # vom Träger zählen nur Ecken, Normalen, Dreiecke und UV; alles andere schreibt w3d_geruest nach Vorschrift
    daten = w3d_chunks.normalen_glaetten(open(roh, 'rb').read(), 0.85 * float(CFG.get('harte_kante', 70)))
    geo = w3d_geruest.geometrie(daten)
    if len(geo) != 1:
        raise SystemExit('Geometrieträger mit %d Netzen' % len(geo))
    open(ziel, 'wb').write(w3d_geruest.requisit(modell, geo[0], textur_name + '.tga'))
    log('EXPORT', ziel, os.path.getsize(ziel), 'Bytes', len(geo[0]['dreiecke']), 'Dreiecke')


# ------------------------------------------------------------------------------------------------
# Kontrollbilder
# ------------------------------------------------------------------------------------------------
def ansicht():
    """Neu, mit --ea=<datei> links daneben das EA-Modell (nur Vergleichsbild); Workbench mit Texturfarbe, Alpha der
    Textur ignoriert."""
    fassung = OPT.get('fassung')
    leeren()
    ea = []
    if OPT.get('ea'):
        w3d_laden(OPT['ea'])
        ea = [o for o in bpy.data.objects if o.type == 'MESH']
    modell = modellname(fassung).upper()
    vorher = set(bpy.data.objects)
    w3d_laden(os.path.join(ARBEIT, 'export', modell + '.w3d'))
    neu = [o for o in bpy.data.objects if o not in vorher and o.type == 'MESH']
    if ea:
        pts = np.concatenate([ecken(o) for o in ea])
        versatz = float(np.ptp(pts[:, 0]) + np.ptp(pts[:, 1]) * 0.5) * 1.3
        for o in [o for o in bpy.data.objects if o not in vorher and o.parent is None]:
            o.location.x += versatz
        bpy.context.view_layer.update()
    # Workbench zeigt die Texturfarbe samt Alpha (Felsentexturen ohne Deckung) -> Alpha ignorieren; die neue Textur liegt
    # nicht neben dem Export, sie wird aus textur/ geladen
    textur_name = CFG['fassungen'][fassung]['textur'] if fassung else CFG['textur']
    stufe = OPT.get('stufe', 'sd')
    neu_bild = bpy.data.images.load(os.path.join(ARBEIT, 'textur', textur_name + ('' if stufe == 'sd' else '_' + stufe)
                                                 + '.png'))
    for o in neu:
        for m in o.data.materials:
            for n in m.node_tree.nodes:
                if n.type == 'TEX_IMAGE' and textur_name.lower() in (n.image.name.lower() if n.image else textur_name.lower()):
                    n.image = neu_bild
    for img in bpy.data.images:
        img.alpha_mode = 'NONE'
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_WORKBENCH'
    sc.display.shading.light = 'STUDIO'
    sc.display.shading.color_type = 'TEXTURE'
    sc.render.resolution_x, sc.render.resolution_y = 1400, 700
    alle = np.concatenate([ecken(o) for o in ea + neu])
    mitte = Vector(((alle[:, 0].min() + alle[:, 0].max()) / 2, (alle[:, 1].min() + alle[:, 1].max()) / 2,
                    (alle[:, 2].min() + alle[:, 2].max()) / 2))
    g = max(np.ptp(alle[:, 0]), np.ptp(alle[:, 1]) * 2)
    ziel = os.path.join(ARBEIT, 'ansicht')
    os.makedirs(ziel, exist_ok=True)
    for name, az, el in (('rts', -90, 50), ('seite', -90, 8), ('schraeg', -55, 30)):
        cam = bpy.data.objects.new('c' + name, bpy.data.cameras.new('c' + name))
        sc.collection.objects.link(cam)
        cam.data.type = 'ORTHO'
        cam.data.ortho_scale = g * 1.15
        r = g * 3
        cam.location = mitte + Vector((r * math.cos(math.radians(az)) * math.cos(math.radians(el)),
                                       r * math.sin(math.radians(az)) * math.cos(math.radians(el)),
                                       r * math.sin(math.radians(el))))
        cam.rotation_euler = (mitte - cam.location).to_track_quat('-Z', 'Y').to_euler()
        cam.data.clip_end = r * 4
        sc.camera = cam
        sc.render.filepath = os.path.join(ziel, '%s%s_%s.png' % (modell.lower(), '' if stufe == 'sd' else '_' + stufe,
                                                                 name))
        bpy.ops.render.render(write_still=True)
        log('BILD', sc.render.filepath)


if __name__ == '__main__':
    {'vorbereiten': vorbereiten, 'backen': backen, 'textur': textur, 'export': export, 'ansicht': ansicht}[SCHRITT]()
