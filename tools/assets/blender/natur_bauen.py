# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Ablaufsteuerung für die CC0-Naturobjekte (TODO 56/78): Blender-Schritte je Objekt, Prüfung gegen
# die eigene Gerüst-Vorschrift (w3d_geruest.py), Ablage im Repo und Testarchiv. Liest keine EA-Datei; der Vergleich mit
# den EA-Modellen steht getrennt in natur_messen.py.
#
#   python natur_bauen.py bauen  <objekt>... [--schritte vorbereiten,backen,textur,export,ansicht] [--k=v ...]
#                                (Bäume: sprites,textur,modell,paket,ansicht; --k=v überschreibt Werte des Auftrags)
#   python natur_bauen.py pruefen <objekt>... [--ordner=<dir>]   Sollwerte: Namen der INIs, Gerüst nach Vorschrift
#                                (Standard: die Exporte im Arbeitsordner; Data/Natur/Art/W3D mit --ordner=repo)
#   python natur_bauen.py ablegen <objekt>...   SD-Fassung (Modelle, Texturen) nach Data/Natur/Art (nach pruefen)
#   python natur_bauen.py packen <ziel.big> <objekt>... [--stufe=sd|hd|uhd]   (Texturen der Stufe unter dem
#                                Namen der SD-Textur: je Stufe ein eigenes -mod-Archiv)
#
# Objekte stehen in natur_objekte.json. Requisiten laufen über objekt_ersetzen.py, Objekte mit "art": "baum" über
# baum_ersetzen.py. Das Testarchiv lädt das Spiel mit -mod <ordner des .big> (nur ein Archiv je Ordner, -mod lädt
# rekursiv). Texturen: PNG -> DDS mit Mip-Kette (DXT1, mit Alpha DXT5); Einträge aus <arbeit>/<objekt>/paket.json
# (Bäume: TGA für den Baum-Atlas) werden unverändert übernommen.
import json
import os
import subprocess
import sys

import numpy as np
from PIL import Image

HIER = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HIER, '..'))
sys.path.insert(0, HIER)
from build_texture_tiers import build_mips, encode_dds, write_big  # noqa: E402
import w3d_geruest  # noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import pfade  # noqa: E402

BLENDER = os.environ.get('BLENDER', 'blender')
AUFTRAG = os.path.join(HIER, 'natur_objekte.json')
REPO_W3D = os.path.normpath(os.path.join(HIER, '..', '..', '..', 'Data', 'Natur', 'Art', 'W3D'))
STANDARD = 'vorbereiten,backen,textur,export,ansicht'


def cfg(objekt):
    d = json.load(open(AUFTRAG, encoding='utf-8'))
    c = dict(d.get('vorgaben', {}))
    if 'arbeit' in c:
        c['arbeit'] = pfade.unter_werkzeuge(c['arbeit'])
    c.update(d['objekte'][objekt])
    return c


def blender(skript, *args):
    r = subprocess.run([BLENDER, '--background', '--factory-startup', '--python', os.path.join(HIER, skript), '--']
                       + list(args), capture_output=True, text=True, encoding='utf-8', errors='replace')
    for z in (r.stdout + '\n' + r.stderr).splitlines():
        if z.startswith(('OBJEKT', 'BAUM', 'Traceback', '  File', 'Error', 'ERROR')) or 'Error:' in z:
            print('   ', z)
    if r.returncode != 0 or 'Traceback' in r.stderr + r.stdout:
        raise SystemExit('Blender-Schritt fehlgeschlagen: %s %s' % (skript, ' '.join(args)))


def bauen(objekte, schritte, extra):
    for o in objekte:
        c = cfg(o)
        if c.get('art') == 'baum':
            for s in (schritte if schritte != STANDARD else 'sprites,textur,modell,paket,ansicht').split(','):
                print('==', o, s, flush=True)
                if s in ('textur', 'modell', 'paket'):
                    r = subprocess.run([sys.executable, os.path.join(HIER, 'baum_ersetzen.py'), s, AUFTRAG, o] + extra)
                    if r.returncode:
                        raise SystemExit('Schritt %s fehlgeschlagen' % s)
                else:
                    blender('baum_ersetzen.py', s, AUFTRAG, o, *extra)
            continue
        skript = 'objekt_ersetzen.py'
        for s in schritte.split(','):
            print('==', o, s, flush=True)
            if s == 'export':
                blender(skript, s, AUFTRAG, o, *extra)
                for f in c.get('fassungen', {}):
                    blender(skript, s, AUFTRAG, o, '--fassung=' + f, *extra)
            else:
                blender(skript, s, AUFTRAG, o, *extra)


def modelle(o):
    """[(modellname, export-datei, textur)] des Objekts samt Fassungen; der Modellname ist der Name der INI und zugleich
    der Archivname Art\\W3D\\<modell>.W3D."""
    c = cfg(o)
    aus = [(c['modell'], c['textur'])]
    aus += [(f['modell'], f.get('textur', c['textur'])) for f in c.get('fassungen', {}).values()]
    return [(m, os.path.join(c['arbeit'], o, 'export', m.upper() + '.w3d'), t + '.tga') for m, t in aus]


