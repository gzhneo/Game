# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
#
# Own tree generator for Blender: trunk and branches as bark tubes, leaf cards carrying one leaf image (ambientCG, CC0).
# For species without a scan (birch, willow, bamboo); baum_frei.py renders the result like a scan. No third-party code:
# the geometry comes only from the source parameters in the order file (section "quellen", key "wachsen") and a fixed seed.
#
#   blender --background --factory-startup --python baum_wachsen.py -- <parameters.json> <target.blend>
#
# Parameters (lengths in metres):
#   art          "baum" (one trunk with branches), "halme" (clump of culms, bamboo) or "palme" (palm: besides the whole tree
#                the objects wedel_1, wedel_2 hold single flat fronds, rendered from above for the frond cards)
#   wedel        palm: number of fronds; wedel_laenge, wedel_breite (leaflet length at mid frond), fiedern (leaflets per side)
#   hoehe, stamm_radius, keim; stamm_anteil (trunk ends at this share of the height; willow: short, branches form the crown)
#   aeste        first-order branches; ast_ab (share of trunk height), ast_winkel (degrees from the trunk),
#                ast_laenge (share of height), haengen (gravity per metre, large for willow), zweige (second order per branch)
#   halme        number of culms (art "halme"), streuung (clump radius), neigung (degrees)
#   blatt_bild   colour/opacity of the leaf image (path relative to <modelle>), blatt_groesse, blatt_dichte (cards per metre),
#                blatt_haengen (cards hang vertically: willow)
#   blatt_toenung  [r, g, b]: leaves only, grey value of the leaf image times this colour (blossom)
#   rinde        "birke" (computed: white with dark horizontal marks) or path of a bark texture
import json
import math
import os
import random
import sys

import bpy
from mathutils import Vector


