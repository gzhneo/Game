# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Nadelbaum aus einem CC0-Scan (Poly Haven) für den Baum-Puffer der Engine.
#
# Bäume zeichnet Zero Hour über W3DTreeBuffer (W3DTreeDraw): ein einziges Netz je Baumart, alle Bäume in einem
# Eckenpuffer (höchstens 30.000 Ecken für alle sichtbaren Bäume), Textur aus der INI (TextureName, TGA unter
# Art\Terrain) als Kachelblock im Baum-Atlas (64er-Kacheln, UV auf 0..1 geklemmt), Alphatest, beidseitig, Licht je
# Ecke aus den Normalen, Wind im Vertex-Shader nach Höhe über dem Fußpunkt, Umfallen als Matrix. Generals zeichnet
# denselben Baum als gewöhnliches W3D-Modell (W3DModelDraw, Textur aus dem Modell, verbrannte Fassung _B mit dem
# Knochen AFLAME01 für das Feuer). Der Ersatz hält das Budget des Baum-Puffers ein (60 Ecken): gefaltete, schräg
# hängende Astschichten (Rauten, von oben gesehen) um einen Stamm, dazu zwei gekreuzte senkrechte Karten für den
# Umriss -- alles Alphatest-Karten mit Bildern, die aus dem Scan gerendert werden. Das W3D-Gerüst (Namen, Kopf,
# Shader, Material, Knochen) schreibt w3d_geruest.py nach eigener Vorschrift; keine EA-Datei wird gelesen.
#
# Aufruf:
#   blender --background --factory-startup --python baum_ersetzen.py -- sprites <auftrag.json> <objekt>
#   python baum_ersetzen.py textur <auftrag.json> <objekt>          (ohne Blender: Atlas, TGA, PNG)
#   python baum_ersetzen.py modell <auftrag.json> <objekt>          (ohne Blender: alle Fassungen, W3D)
#   python baum_ersetzen.py paket <auftrag.json> <objekt>           (ohne Blender: paket*.json je Stufe)
#   blender ... --python baum_ersetzen.py -- ansicht <auftrag.json> <objekt> [--ea=<ea.w3d>]   (EA nur zum Vergleich)
#
# Texturaufteilung (UV, v nach oben; 256 x 256 = 4 x 4 Kacheln, Kennung ohne ZHTS -- der Atlas bleibt für die
# übrigen Bäume unverändert):  Seitenbild u 0..0,42 | Schichtbild oben u 0,43..0,85 v 0,58..1 | Schichtbild unten
# u 0,43..0,85 v 0,08..0,5 | Rinde u 0,86..1 (wie EA: der Kiefernstumpf PTStumpPine nutzt dieselbe TGA mit
# u 0,855..0,996 und bekommt damit weiter Rinde).
import json
import math
import os
import sys

try:
    import bpy  # noqa: F401
    IN_BLENDER = True
except ImportError:
    IN_BLENDER = False

if IN_BLENDER:
    ARGV = sys.argv[sys.argv.index('--') + 1:]
else:
    ARGV = sys.argv[1:]
SCHRITT, AUFTRAG_DATEI, NAME = ARGV[0], ARGV[1], ARGV[2]
OPT = {}
for a in ARGV[3:]:
    k, _, v = a.partition('=')
    OPT[k.lstrip('-')] = v
