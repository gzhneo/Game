# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Blender-Teil des Versuchs "Bodentexturen mit Blender".

Baut je Bodenart ein prozedurales Material aus Shader-Knoten und backt es mit Cycles auf eine Ebene:

  albedo.npy   Farbe (float32, ohne Farbraumumrechnung), die Farbrampe aus der Originaltextur
  hoehe.npy    Hoehenfeld 0..1 fuer die Reliefschattierung (rechnet boden_versuch.py)

Nahtlos kachelbar: Alle Rauschknoten laufen vierdimensional auf einem Torus -- die UV-Koordinaten (u, v)
werden zu (Ru cos 2πu, Ru sin 2πu, Rv cos 2πv) mit W = Rv sin 2πv. Ein Umlauf in u oder v ist genau eine
Texturbreite, also passt der rechte Rand an den linken. Ru != Rv streckt die Struktur (Grashalme).

Farbverteilung: Die Rampen-Stuetzstellen (Farben nach Helligkeitsquantilen der Originaltextur) werden in
einem Vorlauf auf 128 Pixeln an die Verteilung des prozeduralen Werts gelegt (gleiches Quantil = gleiche
Farbe); danach Backen in voller Groesse.

Aufruf (ohne Oberflaeche, von boden_versuch.py):
  blender --background --factory-startup --python boden_backen.py -- auftrag.json
auftrag.json: {"name", "art": "gras"|"erde"|"fels", "groesse", "samples", "seed", "rampe": [[q, [r,g,b]], ...],
               "schichten": [{"maske", "rampe", "anteil", "weich", "hoehe"}, ...], "param": {...}, "aus": Ordner}
