# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Lage der Scan-Teile im Modell nach eigener Regel: liest je Objekt die Regel
# "anordnung" aus dem Auftrag und die natürliche Größe der Scan-Teile (Hülle im glTF, Meter), rechnet Mitte und Winkel je
# Teil und schreibt sie als feste Zahlen ("lage") zurück in natur_objekte.json. Ohne Blender, liest keine EA-Datei.
#
#   python natur_anordnen.py [<objekt>...]
#
# Regeln ("anordnung"):
#   massstab     Spieleinheiten je Meter des Scans (Spielmaßstab 6,5 Einheiten/m mal Gestaltungsfaktor)
#   art          "haufen": größtes Teil in der Mitte, die übrigen der Größe nach im goldenen Winkel (137,5 Grad) so nah
#                angelegt, wie die Überlappung es erlaubt; "reihe": der Größe nach von der Mitte nach außen abwechselnd
#                längs der x-Achse, quer leicht versetzt (Geröllzunge); "einzeln": ein Teil in der Mitte
#   ueberlappung Anteil, um den sich die Ersatzkreise (Fläche der Grundfläche) benachbarter Teile überdecken dürfen
#   startwinkel  Richtung des zweiten Teils bzw. Winkel des einzelnen Teils (Grad)
# Winkel je Teil: Hauptachse tangential zum Haufen (Richtung + 90 Grad), im Wechsel um 25 Grad verdreht, damit die
# Steine nicht wie aufgereiht wirken. Zum Schluss wird die Grundfläche um den Ursprung (Fußpunkt) zentriert.
import json
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import pfade  # noqa: E402

HIER = os.path.dirname(os.path.abspath(__file__))
AUFTRAG = os.path.join(HIER, 'natur_objekte.json')
GOLDEN = 137.508


def teile_groesse(gltf_pfad):
    """{Teilname: (Länge, Breite, Höhe) in Metern}: Hülle der Positionen mal Knotenskalierung; glTF ist y-oben."""
    g = json.load(open(gltf_pfad, encoding='utf-8'))
    out = {}
    for n in g['nodes']:
        if 'mesh' not in n:
            continue
        acc = g['accessors'][g['meshes'][n['mesh']]['primitives'][0]['attributes']['POSITION']]
        sk = n.get('scale', [1, 1, 1])
        dx, dy, dz = [(b - a) * f for a, b, f in zip(acc['min'], acc['max'], sk)]
        out[n['name']] = (max(dx, dz), min(dx, dz), dy)
    return out


def anordnen(groessen, regel):
    """groessen: [(L, B, H)] in Spieleinheiten, Reihenfolge des Auftrags. Liefert [(x, y, winkel)]."""
    ue = float(regel.get('ueberlappung', 0.2))
    start = float(regel.get('startwinkel', 0))
    r = [0.5 * math.sqrt(l * b) for l, b, _ in groessen]
    folge = sorted(range(len(r)), key=lambda i: -r[i])
    lage = {}
    art = regel['art']
    if art == 'einzeln':
        lage[folge[0]] = (0.0, 0.0, start)
    elif art == 'haufen':
        lage[folge[0]] = (0.0, 0.0, start + 90)
        for k, i in enumerate(folge[1:], 1):
            a = math.radians(start + (k - 1) * GOLDEN)
            d = 0.0
            while any(math.hypot(d * math.cos(a) - lage[j][0], d * math.sin(a) - lage[j][1]) < (r[i] + r[j]) * (1 - ue)
                      for j in lage):
                d += 0.05
            lage[i] = (d * math.cos(a), d * math.sin(a), math.degrees(a) + 90 + (25 if k % 2 else -25))
    elif art == 'reihe':
        rechts = links = 0.0
        for k, i in enumerate(folge):
            if k == 0:
                x = 0.0
                rechts, links = r[i], -r[i]
            elif k % 2:
                x = rechts + r[i] * (1 - 2 * ue)
                rechts = x + r[i]
            else:
                x = links - r[i] * (1 - 2 * ue)
                links = x - r[i]
            y = (0.35 if k % 2 else -0.35) * r[i] if k else 0.0
            lage[i] = (x, y, start + (25 if k % 2 else -25) * (k % 3))
    else:
        raise SystemExit('unbekannte Anordnung: %s' % art)
    # Grundfläche (Ersatzkreise) um den Ursprung zentrieren
    x0 = min(lage[i][0] - r[i] for i in lage); x1 = max(lage[i][0] + r[i] for i in lage)
    y0 = min(lage[i][1] - r[i] for i in lage); y1 = max(lage[i][1] + r[i] for i in lage)
    mx, my = (x0 + x1) / 2, (y0 + y1) / 2
    return [(lage[i][0] - mx, lage[i][1] - my, ((lage[i][2] + 180) % 360) - 180) for i in range(len(r))], (x1 - x0, y1 - y0)


def fmt(x, ind=0):
    pad = '  ' * ind
    flach = json.dumps(x, ensure_ascii=False, separators=(', ', ': '))
    if len(flach) + len(pad) <= 118 or not isinstance(x, (dict, list)):
        return flach
    if isinstance(x, dict):
        return '{\n' + ',\n'.join('%s  %s: %s' % (pad, json.dumps(k, ensure_ascii=False), fmt(v, ind + 1))
                                  for k, v in x.items()) + '\n' + pad + '}'
    return '[\n' + ',\n'.join('%s  %s' % (pad, fmt(v, ind + 1)) for v in x) + '\n' + pad + ']'


def main(objekte):
    d = json.load(open(AUFTRAG, encoding='utf-8'))
    modelle = pfade.unter_werkzeuge(d['vorgaben']['modelle'])
    for name, o in d['objekte'].items():
        if 'anordnung' not in o or (objekte and name not in objekte):
            continue
        regel = o['anordnung']
        s = float(regel['massstab'])
        groesse = teile_groesse(os.path.join(modelle, o['quelle']))
        gs = [tuple(c * s for c in groesse[z['quelle']]) for z in o['zuordnung']]
        lagen, flaeche = anordnen(gs, regel)
        for z, (x, y, w), g in zip(o['zuordnung'], lagen, gs):
            z['lage'] = {'mitte': [round(x, 2), round(y, 2)], 'winkel': round(w), 'massstab': s}
            print('%-11s %-26s L %5.1f B %5.1f H %5.1f  Mitte %6.1f %6.1f  Winkel %4.0f' % (
                name, z['quelle'], g[0], g[1], g[2], x, y, w))
        print('%-11s Grundfläche %.1f x %.1f Einheiten' % (name, *flaeche))
    open(AUFTRAG, 'w', encoding='utf-8', newline='').write(fmt(d).replace('\n', '\r\n') + '\r\n')


if __name__ == '__main__':
    main(sys.argv[1:])