def rinde_birke(groesse, rng):
    """Birch bark image: white-grey with dark horizontal lenticels and irregular black patches."""
    import numpy as np
    w, h = groesse
    g = np.random.default_rng(rng.randrange(1 << 30))
    hell = 0.82 + 0.06 * g.standard_normal((h, w)).cumsum(1) / np.sqrt(w)
    bild = np.stack([hell * 0.97, hell * 0.96, hell * 0.92], 2)
    for _ in range(int(w * h / 900)):
        y, x = g.integers(0, h), g.integers(0, w)
        lang, dick = g.integers(w // 30, w // 8), g.integers(1, 4)
        bild[y:y + dick, x:x + lang] *= 0.25 + 0.3 * g.random()
    for _ in range(int(w * h / 20000)):
        y, x = g.integers(0, h), g.integers(0, w)
        r = g.integers(3, 12)
        yy, xx = np.ogrid[:h, :w]
        m = ((yy - y) / (r * 0.6)) ** 2 + ((xx - x) / (r * 1.6)) ** 2 < 1
        bild[m] *= 0.15
    bild = np.clip(bild, 0, 1)
    im = bpy.data.images.new('rinde_birke', w, h, alpha=True)
    px = np.concatenate([bild, np.ones((h, w, 1))], 2)[::-1].astype(np.float32)
    im.pixels.foreach_set(px.ravel())
    im.pack()
    return im


def material_rinde(p, modelle, rng):
    mat = bpy.data.materials.new('rinde')
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes['Principled BSDF']
    tex = nt.nodes.new('ShaderNodeTexImage')
    if p.get('rinde', 'birke') == 'birke':
        tex.image = rinde_birke((512, 512), rng)
    else:
        tex.image = bpy.data.images.load(os.path.join(modelle, p['rinde']))
    nt.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    bsdf.inputs['Roughness'].default_value = 0.8
    return mat


def material_blatt(p, modelle):
    mat = bpy.data.materials.new('blatt')
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes['Principled BSDF']
    farbe = nt.nodes.new('ShaderNodeTexImage')
    farbe.image = bpy.data.images.load(os.path.join(modelle, p['blatt_bild'] + '_Color.png'))
    deck = nt.nodes.new('ShaderNodeTexImage')
    deck.image = bpy.data.images.load(os.path.join(modelle, p['blatt_bild'] + '_Opacity.png'))
    deck.image.colorspace_settings.name = 'Non-Color'
    if p.get('blatt_toenung'):
        # tint only the leaves (blossom): grey value of the leaf image times the tint colour
        grau = nt.nodes.new('ShaderNodeRGBToBW')
        mix = nt.nodes.new('ShaderNodeMix')
        mix.data_type = 'RGBA'
        mix.blend_type = 'MULTIPLY'
        mix.inputs['Factor'].default_value = 1.0
        nt.links.new(farbe.outputs['Color'], grau.inputs[0])
        nt.links.new(grau.outputs[0], mix.inputs['A'])
        mix.inputs['B'].default_value = (*p['blatt_toenung'], 1.0)
        nt.links.new(mix.outputs['Result'], bsdf.inputs['Base Color'])
    else:
        nt.links.new(farbe.outputs['Color'], bsdf.inputs['Base Color'])
    if not p.get('blatt_deckend'):
        nt.links.new(deck.outputs['Color'], bsdf.inputs['Alpha'])
    bsdf.inputs['Roughness'].default_value = 0.6
    return mat


class Netz:
    def __init__(self):
        self.ecken, self.flaechen, self.uv, self.mat = [], [], [], []

    def roehre(self, punkte, radien, seiten, v_massstab):
        """Tube along the points; UV u around, v along the length (v_massstab repeats per metre)."""
        i0 = len(self.ecken)
        laenge = 0.0
        for k, (p, r) in enumerate(zip(punkte, radien)):
            if k:
                laenge += (p - punkte[k - 1]).length
            d = (punkte[min(k + 1, len(punkte) - 1)] - punkte[max(k - 1, 0)]).normalized()
            a = d.orthogonal().normalized()
            b = d.cross(a)
            for s in range(seiten + 1):
                w = 2 * math.pi * s / seiten
                self.ecken.append(p + (a * math.cos(w) + b * math.sin(w)) * r)
                self.uv.append((s / seiten, laenge * v_massstab))
        n = seiten + 1
        for k in range(len(punkte) - 1):
            for s in range(seiten):
                a = i0 + k * n + s
                self.flaechen.append((a, a + 1, a + n + 1, a + n))
                self.mat.append(0)

    def karte(self, mitte, rechts, oben):
        """Leaf card: quad around mitte with edges rechts and oben (full length), UV over the whole leaf image."""
        i = len(self.ecken)
        for du, dv in ((-0.5, -0.5), (0.5, -0.5), (0.5, 0.5), (-0.5, 0.5)):
            self.ecken.append(mitte + rechts * du + oben * dv)
        self.uv.extend([(0, 0), (1, 0), (1, 1), (0, 1)])
        self.flaechen.append((i, i + 1, i + 2, i + 3))
        self.mat.append(1)

    def raute(self, fuss, rechts, laenge):
        """Opaque leaflet: diamond from fuss along laenge, widest (rechts) at 35 % of its length."""
        i = len(self.ecken)
        self.ecken.extend([fuss, fuss + laenge * 0.35 + rechts * 0.5, fuss + laenge, fuss + laenge * 0.35 - rechts * 0.5])
        self.uv.extend([(0.5, 0.0), (0.7, 0.35), (0.5, 1.0), (0.3, 0.35)])
        self.flaechen.append((i, i + 1, i + 2, i + 3))
        self.mat.append(1)


def ast(start, richtung, laenge, radius, p, rng, schritte=8):
    """Branch points: straight along richtung, bent by gravity (haengen) and some randomness."""
    punkte, radien = [start.copy()], [radius]
    d = richtung.normalized()
    s = laenge / schritte
    pos = start.copy()
    for k in range(schritte):
        d = (d + Vector((rng.uniform(-0.15, 0.15), rng.uniform(-0.15, 0.15), -float(p.get('haengen', 0.1)) * s))).normalized()
        pos = pos + d * s
        punkte.append(pos.copy())
        radien.append(radius * (1 - 0.9 * (k + 1) / schritte))
    return punkte, radien


def blaetter(netz, punkte, p, rng, ab=0.3):
    """Leaf cards along a twig, from share 'ab' of its length."""
    gr = float(p['blatt_groesse'])
    gesamt = sum((punkte[k + 1] - punkte[k]).length for k in range(len(punkte) - 1))
    n = max(1, int(gesamt * float(p['blatt_dichte'])))
    for _ in range(n):
        t = rng.uniform(ab, 1.0) * (len(punkte) - 1)
        k = min(int(t), len(punkte) - 2)
        pos = punkte[k].lerp(punkte[k + 1], t - k)
        if p.get('blatt_haengen'):
            oben = Vector((rng.uniform(-0.2, 0.2), rng.uniform(-0.2, 0.2), 1)).normalized() * gr * 1.6
            w = rng.uniform(0, 2 * math.pi)
            rechts = Vector((math.cos(w), math.sin(w), 0)) * gr * 0.8
            pos = pos - oben * 0.45
        else:
            n_ = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(0.2, 1))).normalized()
            rechts = n_.orthogonal().normalized() * gr
            oben = n_.cross(rechts).normalized() * gr
        netz.karte(pos, rechts, oben)