def pruefen(objekte, ordner=None):
    """Jede Datei gegen die Vorschrift: Namen wie in der INI, Gerüst bytegleich dem Neubau aus ihrer eigenen Geometrie,
    Textur des Auftrags. Gibt True zurück, wenn alle bestehen."""
    ok = True
    for o in objekte:
        art = 'baum' if cfg(o).get('art') == 'baum' else 'requisit'
        for modell, export, textur in modelle(o):
            datei = os.path.join(ordner, modell + '.W3D') if ordner else export
            fehler = w3d_geruest.soll(open(datei, 'rb').read(), modell, art, textur)
            print('== %-14s %-8s %s' % (modell, art, 'bestanden' if not fehler else 'NICHT bestanden'))
            for f in fehler:
                print('   ', f)
            if art == 'baum' and not fehler:
                g = w3d_geruest.geometrie(open(datei, 'rb').read())[0]
                f = w3d_geruest.feuerpunkt(g['ecken'], g['dreiecke'])
                hoehe = max(p[2] for p in g['ecken'])
                print('    AFLAME01 bei (%.2f, %.2f, %.2f), %.0f %% der Höhe %.2f' % (f[0], f[1], f[2], 100 * f[2] / hoehe,
                                                                                 hoehe))
            ok &= not fehler
    return ok


def ablegen(objekte):
    """SD-Fassung (Modelle, DDS, Atlas-TGA) unter ihrem Archivpfad nach Data/Natur kopieren."""
    wurzel = os.path.dirname(os.path.dirname(REPO_W3D))
    for name, daten in eintraege(objekte, 'sd'):
        ziel = os.path.join(wurzel, *name.split('\\'))
        os.makedirs(os.path.dirname(ziel), exist_ok=True)
        open(ziel, 'wb').write(daten)
        print('abgelegt', ziel, len(daten))


def eintraege(objekte, stufe='sd'):
    """[(Archivpfad, Bytes)] der Stufe: Modelle und Texturen unter dem Namen der SD-Textur."""
    endung = '' if stufe == 'sd' else '_' + stufe
    eintr = []
    for o in objekte:
        c = cfg(o)
        paket = os.path.join(c['arbeit'], o, 'paket%s.json' % endung)
        if os.path.exists(paket):
            for archivname, datei in json.load(open(paket, encoding='utf-8')):
                eintr.append((archivname, open(datei, 'rb').read()))
            continue
        for modell, neu, _ in modelle(o):
            eintr.append(('Art\\W3D\\%s.W3D' % modell, open(neu, 'rb').read()))
        texturen = [c['textur']] + [f['textur'] for f in c.get('fassungen', {}).values()]
        for t in texturen:
            bild = np.asarray(Image.open(os.path.join(c['arbeit'], o, 'textur', t + endung + '.png')).convert('RGBA')).copy()
            alpha = (bild[..., 3] < 255).any()
            fourcc = 'DXT5' if alpha else 'DXT1'
            eintr.append(('Art\\Textures\\%s.dds' % t.lower(), encode_dds(build_mips(bild, fourcc, 'binary' if alpha
                                                                                      else 'none'), fourcc)))
    return eintr


def packen(ziel, objekte, stufe='sd'):
    eintr = eintraege(objekte, stufe)
    os.makedirs(os.path.dirname(os.path.abspath(ziel)), exist_ok=True)
    write_big(ziel, eintr)
    for n, d in eintr:
        print('  %-40s %8d' % (n, len(d)))
    print('geschrieben', ziel, sum(len(d) for _, d in eintr), 'Bytes')


if __name__ == '__main__':
    befehl = sys.argv[1]
    rest = [a for a in sys.argv[2:] if not a.startswith('--')]
    optionen = [a for a in sys.argv[2:] if a.startswith('--')]
    schritte = STANDARD
    for a in list(optionen):
        if a.startswith('--schritte='):
            schritte = a.split('=', 1)[1]
            optionen.remove(a)
    if befehl == 'bauen':
        bauen(rest, schritte, optionen)
    elif befehl == 'pruefen':
        ordner = None
        for a in optionen:
            if a.startswith('--ordner='):
                ordner = a.split('=', 1)[1]
                ordner = REPO_W3D if ordner == 'repo' else ordner
        sys.exit(0 if pruefen(rest, ordner) else 1)
    elif befehl == 'ablegen':
        ablegen(rest)
    elif befehl == 'packen':
        stufe = 'sd'
        for a in optionen:
            if a.startswith('--stufe='):
                stufe = a.split('=', 1)[1]
        packen(rest[0], rest[1:], stufe)
    else:
        sys.exit('unbekannter Befehl: ' + befehl)