Eine Schicht faerbt die Flaeche, auf der ihre Maske (ein Feld des Materials, etwa Stroh, Moos, offener Boden)
den Anteil der Vorlage deckt, mit einer eigenen Rampe (Farben dieser Pixelgruppe im Original).
"""
import json
import math
import os
import sys

import bpy
import numpy as np

TAU = 2.0 * math.pi


class Knoten:
    """Kleine Huelle um einen Shader-Knotenbaum: Rechenknoten mit einer Zeile anlegen."""

    def __init__(self, tree, seed):
        self.t = tree
        self.n = tree.nodes
        self.l = tree.links
        self.rng = np.random.default_rng(seed)
        uv = self.n.new("ShaderNodeTexCoord")
        sep = self.n.new("ShaderNodeSeparateXYZ")
        self.l.new(uv.outputs["UV"], sep.inputs[0])
        self.u = sep.outputs[0]
        self.v = sep.outputs[1]
        self._torus = {}

    def link(self, a, b):
        if isinstance(a, (int, float)):
            b.default_value = a
        elif isinstance(a, (list, tuple)):
            b.default_value = tuple(a)
        else:
            self.l.new(a, b)

    def math(self, op, a, b=None, c=None, clamp=False):
        m = self.n.new("ShaderNodeMath")
        m.operation = op
        m.use_clamp = clamp
        self.link(a, m.inputs[0])
        if b is not None:
            self.link(b, m.inputs[1])
        if c is not None:
            self.link(c, m.inputs[2])
        return m.outputs[0]

    def add(self, *xs):
        out = xs[0]
        for x in xs[1:]:
            out = self.math("ADD", out, x)
        return out

    def mul(self, a, b):
        return self.math("MULTIPLY", a, b)

    def mix(self, a, b, f):
        """a*(1-f) + b*f fuer Skalare."""
        return self.add(a, self.mul(self.math("SUBTRACT", b, a), f))

    def spanne(self, x, lo, hi, glatt=True):
        """x von [lo, hi] auf [0, 1], geklemmt, mit Smoothstep."""
        m = self.n.new("ShaderNodeMapRange")
        m.interpolation_type = "SMOOTHSTEP" if glatt else "LINEAR"
        m.clamp = True
        self.link(x, m.inputs["Value"])
        m.inputs["From Min"].default_value = lo
        m.inputs["From Max"].default_value = hi
        return m.outputs["Result"]

    def torus(self, fu, fv):
        """Periodische 4D-Koordinaten: fu bzw. fv Einheiten je Texturbreite, zufaellig verschoben."""
        key = (fu, fv, len(self._torus))
        ru, rv = fu / TAU, fv / TAU
        au = self.math("MULTIPLY", self.u, TAU)
        av = self.math("MULTIPLY", self.v, TAU)
        x = self.math("MULTIPLY", self.math("COSINE", au), ru)
        y = self.math("MULTIPLY", self.math("SINE", au), ru)
        z = self.math("MULTIPLY", self.math("COSINE", av), rv)
        w = self.math("MULTIPLY", self.math("SINE", av), rv)
        off = self.rng.uniform(-50.0, 50.0, 4)
        comb = self.n.new("ShaderNodeCombineXYZ")
        self.link(self.math("ADD", x, float(off[0])), comb.inputs[0])
        self.link(self.math("ADD", y, float(off[1])), comb.inputs[1])
        self.link(self.math("ADD", z, float(off[2])), comb.inputs[2])
        w = self.math("ADD", w, float(off[3]))
        self._torus[key] = (comb.outputs[0], w)
        return comb.outputs[0], w

    def warp(self, coords, fu, staerke):
        """Koordinaten mit periodischem Rauschen verbiegen (bleibt periodisch)."""
        vec, w = coords
        nv, nw = self.torus(fu, fu)
        no = self.noise((nv, nw), 1.0, detail=2.0)
        col = self.n.new("ShaderNodeTexNoise")
        col.noise_dimensions = "4D"
        self.link(nv, col.inputs["Vector"])
        self.link(nw, col.inputs["W"])
        col.inputs["Scale"].default_value = 1.0
        vm = self.n.new("ShaderNodeVectorMath")
        vm.operation = "SUBTRACT"
        self.link(col.outputs["Color"], vm.inputs[0])
        vm.inputs[1].default_value = (0.5, 0.5, 0.5)
        sc = self.n.new("ShaderNodeVectorMath")
        sc.operation = "SCALE"
        self.link(vm.outputs[0], sc.inputs[0])
        sc.inputs["Scale"].default_value = staerke
        add = self.n.new("ShaderNodeVectorMath")
        add.operation = "ADD"
        self.link(vec, add.inputs[0])
        self.link(sc.outputs[0], add.inputs[1])
        return add.outputs[0], self.add(w, self.mul(self.math("SUBTRACT", no, 0.5), staerke))

    def noise(self, coords, scale, detail=2.0, rough=0.5, lac=2.0, distortion=0.0, typ="FBM"):
        vec, w = coords
        nn = self.n.new("ShaderNodeTexNoise")
        nn.noise_dimensions = "4D"
        nn.noise_type = typ
        self.link(vec, nn.inputs["Vector"])
        self.link(w, nn.inputs["W"])
        nn.inputs["Scale"].default_value = scale
        nn.inputs["Detail"].default_value = detail
        nn.inputs["Roughness"].default_value = rough
        nn.inputs["Lacunarity"].default_value = lac
        nn.inputs["Distortion"].default_value = distortion
        return nn.outputs["Factor"]

    def voronoi(self, coords, scale, feature="F1", random=1.0):
        vec, w = coords
        vn = self.n.new("ShaderNodeTexVoronoi")
        vn.voronoi_dimensions = "4D"
        vn.feature = feature
        self.link(vec, vn.inputs["Vector"])
        self.link(w, vn.inputs["W"])
        vn.inputs["Scale"].default_value = scale
        vn.inputs["Randomness"].default_value = random
        if "Color" not in vn.outputs:		# DISTANCE_TO_EDGE hat keine Zellfarbe
            return vn.outputs["Distance"], None, None, None
        sep = self.n.new("ShaderNodeSeparateColor")
        self.link(vn.outputs["Color"], sep.inputs[0])
        return vn.outputs["Distance"], sep.outputs[0], sep.outputs[1], sep.outputs[2]

    def zellen(self, coords, scale=1.0):
        """Voronoi-Zellen, wie sie auf der Torusflaeche liegen: Abstand zur Zellkante und zwei Zufallswerte je
        Zelle. Der Abstand zum naechsten Merkmalspunkt (F1) taugt auf dem 4D-Torus nicht: die Punkte liegen
        im 4D-Raum verteilt, meist neben der Flaeche, F1 wird dort selten klein (nur vereinzelte Flecken).
        Die Zellkanten dagegen schneiden die Flaeche ueberall, die Zellen fuellen sie luecken los."""
        kante = self.voronoi(coords, scale, "DISTANCE_TO_EDGE")[0]
        _, r1, r2, _ = self.voronoi(coords, scale, "F1")
        return kante, r1, r2

    def rampe(self, x, stops):
        r = self.n.new("ShaderNodeValToRGB")
        cr = r.color_ramp
        cr.interpolation = "LINEAR"
        while len(cr.elements) > 1:
            cr.elements.remove(cr.elements[-1])
        cr.elements[0].position = stops[0][0]
        cr.elements[0].color = (*stops[0][1], 1.0)
        for pos, col in stops[1:]:
            e = cr.elements.new(pos)
            e.color = (*col, 1.0)
        self.link(x, r.inputs[0])
        return r

    def mixfarbe(self, a, b, f):
        m = self.n.new("ShaderNodeMix")
        m.data_type = "RGBA"
        m.blend_type = "MIX"
        self.link(f, m.inputs[0])
        self.link(a, m.inputs[6])
        self.link(b, m.inputs[7])
        return m.outputs[2]


# -------------------------------------------------------------------------------------------------
# Materialien. Rueckgabe: {"t": Wert fuer die Rampe, "h": Hoehe, weitere: Masken fuer Farbschichten}
# Frequenzen in "Einheiten je Texturbreite". Eine 256er-Originaltextur ist 4x4 Kacheln = 8x8 Kartenzellen.
# -------------------------------------------------------------------------------------------------
def halmlinien(k, fu, fv, breite=0.25):
    """Halme als gestreckte Voronoi-Zellen: jede Zelle ein Strich mit eigener Helligkeit, zur Kante hin dunkel.
    (Nulldurchgaenge eines gestreckten Rauschens -- der erste Versuch -- ergaben wellige Linien wie
    Wasserspiegelungen.) breite: Abstand zur Kante, ab dem der Strich volle Helligkeit hat."""
    kante, r1, _ = k.zellen(k.warp(k.torus(fu, fv), max(fu, fv) / 6, 0.15))
    return k.mul(k.spanne(kante, 0.0, breite), k.add(0.3, k.mul(r1, 0.7)))


def gras(k, p):
    hu, hv = p.get("halme_u", 150), p.get("halme_v", 14)
    gross = k.noise(k.torus(3, 3), 1.0, detail=3.0, rough=0.55)
    mittel = k.noise(k.torus(12, 12), 1.0, detail=4.0, rough=0.6)
    # drei Lagen Halme (gestreckte Linien), dazwischen dunkle Luecken
    h1 = halmlinien(k, hu, hv)
    h2 = halmlinien(k, hu * 1.7, hv * 1.4)
    h3 = halmlinien(k, hu * 2.9, hv * 2.2)
    halme = k.math("MAXIMUM", k.math("MAXIMUM", h1, k.mul(h2, 0.85)), k.mul(h3, 0.7))
    fl = p.get("flecken", 0.25)
    t = k.add(k.mul(halme, 0.5), k.mul(mittel, 0.3), k.mul(gross, fl))
    h = k.add(k.mul(halme, 0.6), k.mul(mittel, 0.3))
    # Stroh: Flecken mittlerer Groesse, darin einzelne Halmlagen
    stroh = k.add(k.mul(k.noise(k.torus(6, 6), 1.0, detail=4.0, rough=0.6), 0.75), k.mul(h2, 0.25))
    # offene Stellen: unregelmaessige Flecken, wo die Halme duenn sind
    bf = p.get("boden_f", 8)
    boden = k.add(k.noise(k.torus(bf, bf), 1.0, detail=6.0, rough=0.65), k.mul(k.math("SUBTRACT", 1.0, halme), 0.08))
    return {"t": t, "h": h, "stroh": stroh, "boden": boden}


def erde(k, p):
    base = k.noise(k.torus(5, 5), 1.0, detail=6.0, rough=0.55)
    mitte = k.noise(k.torus(26, 26), 1.0, detail=4.0, rough=0.6)
    korn = k.noise(k.torus(330, 330), 1.0, detail=1.0)
    t = k.add(k.mul(base, p.get("flecken", 0.25)), k.mul(mitte, 0.3), k.mul(korn, 0.3))
    h = k.add(k.mul(base, 0.1), k.mul(mitte, 0.3), k.mul(korn, 0.2))
    # Kies in Lagen: (Frequenz, Dichte, Groesse); Zufallswert je Zelle entscheidet, ob dort ein Stein liegt
    for freq, dichte, groesse in p.get("kies", [[60, 0.5, 0.45], [150, 0.5, 0.4]]):
        kante, r1, r2 = k.zellen(k.warp(k.torus(freq, freq), freq / 2, 0.6))
        form = k.spanne(kante, groesse * 0.1, groesse * 0.4)	# gewoelbt, zur Zellkante hin flach
        stein = k.mul(form, k.math("LESS_THAN", r1, dichte))
        t = k.mix(t, k.add(0.4, k.mul(r2, 0.6)), stein)
        h = k.add(h, k.mul(stein, 0.5))
    return {"t": t, "h": h}


def fels(k, p):
    fu, fv = p.get("platten", [6, 12])
    koord = k.warp(k.torus(fu, fv), 4, 0.6)
    kante, _, _, _ = k.voronoi(koord, 1.0, "DISTANCE_TO_EDGE")
    _, pr, _, _ = k.voronoi(koord, 1.0, "F1")
    fuge = k.spanne(kante, 0.0, 0.12)				# 0 in der Fuge
    klein = k.warp(k.torus(fu * 3, fv * 3), 10, 0.5)
    kante2, _, _, _ = k.voronoi(klein, 1.0, "DISTANCE_TO_EDGE")
    _, pr2, _, _ = k.voronoi(klein, 1.0, "F1")
    fuge2 = k.spanne(kante2, 0.0, 0.1)
    grat = k.noise(k.torus(40, 55), 1.0, detail=6.0, rough=0.6, typ="RIDGED_MULTIFRACTAL")
    grat = k.spanne(grat, 0.0, 1.2, glatt=False)
    t = k.add(k.mul(pr, 0.25), k.mul(pr2, 0.2), k.mul(grat, 0.4), 0.1)
    t = k.mul(t, k.add(0.4, k.mul(fuge, 0.35), k.mul(fuge2, 0.25)))
    h = k.add(k.mul(fuge, 0.35), k.mul(fuge2, 0.25), k.mul(grat, 0.3), k.mul(pr2, 0.1))
    # Moos in waagerechten Baendern und in den Fugen
    moos_n = k.noise(k.torus(3, 8), 1.0, detail=5.0, rough=0.62)
    moos_f = k.add(moos_n, k.mul(k.math("SUBTRACT", 1.0, fuge), 0.2), k.mul(k.math("SUBTRACT", 1.0, fuge2), 0.1))
    return {"t": t, "h": h, "moos": moos_f}


ARTEN = {"gras": gras, "erde": erde, "fels": fels}


# -------------------------------------------------------------------------------------------------
def szene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.render.bake.margin = 0
    sc.render.bake.use_clear = True
    bpy.ops.mesh.primitive_plane_add(size=1.0)
    obj = bpy.context.active_object
    mat = bpy.data.materials.new("Boden")
    obj.data.materials.append(mat)
    return sc, obj, mat


def backen(sc, mat, ausgabe, groesse, samples, pfad=None):
    """Den Shader-Ausgang `ausgabe` (Farbe oder Wert) als Emission in ein Bild backen; numpy (H, W, 4)."""
    tree = mat.node_tree
    img = bpy.data.images.new("bake", groesse, groesse, alpha=False, float_buffer=True)
    img.colorspace_settings.name = "Non-Color"
    tex = tree.nodes.new("ShaderNodeTexImage")
    tex.image = img
    tree.nodes.active = tex
    em = tree.nodes.new("ShaderNodeEmission")
    tree.links.new(ausgabe, em.inputs["Color"])
    out = tree.nodes.new("ShaderNodeOutputMaterial")
    tree.links.new(em.outputs[0], out.inputs["Surface"])
    for n in tree.nodes:
        if n.bl_idname == "ShaderNodeOutputMaterial":
            n.is_active_output = n == out
    sc.cycles.samples = samples
    bpy.ops.object.bake(type="EMIT")
    px = np.array(img.pixels[:], dtype=np.float32).reshape(groesse, groesse, 4)
    px = px[::-1]		# Blender speichert von unten nach oben; hier oben zuerst wie Pillow
    if pfad:
        np.save(pfad, px[..., :3].astype(np.float32))
    tree.nodes.remove(tex)
    tree.nodes.remove(em)
    tree.nodes.remove(out)
    bpy.data.images.remove(img)
    return px


def rampe_anpassen(stops, werte):
    """Stuetzstellen (Quantil q, Farbe) an die Quantile des prozeduralen Werts legen."""
    q = np.quantile(werte, [s[0] for s in stops])
    q = np.maximum.accumulate(np.clip(q, 0.0, 1.0))
    out = []
    last = -1.0
    for (_, col), pos in zip(stops, q):
        pos = max(pos, last + 1e-4)
        out.append((float(min(pos, 1.0)), col))
        last = pos
    return out


def main():
    auftrag = json.load(open(sys.argv[sys.argv.index("--") + 1], encoding="utf-8"))
    sc, obj, mat = szene()
    if not mat.node_tree:
        mat.use_nodes = True
    tree = mat.node_tree
    for n in list(tree.nodes):
        tree.nodes.remove(n)
    k = Knoten(tree, auftrag.get("seed", 1))
    felder = ARTEN[auftrag["art"]](k, auftrag.get("param", {}))
    t, h = felder["t"], felder["h"]
    groesse, samples = auftrag["groesse"], auftrag.get("samples", 16)

    # Vorlauf: Verteilung von t (und der Masken) auf 128 Pixeln
    probe_t = backen(sc, mat, t, 128, 1)[..., 0].ravel()
    farbe = k.rampe(t, rampe_anpassen(auftrag["rampe"], probe_t)).outputs["Color"]
    info = {"t_quantile": [round(float(x), 4) for x in np.quantile(probe_t, [0.05, 0.5, 0.95])]}
    # Farbschichten: Maske so schwellen, dass sie den Anteil des Originals deckt; eigene Rampe
    for schicht in auftrag.get("schichten", []):
        maske = felder[schicht["maske"]]
        probe_m = backen(sc, mat, maske, 128, 1)[..., 0].ravel()
        schwelle = float(np.quantile(probe_m, 1.0 - schicht["anteil"]))
        weich = schicht.get("weich", 0.02)
        m = k.spanne(maske, schwelle - weich, schwelle + weich)
        farbe2 = k.rampe(t, rampe_anpassen(schicht["rampe"], probe_t)).outputs["Color"]
        farbe = k.mixfarbe(farbe, farbe2, m)
        h = k.math("SUBTRACT", h, k.mul(m, schicht.get("hoehe", 0.0)))
        info[schicht["maske"]] = round(schwelle, 4)
    os.makedirs(auftrag["aus"], exist_ok=True)
    backen(sc, mat, farbe, groesse, samples, os.path.join(auftrag["aus"], "albedo.npy"))
    backen(sc, mat, h, groesse, samples, os.path.join(auftrag["aus"], "hoehe.npy"))
    info["knoten"] = len(tree.nodes)
    json.dump(info, open(os.path.join(auftrag["aus"], "info.json"), "w", encoding="utf-8"), indent=1)
    print("BODEN_BACKEN fertig", auftrag["name"], info)


if __name__ == "__main__":
    main()