def baum(p, rng):
    netz = Netz()
    h, r0 = float(p['hoehe']), float(p['stamm_radius'])
    hs = h * float(p.get('stamm_anteil', 1.0))
    # slightly curved trunk
    stamm, radien = [], []
    neig = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), 0)) * float(p.get('stamm_krumm', 0.03))
    for k in range(13):
        t = k / 12
        stamm.append(Vector((0, 0, hs * t)) + neig * hs * t * t + Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), 0)) * 0.02 * hs * t)
        radien.append(r0 * (1 - 0.85 * t * hs / h))
    netz.roehre(stamm, radien, 8, 1.0)
    n = int(p['aeste'])
    for i in range(n):
        t = float(p['ast_ab']) + (1 - float(p['ast_ab'])) * (i + rng.random()) / n
        k = min(int(t * 12), 11)
        start = stamm[k].lerp(stamm[k + 1], t * 12 - k)
        w = i * 2.39996 + rng.uniform(-0.3, 0.3)            # golden angle
        aw = math.radians(float(p['ast_winkel']) + rng.uniform(-10, 10))
        richtung = Vector((math.cos(w) * math.sin(aw), math.sin(w) * math.sin(aw), math.cos(aw)))
        laenge = h * float(p['ast_laenge']) * (1.15 - 0.6 * t) * rng.uniform(0.8, 1.2)
        pts, rad = ast(start, richtung, laenge, r0 * 0.35 * (1 - t) + 0.01, p, rng)
        netz.roehre(pts, rad, 5, 1.0)
        blaetter(netz, pts, p, rng, 0.4)
        for z in range(int(p.get('zweige', 3))):
            tz = rng.uniform(0.3, 0.9) * (len(pts) - 1)
            kz = min(int(tz), len(pts) - 2)
            sz = pts[kz].lerp(pts[kz + 1], tz - kz)
            d = (pts[kz + 1] - pts[kz]).normalized()
            seite = d.cross(Vector((0, 0, 1))).normalized() * (1 if z % 2 else -1)
            dz = (d + seite * 0.8 + Vector((0, 0, 0.3))).normalized()
            zp, zr = ast(sz, dz, laenge * 0.45 * rng.uniform(0.7, 1.2), 0.012, p, rng, 5)
            netz.roehre(zp, zr, 4, 1.0)
            blaetter(netz, zp, p, rng, 0.0)
    return netz


def halme(p, rng):
    """Bamboo: clump of straight culms leaning outwards; short side twigs with leaf cards near the top."""
    netz = Netz()
    h = float(p['hoehe'])
    for i in range(int(p['halme'])):
        w = rng.uniform(0, 2 * math.pi)
        rr = rng.uniform(0, float(p['streuung']))
        fuss = Vector((math.cos(w) * rr, math.sin(w) * rr, 0))
        nw = math.radians(rng.uniform(0, float(p['neigung'])))
        d = Vector((math.cos(w) * math.sin(nw), math.sin(w) * math.sin(nw), math.cos(nw)))
        hh = h * rng.uniform(0.6, 1.0)
        pts, rad = [], []
        for k in range(11):
            t = k / 10
            # culms bend outwards under the foliage near the top
            pts.append(fuss + d * hh * t + Vector((math.cos(w), math.sin(w), 0)) * float(p.get('biegen', 0.1)) * hh * t ** 3)
            rad.append(float(p['stamm_radius']) * (1 - 0.6 * t))
        netz.roehre(pts, rad, 6, 0.6)
        for z in range(int(p.get('zweige', 6))):
            t = rng.uniform(0.45, 1.0)
            k = min(int(t * 10), 9)
            s = pts[k].lerp(pts[k + 1], t * 10 - k)
            ww = rng.uniform(0, 2 * math.pi)
            dz = Vector((math.cos(ww), math.sin(ww), rng.uniform(0.0, 0.6))).normalized()
            zp, zr = ast(s, dz, hh * 0.12 * rng.uniform(0.7, 1.3), 0.01, p, rng, 4)
            netz.roehre(zp, zr, 3, 1.0)
            blaetter(netz, zp, p, rng, 0.0)
    return netz


