# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Freie Bäume und Büsche aus CC0-Scans: Massenauftrag (baeume_frei.json) für viele Schnittstellennamen aus wenigen
# Grundtypen. Liest keine EA-Datei: Namen der Modelle und Texturen sind Schnittstelle und stehen im
# Auftrag (Vorlage aus der Bestandsliste von baum_bestand.py, Befehl "auftrag"), Größe nach eigener Höhentabelle
# (Meter x Spielmaßstab 6,5), Farbe aus dem Scan bzw. echten Herbstblättern (ambientCG), Gerüst nach w3d_geruest.py.
#
# Zero Hour zeichnet Bäume über den Baum-Puffer (W3DTreeBuffer): ein Netz je Art, Textur aus der INI (TextureName). Gibt es
# unter Art\Textures die Datei <TextureName ohne Endung>_tree.dds, nimmt der Puffer diese eigene Textur samt ihrer
# Mip-Kette statt des Kachelblocks im Baum-Atlas. Generals (und Zero-Hour-Objekte über W3DModelDraw wie GenericTree) lesen
# die Textur aus dem Modell -- das Modell nennt dieselbe Datei. Damit gehört zu jeder TGA-Schnittstelle genau eine
# eigene Textur, und alle Modelle, die diese TGA in der INI nutzen, legen ihre UV auf deren Aufteilung.
#
# Ablauf je Textur:  blattsatz (nur Herbst) -> render (Blender) -> textur -> modell -> paket;  pruefen; packen
#   python baum_frei.py auftrag <bestand.json> --familie=ptdogwod [--grundtyp=A] [--aus=<vorlage.json>]
#                                                     Vorlage für den Auftrag: je TGA die Modelle, Objekte, Instanzen
#   python baum_frei.py jahreszeit <bestand.json> [--schwelle=0.5]   Sommer/Herbst je Textur aus Name und Kartenverwendung
#   python baum_frei.py blattsatz <quelle>_<ton>...   Herbst: Blattplätze des Scan-Atlas mit echten Herbstblättern füllen
#   (Texturen, Modelle, Pakete je Farbvariante: --variante=gruen|rotorange|gelb, Ordner arbeit/varianten/<variante>)
#   python baum_frei.py render <bild>...              (ruft Blender) Seitenbilder, Aufsicht, Kronenscheibe
#   python baum_frei.py wachsen <quelle>...           (ruft Blender) Quelle ohne Scan mit baum_wachsen.py erzeugen
#   python baum_frei.py bauen <textur>... [--schritte=render,textur,modell,paket]   (Vorgabe: alle; render nur, wenn es fehlt)
#   python baum_frei.py pruefen [<textur>...]         Gerüst (w3d_geruest.soll), Texturname, Eckenbudget, Feuerpunkt
#   python baum_frei.py packen <ziel.big> [<textur>...] --stufe=sd|hd|uhd
#   python baum_frei.py ablegen <ordner> [<textur>...] [--stufe=hd]   Paket als Ordner für ProjectData\\Inhalt mit texturpaket.txt
#                                                     (vorher bauen ... --schritte=paket --stufen=hd)
#   python baum_frei.py vorschau [<textur>...]        Übersichtsbild der Texturen (arbeit/vorschau.png)
#   python baum_frei.py ansicht <ziel> [<textur>...]  (ruft Blender) fertige Modelle in einer Reihe, schräg von oben und nah
#   blender --background --factory-startup --python baum_frei.py -- render <bild>   (Blender-Teil von "render")
import glob
import json
import math
import os
import subprocess
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HIER, '..'))
sys.path.insert(0, HIER)
AUFTRAG_DATEI = os.environ.get('BAEUME_AUFTRAG', os.path.join(HIER, 'baeume_frei.json'))
BLENDER = os.environ.get('BLENDER', 'blender')
EIGEN = '_tree'              # Schnittstelle der Engine: <TextureName ohne Endung>_tree.dds
ALPHA_REF = 96               # Alphatest-Schwelle der Engine (0x60, shader.cpp)
MAX_ECKEN = 60               # Budget des Baum-Puffers je Baum (wie EA)

try:
    import bpy  # noqa: F401
    IN_BLENDER = True
except ImportError:
    IN_BLENDER = False

A = json.load(open(AUFTRAG_DATEI, encoding='utf-8'))
V = A['vorgaben']
# Pfade außerhalb des Repos; andere Rechner setzen sie über die Umgebung
for _k, _e in (('arbeit', 'BAEUME_ARBEIT'), ('modelle', 'BAEUME_MODELLE'), ('texconv', 'BAEUME_TEXCONV')):
    if os.environ.get(_e):
        V[_k] = os.environ[_e]
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import pfade  # noqa: E402
for _k in ('arbeit', 'modelle', 'texconv'):
    if _k in V:
        V[_k] = pfade.unter_werkzeuge(V[_k])
# Verbrannte Fassungen (_B, Generals und Palmen): eine Textur mit "b_fassung" bekommt die Textur <Stamm>_B (Farbe verkohlt,
# Laub in Flecken weg, Schwelle "verbrannt" der Vorgaben) und je Modell ohne "ohne_b" das Modell <Modell>_B mit derselben Form
for _s, _t in list(A['texturen'].items()):
    if _t.get('b_fassung'):
        _b = {k: v for k, v in _t.items() if k not in ('b_fassung', 'schnee')}
        _b['verbrannt'] = float(V.get('verbrannt', 0.6))
        _b['modelle'] = {m + '_B': dict(v, _objekte=v.get('_objekte', '') + ' (verbrannt)')
                         for m, v in _t['modelle'].items() if not v.get('ohne_b')}
        A['texturen'][_s + '_B'] = _b

# Stufen für paket/ablegen (--stufen=sd,hd); Vorgabe alle des Auftrags
STUFEN = list(V['stufen'])

# Farbvariante (Auftrag "varianten"): welcher Ton für Sommer- und für Herbstbäume; über --variante=<name> oder BAEUME_VARIANTE
VARIANTE = os.environ.get('BAEUME_VARIANTE', V.get('variante', 'gruen'))


def log(*a):
    print('BAUM', *a, flush=True)


def arbeit(*teile):
    return os.path.join(V['arbeit'], *teile)


def arbeit_v(*teile):
    """Arbeitsordner der Farbvariante (Texturen, Modelle, Pakete)."""
    return os.path.join(V['arbeit'], 'varianten', VARIANTE, *teile)


def bild_cfg(bild):
    """Bild = <Basis>_<Ton>: Basis = Quelle und Blickwinkel (Auftrag "basen"), Ton = Laub (Auftrag "toene"; ohne Blattsatz
    das Laub des Scans, sonst Blattsatz <Quelle>_<Ton> aus echten Herbstblättern)."""
    basis, ton = bild.rsplit('_', 1)
    b = dict(A['basen'][basis])
    if A['toene'].get(ton):
        b['blattsatz'] = b['quelle'] + '_' + ton
    b['ton'] = ton
    return b


def blattsatz_cfg(name):
    quelle, ton = name.rsplit('_', 1)
    c = dict(A['toene'][ton])
    c['quelle'] = quelle
    return c


def modelle_pfad(*teile):
    return os.path.join(V['modelle'], *teile)


def textur_datei(stamm):
    """Name der eigenen Textur (ohne Endung) zur TGA-Schnittstelle <stamm>.tga."""
    return stamm + EIGEN


# ------------------------------------------------------------------------------------------------
# Auftrag: Vorlage aus der Bestandsliste (nur Namen)
# ------------------------------------------------------------------------------------------------
def auftrag(bestand, familie, grundtyp):
    """Alle Objekte, deren Baum-Puffer-TGA zur Familie gehört, nach TGA gruppiert: je TGA die Modelle beider Spiele, die
    ihre UV darauf legen (Zero Hour über den Puffer, Generals über das Modell), mit Objektnamen und Instanzen."""
    b = json.load(open(bestand, encoding='utf-8'))
    tex, modell_zu = {}, {}

    def eintragen(stamm, o, spiel):
        eintrag = tex.setdefault(stamm, {'grundtyp': grundtyp, 'bild': None, 'modelle': {}})
        m = o['modelle'][0]
        modell_zu[m['name'].lower()] = stamm
        mm = eintrag['modelle'].setdefault(m['name'], {'hoehe_m': None, 'objekte': [], 'instanzen': 0})
        if o['name'] not in mm['objekte']:
            mm['objekte'].append(o['name'])
        mm['instanzen'] += o['instanzen']
        mm.setdefault('spiele', [])
        if spiel not in mm['spiele']:
            mm['spiele'].append(spiel)

    # Zuerst die Baum-Puffer-Objekte (die TGA der INI bestimmt die Gruppe), dann Objekte über W3DModelDraw (Generals,
    # GenericTree) beim Modell gleichen Namens, sonst bei der Textur ihres Modells
    rest = []
    for spiel, e in b.items():
        for o in e['objekte'].values():
            if not o['modelle']:
                continue
            t = o.get('baumtextur')
            if t and t[0]:
                stamm = os.path.splitext(t[0])[0]
                if stamm.lower().startswith(familie.lower()):
                    eintragen(stamm, o, spiel)
            else:
                rest.append((spiel, o))
    for spiel, o in rest:
        m = o['modelle'][0]
        stamm = modell_zu.get(m['name'].lower())
        if stamm is None and m.get('texturen'):
            stamm = os.path.splitext(m['texturen'][0])[0]
            if not stamm.lower().startswith(familie.lower()) or stamm.lower().endswith('_b'):
                continue
        if stamm:
            eintragen(stamm, o, spiel)
    for stamm, e in sorted(tex.items()):
        print('%-12s %s' % (stamm, ', '.join('%s (%s, %d)' % (m, '/'.join(v['objekte']), v['instanzen'])
                                             for m, v in e['modelle'].items())))
    return tex