AUFTRAG = json.load(open(AUFTRAG_DATEI, encoding='utf-8'))
CFG = dict(AUFTRAG.get('vorgaben', {}))
CFG.update(AUFTRAG['objekte'][NAME])
CFG.update(OPT)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import pfade  # noqa: E402
ARBEIT_WURZEL = pfade.unter_werkzeuge(CFG.get('arbeit', 'cc0/arbeit'))
ARBEIT = os.path.join(ARBEIT_WURZEL, NAME)
MODELLE = pfade.unter_werkzeuge(CFG.get('modelle', 'cc0/modelle'))
N = int(CFG.get('atlas_groesse', 256))
# Texturstufen: Stufe -> (Kantenlänge der Atlas-TGA, Kachelmaßstab s der Kennung ZHTS<s>,
# Kantenlänge der DDS für Generals). Ein Baum darf im Atlas höchstens 10 x 10 Kacheln belegen
# (WorldHeightMap::countTiles): SD 640 (Kacheln 64), HD 1280 (ZHTS2, Kacheln 128), UHD 2560 (ZHTS4).
STUFEN = {k: tuple(v) for k, v in (CFG.get('stufen') or {'sd': [N, 0, N]}).items()}
# Bereiche im Atlas (u0, v0, u1, v1), v nach oben
BEREICH = {
    'seite': (0.0, 0.0, 0.42, 1.0),
    'schicht_oben': (0.43, 0.58, 0.85, 1.0),
    'schicht_unten': (0.43, 0.08, 0.85, 0.50),
    'rinde': (0.86, 0.0, 1.0, 1.0),
}
SCHICHTEN = {'schicht_oben': (0.58, 0.80), 'schicht_unten': (0.28, 0.50)}   # Scheiben des Scans (Anteil der Höhe)


def log(*a):
    print('BAUM', NAME, *a, flush=True)


def kegel(t):
    """Stauchung der Krone über der Höhe t (0..1): ab 'kegel_ab' läuft sie linear zur Spitze zu. Die Scan-Tanne hat
    eine walzenförmige Krone; von oben läse sie sich wie ein Laubbaum. Die EA-Kiefer ist ein Kegel -- so bleibt der
    Baum aus der Spielsicht als Nadelbaum erkennbar. Wirkt gleich auf Schichten und Seitenbild."""
    ab = float(CFG.get('kegel_ab', 0.4))
    spitze = float(CFG.get('kegel_spitze', 0.06))
    if t <= ab:
        return 1.0
    return max(spitze, 1 - (1 - spitze) * (t - ab) / (1 - ab))