def wedel(netz, p, rng, ursprung, aus, quer, hoch):
    """One frond: rachis from ursprung along 'aus' (unit vector), leaflet cards on both sides pointing forwards and
    sideways (axis 'quer'), lifted along 'hoch'. Flat when hoch is zero; the game mesh bends the rendered frond itself."""
    laenge, breite = float(p['wedel_laenge']) * rng.uniform(0.9, 1.1), float(p['wedel_breite'])
    n = int(p.get('fiedern', 36))
    netz.roehre([ursprung, ursprung + aus * laenge], [0.04, 0.01], 4, 1.0)
    for i in range(n):
        s = (i + 0.5) / n
        lang = breite * math.sin(math.pi * min(1.0, 0.15 + s)) ** 0.6 * (1.0 - 0.35 * s) * rng.uniform(0.85, 1.1)
        basis = ursprung + aus * laenge * s
        for seite in (-1, 1):
            w = math.radians(rng.uniform(25, 40))
            richt = (aus * math.cos(w) * 0.6 + quer * seite * math.sin(w) * 1.4).normalized()
            rechts = richt.cross(quer.cross(aus)).normalized() * float(p.get('fieder_breite', 0.09))
            # every leaflet a little higher than the previous one: coplanar overlapping leaflets render black (shadow acne)
            versatz = Vector((0, 0, 0.003 * (2 * i + (seite > 0))))
            netz.raute(basis + hoch * lang * 0.05 + versatz, rechts, richt * lang + hoch * lang * 0.1)


def palme(p, rng):
    """Palm: leaning trunk with bark, fronds radiating from the crown (for the preview; the game mesh is form 'wedel')."""
    netz = Netz()
    h, r0 = float(p['hoehe']), float(p['stamm_radius'])
    neig = math.radians(float(p.get('neigung', 8)))
    pts, rad = [], []
    for k in range(11):
        t = k / 10
        pts.append(Vector((math.sin(neig) * h * t * t, 0, h * t)))
        rad.append(r0 * (1.25 - 0.4 * t))
    netz.roehre(pts, rad, 8, 0.5)
    kopf = pts[-1]
    for i in range(int(p.get('wedel', 12))):
        w = i * 2.39996
        aus = Vector((math.cos(w), math.sin(w), rng.uniform(-0.3, 0.4))).normalized()
        quer = Vector((-math.sin(w), math.cos(w), 0))
        wedel(netz, p, rng, kopf, aus, quer, Vector((0, 0, 1)))
    return netz


def objekt(name, netz, p, modelle, rng):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(e) for e in netz.ecken], [], netz.flaechen)
    uv = me.uv_layers.new()
    for poly in me.polygons:
        for li in poly.loop_indices:
            uv.data[li].uv = netz.uv[me.loops[li].vertex_index]
    me.materials.append(material_rinde(p, modelle, rng))
    me.materials.append(material_blatt(p, modelle))
    me.polygons.foreach_set('material_index', netz.mat)
    me.update()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    return o


def main():
    argv = sys.argv[sys.argv.index('--') + 1:]
    p = json.load(open(argv[0], encoding='utf-8'))
    ziel = argv[1]
    modelle = p['_modelle']
    rng = random.Random(int(p.get('keim', 1)))
    bpy.ops.wm.read_factory_settings(use_empty=True)
    art = p.get('art', 'baum')
    netz = {'halme': halme, 'palme': palme}.get(art, baum)(p, rng)
    objekt('baum', netz, p, modelle, rng)
    if art == 'palme':
        # single flat fronds side by side, base at y = 0, tip towards +y
        for k in (1, 2):
            w = Netz()
            wedel(w, p, rng, Vector((20.0 * k, 0, 0)), Vector((0, 1, 0)), Vector((1, 0, 0)), Vector((0, 0, 0)))
            objekt('wedel_%d' % k, w, p, modelle, rng)
    os.makedirs(os.path.dirname(ziel), exist_ok=True)
    bpy.ops.file.pack_all()
    bpy.ops.wm.save_as_mainfile(filepath=ziel)
    print('BAUM WACHSEN', ziel, len(netz.ecken), 'Ecken', len(netz.flaechen), 'Flächen', flush=True)


main()