def jahreszeit(bestand, schwelle):
    """Sommer oder Herbst je TGA-Schnittstelle aus Name und Verwendung (nur Namen und Kartenlisten, keine Pixel): Namen mit
    'Fall' sind Herbst; sonst Herbst, wenn mindestens `schwelle` der Zero-Hour-Instanzen der Modelle auf Karten stehen, die
    auch Herbsteichen tragen (Herbstwälder). Ergebnis für das Feld "jahreszeit" im Auftrag."""
    b = json.load(open(bestand, encoding='utf-8'))
    karten = b['ZH']['karten']
    zaehl, herbst = {}, {}
    for objs in karten.values():
        h = any('fall' in o.lower() for o in objs)
        for o, n in objs.items():
            zaehl[o.lower()] = zaehl.get(o.lower(), 0) + n
            herbst[o.lower()] = herbst.get(o.lower(), 0) + (n if h else 0)
    for stamm, t in sorted(A['texturen'].items()):
        objekte = [o.strip().split(' ')[0].lower() for m in t['modelle'].values() for o in m.get('_objekte', '').split(',')]
        n = sum(zaehl.get(o, 0) for o in objekte)
        hn = sum(herbst.get(o, 0) for o in objekte)
        anteil = hn / n if n else 0.0
        js = 'herbst' if 'fall' in stamm.lower() or anteil >= schwelle else 'sommer'
        print('%-12s %-7s Anteil auf Herbstkarten %3.0f %% (%d Instanzen; im Auftrag: %s)' % (
            stamm, js, 100 * anteil, n, t.get('jahreszeit', '-')))


# ------------------------------------------------------------------------------------------------
# Blattsatz (Herbst): echte Herbstblätter in die Blattplätze des Scan-Atlas
# ------------------------------------------------------------------------------------------------
def _flecken(maske, min_anteil):
    """Zusammenhängende Flecken einer Maske: [(Ausschnitt y0, x0, y1, x1, Fläche, Winkel der Hauptachse)]."""
    import numpy as np
    from scipy import ndimage
    lab, n = ndimage.label(maske)
    out = []
    for i, sl in enumerate(ndimage.find_objects(lab), 1):
        m = lab[sl] == i
        fl = int(m.sum())
        if fl < min_anteil * maske.size:
            continue
        ys, xs = np.nonzero(m)
        c = np.cov(np.stack([xs - xs.mean(), ys - ys.mean()]))
        w, v = np.linalg.eigh(c)
        ax = v[:, 1]
        out.append((sl[0].start, sl[1].start, sl[0].stop, sl[1].stop, fl, math.degrees(math.atan2(ax[1], ax[0])), i, lab))
    return out


