# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# CC0-Modelle von Poly Haven herunterladen und die Herkunft festhalten.
#
#   python tools/assets/cc0_laden.py <asset-id>... [--format gltf|blend] [--aufloesung 1k|2k|4k] [--ziel <ordner>]
#   python tools/assets/cc0_laden.py --quelle ambientcg LeafSet012 Bark012 [--aufloesung 2k]   (ambientCG, PNG-Zip)
#
# Lädt je Asset die Datei samt allen Begleitdateien (Texturen, .bin) über die offene API api.polyhaven.com nach
# <ziel>\<asset-id>\ (Vorgabe <GZH_WERKZEUGE>\cc0\modelle, außerhalb des Repos) und trägt Name, Autoren, Lizenz,
# Adresse und Abrufdatum in <ziel>\LIZENZEN.md ein (eine Zeile je Asset, vorhandene Zeile wird ersetzt).
# Poly Haven stellt alle Assets unter CC0 1.0 (https://polyhaven.com/license); die Nennung ist nicht verlangt,
# wir führen sie trotzdem (ASSET_CREDITS.md).
import argparse
import datetime
import json
import os
import sys
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import pfade  # noqa: E402

API = 'https://api.polyhaven.com'
KOPF = {'User-Agent': 'cnc-generals-modernisierung (TODO 56, Einzelabruf)'}


def hole(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=KOPF), timeout=600) as r:
        return r.read()


def laden(asset, fmt, res, ziel):
    info = json.loads(hole('%s/info/%s' % (API, asset)))
    dateien = json.loads(hole('%s/files/%s' % (API, asset)))
    eintrag = dateien[fmt][res][fmt]
    ordner = os.path.join(ziel, asset)
    os.makedirs(ordner, exist_ok=True)
    liste = [(os.path.basename(eintrag['url']), eintrag['url'], eintrag['size'])]
    liste += [(pfad, x['url'], x['size']) for pfad, x in eintrag.get('include', {}).items()]
    for pfad, url, groesse in liste:
        datei = os.path.join(ordner, pfad.replace('/', os.sep))
        if os.path.exists(datei) and os.path.getsize(datei) == groesse:
            continue
        os.makedirs(os.path.dirname(datei), exist_ok=True)
        daten = hole(url)
        with open(datei, 'wb') as f:
            f.write(daten)
        print('geladen %-60s %10d' % (pfad, len(daten)))
    json.dump(info, open(os.path.join(ordner, 'info.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    autoren = ', '.join('%s (%s)' % (n, r) for n, r in info.get('authors', {}).items())
    zeile = '| `%s` | %s | %s | CC0 1.0 | https://polyhaven.com/a/%s | %s, %s %s | %s |' % (
        asset, info.get('name', asset), autoren, asset, fmt, res, os.path.basename(eintrag['url']),
        datetime.date.today().isoformat())
    lizenz_zeile(ziel, asset, zeile)
    print('fertig', asset, '->', ordner, '|', info.get('name'), '|', autoren)


def lizenz_zeile(ziel, asset, zeile):
    lizenz = os.path.join(ziel, 'LIZENZEN.md')
    zeilen = open(lizenz, encoding='utf-8').read().splitlines() if os.path.exists(lizenz) else [
        '# CC0 models (raw files, not in the repository)', '',
        'Source Poly Haven, licence CC0 1.0 (https://polyhaven.com/license). Downloaded with tools/assets/cc0_laden.py.', '',
        '| Asset | Name | Authors | Licence | Address | File | Downloaded |', '|---|---|---|---|---|---|---|']
    zeilen = [z for z in zeilen if not z.startswith('| `%s` |' % asset)] + [zeile]
    open(lizenz, 'w', encoding='utf-8').write('\n'.join(zeilen) + '\n')


def laden_ambientcg(asset, res, ziel):
    """ambientCG (Blattsätze, Rinde): <id>_<RES>-PNG.zip über die offene API laden und entpacken. Lizenz CC0 1.0
    für alle Assets (https://docs.ambientcg.com/license/); die Zeile in LIZENZEN.md nennt Adresse und Abrufdatum."""
    import io
    import zipfile
    meta = json.loads(hole('https://ambientcg.com/api/v2/full_json?id=%s&include=tagData,displayData' % asset))
    a = meta['foundAssets'][0]
    datei = '%s_%s-PNG.zip' % (asset, res.upper())
    ordner = os.path.join(ziel, asset)
    os.makedirs(ordner, exist_ok=True)
    if not any(n.lower().endswith('.png') for n in os.listdir(ordner)):
        daten = hole('https://ambientcg.com/get?file=' + datei)
        zipfile.ZipFile(io.BytesIO(daten)).extractall(ordner)
        print('geladen %-40s %10d' % (datei, len(daten)))
    info = {'name': a.get('displayName', asset), 'tags': a.get('tags', []), 'quelle': 'ambientCG',
            'lizenz': 'CC0 1.0 Universal', 'seite': 'https://ambientcg.com/view?id=' + asset, 'datei': datei}
    json.dump(info, open(os.path.join(ordner, 'info.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    lizenz_zeile(ziel, asset, '| `%s` | %s | Lennart Demes (ambientCG) | CC0 1.0 | https://ambientcg.com/view?id=%s | %s | %s |' % (
        asset, info['name'], asset, datei, datetime.date.today().isoformat()))
    print('fertig', asset, '->', ordner, '|', info['name'], '|', ','.join(info['tags']))


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('assets', nargs='+')
    ap.add_argument('--quelle', default='polyhaven', choices=('polyhaven', 'ambientcg'))
    ap.add_argument('--format', default='gltf', choices=('gltf', 'blend', 'fbx'))
    ap.add_argument('--aufloesung', default='2k')
    vorgabe = pfade.werkzeuge('cc0', 'modelle', pflicht=False)
    ap.add_argument('--ziel', default=vorgabe, required=vorgabe is None)
    a = ap.parse_args()
    for x in a.assets:
        if a.quelle == 'ambientcg':
            laden_ambientcg(x, a.aufloesung, a.ziel)
        else:
            laden(x, a.format, a.aufloesung, a.ziel)
