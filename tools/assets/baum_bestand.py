#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour (Electronic Arts).
# Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Bestand der Bäume, Büsche und Pflanzen: Messwerkzeug, liest EA-Archive nur lesend und
erzeugt nichts fürs Paket.

Je Spiel (Zero Hour, Generals) werden die Objekt-INIs gelesen (Object und ObjectReskin samt Erbe), alle Objekte mit
KindOf/EditorSorting SHRUBBERY oder einem Modell PT* ausgewählt, ihre Modelle (Dreiecke, Ecken, Texturen, Alphatest,
Knochen AFLAME*) und Texturen (Größe, Format) bestimmt und die Instanzen über alle Karten der Kartenarchive gezählt.

  python tools/assets/baum_bestand.py                 Markdown-Tabellen (Objekte, Arten, Texturen) auf stdout
  python tools/assets/baum_bestand.py --json <datei>  zusätzlich alle Rohdaten als JSON
  python tools/assets/baum_bestand.py --karten        dazu Bauminstanzen je Karte
"""
import argparse
import collections
import json
import os
import re
import struct
import sys
import zlib

HIER = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HIER, '..', 'render'))
from map_props import parse_chunks, unpack_map  # noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import pfade  # noqa: E402
ZH = pfade.assets('zh')
GEN = pfade.assets('generals')
SPIELE = {
    # Reihenfolge je Liste: vorderes Archiv gewinnt (Zero Hour vor den Generals-Grunddaten)
    'ZH': {
        'ini': [ZH + r'\INIZH.big'],
        'w3d': [ZH + r'\W3DZH.big', ZH + r'\ZH_Generals\W3D.big'],
        'tex': [ZH + r'\TexturesZH.big', ZH + r'\ZH_Generals\Textures.big'],
        'ter': [ZH + r'\TerrainZH.big', ZH + r'\ZH_Generals\Terrain.big'],
        'maps': [ZH + r'\MapsZH.big'],
    },
    'Gen': {
        'ini': [GEN + r'\INI.big'],
        'w3d': [GEN + r'\W3D.big'],
        'tex': [GEN + r'\Textures.big'],
        'ter': [GEN + r'\Terrain.big'],
        'maps': [GEN + r'\maps.big'],
    },
}

ARTEN = [  # (Muster im Objekt- oder Modellnamen, Art) -- erste Übereinstimmung gilt
    (r'stump', 'Stumpf'),
    (r'palm', 'Palme'),
    (r'blossom|cherry', 'Blütenbaum'),
    (r'bamboo', 'Bambus'),
    (r'vine', 'Kletterpflanze'),
    (r'bush|shrub|hedge', 'Busch'),
    (r'grass|fern|flower', 'Gras/Farn'),
    (r'pine|fir|spruce|cypress|arborvitae|cedar', 'Nadelbaum'),
    (r'tree|oak|maple|dogwo|birch|willow|bonsai|orange|elm', 'Laubbaum'),
]


# --- Archive ----------------------------------------------------------------------------------------

class Archiv:
    def __init__(self, pfade):
        self.eintraege = {}
        for p in pfade:
            if not os.path.exists(p):
                continue
            with open(p, 'rb') as f:
                if f.read(4) != b'BIGF':
                    continue
                f.read(4)
                n, = struct.unpack('>I', f.read(4))
                f.read(4)
                for _ in range(n):
                    off, gr = struct.unpack('>II', f.read(8))
                    nm = b''
                    while True:
                        c = f.read(1)
                        if c in (b'\0', b''):
                            break
                        nm += c
                    schl = nm.decode('latin-1').lower().replace('/', '\\')
                    self.eintraege.setdefault(schl, (p, off, gr, nm.decode('latin-1')))

    def lesen(self, schl):
        e = self.eintraege.get(schl.lower())
        if not e:
            return None
        with open(e[0], 'rb') as f:
            f.seek(e[1])
            return f.read(e[2])

    def namen(self, praefix):
        praefix = praefix.lower()
        return [k for k in self.eintraege if k.startswith(praefix)]


# --- INI ----------------------------------------------------------------------------------------------

KOPF = re.compile(r'^(Object|ObjectReskin)\s+(\S+)(?:\s+(\S+))?', re.I)


def ini_bloecke(text):
    bloecke, akt = [], None
    for zeile in text.splitlines():
        roh = re.split(r';|//', zeile, maxsplit=1)[0].rstrip()
        m = KOPF.match(roh)
        if m:
            akt = {'name': m.group(2), 'basis': m.group(3) if m.group(1).lower() == 'objectreskin' else None, 'zeilen': []}
            bloecke.append(akt)
        elif akt is not None and roh.strip():
            akt['zeilen'].append(roh.strip())
    return bloecke


def block_auswerten(b):
    d = {'kindof': set(), 'sorting': None, 'draws': [], 'modelle': [], 'treetex': None, 'verhalten': set(),
         'partikelknochen': set(), 'stumpf': None, 'geometrie': {}, 'topple': None, 'schatten': None}
    for z in b['zeilen']:
        teil = [t.strip() for t in z.split('=', 1)]
        schl = teil[0].lower()
        wert = teil[1] if len(teil) > 1 else ''
        if schl == 'kindof':
            d['kindof'] |= set(w.upper() for w in wert.split() if not w.startswith(('+', '-')))
            d['kindof'] |= set(w[1:].upper() for w in wert.split() if w.startswith('+'))
        elif schl == 'editorsorting':
            d['sorting'] = wert.split()[0].upper() if wert else None
        elif schl == 'draw':
            d['draws'].append(wert.split()[0])
        elif schl in ('model', 'modelname'):
            m = wert.split()[0] if wert else ''
            if m and m.lower() != 'none' and m not in d['modelle']:
                d['modelle'].append(m)
        elif schl == 'texturename':
            d['treetex'] = wert.split()[0]
        elif schl in ('behavior', 'clientupdate', 'body'):
            d['verhalten'].add(wert.split()[0])
        elif schl == 'particlesysbone':
            d['partikelknochen'].add(wert.split()[0].upper())
        elif schl == 'stumpname':
            d['stumpf'] = wert.split()[0]
        elif schl.startswith('geometry'):
            d['geometrie'][schl] = wert.split()[0] if wert else ''
        elif schl == 'dotopple':
            d['topple'] = wert.split()[0].lower() == 'yes'
        elif schl == 'doshadow':
            d['schatten'] = wert.split()[0].lower() == 'yes'
    return d


def objekte_lesen(ini):
    roh = {}
    for schl in ini.namen('data\\ini\\object\\'):
        if not schl.endswith('.ini'):
            continue
        for b in ini_bloecke(ini.lesen(schl).decode('latin-1')):
            b['datei'] = schl.split('\\')[-1]
            roh[b['name'].lower()] = b

    fertig = {}

    def effektiv(name, tiefe=0):
        if name in fertig:
            return fertig[name]
        b = roh[name]
        d = block_auswerten(b)
        if b['basis'] and b['basis'].lower() in roh and tiefe < 10:
            e = effektiv(b['basis'].lower(), tiefe + 1)
            if not d['draws']:
                d['draws'], d['modelle'], d['treetex'] = e['draws'], e['modelle'], e['treetex']
                d['partikelknochen'] = d['partikelknochen'] | e['partikelknochen']
                if d['topple'] is None:
                    d['topple'] = e['topple']
                if d['schatten'] is None:
                    d['schatten'] = e['schatten']
            d['kindof'] = d['kindof'] or e['kindof']
            d['sorting'] = d['sorting'] or e['sorting']
            d['verhalten'] = d['verhalten'] | e['verhalten']
            d['stumpf'] = d['stumpf'] or e['stumpf']
            d['partikelknochen'] |= e['partikelknochen'] if d['draws'] == e['draws'] else set()
            g = dict(e['geometrie'])
            g.update(d['geometrie'])
            d['geometrie'] = g
        d['name'] = b['name']
        d['basis'] = b['basis']
        d['datei'] = b['datei']
        fertig[name] = d
        return d

    return {k: effektiv(k) for k in roh}


def ist_pflanze(d):
    if 'SHRUBBERY' in d['kindof'] or d['sorting'] == 'SHRUBBERY':
        return True
    return any(m.lower().startswith('pt') for m in d['modelle'])


def art(d):
    text = (d['name'] + ' ' + ' '.join(d['modelle'])).lower()
    for muster, a in ARTEN:
        if re.search(muster, text):
            return a
    return 'Sonstiges'


# --- W3D und Texturen ------------------------------------------------------------------------------

def abschnitte(daten):
    p = 0
    while p + 8 <= len(daten):
        typ, gr = struct.unpack('<II', daten[p:p + 8])
        n = gr & 0x7FFFFFFF
        yield typ, daten[p + 8:p + 8 + n]
        p += 8 + n


def cstr(b):
    return b.split(b'\0', 1)[0].decode('latin-1')


def w3d_auswerten(daten):
    r = {'netze': 0, 'dreiecke': 0, 'ecken': 0, 'texturen': [], 'alphatest': False, 'alphablend': False,
         'knochen': [], 'hierarchie': None, 'zweiseitig': False}
    for typ, b in abschnitte(daten):
        if typ == 0x0000:
            r['netze'] += 1
            for t2, b2 in abschnitte(b):
                if t2 == 0x001F:
                    attr, = struct.unpack('<I', b2[4:8])
                    ndr, neck = struct.unpack('<II', b2[40:48])
                    r['dreiecke'] += ndr
                    r['ecken'] += neck
                    r['zweiseitig'] |= bool(attr & 0x2000)
                elif t2 == 0x0029:
                    for i in range(len(b2) // 16):
                        s = b2[16 * i:16 * i + 16]
                        r['alphatest'] |= s[12] != 0
                        r['alphablend'] |= s[3] != 0
                elif t2 == 0x0030:
                    for t3, b3 in abschnitte(b2):
                        for t4, b4 in abschnitte(b3):
                            if t4 == 0x0032:
                                n = cstr(b4)
                                if n not in r['texturen']:
                                    r['texturen'].append(n)
        elif typ == 0x0100:
            for t2, b2 in abschnitte(b):
                if t2 == 0x0102:
                    for i in range(len(b2) // 60):
                        r['knochen'].append(cstr(b2[60 * i:60 * i + 16]))
        elif typ == 0x0700:
            for t2, b2 in abschnitte(b):
                if t2 == 0x0701:
                    r['hierarchie'] = cstr(b2[24:40])
    return r


def modell(w3d, name, cache):
    if name.lower() in cache:
        return cache[name.lower()]
    # 'Datei.Netz' nennt ein Netz in einer Datei
    daten = w3d.lesen('art\\w3d\\%s.w3d' % name.split('.')[0])
    r = w3d_auswerten(daten) if daten else None
    if r and not r['knochen'] and r['hierarchie'] and r['hierarchie'].lower() != name.lower():
        h = w3d.lesen('art\\w3d\\%s.w3d' % r['hierarchie'])
        if h:
            r['knochen'] = w3d_auswerten(h)['knochen']
    cache[name.lower()] = r
    return r


def bildgroesse(daten):
    if daten is None:
        return None
    if daten[:4] == b'DDS ':
        h, w = struct.unpack('<II', daten[12:20])
        fcc = daten[84:88]
        fmt = fcc.decode('latin-1') if fcc.strip(b'\0') else 'RGB%d' % struct.unpack('<I', daten[88:92])[0]
        return w, h, fmt
    if len(daten) > 18:
        w, h = struct.unpack('<HH', daten[12:16])
        return w, h, 'TGA%d' % daten[16]
    return None


def textur(tex, name):
    stamm = os.path.splitext(name)[0].lower()
    for ext in ('.dds', '.tga'):
        g = bildgroesse(tex.lesen('art\\textures\\' + stamm + ext))
        if g:
            return stamm + ext, g
    return name.lower(), None


# --- Karten ------------------------------------------------------------------------------------------

def karten_objekte(archiv):
    for schl, (pfad, off, gr, orig) in sorted(archiv.eintraege.items()):
        if not schl.endswith('.map') or schl.count('\\') < 2:
            continue
        with open(pfad, 'rb') as f:
            f.seek(off)
            try:
                d = f.read(gr)
                # 'ZL5\0' + Länge: zlib statt RefPack (eine Generals-Karte)
                d = zlib.decompress(d[8:]) if d[:2] == b'ZL' else unpack_map(d)
                names, top = parse_chunks(d)
            except Exception as e:  # noqa: BLE001
                print('Karte nicht lesbar: %s (%s)' % (schl, e), file=sys.stderr)
                continue
        z = collections.Counter()
        if 'ObjectsList' in top:
            body = top['ObjectsList'][1]
            p = 0
            while p + 10 <= len(body):
                cid, ver, ln = struct.unpack('<IHI', body[p:p + 10])
                ob = body[p + 10:p + 10 + ln]
                p += 10 + ln
                if names.get(cid) != 'Object':
                    continue
                q = 20
                n, = struct.unpack('<H', ob[q:q + 2])
                z[ob[q + 2:q + 2 + n].decode('latin-1').lower()] += 1
        yield orig.split('\\')[-2], z


# --- Ablauf ------------------------------------------------------------------------------------------

def bestand():
    erg = {}
    for spiel, a in SPIELE.items():
        ini, w3d, tex, ter, maps = (Archiv(a[k]) for k in ('ini', 'w3d', 'tex', 'ter', 'maps'))
        objs = objekte_lesen(ini)
        cache = {}
        pfl = {}
        for k, d in objs.items():
            if not ist_pflanze(d):
                continue
            mods = []
            for m in d['modelle']:
                r = modell(w3d, m, cache)
                eintrag = {'name': m, 'gefunden': r is not None}
                if r:
                    eintrag.update(r)
                    eintrag['texturdateien'] = [textur(tex, t) for t in r['texturen']]
                mods.append(eintrag)
            baumtex = None
            if d['treetex']:
                # wie W3DTreeBuffer::updateTexture: erst Art/Terrain, dann Art/Textures, nur die TGA selbst
                g = bildgroesse(ter.lesen('art\\terrain\\' + d['treetex'].lower()))
                if g is None:
                    g = bildgroesse(tex.lesen('art\\textures\\' + d['treetex'].lower()))
                baumtex = (d['treetex'], g)
            pfl[k] = {
                'name': d['name'], 'basis': d['basis'], 'datei': d['datei'], 'art': art(d),
                'puffer': 'W3DTreeDraw' in d['draws'], 'draws': d['draws'], 'modelle': mods,
                'baumtextur': baumtex, 'verhalten': sorted(d['verhalten']), 'kindof': sorted(d['kindof']),
                'partikelknochen': sorted(d['partikelknochen']), 'stumpf': d['stumpf'], 'topple': d['topple'],
                'schatten': d['schatten'], 'geometrie': d['geometrie'], 'instanzen': 0, 'karten': 0,
            }
        karten = {}
        unbekannt = collections.Counter()
        for karte, z in karten_objekte(maps):
            je = {}
            for obj, n in z.items():
                if obj in pfl:
                    pfl[obj]['instanzen'] += n
                    pfl[obj]['karten'] += 1
                    je[obj] = n
                elif obj not in objs and re.search(r'tree|bush|shrub|palm|pine|grass|plant', obj):
                    unbekannt[obj] += n
            karten[karte] = je
        # alle pt*-Texturen unter art/textures und Baum-TGAs unter art/terrain
        pt_tex = {}
        for schl in tex.namen('art\\textures\\pt'):
            pt_tex[schl.split('\\')[-1]] = bildgroesse(tex.lesen(schl))
        pt_ter = {}
        for schl in ter.namen('art\\terrain\\pt'):
            pt_ter[schl.split('\\')[-1]] = bildgroesse(ter.lesen(schl))
        pt_w3d = sorted(s.split('\\')[-1] for s in w3d.namen('art\\w3d\\pt'))
        erg[spiel] = {'objekte': pfl, 'karten': karten, 'unbekannt': dict(unbekannt), 'pt_texturen': pt_tex,
                      'pt_terrain': pt_ter, 'pt_w3d': pt_w3d}
    return erg


def groesse_text(g):
    return '%d² %s' % (g[0], g[2]) if g and g[0] == g[1] else ('%dx%d %s' % g if g else '--')


def besonderheiten(o):
    b = []
    v = set(x.lower() for x in o['verhalten'])
    if 'swayclientupdate' in v or o['puffer']:
        b.append('Wind')
    if o['topple'] or 'toppleupdate' in v:
        b.append('Umfallen')
    if 'flammableupdate' in v:
        b.append('Brennen')
    if any(m['name'].lower().endswith('_b') for m in o['modelle']):
        b.append('_B')
    if 'snow' in o['name'].lower() or any(m['name'].lower().endswith('_s') for m in o['modelle']):
        b.append('Schnee')
    if 'fall' in o['name'].lower():
        b.append('Herbst')
    if any(k.startswith('AFLAME') for m in o['modelle'] if m.get('knochen') for k in [x.upper() for x in m['knochen']]):
        b.append('AFLAME-Knochen')
    if o['stumpf']:
        b.append('Stumpf ' + o['stumpf'])
    if o['name'].lower().startswith('cine'):
        b.append('Film')
    return ', '.join(b)


def ausgeben(erg, je_karte):
    for spiel, e in erg.items():
        objs = e['objekte']
        summe = sum(o['instanzen'] for o in objs.values())
        print('\n## %s: %d Pflanzenobjekte, %d Instanzen auf %d Karten\n' % (spiel, len(objs), summe, len(e['karten'])))
        print('| Objekt | Art | Modell(e) | Textur(en) | Dr. | Puffer | Instanzen | Anteil | Karten | Besonderheiten |')
        print('|---|---|---|---|---|---|---|---|---|---|')
        for o in sorted(objs.values(), key=lambda o: (-o['instanzen'], o['name'])):
            m0 = o['modelle'][0] if o['modelle'] else {}
            texs = []
            if o['baumtextur']:
                texs.append('%s (%s)' % (o['baumtextur'][0], groesse_text(o['baumtextur'][1])))
            for m in o['modelle'][:1]:
                for t, g in m.get('texturdateien', []):
                    texs.append('%s (%s)' % (t, groesse_text(g)))
            print('| %s | %s | %s | %s | %s | %s | %d | %.1f %% | %d | %s |' % (
                o['name'], o['art'], ', '.join(m['name'] + ('' if m['gefunden'] else ' (fehlt)') for m in o['modelle']),
                '; '.join(texs), m0.get('dreiecke', '--'), 'ja' if o['puffer'] else 'nein', o['instanzen'],
                100.0 * o['instanzen'] / summe if summe else 0, o['karten'], besonderheiten(o)))
        arten = collections.Counter()
        for o in objs.values():
            arten[o['art']] += o['instanzen']
        print('\n| Art | Instanzen | Anteil |\n|---|---|---|')
        for a, n in arten.most_common():
            print('| %s | %d | %.1f %% |' % (a, n, 100.0 * n / summe if summe else 0))
        if e['unbekannt']:
            print('\nKartenobjekte ohne INI-Eintrag mit Pflanzennamen: %s' % e['unbekannt'])
        print('\npt*-Texturen unter art/textures: %d; pt*-TGAs unter art/terrain: %d; pt*-Modelle: %d' % (
            len(e['pt_texturen']), len(e['pt_terrain']), len(e['pt_w3d'])))
        groessen = collections.Counter(groesse_text(g) for g in e['pt_texturen'].values())
        print('Texturgrößen art/textures: %s' % dict(groessen.most_common()))
        groessen = collections.Counter(groesse_text(g) for g in e['pt_terrain'].values())
        print('Texturgrößen art/terrain: %s' % dict(groessen.most_common()))
        if je_karte:
            print('\n| Karte | Bauminstanzen | Arten |\n|---|---|---|')
            for k, je in sorted(e['karten'].items(), key=lambda kv: -sum(kv[1].values())):
                print('| %s | %d | %d |' % (k, sum(je.values()), len(je)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--json', help='Rohdaten als JSON in diese Datei')
    ap.add_argument('--karten', action='store_true', help='Bauminstanzen je Karte ausgeben')
    a = ap.parse_args()
    sys.stdout.reconfigure(encoding='utf-8')
    erg = bestand()
    ausgeben(erg, a.karten)
    if a.json:
        with open(a.json, 'w', encoding='utf-8') as f:
            json.dump(erg, f, ensure_ascii=False, indent=1, default=list)


if __name__ == '__main__':
    main()