def blattsatz(name):
    """Der Blattatlas des Scans (eine Fläche je Blatt) behält Aufteilung und UV; in jeden Blattplatz kommt ein ganzes Blatt
    aus den ambientCG-Sätzen (Farbe und Deckkraft), auf die Lage des Platzes gedreht und eingepasst. So trägt der
    gerenderte Baum echte Herbstblätter, nicht umgefärbte Sommerblätter."""
    import numpy as np
    from PIL import Image
    import colorsys
    cfg = blattsatz_cfg(name)
    q = A['quellen'][cfg['quelle']]
    farbe = Image.open(modelle_pfad(q['blatt_farbe'])).convert('RGB')
    deck = Image.open(modelle_pfad(q['blatt_alpha'])).convert('L').resize(farbe.size)
    plaetze = _flecken(np.asarray(deck) > 127, 0.002)
    blaetter = []
    for satz in cfg['saetze']:
        c = Image.open(glob.glob(modelle_pfad(satz, '*_Color.png'))[0]).convert('RGB')
        o = Image.open(glob.glob(modelle_pfad(satz, '*_Opacity.png'))[0]).convert('L').resize(c.size)
        oa = np.asarray(o)
        rgba = np.concatenate([np.asarray(c), oa[..., None]], 2)
        for y0, x0, y1, x1, fl, winkel, i, lab in _flecken(oa > 127, 0.002):
            stueck = rgba[y0:y1, x0:x1].copy()
            stueck[..., 3] = np.where(lab[y0:y1, x0:x1] == i, stueck[..., 3], 0)
            # Auswahl nach dem eigenen Farbton des Blatts (Rot/Orange, Gelb): ganze echte Blätter, nichts umgefärbt
            sicht = stueck[..., 3] > 127
            mrgb = stueck[..., :3][sicht].mean(0) / 255
            hue, sat, _v = colorsys.rgb_to_hsv(*mrgb)
            hue *= 360
            if cfg.get('farbton') and not any(a <= hue <= b for a, b in cfg['farbton']):
                continue
            if sat < float(cfg.get('saettigung_min', 0)):
                continue
            # Hauptachse senkrecht stellen (Bildwinkel y nach unten: Drehung gegen den Uhrzeigersinn um 90 + Winkel)
            blatt = Image.fromarray(stueck, 'RGBA').rotate(90 + winkel, resample=Image.BICUBIC, expand=True)
            blatt = blatt.crop(blatt.getbbox())
            blaetter.append((satz, blatt))
    if not blaetter:
        raise SystemExit('%s: keine Blätter in %s' % (name, cfg['saetze']))
    w, h = farbe.size
    neu = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    schritt = int(cfg.get('schritt', 3))
    for k, (y0, x0, y1, x1, fl, winkel, _i, _lab) in enumerate(plaetze):
        satz, blatt = blaetter[(k * schritt + int(cfg.get('versatz', 0))) % len(blaetter)]
        # auf die Lage des Platzes drehen; bei rundlichen Blättern (Ahorn) ist die Hauptachse unsicher -- die Lage, die den
        # Platz besser füllt, gewinnt
        best = None
        for zusatz in (0, 90):
            b = blatt.rotate(-winkel - 90 + zusatz, resample=Image.BICUBIC, expand=True)
            b = b.crop(b.getbbox())
            s = min((x1 - x0) / b.width, (y1 - y0) / b.height)
            if best is None or s > best[1]:
                best = (b, s)
        b, s = best[0], best[1] * float(cfg.get('groesse', 1.0))
        b = b.resize((max(1, round(b.width * s)), max(1, round(b.height * s))), Image.LANCZOS)
        neu.alpha_composite(b, (x0 + ((x1 - x0) - b.width) // 2, y0 + ((y1 - y0) - b.height) // 2))
    rgba = np.asarray(neu).astype(np.float32) / 255
    rgb = _farbe_fortsetzen(rgba[..., :3], rgba[..., 3] > 0.5)
    ziel = arbeit('blattsaetze', name)
    os.makedirs(ziel, exist_ok=True)
    Image.fromarray(np.clip(rgb * 255 + 0.5, 0, 255).astype(np.uint8), 'RGB').save(os.path.join(ziel, 'farbe.png'))
    Image.fromarray((rgba[..., 3] * 255 + 0.5).astype(np.uint8), 'L').save(os.path.join(ziel, 'deckkraft.png'))
    log('BLATTSATZ', name, '%d Plätze, %d Blätter aus %s' % (len(plaetze), len(blaetter), ', '.join(cfg['saetze'])))


def _farbe_fortsetzen(rgb, sicht, schritte=16):
    """Farbe in die transparenten Pixel fortsetzen (Mip-Stufen und Filtern ziehen sonst dunkle Säume)."""
    import numpy as np
    rgb = rgb.copy()
    sicht = sicht.copy()
    for _ in range(schritte):
        summe = np.zeros_like(rgb)
        anz = np.zeros(sicht.shape, np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            summe += np.roll(np.roll(rgb * sicht[..., None], dy, 0), dx, 1)
            anz += np.roll(np.roll(sicht.astype(np.float32), dy, 0), dx, 1)
        neu = (anz > 0) & ~sicht
        if not neu.any():
            break
        rgb[neu] = summe[neu] / anz[neu][:, None]
        sicht = sicht | neu
    if (~sicht).any():
        rgb[~sicht] = rgb[sicht].mean(0)
    return rgb


# ------------------------------------------------------------------------------------------------
# Render (Blender): Seitenbilder je Azimut, Aufsicht der ganzen Krone, Aufsicht einer Kronenscheibe
# ------------------------------------------------------------------------------------------------
def render_blender(bild):
    import bpy
    import numpy as np
    from mathutils import Vector
    cfg = bild_cfg(bild)
    q = A['quellen'][cfg['quelle']]
    gt = A['grundtypen'][cfg.get('grundtyp', 'A')]
    bpy.ops.wm.open_mainfile(filepath=modelle_pfad(q['datei']))
    baum = bpy.data.objects[q['objekt']]
    if float(q.get('blatt_skala', 1.0)) != 1.0:
        blaetter_vergroessern(baum, q['blatt_material'], float(q['blatt_skala']))
    for o in bpy.data.objects:
        o.hide_render = o is not baum
    for c in bpy.data.collections:
        c.hide_render = False
    if cfg.get('blattsatz'):
        bs = arbeit('blattsaetze', cfg['blattsatz'])
        mat = bpy.data.materials[q['blatt_material']]
        for nd in mat.node_tree.nodes:
            if nd.type != 'TEX_IMAGE' or not nd.image:
                continue
            p = nd.image.filepath.lower()
            if 'leaves_diff' in p:
                nd.image = bpy.data.images.load(os.path.join(bs, 'farbe.png'))
            elif 'leaves_alpha' in p:
                nd.image = bpy.data.images.load(os.path.join(bs, 'deckkraft.png'))
                nd.image.colorspace_settings.name = 'Non-Color'
            elif 'leaves_' in p and cfg.get('blatt_glatt', True):
                # Normalen und Rauheit des Sommerblatts passen nicht auf die neuen Blätter: neutral lassen
                for l in list(nd.outputs[0].links):
                    mat.node_tree.links.remove(l)
    ziel = arbeit('bilder', bild)
    os.makedirs(ziel, exist_ok=True)
    me = baum.data
    co = np.empty(len(me.vertices) * 3)
    me.vertices.foreach_get('co', co)
    co = co.reshape(-1, 3)
    M = np.array(baum.matrix_world)
    co = co @ M[:3, :3].T + M[:3, 3]
    z0, z1 = co[:, 2].min(), co[:, 2].max()
    hoehe = z1 - z0
    unten = co[co[:, 2] < z0 + 0.05 * hoehe]
    mitte = np.array([np.median(unten[:, 0]), np.median(unten[:, 1])])
    r = np.hypot(co[:, 0] - mitte[0], co[:, 1] - mitte[1])
    profil = []
    for i in range(20):
        a, b = z0 + hoehe * i / 20, z0 + hoehe * (i + 1) / 20
        m = (co[:, 2] >= a) & (co[:, 2] < b)
        profil.append(float(np.percentile(r[m], 97)) if m.sum() > 20 else 0.0)
    r_max = max(profil)
    log('SCAN', bild, 'Höhe %.2f Mitte %.2f %.2f r_max %.2f' % (hoehe, mitte[0], mitte[1], r_max))
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'
    sc.cycles.samples = int(V.get('proben', 48))
    sc.cycles.use_denoising = False
    sc.render.film_transparent = True
    sc.view_settings.view_transform = 'Standard'
    sc.view_settings.look = 'None'
    sc.render.image_settings.file_format = 'PNG'
    sc.render.image_settings.color_mode = 'RGBA'
    # Licht drehinvariant (jeder Baum der Karte steht anders gedreht): Himmel und Sonne von oben
    welt = bpy.data.worlds.new('himmel')
    welt.use_nodes = True
    welt.node_tree.nodes['Background'].inputs['Color'].default_value = (0.85, 0.9, 1.0, 1)
    welt.node_tree.nodes['Background'].inputs['Strength'].default_value = float(V.get('himmel', 0.9))
    sc.world = welt
    for o in list(bpy.data.objects):
        if o.type == 'LIGHT':
            bpy.data.objects.remove(o, do_unlink=True)
    sonne = bpy.data.objects.new('sonne', bpy.data.lights.new('sonne', 'SUN'))
    sonne.data.energy = float(V.get('sonne', 2.5))
    sonne.data.angle = math.radians(20)
    sc.collection.objects.link(sonne)
    info = {'hoehe': hoehe, 'z0': z0, 'mitte': mitte.tolist(), 'r_max': r_max, 'profil': [p / r_max for p in profil],
            'bilder': {}}

    kameras = []

    def kamera(name, ort, ziel_pkt, breite, hoch, clip=None, oben=None, aufl_faktor=1.0):
        cam = bpy.data.objects.new(name, bpy.data.cameras.new(name))
        sc.collection.objects.link(cam)
        cam.data.type = 'ORTHO'
        cam.data.ortho_scale = max(breite, hoch)
        cam.location = ort
        if oben:
            cam.rotation_euler = (0.0, 0.0, 0.0)      # senkrecht nach unten, Bild rechts = +X, Bild oben = +Y
        else:
            cam.rotation_euler = (Vector(ziel_pkt) - Vector(ort)).to_track_quat('-Z', 'Y').to_euler()
        cam.data.clip_start, cam.data.clip_end = clip or (0.01, 1000)
        sc.camera = cam
        aufl = int(int(V.get('render', 2048)) * aufl_faktor)
        sc.render.resolution_x = max(8, int(round(aufl * breite / max(breite, hoch))))
        sc.render.resolution_y = max(8, int(round(aufl * hoch / max(breite, hoch))))
        sc.render.filepath = os.path.join(ziel, '%s.png' % name)
        bpy.ops.render.render(write_still=True)
        kameras.append((name, cam, sc.render.resolution_x, sc.render.resolution_y))
        info['bilder'][name] = {'breite': breite, 'hoehe': hoch, 'datei': sc.render.filepath}
        log('BILD', bild, name, sc.render.resolution_x, 'x', sc.render.resolution_y)

    if gt['form'] == 'wedel':
        # Palme: je Wedelobjekt (flach, Fuß bei y = 0, Spitze nach +y) eine Aufsicht wedel_a, wedel_b, ...
        for i, name in enumerate(q['wedel_objekte']):
            wo = bpy.data.objects[name]
            for o in bpy.data.objects:
                o.hide_render = o is not wo
            wc = np.array([wo.matrix_world @ v.co for v in wo.data.vertices])
            lo, hi = wc.min(0), wc.max(0)
            kamera('wedel_' + 'abcd'[i], (float(lo[0] + hi[0]) / 2, float(lo[1] + hi[1]) / 2, float(hi[2]) + 5), None,
                   float(hi[0] - lo[0]) * 1.02, float(hi[1] - lo[1]) * 1.02, oben=True)
        if gt.get('normalen'):
            normalen_bilder(baum, kameras, ziel, info)
        json.dump(info, open(os.path.join(ziel, 'bilder.json'), 'w'), indent=1)
        return

    zm = z0 + hoehe / 2
    # Seitenbilder seite_a, seite_b, ... aus den Azimuten des Bildes (andere Azimute = anderer Umriss derselben Quelle)
    for i, az in enumerate(cfg.get('azimute', gt.get('azimute', [0, 90]))):
        w = math.radians(az)
        d = Vector((math.sin(w), -math.cos(w), 0)) * 100
        kamera('seite_' + 'abcd'[i], (mitte[0] + d.x, mitte[1] + d.y, zm), (mitte[0], mitte[1], zm), 2 * r_max * 1.04,
               hoehe)
        info['bilder']['seite_' + 'abcd'[i]]['azimut'] = az
    # Aufsichten auf die Krone selbst ausgerichtet (Scans stehen oft schief: die Krone sitzt neben dem Stammfuß);
    # Mitte und halbe Kantenlänge relativ zum Stammfuß stehen in bilder.json, die Netze legen Kuppel und Schichten dorthin
    ob = z1 + 5

    def aufsicht(name, von, bis):
        teil = co[(co[:, 2] >= z0 + von * hoehe) & (co[:, 2] <= z0 + bis * hoehe), :2]
        lo, hi = np.percentile(teil, 1, axis=0), np.percentile(teil, 99, axis=0)
        c = (lo + hi) / 2
        halb = float(max(hi - lo)) / 2 * 1.04
        # die Aufsichten belegen in der Textur oft nur einen kleinen Bereich ('aufsicht_aufl': Anteil der Renderauflösung)
        kamera(name, (c[0], c[1], ob), (c[0], c[1], z0), 2 * halb, 2 * halb,
               clip=(ob - (z0 + bis * hoehe), ob - (z0 + von * hoehe)), oben=True,
               aufl_faktor=float(gt.get('aufsicht_aufl', 1.0)))
        info['bilder'][name].update({'mitte': (c - mitte).tolist(), 'halb': halb})

    aufsicht('oben', float(gt.get('krone_ab', 0.3)), 1.01)
    a, b = cfg.get('scheibe', gt.get('scheibe', [0.35, 0.65]))
    aufsicht('scheibe', a, b)
    if gt.get('normalen'):
        normalen_bilder(baum, kameras, ziel, info)
    json.dump(info, open(os.path.join(ziel, 'bilder.json'), 'w'), indent=1)


def normalen_bilder(baum, kameras, ziel, info):
    """Zweite Aufnahme je Kamera mit der Höhenkomponente der Normalen statt der Farbe (Emission z * 0,5 + 0,5, Deckkraft
    der Zweigkarten bleibt): daraus legt die Textur Schnee auf die nach oben weisenden Zweige (Fassungen _s, _s2).
    Cycles dreht die Normale abgewandter Flächen zur Kamera."""
    import bpy
    sc = bpy.context.scene
    sc.world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.0
    sc.cycles.samples = 16
    try:
        sc.view_settings.view_transform = 'Raw'
    except TypeError:
        pass
    for mat in {m for m in baum.data.materials if m}:
        nt = mat.node_tree
        bsdf = next((n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED'), None)
        out = next((n for n in nt.nodes if n.type == 'OUTPUT_MATERIAL'), None)
        if bsdf is None or out is None:
            continue
        geo = nt.nodes.new('ShaderNodeNewGeometry')
        xyz = nt.nodes.new('ShaderNodeSeparateXYZ')
        nt.links.new(geo.outputs['Normal'], xyz.inputs[0])
        ma = nt.nodes.new('ShaderNodeMath')
        ma.operation = 'MULTIPLY_ADD'
        ma.inputs[1].default_value = 0.5
        ma.inputs[2].default_value = 0.5
        nt.links.new(xyz.outputs['Z'], ma.inputs[0])
        em = nt.nodes.new('ShaderNodeEmission')
        nt.links.new(ma.outputs[0], em.inputs['Color'])
        tr = nt.nodes.new('ShaderNodeBsdfTransparent')
        mix = nt.nodes.new('ShaderNodeMixShader')
        alpha = bsdf.inputs['Alpha']
        if alpha.links:
            nt.links.new(alpha.links[0].from_socket, mix.inputs['Fac'])
        else:
            mix.inputs['Fac'].default_value = alpha.default_value
        nt.links.new(tr.outputs[0], mix.inputs[1])
        nt.links.new(em.outputs[0], mix.inputs[2])
        nt.links.new(mix.outputs[0], out.inputs['Surface'])
    for name, cam, rx, ry in kameras:
        sc.camera = cam
        sc.render.resolution_x, sc.render.resolution_y = rx, ry
        sc.render.filepath = os.path.join(ziel, '%s_nz.png' % name)
        bpy.ops.render.render(write_still=True)
        info['bilder'][name]['normalen'] = sc.render.filepath
        log('NORMALEN', name)


def blaetter_vergroessern(baum, material, faktor):
    """Dichtere Krone: jedes Blatt (zusammenhängende Fläche des Blattmaterials) um seinen eigenen Mittelpunkt vergrößern.
    Äste, Stamm und Lage der Blätter bleiben; die Krone wirkt geschlossener, ohne zu einem Klumpen zu werden."""
    import numpy as np
    me = baum.data
    idx = [i for i, m in enumerate(me.materials) if m and m.name == material]
    if not idx:
        raise SystemExit('Blattmaterial %s fehlt' % material)
    npoly, nloop, nv = len(me.polygons), len(me.loops), len(me.vertices)
    pm = np.empty(npoly, np.int64); me.polygons.foreach_get('material_index', pm)
    ls = np.empty(npoly, np.int64); me.polygons.foreach_get('loop_start', ls)
    lt = np.empty(npoly, np.int64); me.polygons.foreach_get('loop_total', lt)
    lv = np.empty(nloop, np.int64); me.loops.foreach_get('vertex_index', lv)
    poly_of_loop = np.repeat(np.arange(npoly), lt)
    blatt = pm[poly_of_loop] == idx[0]
    loops = np.nonzero(blatt)[0]
    p = poly_of_loop[loops]
    naechste = np.where(loops - ls[p] == lt[p] - 1, ls[p], loops + 1)
    a, b = lv[loops], lv[naechste]
    lab = np.arange(nv)
    for _ in range(200):
        m = np.minimum(lab[a], lab[b])
        alt = lab.copy()
        np.minimum.at(lab, a, m)
        np.minimum.at(lab, b, m)
        lab = lab[lab]
        if np.array_equal(lab, alt):
            break
    ecken = np.unique(lv[loops])
    co = np.empty(nv * 3); me.vertices.foreach_get('co', co); co = co.reshape(-1, 3)
    grp = lab[ecken]
    _, inv, anz = np.unique(grp, return_inverse=True, return_counts=True)
    mitte = np.stack([np.bincount(inv, co[ecken, k]) / anz for k in range(3)], 1)
    co[ecken] = mitte[inv] + (co[ecken] - mitte[inv]) * faktor
    me.vertices.foreach_set('co', co.ravel())
    me.update()
    log('BLAETTER', material, '%d Blätter um Faktor %.2f vergrößert' % (len(anz), faktor))


def wachsen(name):
    """Quelle ohne Scan: baum_wachsen.py erzeugt die Blender-Datei aus den Parametern der Quelle ("wachsen")."""
    q = A['quellen'][name]
    p = dict(q['wachsen'])
    p['_modelle'] = V['modelle']
    ziel = modelle_pfad(q['datei'])
    datei = arbeit('wachsen', name + '.json')
    os.makedirs(os.path.dirname(datei), exist_ok=True)
    json.dump(p, open(datei, 'w', encoding='utf-8'), indent=1)
    r = subprocess.run([BLENDER, '--background', '--factory-startup', '--python', os.path.join(HIER, 'baum_wachsen.py'), '--',
                        datei, ziel], capture_output=True, text=True, encoding='utf-8', errors='replace')
    for z in (r.stdout + '\n' + r.stderr).splitlines():
        if z.startswith(('BAUM', 'Traceback', '  File', 'Error')) or 'Error:' in z:
            print('   ', z)
    if r.returncode != 0 or 'Traceback' in r.stderr + r.stdout:
        raise SystemExit('Wachsen fehlgeschlagen: ' + name)


def render(bilder):
    for bild in bilder:
        cfg = bild_cfg(bild)
        q = A['quellen'][cfg['quelle']]
        if q.get('wachsen') and not os.path.exists(modelle_pfad(q['datei'])):
            wachsen(cfg['quelle'])
        if cfg.get('blattsatz') and not os.path.exists(arbeit('blattsaetze', cfg['blattsatz'], 'farbe.png')):
            blattsatz(cfg['blattsatz'])
        r = subprocess.run([BLENDER, '--background', '--factory-startup', '--python', os.path.abspath(__file__), '--',
                            'render', bild], capture_output=True, text=True, encoding='utf-8', errors='replace')
        for z in (r.stdout + '\n' + r.stderr).splitlines():
            if z.startswith(('BAUM', 'Traceback', '  File', 'Error', 'ERROR')) or 'Error:' in z:
                print('   ', z)
        if r.returncode != 0 or 'Traceback' in r.stderr + r.stdout:
            raise SystemExit('Render fehlgeschlagen: ' + bild)


# ------------------------------------------------------------------------------------------------
# Textur: Bereiche des Grundtyps aus den Bildern, Farbe, Alpha hart
# ------------------------------------------------------------------------------------------------
def tcfg(stamm):
    """Auftrag einer Textur in der aktuellen Farbvariante: Sommerbäume nehmen den Sommerton, Herbstbäume reihum die Herbsttöne
    der Variante (Index "ton"); die Jahreszeit je Textur steht im Auftrag (abgeleitet mit dem Befehl "jahreszeit")."""
    t = dict(A['texturen'][stamm])
    t['gt'] = A['grundtypen'][t['grundtyp']]
    toene = A['varianten'][VARIANTE][t.get('jahreszeit', 'sommer')]
    t['ton'] = toene[int(t.get('ton', 0)) % len(toene)]
    t['bild'] = t['basis'] + '_' + t['ton']
    if A['toene'].get(t['ton']):
        t.pop('farbe', None)          # echte Herbstblätter: keine Farbpalette
    return t


def bereich_px(rect, n):
    u0, v0, u1, v1 = rect
    return int(round(u0 * n)), int(round((1 - v1) * n)), int(round(u1 * n)), int(round((1 - v0) * n))


def kegel(t, gt):
    """Stauchung der Krone über der Höhe t (0..1): ab 'kegel_ab' läuft sie linear auf 'kegel_spitze' zu (Nadelbäume: die
    Scans haben walzenförmige Kronen, aus der Spielsicht liest sich ein Kegel als Nadelbaum). Ohne 'kegel_ab' keine."""
    if 'kegel_ab' not in gt:
        return 1.0
    ab, spitze = float(gt['kegel_ab']), float(gt.get('kegel_spitze', 0.06))
    if t <= ab:
        return 1.0
    return max(spitze, 1 - (1 - spitze) * (t - ab) / (1 - ab))


def _kegel_stauchen(teil, gt):
    """Seitenbild zeilenweise zur Mitte stauchen (Kegel); die Zweige bleiben Zweige."""
    import numpy as np
    h, w = teil.shape[:2]
    xs = np.arange(w) + 0.5 - w / 2
    for y in range(h):
        f = kegel(1 - (y + 0.5) / h, gt)
        if f < 1.0:
            quelle = xs / f + w / 2 - 0.5
            for c in range(teil.shape[2]):
                teil[y, :, c] = np.interp(quelle, np.arange(w), teil[y, :, c], left=0, right=0)
    return teil


def _rinde(datei, w, h):
    """Rindenstreifen w x h aus der Rindentextur des Scans, senkrecht gekachelt."""
    import numpy as np
    from PIL import Image
    rinde = Image.open(datei).convert('RGB')
    breit = rinde.resize((w, max(1, round(w * rinde.size[1] / rinde.size[0]))), Image.LANCZOS)
    s = np.asarray(breit, np.float32) / 255
    return np.concatenate([s] * (h // s.shape[0] + 1), 0)[:h]


def textur(stamm):
    import numpy as np
    from PIL import Image
    from build_texture_tiers import alpha_weich
    t = tcfg(stamm)
    gt = t['gt']
    info = json.load(open(arbeit('bilder', t['bild'], 'bilder.json')))
    n = max(V['stufen'].values())
    atlas = np.zeros((n, n, 4), np.float32)
    nz = np.full((n, n), -1.0, np.float32)            # Höhenkomponente der Normalen (Schnee), -1 = keine
    rinde = np.zeros((n, n), bool)
    schnee_bereiche = []
    for bname, b in gt['bereiche'].items():
        x0, y0, x1, y1 = bereich_px(b['rect'], n)
        if b.get('rinde'):
            atlas[y0:y1, x0:x1, :3] = _rinde(modelle_pfad(A['quellen'][bild_cfg(t['bild'])['quelle']]['rinde']),
                                             x1 - x0, y1 - y0)
            atlas[y0:y1, x0:x1, 3] = 1
            rinde[y0:y1, x0:x1] = True
            continue
        bi = info['bilder'][b['bild']]
        a = np.asarray(Image.open(bi['datei']).convert('RGBA'), np.float32) / 255
        vm = np.concatenate([a[..., :3] * a[..., 3:], a[..., 3:]], 2)      # vormultipliziert verkleinern
        klein = np.stack([np.asarray(Image.fromarray(vm[..., c]).resize((x1 - x0, y1 - y0), Image.LANCZOS))
                          for c in range(4)], 2)
        klein = np.clip(klein, 0, 1)
        if bname.startswith('seite'):
            klein = _kegel_stauchen(klein, gt)
        atlas[y0:y1, x0:x1] = klein
        if t.get('schnee') and bi.get('normalen'):
            z = np.asarray(Image.open(bi['normalen']).convert('RGB'), np.float32)[..., 0] / 255 * 2 - 1
            z = np.asarray(Image.fromarray(z).resize((x1 - x0, y1 - y0), Image.BILINEAR))[..., None]
            if bname.startswith('seite'):
                z = _kegel_stauchen(z.copy(), gt)
            nz[y0:y1, x0:x1] = z[..., 0]
            schnee_bereiche.append(((slice(y0, y1), slice(x0, x1)), bname.startswith('seite')))
    alpha = atlas[..., 3]
    rgb = np.where(alpha[..., None] > 1e-4, atlas[..., :3] / np.maximum(alpha[..., None], 1e-4), 0)
    sicht = (alpha >= float(V.get('alpha_schwelle', 0.5))) & ~rinde
    stamm_rgb = rgb[rinde].copy()
    # Farbe: Laub je Kanal auf Mittel/Streuung des Auftrags ziehen, nur zum Anteil "anteil" (fotografischer Eindruck:
    # der Scan behält seine eigene Streuung); ohne "farbziel" bleibt die Scanfarbe (Herbstblätter)
    fz = t.get('farbziel') or (A['farben'][t['farbe']] if t.get('farbe') else None)
    if fz:
        m, s = rgb[sicht].mean(0), rgb[sicht].std(0)
        mz, sz = np.array(fz['mittel']), np.array(fz.get('streuung', s.tolist()))
        neu = (rgb - m) * np.where(s > 1e-4, sz / np.maximum(s, 1e-4), 1) + mz
        anteil = float(t.get('anteil', 1.0))
        rgb = anteil * neu + (1 - anteil) * rgb
        log('FARBE', stamm, 'Scan %s / %s -> Ziel %s / %s Anteil %.2f' % (np.round(m, 3), np.round(s, 3), mz, sz, anteil))
    # Helligkeit: die Bilder sind drehinvariant von oben beleuchtet, die Seiten liegen im Himmelslicht und wirken im Spiel
    # (Licht je Ecke dazu) zu dunkel. Mittlere Leuchtdichte des Sichtbaren mindestens auf ein festes Ziel anheben,
    # Farbverhältnisse bleiben; hellere Bilder (Herbstlaub) bleiben unverändert.
    # Herbstlaub mit eigenem, niedrigerem Ziel und begrenztem Faktor: rote Blätter haben wenig Leuchtdichte, voll angehoben
    # würden sie rosa und die Stämme weiß.
    herbst = bool(A['toene'].get(t['ton']))
    ziel = V.get('hell_ziel_herbst') if herbst else t['gt'].get('hell_ziel', V.get('hell_ziel'))
    if ziel:
        l = float((rgb[sicht] @ np.array([0.2126, 0.7152, 0.0722])).mean())
        f = min(max(1.0, float(ziel) / max(l, 1e-3)), float(V.get('hell_faktor_max', 2.0)) if herbst else 99.0)
        rgb = rgb * f
        log('HELL', stamm, 'Leuchtdichte %.3f -> %.3f' % (l, l * f))
    rgb = rgb * float(t.get('helligkeit', 1.0))
    # Kräftigere Farbe (Sättigung um die Leuchtdichte je Pixel gestreckt); die Spielsicht zeigt Bäume klein und im Kartenlicht
    s = float(t.get('saettigung', V.get('saettigung', 1.0)))
    if s != 1.0:
        l = (rgb @ np.array([0.2126, 0.7152, 0.0722]))[..., None]
        rgb = l + (rgb - l) * s
    # Zielwerte statt Faktoren (Angleichung an gemessene Mittelwerte): mittlere Leuchtdichte und mittlere HSV-Sättigung
    # des Sichtbaren; die Sättigung wird um die Leuchtdichte je Pixel gestreckt, daher in wenigen Schritten eingeregelt
    gewicht = np.array([0.2126, 0.7152, 0.0722])
    if t.get('ziel_saettigung'):
        for _ in range(4):
            c = np.clip(rgb[sicht], 0, 1)
            sat = float(((c.max(1) - c.min(1)) / np.maximum(c.max(1), 1e-4)).mean())
            l = (rgb @ gewicht)[..., None]
            rgb = l + (rgb - l) * (float(t['ziel_saettigung']) / max(sat, 1e-3))
    if t.get('ziel_leuchtdichte'):
        l = float((np.clip(rgb[sicht], 0, 1) @ gewicht).mean())
        rgb = rgb * (float(t['ziel_leuchtdichte']) / max(l, 1e-3))
    if t.get('ziel_saettigung') or t.get('ziel_leuchtdichte'):
        c = np.clip(rgb[sicht], 0, 1)
        log('ZIEL', stamm, 'Leuchtdichte %.3f Sättigung %.3f' % (float((c @ gewicht).mean()),
                                                                float(((c.max(1) - c.min(1)) / np.maximum(c.max(1), 1e-4)).mean())))
    if rinde.any():
        rz = A['farben'].get(gt.get('rinde_farbe', ''))
        if rz:
            m, s = stamm_rgb.mean(0), stamm_rgb.std(0)
            stamm_rgb = (stamm_rgb - m) * np.where(s > 1e-4, np.array(rz['streuung']) / np.maximum(s, 1e-4), 1) + rz['mittel']
        rgb[rinde] = stamm_rgb
    # Schnee auf die nach oben weisenden, hellen Zweige, in Flecken: Wertung aus Normale, Helligkeit und weichem Rauschen;
    # je Bereich deckt Schnee den Anteil 'schnee' x Faktor (Aufsichten mehr als Seiten) der sichtbaren Pixel
    if t.get('schnee'):
        menge = float(t['schnee'])
        l = rgb @ np.array([0.2126, 0.7152, 0.0722])
        rng = np.random.default_rng(len(stamm))
        grob = Image.fromarray(rng.random((n // 64, n // 64)).astype(np.float32)).resize((n, n), Image.BICUBIC)
        rauschen = np.asarray(grob, np.float32)
        m = np.zeros((n, n), np.float32)
        for sl, seite in schnee_bereiche:
            sb = sicht[sl]
            if not sb.any():
                continue
            lb = l[sl]
            wert = nz[sl] + 0.6 * (lb - lb[sb].mean()) / max(float(lb[sb].std()), 1e-3) * 0.25 + 0.5 * rauschen[sl]
            anteil = min(0.95, menge * (0.55 if seite else 0.7))
            s = float(np.quantile(wert[sb], 1 - anteil))
            x = np.clip((wert - s) / 0.08 + 0.5, 0, 1)
            m[sl] = x * x * (3 - 2 * x) * sb
        weiss = np.array(V.get('schnee_farbe', [0.9, 0.93, 0.97]))
        schatten = (0.7 + 0.2 * np.clip(nz, 0, 1) + 0.1 * np.clip(l / max(float(l[sicht].mean()), 1e-3), 0, 1.5))[..., None]
        rgb = rgb * (1 - m[..., None]) + weiss * schatten * m[..., None]
        log('SCHNEE', stamm, 'Menge %.2f Anteil %.2f' % (menge, float(m[sicht].mean())))
    # Verbrannte Fassung (_B): verkohlt -- dunkel und entsättigt, das Laub in Flecken weggebrannt
    if t.get('verbrannt'):
        l = (rgb @ np.array([0.2126, 0.7152, 0.0722]))[..., None]
        rgb = (l + (rgb - l) * 0.15) * 0.35
        rng = np.random.default_rng(7)
        loch = np.asarray(Image.fromarray(rng.random((n // 32, n // 32)).astype(np.float32)).resize((n, n), Image.BICUBIC))
        loch = loch > float(t['verbrannt'])
        sicht = sicht & loch
        log('VERBRANNT', stamm, 'Laub übrig %.2f' % float(sicht.mean()))
    deck = sicht | rinde
    rgb = _farbe_fortsetzen(np.clip(rgb, 0, 1), deck, 24)
    # Deckkraft: 'weich' (Vorgabe) behält die geglättete Kante des Renders und legt die Kontur auf die Alphatest-Schwelle
    # der Engine; bilinear gefiltert folgt der Alphatest dann der Kontur unter einem Texel statt Treppen. 'hart': 0/255.
    if V.get('alpha', 'weich') == 'weich':
        weich = np.clip(alpha - float(V.get('alpha_schwelle', 0.5)) + ALPHA_REF / 255.0, 0, 1)
        if t.get('verbrannt'):
            weich = weich * loch
        weich = np.where(rinde, 1.0, weich)
        a8 = np.clip(weich * 255 + 0.5, 0, 255).astype(np.uint8)
    else:
        a8 = np.where(deck, 255, 0).astype(np.uint8)
    rgba = np.concatenate([np.clip(rgb * 255 + 0.5, 0, 255).astype(np.uint8), a8[..., None]], 2)
    sicht = deck
    ziel = arbeit_v('texturen', stamm)
    os.makedirs(ziel, exist_ok=True)
    for stufe, groesse in V['stufen'].items():
        im = Image.fromarray(rgba, 'RGBA')
        if groesse != n:
            # Farbe und Deckkraft getrennt: Deckkraft mit Abdeckung erhalten (wie build_mips)
            rgb_k = im.convert('RGB').resize((groesse, groesse), Image.LANCZOS)
            a_k = np.asarray(im.getchannel('A').resize((groesse, groesse), Image.BOX), np.float32)
            if V.get('alpha', 'weich') == 'weich':
                a_k = Image.fromarray(alpha_weich(a_k, float((a8 >= ALPHA_REF).mean())), 'L')
            else:
                ziel_anteil = float(sicht.mean())
                schwelle = float(np.quantile(a_k, 1 - ziel_anteil)) if 0 < ziel_anteil < 1 else 128
                a_k = Image.fromarray(np.where(a_k >= max(1.0, schwelle), 255, 0).astype(np.uint8), 'L')
            im = Image.merge('RGBA', (*rgb_k.split(), a_k))
        im.save(os.path.join(ziel, '%s_%s.png' % (stamm, stufe)))
    log('TEXTUR', stamm, 'Bild', t['bild'], 'Abdeckung %.2f' % sicht.mean(), 'Stufen', V['stufen'])


# ------------------------------------------------------------------------------------------------
# Modell: Netze der Formen (Kugelkrone, Busch), Gerüst nach w3d_geruest.py
# ------------------------------------------------------------------------------------------------
class Netz:
    def __init__(self, bereiche):
        self.bereiche = bereiche
        self.ecken, self.dreiecke, self.uv = [], [], []

    def uv_in(self, b, u, v, spiegeln=False):
        u0, v0, u1, v1 = self.bereiche[b]['rect']
        if spiegeln:
            u = 1 - u
        # einen halben Texel der Größten Stufe einrücken: der Rand des Bereichs grenzt an den Nachbarn
        e = 0.5 / max(V['stufen'].values())
        return (u0 + e + (u1 - u0 - 2 * e) * u, v0 + e + (v1 - v0 - 2 * e) * v)

    def quad(self, p, uv):
        i = len(self.ecken)
        self.ecken.extend(p)
        self.uv.extend(uv)
        self.dreiecke.extend([(i, i + 1, i + 2), (i, i + 2, i + 3)])


def form_kugel(h, r, info, gt, v):
    """Laubkrone breit (Grundtyp A): senkrechte Karten mit Seitenbildern (Umriss, Stamm), eine Kuppel aus der Aufsicht der
    ganzen Krone (Mitte oben, Rand tiefer -- von schräg oben liest sich die Krone rund) und gefaltete waagrechte Schichten
    aus der Kronenscheibe (Fülle zwischen Kuppel und Karten). Fuß im Ursprung, Höhe h, Kronenhalbmesser r."""
    g = Netz(gt['bereiche'])
    karten = v.get('karten', ['seite_a', 'seite_b', 'seite_a'])
    spiegel = v.get('spiegel', [False, False, True])
    dreh = math.radians(float(v.get('drehung', 0)))
    halb = r * 1.04
    for k, bname in enumerate(karten):
        w = dreh + math.pi * k / len(karten)
        dx, dy = math.cos(w) * halb, math.sin(w) * halb
        p = [(-dx, -dy, 0.0), (dx, dy, 0.0), (dx, dy, h), (-dx, -dy, h)]
        sp = spiegel[k % len(spiegel)]
        g.quad(p, [g.uv_in(bname, 0, 0, sp), g.uv_in(bname, 1, 0, sp), g.uv_in(bname, 1, 1, sp), g.uv_in(bname, 0, 1, sp)])
    # Maßstab Scan -> Spiel; die Aufsichten sind auf die Krone ausgerichtet (Mitte relativ zum Stammfuß)
    s_xy = r / info['r_max']
    dc, ds = math.cos(dreh), math.sin(dreh)

    def aufsicht(name):
        b = info['bilder'][name]
        mx, my = b['mitte'][0] * s_xy, b['mitte'][1] * s_xy
        # das Netz wird um dreh gedreht; die Bildachsen drehen mit (UV aus den ungedrehten Koordinaten)
        return mx, my, b['halb'] * s_xy

    def drehen(x, y):
        return (dc * x - ds * y, ds * x + dc * y)

    # Kuppel (wahlweise, kuppel_ecken 0 = keine): Mitte und Ring; UV = Lage in der Aufsicht
    nring = int(v.get('kuppel_ecken', 8))
    zo, zr = h * float(v.get('kuppel_oben', 0.93)), h * float(v.get('kuppel_rand', 0.58))
    mx, my, ho = aufsicht('oben')
    rr = ho * float(v.get('kuppel_weite', 1.0))
    i0 = len(g.ecken)
    if nring:
        g.ecken.append((*drehen(mx, my), zo))
        g.uv.append(g.uv_in('oben', 0.5, 0.5))
    wd = math.radians(float(v.get('kuppel_drehung', 22.5)))
    for j in range(nring):
        w = wd + 2 * math.pi * j / nring
        x, y = math.cos(w) * rr, math.sin(w) * rr
        g.ecken.append((*drehen(mx + x, my + y), zr))
        g.uv.append(g.uv_in('oben', 0.5 + 0.5 * x / ho, 0.5 + 0.5 * y / ho))
    for j in range(nring):
        g.dreiecke.append((i0, i0 + 1 + j, i0 + 1 + (j + 1) % nring))
    # Schichten aus der Kronenscheibe: gefaltete Rauten (First oben, Spitzen hängen), je Schicht gedreht; die Raute
    # ist so groß, dass ihr Inkreis-Quadrat den Bildrahmen trägt
    for i, schicht in enumerate(v.get('schichten', [0.42, 0.62])):
        t, bname = (schicht, 'scheibe') if not isinstance(schicht, list) else schicht
        sx, sy, hs = aufsicht(bname)
        e = hs * math.sqrt(2) * float(v.get('schicht_faktor', 1.0))
        hg = e * float(v.get('haengen', 0.25))
        w = math.radians(45 * i + 20)
        c, s = math.cos(w), math.sin(w)
        z = t * h
        # Ecken der Raute (rechts, vorn, links, hinten) auf die Ecken des Bildrahmens: das Bild liegt um 45 Grad gedreht
        # genau im Quadrat der Raute
        p = []
        for (x, y, dz) in ((e, 0, -hg), (0, e, 0), (-e, 0, -hg), (0, -e, 0)):
            p.append((*drehen(sx + c * x - s * y, sy + s * x + c * y), z + dz))
        uv = [g.uv_in(bname, 1, 0), g.uv_in(bname, 1, 1), g.uv_in(bname, 0, 1), g.uv_in(bname, 0, 0)]
        j = len(g.ecken)
        g.ecken.extend(p)
        g.uv.extend(uv)
        g.dreiecke.extend([(j, j + 1, j + 3), (j + 2, j + 3, j + 1)])
    return g


def form_busch(h, r, info, gt, v):
    """Busch: gekreuzte senkrechte Karten (Vorgabe: Aufsicht der Krone als dichter Laubballen; 'busch_bilder' = andere
    Bilder reihum, etwa die Seitenbilder eines Buschscans) und eine waagrechte Karte obenauf ('busch_deckel' bei
    'busch_deckel_hoehe'); Fuß leicht im Boden."""
    g = Netz(gt['bereiche'])
    n = int(v.get('busch_karten', 3))
    bilder = v.get('busch_bilder', ['oben'])
    dreh = math.radians(float(v.get('drehung', 0)))
    tief = -0.06 * h
    for k in range(n):
        w = dreh + math.pi * k / n
        dx, dy = math.cos(w) * r, math.sin(w) * r
        sp = k % 2 == 1
        b = bilder[k % len(bilder)]
        g.quad([(-dx, -dy, tief), (dx, dy, tief), (dx, dy, h), (-dx, -dy, h)],
               [g.uv_in(b, 0, 0, sp), g.uv_in(b, 1, 0, sp), g.uv_in(b, 1, 1, sp), g.uv_in(b, 0, 1, sp)])
    z = float(v.get('busch_deckel_hoehe', 0.62)) * h
    e = r * 0.9
    d = v.get('busch_deckel', 'scheibe')
    g.quad([(-e, -e, z), (e, -e, z), (e, e, z), (-e, e, z)],
           [g.uv_in(d, 0, 0), g.uv_in(d, 1, 0), g.uv_in(d, 1, 1), g.uv_in(d, 0, 1)])
    return g


def form_kegel(h, r, info, gt, v):
    """Nadelbaum (Grundtyp C): gekreuzte senkrechte Karten mit den Seitenbildern (Umriss), gefaltete Rauten-Schichten
    übereinander (unten aus der Kronenscheibe, oben aus der Aufsicht der oberen Krone), je Schicht so groß wie die Krone
    des Scans in dieser Höhe mal Kegel, und ein Stamm als vierseitige Pyramide mit Rinde. Fuß im Ursprung."""
    g = Netz(gt['bereiche'])
    karten = v.get('karten', ['seite_a', 'seite_b'])
    spiegel = v.get('spiegel', [False, False, True])
    dreh = math.radians(float(v.get('drehung', 20)))
    halb = r * 1.04
    for k, bname in enumerate(karten):
        w = dreh + math.pi * k / len(karten)
        dx, dy = math.cos(w) * halb, math.sin(w) * halb
        sp = spiegel[k % len(spiegel)]
        g.quad([(-dx, -dy, 0.0), (dx, dy, 0.0), (dx, dy, h), (-dx, -dy, h)],
               [g.uv_in(bname, 0, 0, sp), g.uv_in(bname, 1, 0, sp), g.uv_in(bname, 1, 1, sp), g.uv_in(bname, 0, 1, sp)])
    profil = info['profil']
    n = int(v.get('schichten_zahl', 8))
    t0, t1 = float(v.get('schicht_von', 0.2)), float(v.get('schicht_bis', 0.9))
    grenze = float(v.get('schicht_grenze', 0.5))
    for i in range(n):
        t = t0 + (t1 - t0) * i / max(1, n - 1)
        bname = 'scheibe' if t < grenze else 'oben'
        rs = max(profil[min(19, int(t * 20))] * kegel(t, gt), 0.08) * r * float(v.get('schicht_faktor', 1.0))
        e = rs * math.sqrt(2)
        hg = e * float(v.get('haengen', 0.3))
        w = math.radians(45 * i + 10 * (i % 3)) + dreh
        c, s = math.cos(w), math.sin(w)
        z = t * h
        p = [(c * x - s * y, s * x + c * y, z + dz) for (x, y, dz) in ((e, 0, -hg), (0, e, 0), (-e, 0, -hg), (0, -e, 0))]
        uv = [g.uv_in(bname, 1, 0), g.uv_in(bname, 1, 1), g.uv_in(bname, 0, 1), g.uv_in(bname, 0, 0)]
        j = len(g.ecken)
        g.ecken.extend(p)
        g.uv.extend(uv)
        g.dreiecke.extend([(j, j + 1, j + 3), (j + 2, j + 3, j + 1)])
    sb = float(v.get('stamm', 0.012)) * h
    if sb > 0:
        spitze = (0.0, 0.0, h * float(v.get('stamm_hoehe', 0.9)))
        boden = [(sb * math.cos(math.radians(45 + 90 * k)), sb * math.sin(math.radians(45 + 90 * k)), -0.05)
                 for k in range(4)]
        for k in range(4):
            i = len(g.ecken)
            g.ecken.extend([boden[k], boden[(k + 1) % 4], spitze])
            g.uv.extend([g.uv_in('rinde', 0.05, 0), g.uv_in('rinde', 0.95, 0), g.uv_in('rinde', 0.5, 1)])
            g.dreiecke.append((i, i + 1, i + 2))
    return g


def form_wedel(h, r, info, gt, v):
    """Palme (Grundtyp D, Modell über W3DModelDraw): geneigter Stamm als Röhre mit Rinde, oben Wedel als gebogene Streifen
    (drei Abschnitte), die das Bild eines einzelnen Wedels tragen (wedel_a, wedel_b im Wechsel). r = Wedellänge."""
    g = Netz(gt['bereiche'])
    dreh = math.radians(float(v.get('drehung', 0)))
    neig = math.radians(float(v.get('neigung', 8)))
    hs = h * float(v.get('stamm_hoehe', 0.88))
    seiten, ringe = 6, 4
    rs = float(v.get('stamm', 0.03)) * h
    achse = lambda t: (math.cos(dreh) * math.sin(neig) * hs * t * t, math.sin(dreh) * math.sin(neig) * hs * t * t, hs * t)
    for k in range(ringe):
        t = k / (ringe - 1)
        cx, cy, cz = achse(t)
        rr = rs * (1.2 - 0.4 * t)
        for s in range(seiten + 1):
            w = 2 * math.pi * s / seiten
            g.ecken.append((cx + math.cos(w) * rr, cy + math.sin(w) * rr, cz - (0.1 if k == 0 else 0)))
            g.uv.append(g.uv_in('rinde', s / seiten, t))
    for k in range(ringe - 1):
        for s in range(seiten):
            a = k * (seiten + 1) + s
            g.dreiecke.extend([(a, a + 1, a + seiten + 2), (a, a + seiten + 2, a + seiten + 1)])
    kx, ky, kz = achse(1.0)
    n = int(v.get('wedel', 8))
    bilder = [b for b in gt['bereiche'] if b.startswith('wedel')]
    for i in range(n):
        b = bilder[i % len(bilder)]
        bi = info['bilder'][b]
        breite = r * bi['breite'] / bi['hoehe']
        w = dreh + 2 * math.pi * (i + 0.5 * (i % 2)) / n
        aus = (math.cos(w), math.sin(w))
        quer = (-math.sin(w), math.cos(w))
        hebung = float(v.get('heben', 0.35)) + 0.25 * ((i * 7) % 3 - 1)
        haengen = float(v.get('haengen', 0.9))
        j0 = len(g.ecken)
        for k in range(4):
            s = k / 3
            d = r * s * (1 - 0.15 * s)
            z = kz + r * (hebung * s - haengen * s * s)
            for seite in (-0.5, 0.5):
                g.ecken.append((kx + aus[0] * d + quer[0] * breite * seite, ky + aus[1] * d + quer[1] * breite * seite, z))
                g.uv.append(g.uv_in(b, 0.5 + seite, s))
        for k in range(3):
            a = j0 + 2 * k
            g.dreiecke.extend([(a, a + 1, a + 3), (a, a + 3, a + 2)])
    return g


def form_stumpf(h, r, info, gt, v):
    """Baumstumpf: vierseitiger Kegelstumpf aus Rinde mit waagrechter Schnittfläche (dort sitzt der Feuerpunkt)."""
    g = Netz(gt['bereiche'])
    oben = r * 0.8
    ring = [(math.cos(math.radians(45 + 90 * k)), math.sin(math.radians(45 + 90 * k))) for k in range(4)]
    for k in range(4):
        (x0, y0), (x1, y1) = ring[k], ring[(k + 1) % 4]
        g.quad([(x0 * r, y0 * r, -0.1), (x1 * r, y1 * r, -0.1), (x1 * oben, y1 * oben, h), (x0 * oben, y0 * oben, h)],
               [g.uv_in('rinde', 0, 0), g.uv_in('rinde', 1, 0), g.uv_in('rinde', 1, 0.3), g.uv_in('rinde', 0, 0.3)])
    g.quad([(x * oben, y * oben, h) for x, y in ring],
           [g.uv_in('rinde', 0.2, 0.6), g.uv_in('rinde', 0.8, 0.6), g.uv_in('rinde', 0.8, 0.9), g.uv_in('rinde', 0.2, 0.9)])
    return g


FORMEN = {'kugel': form_kugel, 'busch': form_busch, 'kegel': form_kegel, 'stumpf': form_stumpf, 'wedel': form_wedel}


def normalen(ecken, h, aussen):
    """Weiche, drehinvariante Beleuchtung je Ecke: nach oben und etwas nach außen (Kugel um die Kronenmitte)."""
    out = []
    for x, y, z in ecken:
        l = math.hypot(x, y)
        n = (0.0, 0.0, 1.0) if l < 1e-3 else (x / l * aussen, y / l * aussen, 1.0)
        b = math.sqrt(sum(c * c for c in n))
        out.append(tuple(c / b for c in n))
    return out


def modell(stamm):
    import w3d_geruest
    t = tcfg(stamm)
    info = json.load(open(arbeit('bilder', t['bild'], 'bilder.json')))
    massstab = float(V.get('massstab', 6.5))
    for name, m in t['modelle'].items():
        v = dict(t['gt'].get('form_vorgaben', {}))
        v.update(t.get('form', {}))
        v.update(m.get('form', {}))
        form = m.get('form_art', t['gt']['form'])
        h = float(m['hoehe_m']) * massstab
        # Kronenbreite aus den Proportionen des Scans, mal Faktor des Grundtyps und des Modells (Scans haben oft sehr
        # ausladende Kronen; im Wald der Karte stünden sie sonst als geschlossenes Dach)
        r = h * info['r_max'] / info['hoehe'] * float(t['gt'].get('breite', 1.0)) * float(m.get('breite', 1.0))
        # junge Scans sind sehr breit; der Grundtyp begrenzt das Verhältnis Halbmesser zu Höhe
        r = min(r, h * float(t['gt'].get('breite_max', 9.0)) * float(m.get('breite', 1.0)))
        if form == 'wedel':
            r = float(m.get('wedel_m', t['gt'].get('wedel_m', 3.5))) * massstab
        if form == 'stumpf' or (form == 'busch' and 'breite_m' in m):
            r = float(m.get('breite_m', m['hoehe_m'])) * massstab * 0.5
        g = FORMEN[form](h, r, info, t['gt'], v)

        def ohne_minus_null(werte):
            # -0.0 + 0.0 = 0.0: die Prüfung baut aus der Datei neu und rechnet dabei Knochenversatz + Ecke
            return [tuple(float(c) + 0.0 for c in p) for p in werte]
        ecken = ohne_minus_null(g.ecken)
        geo = {'ecken': ecken, 'normalen': ohne_minus_null(normalen(ecken, h, float(v.get('normale_aussen', 0.45)))),
               'dreiecke': g.dreiecke, 'uv': ohne_minus_null(g.uv)}
        ziel = arbeit_v('export', name.upper() + '.w3d')
        os.makedirs(os.path.dirname(ziel), exist_ok=True)
        open(ziel, 'wb').write(w3d_geruest.baum(name, geo, textur_datei(stamm) + '.tga'))
        log('MODELL %s %s %d Ecken %d Dreiecke Höhe %.1f Halbmesser %.1f' % (name, form, len(g.ecken), len(g.dreiecke), h, r))


def paket(stamm):
    """Einträge je Stufe: Modelle und die eigene Textur (SD DXT5, HD/UHD BC7, Mip-Kette mit erhaltener Abdeckung)."""
    import numpy as np
    from PIL import Image
    from build_texture_tiers import build_mips, encode_bc7, encode_dds
    t = tcfg(stamm)
    for stufe in STUFEN:
        eintr = [['Art\\W3D\\%s.W3D' % name, arbeit_v('export', name.upper() + '.w3d')] for name in t['modelle']]
        rgba = np.asarray(Image.open(arbeit_v('texturen', stamm, '%s_%s.png' % (stamm, stufe))).convert('RGBA')).copy()
        stufen_mips = build_mips(rgba, 'DXT5', 'weich' if V.get('alpha', 'weich') == 'weich' else 'binary')
        fmt = V.get('format', {}).get(stufe, 'DXT5')
        if fmt == 'BC7':
            daten = encode_bc7(stufen_mips, {'texconv': V['texconv'], 'bc7_dir': arbeit('bc7')})
        else:
            daten = encode_dds(stufen_mips, 'DXT5')
        dds = arbeit_v('texturen', stamm, '%s_%s.dds' % (textur_datei(stamm).lower(), stufe))
        open(dds, 'wb').write(daten)
        eintr.append(['Art\\Textures\\%s.dds' % textur_datei(stamm).lower(), dds])
        json.dump(eintr, open(arbeit_v('texturen', stamm, 'paket_%s.json' % stufe), 'w'), indent=1)
        log('PAKET', stamm, stufe, fmt, rgba.shape[1], len(daten), 'Bytes')


def pruefen(staemme):
    import w3d_geruest
    ok = True
    for stamm in staemme:
        t = tcfg(stamm)
        for name in t['modelle']:
            daten = open(arbeit_v('export', name.upper() + '.w3d'), 'rb').read()
            fehler = w3d_geruest.soll(daten, name, 'baum', textur_datei(stamm) + '.tga')
            g = w3d_geruest.geometrie(daten)[0]
            budget = int(t['gt'].get('max_ecken', MAX_ECKEN))
            if len(g['ecken']) > budget:
                fehler.append('%d Ecken > Budget %d' % (len(g['ecken']), budget))
            f = w3d_geruest.feuerpunkt(g['ecken'], g['dreiecke'])
            hoehe = max(p[2] for p in g['ecken'])
            print('== %-14s %-14s %s  %2d Ecken %2d Dreiecke  AFLAME01 bei %.0f %% der Höhe %.1f' % (
                name, stamm, 'bestanden' if not fehler else 'NICHT bestanden', len(g['ecken']), len(g['dreiecke']),
                100 * f[2] / hoehe, hoehe))
            for x in fehler:
                print('   ', x)
            ok &= not fehler
    return ok


def packen(ziel, staemme, stufe):
    from build_texture_tiers import write_big
    eintr = []
    for stamm in staemme:
        for archivname, datei in json.load(open(arbeit_v('texturen', stamm, 'paket_%s.json' % stufe))):
            eintr.append((archivname, open(datei, 'rb').read()))
    os.makedirs(os.path.dirname(os.path.abspath(ziel)), exist_ok=True)
    write_big(ziel, eintr)
    print('geschrieben', ziel, len(eintr), 'Einträge', sum(len(d) for _, d in eintr), 'Bytes')


def vorschau(staemme):
    """Übersicht: je Textur die HD-Fassung auf Grau (arbeit/vorschau.png)."""
    from PIL import Image
    k = 384
    bild = Image.new('RGB', (k * min(4, len(staemme)), k * ((len(staemme) + 3) // 4)), (40, 40, 40))
    for i, stamm in enumerate(staemme):
        im = Image.open(arbeit_v('texturen', stamm, '%s_hd.png' % stamm)).convert('RGBA').resize((k, k), Image.LANCZOS)
        grund = Image.new('RGBA', (k, k), (128, 128, 128, 255))
        grund.alpha_composite(im)
        bild.paste(grund.convert('RGB'), ((i % 4) * k, (i // 4) * k))
    bild.save(arbeit_v('vorschau.png'))
    print('Vorschau', arbeit_v('vorschau.png'))


def ansicht(staemme, ziel):
    """Ansichtsbild der fertigen Modelle (ohne Spiel): alle Modelle der Texturen in einer Reihe auf grünem Boden, Sonne
    schräg, Kamera schräg von oben wie die Spielsicht und näher (<ziel>_weit.png, <ziel>_nah.png). Zeigt Netz, UV, Alphatest
    und Farbe; das Licht je Ecke und der Nebel der Engine fehlen."""
    import w3d_geruest
    spec = []
    for stamm in staemme:
        t = tcfg(stamm)
        for name in t['modelle']:
            g = w3d_geruest.geometrie(open(arbeit_v('export', name.upper() + '.w3d'), 'rb').read())[0]
            spec.append({'name': name, 'ecken': g['ecken'], 'dreiecke': g['dreiecke'], 'uv': g['uv'],
                         'textur': arbeit_v('texturen', stamm, '%s_hd.png' % stamm)})
    datei = os.path.abspath(ziel) + '.json'
    os.makedirs(os.path.dirname(datei), exist_ok=True)
    json.dump({'modelle': spec, 'ziel': os.path.abspath(ziel)}, open(datei, 'w'))
    r = subprocess.run([BLENDER, '--background', '--factory-startup', '--python', os.path.abspath(__file__), '--',
                        'ansicht', datei], capture_output=True, text=True, encoding='utf-8', errors='replace')
    if r.returncode != 0 or 'Traceback' in r.stderr + r.stdout:
        print(r.stdout[-3000:], r.stderr[-3000:])
        raise SystemExit('Ansicht fehlgeschlagen')
    print('Ansicht', ziel + '_weit.png', ziel + '_nah.png')


def ansicht_blender(datei):
    import bpy
    from mathutils import Vector
    spec = json.load(open(datei))
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'
    sc.cycles.samples = 32
    sc.cycles.use_denoising = False
    sc.view_settings.view_transform = 'Standard'
    welt = bpy.data.worlds.new('himmel')
    welt.use_nodes = True
    welt.node_tree.nodes['Background'].inputs['Color'].default_value = (0.6, 0.7, 0.9, 1)
    welt.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.8
    sc.world = welt
    sonne = bpy.data.objects.new('sonne', bpy.data.lights.new('sonne', 'SUN'))
    sonne.data.energy = 3.0
    sonne.rotation_euler = (math.radians(40), 0, math.radians(30))
    sc.collection.objects.link(sonne)
    n = len(spec['modelle'])
    abstand = max(26.0, 2.2 * max(max(math.hypot(p[0], p[1]) for p in m['ecken']) for m in spec['modelle']))
    for i, m in enumerate(spec['modelle']):
        me = bpy.data.meshes.new(m['name'])
        me.from_pydata([tuple(p) for p in m['ecken']], [], [tuple(d) for d in m['dreiecke']])
        uv = me.uv_layers.new()
        for poly in me.polygons:
            for li in poly.loop_indices:
                uv.data[li].uv = m['uv'][me.loops[li].vertex_index]
        mat = bpy.data.materials.new(m['name'])
        mat.use_nodes = True
        nt = mat.node_tree
        bsdf = nt.nodes['Principled BSDF']
        bild = nt.nodes.new('ShaderNodeTexImage')
        bild.image = bpy.data.images.load(m['textur'])
        nt.links.new(bild.outputs['Color'], bsdf.inputs['Base Color'])
        stufe = nt.nodes.new('ShaderNodeMath')
        stufe.operation = 'GREATER_THAN'
        stufe.inputs[1].default_value = ALPHA_REF / 255.0
        nt.links.new(bild.outputs['Alpha'], stufe.inputs[0])
        nt.links.new(stufe.outputs[0], bsdf.inputs['Alpha'])
        bsdf.inputs['Roughness'].default_value = 0.9
        me.materials.append(mat)
        o = bpy.data.objects.new(m['name'], me)
        o.location = ((i - (n - 1) / 2) * abstand, 0, 0)
        sc.collection.objects.link(o)
    boden = bpy.data.meshes.new('boden')
    b = abstand * (n + 2)
    boden.from_pydata([(-b, -b, 0), (b, -b, 0), (b, b, 0), (-b, b, 0)], [], [(0, 1, 2, 3)])
    bm = bpy.data.materials.new('boden')
    bm.use_nodes = True
    bm.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.16, 0.2, 0.1, 1)
    boden.materials.append(bm)
    sc.collection.objects.link(bpy.data.objects.new('boden', boden))
    cam = bpy.data.objects.new('kamera', bpy.data.cameras.new('kamera'))
    sc.collection.objects.link(cam)
    sc.camera = cam
    breite = abstand * n
    hoch = max(max(p[2] for p in m['ecken']) for m in spec['modelle'])
    for name, neigung, faktor, aufl in (('weit', 50, 1.0, (1600, 600)), ('nah', 25, 0.6, (1600, 800))):
        w = math.radians(neigung)
        d = (breite + 1.5 * hoch) * 1.3 * faktor
        ziel = Vector((0, 0, hoch * (0.2 + 0.4 * (1 - faktor))))
        cam.location = ziel + Vector((0, -math.cos(w) * d, math.sin(w) * d))
        cam.rotation_euler = (ziel - cam.location).to_track_quat('-Z', 'Y').to_euler()
        cam.data.lens = 50
        sc.render.resolution_x, sc.render.resolution_y = aufl
        sc.render.filepath = spec['ziel'] + '_%s.png' % name
        bpy.ops.render.render(write_still=True)


def ablegen(ziel, staemme, stufe='hd'):
    """Paket als Ordner für ProjectData\\Inhalt (wie Data/FreieStrassen): Modelle unter Art/W3D, die eigenen Texturen nur in
    der höchsten Stufe unter Art/Textures, dazu texturpaket.txt (Kopf mit der Stufe, je Textur Pfad und SD-Größe; die Engine
    lässt in kleineren Stufen Mip-Stufen aus). Setzt 'paket' für diese Stufe voraus."""
    import shutil
    zeilen = ['TEXTURPAKET 1 ' + stufe.upper(),
              '; free trees and plants (baum_frei.py), textures in %s size, sizes are the SD tier' % stufe.upper()]
    sd = int(V['stufen']['sd'])
    n = 0
    for stamm in staemme:
        for archivname, datei in json.load(open(arbeit_v('texturen', stamm, 'paket_%s.json' % stufe))):
            pfad = os.path.join(ziel, *archivname.split('\\'))
            os.makedirs(os.path.dirname(pfad), exist_ok=True)
            shutil.copyfile(datei, pfad)
            n += 1
            if archivname.startswith('Art\\Textures\\'):
                zeilen.append('%s %d %d' % (archivname, sd, sd))
    with open(os.path.join(ziel, 'texturpaket.txt'), 'w', encoding='ascii', newline='\r\n') as f:
        f.write('\n'.join(zeilen) + '\n')
    print('abgelegt', ziel, n, 'Dateien,', len(zeilen) - 2, 'Texturen im Manifest')


def bauen(staemme, schritte):
    for stamm in staemme:
        t = tcfg(stamm)
        # 'alle': rendern nur, wenn das Bild fehlt; ausdrücklich 'render': immer neu
        fehlt = not os.path.exists(arbeit('bilder', t['bild'], 'bilder.json'))
        if (schritte == 'alle' and fehlt) or 'render' in schritte.split(','):
            render([t['bild']])
        for s, f in (('textur', textur), ('modell', modell), ('paket', paket)):
            if schritte == 'alle' or s in schritte.split(','):
                f(stamm)


if __name__ == '__main__':
    if IN_BLENDER:
        argv = sys.argv[sys.argv.index('--') + 1:]
        if argv[0] == 'render':
            render_blender(argv[1])
        elif argv[0] == 'ansicht':
            ansicht_blender(argv[1])
        sys.exit(0)
    befehl = sys.argv[1]
    rest = [a.strip() for a in sys.argv[2:] if not a.startswith('--')]
    opt = dict(a[2:].split('=', 1) if '=' in a else (a[2:], '1') for a in sys.argv[2:] if a.startswith('--'))
    alle = list(A['texturen'])
    if opt.get('variante'):
        VARIANTE = opt['variante']
    if opt.get('stufen'):
        STUFEN = [s for s in opt['stufen'].split(',') if s in V['stufen']]
    if befehl == 'jahreszeit':
        jahreszeit(rest[0], float(opt.get('schwelle', 0.5)))
    elif befehl == 'auftrag':
        vorlage = auftrag(rest[0], opt['familie'], opt.get('grundtyp', 'A'))
        if opt.get('aus'):
            json.dump(vorlage, open(opt['aus'], 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    elif befehl == 'blattsatz':
        for x in rest:
            blattsatz(x)
    elif befehl == 'render':
        render(rest)
    elif befehl == 'wachsen':
        for x in rest:
            wachsen(x)
    elif befehl == 'bauen':
        bauen(rest or alle, opt.get('schritte', 'alle'))
    elif befehl == 'pruefen':
        sys.exit(0 if pruefen(rest or alle) else 1)
    elif befehl == 'packen':
        packen(rest[0], rest[1:] or alle, opt.get('stufe', 'sd'))
    elif befehl == 'vorschau':
        vorschau(rest or alle)
    elif befehl == 'ablegen':
        ablegen(rest[0], rest[1:] or alle, opt.get('stufe', 'hd'))
    elif befehl == 'ansicht':
        ansicht(rest[1:] or alle, rest[0])
    else:
        sys.exit('unbekannter Befehl: ' + befehl)