# ------------------------------------------------------------------------------------------------
# Schritt 1 (Blender): Bilder aus dem Scan rendern, Umriss vermessen
# ------------------------------------------------------------------------------------------------
def sprites():
    import bpy
    import numpy as np
    from mathutils import Vector
    bpy.ops.wm.open_mainfile(filepath=os.path.join(MODELLE, CFG['quelle']))
    baum = bpy.data.objects[CFG['quelle_objekt']]
    for o in bpy.data.objects:
        o.hide_render = o is not baum
    for c in bpy.data.collections:
        c.hide_render = False
    os.makedirs(ARBEIT, exist_ok=True)
    # Umriss: Halbmesser über der Höhe (Ecken der Zweig-Flächen), Mitte = Stammfuß
    me = baum.data
    co = np.empty(len(me.vertices) * 3); me.vertices.foreach_get('co', co); co = co.reshape(-1, 3)
    M = np.array(baum.matrix_world); co = co @ M[:3, :3].T + M[:3, 3]
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
    log('SCAN Hoehe %.2f Mitte %.2f %.2f r_max %.2f Profil %s' % (hoehe, mitte[0], mitte[1], r_max,
                                                                  [round(p / r_max, 2) for p in profil]))
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'
    sc.cycles.samples = int(CFG.get('proben', 48))
    sc.cycles.use_denoising = False
    sc.render.film_transparent = True
    sc.view_settings.view_transform = 'Standard'
    sc.view_settings.look = 'None'
    sc.render.image_settings.file_format = 'PNG'
    sc.render.image_settings.color_mode = 'RGBA'
    # Licht: gleichmäßiger Himmel (Verdeckung zwischen den Zweigen) und Sonne senkrecht von oben -- drehinvariant, jeder
    # Baum der Karte steht anders gedreht; die Richtung des Kartenlichts gibt die Engine je Ecke dazu
    welt = bpy.data.worlds.new('himmel'); welt.use_nodes = True
    welt.node_tree.nodes['Background'].inputs['Color'].default_value = (0.85, 0.9, 1.0, 1)
    welt.node_tree.nodes['Background'].inputs['Strength'].default_value = float(CFG.get('himmel', 0.9))
    sc.world = welt
    for o in list(bpy.data.objects):
        if o.type == 'LIGHT':
            bpy.data.objects.remove(o, do_unlink=True)
    sonne = bpy.data.objects.new('sonne', bpy.data.lights.new('sonne', 'SUN'))
    sonne.data.energy = float(CFG.get('sonne', 2.5))
    sonne.data.angle = math.radians(20)
    sc.collection.objects.link(sonne)
    info = {'hoehe': hoehe, 'z0': z0, 'mitte': mitte.tolist(), 'r_max': r_max, 'profil': [p / r_max for p in profil],
            'bilder': {}}

    def kamera(name, ort, ziel, breite, hoch, clip=None):
        cam = bpy.data.objects.new(name, bpy.data.cameras.new(name))
        sc.collection.objects.link(cam)
        cam.data.type = 'ORTHO'
        cam.data.ortho_scale = max(breite, hoch)
        cam.location = ort
        cam.rotation_euler = (Vector(ziel) - Vector(ort)).to_track_quat('-Z', 'Y').to_euler()
        if clip:
            cam.data.clip_start, cam.data.clip_end = clip
        else:
            cam.data.clip_start, cam.data.clip_end = 0.01, 1000
        sc.camera = cam
        aufl = int(CFG.get('render', 1024))
        sc.render.resolution_x = max(8, int(round(aufl * breite / max(breite, hoch))))
        sc.render.resolution_y = max(8, int(round(aufl * hoch / max(breite, hoch))))
        sc.render.filepath = os.path.join(ARBEIT, 'sprite_%s.png' % name)
        bpy.ops.render.render(write_still=True)
        info['bilder'][name] = {'breite': breite, 'hoehe': hoch, 'datei': sc.render.filepath}
        log('BILD', name, sc.render.resolution_x, 'x', sc.render.resolution_y)

    # Seitenbild: Rahmen genau Stammfuß bis Spitze, Breite 2 r_max
    breite = 2 * r_max * 1.04
    zm = z0 + hoehe / 2
    kamera('seite', (mitte[0], mitte[1] - 100, zm), (mitte[0], mitte[1], zm), breite, hoehe)
    # Schichtbilder: von oben, nur eine Scheibe des Baums (Clip-Ebenen der Kamera)
    for name, (a, b) in SCHICHTEN.items():
        za, zb = z0 + a * hoehe, z0 + b * hoehe
        rr = max(p for i, p in enumerate(profil) if a <= (i + 0.5) / 20 <= b) * 1.04
        ob = z1 + 5
        kamera(name, (mitte[0], mitte[1], ob), (mitte[0], mitte[1], z0), 2 * rr, 2 * rr, clip=(ob - zb, ob - za))
        info['bilder'][name]['r'] = rr
    json.dump(info, open(os.path.join(ARBEIT, 'sprites.json'), 'w'), indent=1)


# ------------------------------------------------------------------------------------------------
# Schritt 2 (Python mit PIL): Atlas zusammensetzen, Farbe an feste Zielwerte angleichen
# ------------------------------------------------------------------------------------------------
def textur():
    global N
    for stufe, (groesse, _s, _d) in STUFEN.items():
        N = groesse
        textur_stufe('' if stufe == 'sd' else '_' + stufe)


def textur_stufe(endung):
    import numpy as np
    from PIL import Image
    info = json.load(open(os.path.join(ARBEIT, 'sprites.json')))
    atlas = np.zeros((N, N, 4), np.float32)

    def bereich_px(b):
        u0, v0, u1, v1 = BEREICH[b]
        return int(round(u0 * N)), int(round((1 - v1) * N)), int(round(u1 * N)), int(round((1 - v0) * N))

    def einsetzen(b, bild):
        x0, y0, x1, y1 = bereich_px(b)
        # vormultipliziert verkleinern (keine dunklen Säume an transparenten Rändern)
        a = np.asarray(bild.convert('RGBA'), np.float32) / 255
        vm = np.concatenate([a[..., :3] * a[..., 3:], a[..., 3:]], 2)
        klein = np.stack([np.asarray(Image.fromarray(vm[..., c]).resize((x1 - x0, y1 - y0), Image.LANCZOS))
                          for c in range(4)], 2)
        klein = np.clip(klein, 0, 1)
        atlas[y0:y1, x0:x1] = klein

    for b in ('seite', 'schicht_oben', 'schicht_unten'):
        einsetzen(b, Image.open(info['bilder'][b]['datei']))
    # Seitenbild zeilenweise zur Mitte stauchen (Kegel), die Zweige bleiben dabei Zweige
    x0, y0, x1, y1 = bereich_px('seite')
    teil = atlas[y0:y1, x0:x1].copy()
    h, w = teil.shape[:2]
    xs = np.arange(w) + 0.5 - w / 2
    for y in range(h):
        f = kegel(1 - (y + 0.5) / h)
        quelle = xs / f + w / 2 - 0.5
        for c in range(4):
            teil[y, :, c] = np.interp(quelle, np.arange(w), teil[y, :, c], left=0, right=0)
    atlas[y0:y1, x0:x1] = teil
    # Rinde: Rindentextur des Scans, senkrecht gekachelt
    rinde = Image.open(os.path.join(MODELLE, CFG['rinde'])).convert('RGB')
    x0, y0, x1, y1 = bereich_px('rinde')
    w, h = x1 - x0, y1 - y0
    rinde = rinde.resize((w * 3, int(w * 3 * rinde.size[1] / rinde.size[0])), Image.LANCZOS)
    streifen = rinde.crop((0, 0, w * 3, rinde.size[1])).resize((w, max(1, rinde.size[1] // 3)), Image.LANCZOS)
    s = np.asarray(streifen, np.float32) / 255
    reihen = np.concatenate([s] * (h // s.shape[0] + 1), 0)[:h]
    atlas[y0:y1, x0:x1, :3] = reihen
    atlas[y0:y1, x0:x1, 3] = 1
    # Farbe: vormultipliziert -> gerade; Laub und Rinde je Kanal auf feste Zielwerte (Mittel/Streuung, sRGB 0..1) aus
    # dem Auftrag ("farbziel") -- das Werkzeug liest keine EA-Datei
    alpha = atlas[..., 3]
    rgb = np.where(alpha[..., None] > 1e-4, atlas[..., :3] / np.maximum(alpha[..., None], 1e-4), 0)
    farbziel = CFG['farbziel']
    laub = np.zeros((N, N), bool)
    for b in ('seite', 'schicht_oben', 'schicht_unten'):
        x0, y0, x1, y1 = bereich_px(b)
        laub[y0:y1, x0:x1] = True
    rin = np.zeros((N, N), bool)
    x0, y0, x1, y1 = bereich_px('rinde'); rin[y0:y1, x0:x1] = True
    for maske, ziel, name, kontrast in ((laub & (alpha > 0.5), farbziel['laub'], 'Laub', float(CFG.get('kontrast', 1.0))),
                                        (rin, farbziel['rinde'], 'Rinde', 1.0)):
        m, s_ = rgb[maske].mean(0), rgb[maske].std(0)
        mz, sz = np.array(ziel['mittel']), np.array(ziel['streuung']) * kontrast
        anteil = float(CFG.get('farbe_angleichen', 1.0))
        bereich = (laub if name == 'Laub' else rin)
        neu = (rgb - m) * np.where(s_ > 1e-4, sz / np.maximum(s_, 1e-4), 1) + mz
        rgb = np.where(bereich[..., None], anteil * neu + (1 - anteil) * rgb, rgb)
        log('FARBE %s Scan %s / %s -> Ziel %s / %s' % (name, np.round(m, 3), np.round(s_, 3), np.round(mz, 3),
                                                     np.round(sz, 3)))
    rgb = np.clip(rgb, 0, 1)
    # Farbe in transparente Pixel fortsetzen (Mip-Stufen, bilineares Filtern im Atlas)
    sicht = alpha > 0.5
    for _ in range(12):
        summe = np.zeros_like(rgb); anz = np.zeros(sicht.shape, np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            summe += np.roll(np.roll(rgb * sicht[..., None], dy, 0), dx, 1)
            anz += np.roll(np.roll(sicht.astype(np.float32), dy, 0), dx, 1)
        neu = (anz > 0) & ~sicht
        rgb[neu] = summe[neu] / anz[neu][:, None]
        sicht = sicht | neu
    # Alpha hart (Alphatest): Schwelle so, dass die Abdeckung erhalten bleibt
    a8 = np.where(alpha >= float(CFG.get('alpha_schwelle', 0.45)), 255, 0).astype(np.uint8)
    rgba = np.concatenate([np.clip(rgb * 255 + 0.5, 0, 255).astype(np.uint8), a8[..., None]], 2)
    os.makedirs(os.path.join(ARBEIT, 'textur'), exist_ok=True)
    Image.fromarray(rgba, 'RGBA').save(os.path.join(ARBEIT, 'textur', CFG['textur'] + endung + '.png'))
    vorschau = Image.new('RGBA', (N, N), (255, 0, 255, 255))
    vorschau.alpha_composite(Image.fromarray(rgba, 'RGBA'))
    vorschau.convert('RGB').save(os.path.join(ARBEIT, 'textur', 'vorschau%s.png' % endung))
    # verbrannte Fassungen (Generals: PTPine01_B): verkohlt -- dunkel, entsättigt, Glut an den Spitzen weg
    for f in CFG.get('fassungen', {}).values():
        if 'textur' in f:
            l = (rgba[..., :3].astype(np.float32) @ np.array([0.3, 0.59, 0.11]))[..., None]
            d = (l + (rgba[..., :3] - l) * float(f.get('saettigung', 0.15))) * float(f.get('dunkel', 0.35))
            Image.fromarray(np.concatenate([np.clip(d, 0, 255).astype(np.uint8), a8[..., None]], 2), 'RGBA').save(
                os.path.join(ARBEIT, 'textur', f['textur'] + endung + '.png'))
    log('TEXTUR', CFG['textur'] + endung, N, 'x', N, 'Abdeckung Laub %.2f' % (a8[laub] > 0).mean())


# ------------------------------------------------------------------------------------------------
# Schritt 3 (Python): Netz bauen und mit eigenem Gerüst schreiben (w3d_geruest.py)
# ------------------------------------------------------------------------------------------------
def netz_bauen(hoehe, r_baum, mitte, info):
    """Baum mit Fuß in `mitte` (Gelände z 0), Höhe `hoehe`, Kronenhalbmesser `r_baum`: Rauten-Schichten, zwei gekreuzte
    Karten, Stamm. Liefert (ecken, flaechen, uv, art je Ecke, mitte); UV mit v nach oben."""
    profil = info['profil']

    def r_bei(t):
        i = min(19, max(0, int(t * 20)))
        return profil[i] * r_baum

    ecken, flaechen, uvs, art = [], [], [], []

    def uv_in(b, u, v):
        u0, v0, u1, v1 = BEREICH[b]
        return (u0 + (u1 - u0) * u, v0 + (v1 - v0) * v)

    def quad(p, uv, a):
        i = len(ecken)
        ecken.extend(p); uvs.extend(uv); art.extend([a] * 4)
        flaechen.append((i, i + 1, i + 2))
        flaechen.append((i, i + 2, i + 3))

    # Gekreuzte Karten (Umriss): Rahmen des Seitenbilds = 2 r_max x Höhe
    halb = r_baum * 1.04
    for k in range(int(CFG.get('karten', 2))):
        w = math.pi * k / int(CFG.get('karten', 2)) + math.radians(float(CFG.get('karten_winkel', 20)))
        dx, dy = math.cos(w) * halb, math.sin(w) * halb
        p = [(mitte[0] - dx, mitte[1] - dy, 0.0), (mitte[0] + dx, mitte[1] + dy, 0.0),
             (mitte[0] + dx, mitte[1] + dy, hoehe), (mitte[0] - dx, mitte[1] - dy, hoehe)]
        quad(p, [uv_in('seite', 0, 0), uv_in('seite', 1, 0), uv_in('seite', 1, 1), uv_in('seite', 0, 1)], 'karte')
    # Schichten: gefaltete Rauten (First oben, Seiten hängen nach außen), je Schicht um 45 Grad weiter gedreht
    n = int(CFG.get('schichten', 10))
    t0, t1 = float(CFG.get('schicht_von', 0.16)), float(CFG.get('schicht_bis', 0.93))
    for i in range(n):
        t = t0 + (t1 - t0) * i / (n - 1)
        b = 'schicht_unten' if t < 0.5 else 'schicht_oben'
        # das Schichtbild reicht bis r der Scheibe; Raute so groß, dass ihr Inkreis-Quadrat den Rahmen trägt
        r = max(r_bei(t) * kegel(t), 0.08 * r_baum) * 1.04 * float(CFG.get('schicht_faktor', 1.0))
        e = r * math.sqrt(2)                       # halbe Diagonale der Raute
        haengen = e * float(CFG.get('haengen', 0.30))
        w = math.radians(45 * i + 10 * (i % 3))
        z = t * hoehe
        c, s = math.cos(w), math.sin(w)

        def pt(x, y, dz):
            return (mitte[0] + c * x - s * y, mitte[1] + s * x + c * y, z + dz)
        # Ecken: rechts (außen, tief), vorn (First), links (außen, tief), hinten (First); UV: Ecken des Bildrahmens
        p = [pt(e, 0, -haengen), pt(0, e, 0), pt(-e, 0, -haengen), pt(0, -e, 0)]
        uv = [uv_in(b, 1, 0), uv_in(b, 1, 1), uv_in(b, 0, 1), uv_in(b, 0, 0)]
        i0 = len(ecken)
        ecken.extend(p); uvs.extend(uv); art.extend(['schicht'] * 4)
        # entlang des Firsts (vorn-hinten) falten: Dreiecke (rechts, vorn, hinten) und (links, hinten, vorn)
        flaechen.append((i0, i0 + 1, i0 + 3))
        flaechen.append((i0 + 2, i0 + 3, i0 + 1))
    # Stamm: vierseitige Pyramide, Rinde
    sb = float(CFG.get('stamm', 0.8))
    spitze = (mitte[0], mitte[1], hoehe * 0.97)
    boden = [(mitte[0] + sb * math.cos(math.radians(45 + 90 * k)), mitte[1] + sb * math.sin(math.radians(45 + 90 * k)),
              -0.05) for k in range(4)]
    for k in range(4):
        i = len(ecken)
        ecken.extend([boden[k], boden[(k + 1) % 4], spitze])
        uvs.extend([uv_in('rinde', 0.05, 0), uv_in('rinde', 0.95, 0), uv_in('rinde', 0.5, 1)])
        art.extend(['stamm'] * 3)
        flaechen.append((i, i + 1, i + 2))
    return ecken, flaechen, uvs, art, mitte


def baum_normalen(ecken, mitte):
    """Laub nach oben und etwas nach außen (weiche, drehinvariante Beleuchtung wie ein Kegel), Stamm waagrecht nach
    außen. Die Engine beleuchtet Bäume nur je Ecke."""
    aussen = float(CFG.get('normale_aussen', 0.45))
    out = []
    for p in ecken:
        dx, dy = p[0] - mitte[0], p[1] - mitte[1]
        l = math.hypot(dx, dy) or 1.0
        n = (dx / l, dy / l, 0.3) if l < 1.2 else (dx / l * aussen, dy / l * aussen, 1.0)
        b = math.sqrt(sum(c * c for c in n))
        out.append(tuple(c / b for c in n))
    return out


def modell():
    """Alle Fassungen ohne Blender: Netz aus netz_bauen (Fuß im Ursprung, Maße aus dem Auftrag: "hoehe",
    "kronen_halbmesser"), Gerüst aus w3d_geruest.baum (Namen der INIs, Knochen aus der Geometrie)."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import w3d_geruest
    info = json.load(open(os.path.join(ARBEIT, 'sprites.json')))
    paket = []
    fassungen = [(CFG['modell'], CFG['textur'])] + [(f['modell'], f.get('textur', CFG['textur']))
                                                    for f in CFG.get('fassungen', {}).values()]
    for modellname, textur_name in fassungen:
        ecken, flaechen, uvs, _art, mitte = netz_bauen(float(CFG['hoehe']), float(CFG['kronen_halbmesser']), (0.0, 0.0),
                                                       info)
        # UV unverändert (v nach oben) wie bisher im Export des Blender-Add-ons
        g = {'ecken': ecken, 'normalen': baum_normalen(ecken, mitte), 'dreiecke': flaechen, 'uv': uvs}
        ziel = os.path.join(ARBEIT, 'export', modellname.upper() + '.w3d')
        os.makedirs(os.path.dirname(ziel), exist_ok=True)
        open(ziel, 'wb').write(w3d_geruest.baum(modellname, g, textur_name + '.tga'))
        log('EXPORT %s %d Ecken %d Dreiecke Textur %s' % (ziel, len(ecken), len(flaechen), textur_name))
        paket.append(['Art\\W3D\\%s.W3D' % modellname, ziel])
    # Texturen: Generals liest die Textur aus dem Modell (DDS), Zero Hour den Atlas-Kachelblock aus Art\Terrain (TGA)
    json.dump(paket, open(os.path.join(ARBEIT, 'paket_modelle.json'), 'w'), indent=1)


def paket_schreiben():
    """Einträge fürs Testarchiv je Stufe (natur_bauen.py packen --stufe=..): Modelle, DDS je Textur (Generals liest
    die Textur aus dem Modell, Kantenlänge eine Zweierpotenz), TGA mit Kennung ZHTS<s> für den Zero-Hour-Atlas."""
    import numpy as np
    from PIL import Image
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    from build_texture_tiers import build_mips, encode_dds, encode_tga
    texturen = {CFG['textur']} | {f.get('textur', CFG['textur']) for f in CFG.get('fassungen', {}).values()}
    for stufe, (groesse, kachel, dds_groesse) in STUFEN.items():
        endung = '' if stufe == 'sd' else '_' + stufe
        paket = json.load(open(os.path.join(ARBEIT, 'paket_modelle.json')))
        for t in sorted(texturen):
            bild = Image.open(os.path.join(ARBEIT, 'textur', t + endung + '.png')).convert('RGBA')
            if bild.size[0] != dds_groesse:
                rgb = bild.convert('RGB').resize((dds_groesse, dds_groesse), Image.LANCZOS)
                a = bild.getchannel('A').resize((dds_groesse, dds_groesse), Image.BILINEAR).point(lambda v: 255 if v >= 128 else 0)
                bild = Image.merge('RGBA', (*rgb.split(), a))
            dds = os.path.join(ARBEIT, 'textur', t.lower() + endung + '.dds')
            open(dds, 'wb').write(encode_dds(build_mips(np.asarray(bild).copy(), 'DXT5', 'binary'), 'DXT5'))
            paket.append([r'Art\Textures\%s.dds' % t.lower(), dds])
        for tga_name, t in CFG.get('atlas_tga', {}).items():
            rgba = np.asarray(Image.open(os.path.join(ARBEIT, 'textur', t + endung + '.png')).convert('RGBA')).copy()
            tga = os.path.join(ARBEIT, 'textur', os.path.splitext(tga_name)[0] + endung + '.tga')
            open(tga, 'wb').write(encode_tga(rgba, 'binary', int(kachel)))
            paket.append([r'Art\Terrain\%s' % tga_name, tga])
        json.dump(paket, open(os.path.join(ARBEIT, 'paket%s.json' % endung), 'w'), indent=1)
        log('PAKET', stufe, [p[0] for p in paket])


def ansicht():
    """Neu (und mit --ea=<datei> zum Vergleich das EA-Modell) je für sich (gleiche Kamera), Cycles mit Alphatest,
    Blick wie im Spiel (52 Grad) und von der Seite -> ansicht/<ea|neu>_<rts|seite>.png. Erzeugt nur Bilder."""
    import addon_utils
    import bpy
    from mathutils import Vector
    addon_utils.enable('io_mesh_w3d', default_set=True, persistent=False)
    os.makedirs(os.path.join(ARBEIT, 'ansicht'), exist_ok=True)
    laeufe = [('neu', os.path.join(ARBEIT, 'export', CFG['modell'].upper() + '.w3d'), CFG['textur'])]
    if CFG.get('ea'):
        laeufe.insert(0, ('ea', CFG['ea'], None))
    for fassung, datei, bildname in laeufe:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        addon_utils.enable('io_mesh_w3d', default_set=True, persistent=False)
        bpy.ops.import_mesh.westwood_w3d(filepath=datei)
        bild = bpy.data.images.load(os.path.join(ARBEIT, 'textur', bildname + '.png')) if bildname else None
        for o in bpy.data.objects:
            if o.type == 'MESH':
                for m in o.data.materials:
                    for nd in m.node_tree.nodes:
                        if nd.type == 'TEX_IMAGE' and bild:
                            nd.image = bild
        bpy.ops.mesh.primitive_plane_add(size=400, location=(0, 0, 0))
        boden = bpy.context.active_object
        bm = bpy.data.materials.new('boden'); bm.use_nodes = True
        bm.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.18, 0.2, 0.08, 1)
        boden.data.materials.append(bm)
        sc = bpy.context.scene
        sc.render.engine = 'CYCLES'
        sc.cycles.samples = 32
        sc.render.resolution_x, sc.render.resolution_y = 700, 800
        welt = bpy.data.worlds.new('w'); welt.use_nodes = True
        welt.node_tree.nodes['Background'].inputs['Color'].default_value = (0.55, 0.6, 0.7, 1)
        sc.world = welt
        sonne = bpy.data.objects.new('s', bpy.data.lights.new('s', 'SUN'))
        sonne.data.energy = 3
        sonne.rotation_euler = (math.radians(40), 0, math.radians(30))
        sc.collection.objects.link(sonne)
        for name, el, abst in (('rts', 52, 150), ('seite', 5, 150)):
            cam = bpy.data.objects.new(name, bpy.data.cameras.new(name))
            sc.collection.objects.link(cam)
            cam.data.type = 'ORTHO'
            cam.data.ortho_scale = 60
            ziel = Vector((0, 0, 22))
            cam.location = ziel + Vector((0, -abst * math.cos(math.radians(el)), abst * math.sin(math.radians(el))))
            cam.rotation_euler = (ziel - cam.location).to_track_quat('-Z', 'Y').to_euler()
            sc.camera = cam
            sc.render.filepath = os.path.join(ARBEIT, 'ansicht', '%s_%s.png' % (fassung, name))
            bpy.ops.render.render(write_still=True)
            log('BILD', sc.render.filepath)


if __name__ == '__main__':
    {'sprites': sprites, 'textur': textur, 'modell': modell, 'paket': paket_schreiben, 'ansicht': ansicht}[SCHRITT]()
