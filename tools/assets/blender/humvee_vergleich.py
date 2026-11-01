# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Vergleichsseite für den KI-3D-Humvee -- lokal, nicht versioniert.
# Drei Fassungen: EA, Pilot 1 (2026-09-26 früh), nachgebessert (Kontrast, Felgen, Teamfarbe, beschädigte Fassung).
#
#   python tools/assets/blender/humvee_vergleich.py <render-out/humvee-pilot> <arbeitsordner>
#
# Erwartet in <render-out/humvee-pilot> je Szene die Ordner <szene>-ea, <szene>-neu und <szene>-neu2 mit shot_*.bmp
# (tools/render/run_render_test.py, Fahrten tests/render/paths/humvee_*.txt) und im Arbeitsordner die
# Blender-Ansichten ansicht/<praefix>_*.png (humvee_bauen.py ansicht). Schreibt index.html (Umschalten der
# Fassungen, alle nebeneinander, Lupe), bilder/ (JPG) und einzelbilder/ (wichtigste Ausschnitte je Fassung und
# nebeneinander).
import html
import os
import sys

from PIL import Image, ImageDraw

WURZEL, ARBEIT = sys.argv[1], sys.argv[2]
FASSUNGEN = [('ea', 'EA'), ('neu', 'Pilot 1'), ('neu2', 'nachgebessert')]

SZENEN = [
    ('frueh', 'Früh: zwei Humvees mit MG-Turm (vor der TOW-Aufrüstung)',
     'Replay Tournament Lake (3 KI, Seed 2), Basis von Player_2, Frame 4600 bis 5400: Spielsicht, dann nah mit '
     'Kameradrehung. Fahrt tests/render/paths/humvee_frueh.txt.'),
    ('nah', 'Basis: fünf Humvees mit TOW-Aufrüstung',
     'Frame 7000 Spielsicht (Standardzoom), 7050/7100 näher, 7150 bis 7300 nah mit Kameradrehung, 7350/7400 flach '
     'von der Seite. Der TOW-Aufsatz (TURRETUP01) ist das EA-Teil. Fahrt humvee_nah.txt.'),
    ('fahrt', 'Fahrt und Gefecht (mit beschädigten Fahrzeugen)',
     'Zwei Humvees fahren nach Westen und kämpfen um Frame 8350 bis 8700 (Turm dreht, Raketen, Treffer). Das Wrack '
     'ab Frame 8600 ist ein eigenes EA-Modell (AvHummer_D1, Objekt AmericaVehicleHumveeDeadHull) und bleibt EA. '
     'Fahrt humvee_fahrt.txt.'),
    ('farben', 'Zweite Teamfarbe',
     'Skirmish Tournament Lake, 3 KI, Seed 7: Laser-General auf zwei Seiten in verschiedenen Farben '
     '(Lazr_AmericaVehicleHumvee, gleiches Modell AVHUMMER). Fahrt humvee_farben.txt.'),
    ('beschaedigt', 'Schwer beschädigt (AvHummer_D)',
     'Skirmish Tournament Lake, 3 KI, Seed 7: ein Humvee des Laser-Generals steht ab Frame 8480 mit 50 von 240 '
     'Lebenspunkten (Zustand REALLYDAMAGED, Modell AvHummer_D). Pilot 1 fällt hier auf das EA-Modell zurück; '
     'nachgebessert ist die beschädigte Fassung aus dem Meshy-Modell (dunkler, Ruß, Brandränder). '
     'Fahrt humvee_beschaedigt.txt.'),
    ('raeder', 'Räder in Bewegung',
     'Bilder alle 4 Frames bei nahem Zoom (humvee_raeder.txt) -- die Felge wandert von Bild zu Bild.'),
]

# Einzelbilder: (Name, Szene, Frame, Ausschnitt x, y, b, h im 1920x1080-Bild, Beschreibung)
EINZEL = [
    ('01_spielsicht_frueh', 'frueh', 4600, (0, 0, 1920, 820), 'Spielsicht, Standardzoom, MG-Turm'),
    ('02_nah_oben_teamfarbe', 'frueh', 4800, (450, 300, 700, 460), 'nah von oben, MG-Turm, Teamfarbe blau'),
    ('03_spielsicht_tow', 'nah', 7000, (0, 0, 1920, 820), 'Spielsicht, Standardzoom, fünf Humvees mit TOW'),
    ('04_nah_oben', 'nah', 7150, (500, 330, 720, 450), 'nah, Standardneigung'),
    ('05_seite', 'nah', 7350, (250, 300, 1250, 420), 'flach von der Seite'),
    ('06_gefecht', 'fahrt', 8350, (300, 100, 800, 400), 'Gefecht: Turm gedreht, Raketen'),
    ('07_gefecht_beschaedigt', 'beschaedigt', 8520, (560, 200, 800, 450), 'Gefecht: schwer beschädigt (AvHummer_D)'),
    ('08_fahrt', 'fahrt', 7600, (0, 150, 1100, 500), 'Kolonne in Fahrt'),
    ('09_teamfarbe_gruen', 'farben', 7000, (450, 280, 600, 400), 'Teamfarbe grün (Laser-General, Player_1)'),
    ('10_teamfarbe_blau', 'farben', 7050, (500, 150, 800, 450), 'Teamfarbe blau (Laser-General, Player_2)'),
]

# Blender-Nahaufnahmen: (Stamm, Beschreibung, {Fassung: Bildpräfix in ansicht/})
BLENDER = [('blender_%s' % n, 'Blender, ' + n, {'ea': 'ea_' + n, 'neu': 'export_' + n, 'neu2': 'neu2_' + n})
           for n in ('vorne', 'hinten', 'seite', 'oben', 'rts')]
BLENDER += [('blender_d_%s' % n, 'Blender, beschädigt, ' + n, {'ea': 'ead_' + n, 'neu2': 'neu2d_' + n})
            for n in ('vorne', 'rts')]
BLENDER += [('feuer_seite', 'Blender mit Mündungsfeuer (MUZZLEFX01), Seite',
             {'ea': 'feuer_ea_seite', 'neu': 'feuer_neu_seite', 'neu2': 'feuer_neu2_seite'})]


def bild(pfad):
    return Image.open(pfad).convert('RGB')


def jpg(quelle, ziel):
    if not os.path.exists(ziel) or os.path.getmtime(ziel) < os.path.getmtime(quelle):
        bild(quelle).save(ziel, quality=92)


def main():
    os.makedirs(os.path.join(WURZEL, 'bilder'), exist_ok=True)
    os.makedirs(os.path.join(WURZEL, 'einzelbilder'), exist_ok=True)
    teile = []
    for szene, titel, text in SZENEN:
        ordner = {v: os.path.join(WURZEL, '%s-%s' % (szene, v)) for v, _ in FASSUNGEN}
        da = [v for v, _ in FASSUNGEN if os.path.isdir(ordner[v])]
        if len(da) < 2:
            continue
        basis = ordner[da[-1]]
        shots = sorted(f for f in os.listdir(basis) if f.startswith('shot_') and f.endswith('.bmp'))
        figs = []
        for f in shots:
            stamm = '%s_%s' % (szene, f[5:11])
            vs = {}
            for v in da:
                q = os.path.join(ordner[v], f)
                if os.path.exists(q):
                    jpg(q, os.path.join(WURZEL, 'bilder', '%s_%s.jpg' % (stamm, v)))
                    vs[v] = 'bilder/%s_%s.jpg' % (stamm, v)
            figs.append((stamm, 'Frame %d' % int(f[5:11]), vs))
        teile.append((titel, text, figs))
    figs = []
    for stamm, unter, quellen in BLENDER:
        vs = {}
        for v, praefix in quellen.items():
            q = os.path.join(ARBEIT, 'ansicht', praefix + '.png')
            if os.path.exists(q):
                jpg(q, os.path.join(WURZEL, 'bilder', '%s_%s.jpg' % (stamm, v)))
                vs[v] = 'bilder/%s_%s.jpg' % (stamm, v)
        if len(vs) >= 2:
            figs.append((stamm, unter, vs))
    if figs:
        teile.append(('Nahaufnahme in Blender', 'Das exportierte W3D bzw. das EA-Original, mit dem Add-on io_mesh_w3d '
                      'geladen, ohne Spielbeleuchtung (Workbench, Texturfarbe). Pilot 1 und EA: Teamfarbenflächen '
                      'erscheinen weiß; nachgebessert: Vorschau der Teamfarbe in Blau, wie die Engine sie einfärbt.',
                      figs))
    einzel = []
    for name, szene, frame, (x, y, b, h), txt in EINZEL:
        bilder = []
        for v, etikett in FASSUNGEN:
            p = os.path.join(WURZEL, '%s-%s' % (szene, v), 'shot_%06d.bmp' % frame)
            if os.path.exists(p):
                ib = bild(p).crop((x, y, x + b, y + h))
                ib.save(os.path.join(WURZEL, 'einzelbilder', '%s_%s.png' % (name, v)))
                bilder.append((etikett, ib))
        if len(bilder) < 2:
            continue
        nb = Image.new('RGB', (len(bilder) * (b + 12) - 12, h + 34), (20, 20, 20))
        d = ImageDraw.Draw(nb)
        for i, (etikett, ib) in enumerate(bilder):
            nb.paste(ib, (i * (b + 12), 34))
            d.text((i * (b + 12) + 8, 10), etikett + ('  --  ' + txt if i == 0 else ''), fill=(255, 255, 255))
        nb.save(os.path.join(WURZEL, 'einzelbilder', name + '_nebeneinander.png'))
        einzel.append((name, txt, [v for v, _ in FASSUNGEN
                                   if os.path.exists(os.path.join(WURZEL, 'einzelbilder', '%s_%s.png' % (name, v)))]))
    schreibe_seite(teile, einzel)


SEITE = r'''<!doctype html>
<html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Humvee-Pilot</title>
<style>
:root { --bg:#16181b; --fl:#202328; --tx:#e6e6e6; --mu:#9aa0a6; --ak:#6fb3ff; --li:#33373d; }
@media (prefers-color-scheme: light) { :root { --bg:#f4f4f2; --fl:#fff; --tx:#1c1d1f; --mu:#5f6368; --ak:#1a5fb4; --li:#d5d5d0; } }
* { box-sizing:border-box; }
body { margin:0; background:var(--bg); color:var(--tx); font:15px/1.5 system-ui, "Segoe UI", sans-serif; }
main { max-width:2400px; margin:0 auto; padding:16px; }
h1 { font-size:22px; margin:8px 0 4px; } h2 { font-size:18px; margin:30px 0 6px; border-bottom:1px solid var(--li); padding-bottom:4px; }
.mu { color:var(--mu); }
.leiste { position:sticky; top:0; z-index:5; background:var(--bg); padding:8px 0; display:flex; flex-wrap:wrap; gap:8px; align-items:center; border-bottom:1px solid var(--li); }
button { background:var(--fl); color:var(--tx); border:1px solid var(--li); border-radius:6px; padding:4px 10px; cursor:pointer; font:inherit; font-size:14px; }
button.an { border-color:var(--ak); color:var(--ak); font-weight:600; }
figure { margin:10px 0; background:var(--fl); border:1px solid var(--li); border-radius:6px; padding:6px; }
figcaption { font-size:13px; color:var(--mu); margin:4px 2px; }
.reihe { display:grid; grid-template-columns:1fr; gap:6px; }
body.alle .reihe { grid-template-columns:repeat(auto-fit, minmax(300px, 1fr)); }
body:not(.alle) .spalte { display:none; }
body:not(.alle) .spalte.gewaehlt { display:block; }
.bildrahmen { position:relative; overflow:hidden; cursor:crosshair; }
.bildrahmen img { display:block; width:100%; height:auto; }
.etikett { position:absolute; top:6px; left:6px; background:#000b; color:#fff; font-size:13px; padding:1px 7px; border-radius:4px; pointer-events:none; }
.fehlt { padding:40px; text-align:center; color:var(--mu); border:1px dashed var(--li); }
.lupe { position:absolute; width:320px; height:320px; border:2px solid #fff; border-radius:50%; box-shadow:0 2px 10px #000a;
        pointer-events:none; display:none; background-repeat:no-repeat; }
.einzel a { color:var(--ak); }
</style></head><body><main>
<h1>Humvee-Pilot: EA, Pilot 1 und nachgebessert (KI-3D-Modell, Meshy)</h1>
<p class="mu">1920x1080, D3D11 mit fxaa, sharpen, softparticles, colorgrade, ssao,
shadowmap=4096, Texturstufe SD, -gamma 50. Alle Fassungen aus derselben Exe und demselben Replay bzw. Seed -- die Szene
ist Bild für Bild dieselbe. Nachgebessert: Kontrast und Kanten aus dem feinen Modell, Farbton heller Sand wie EA, helle
Felgen, Teamfarbe großflächig (Haube, Türen, Turmschilde), beschädigte Fassung aus dem Meshy-Modell.</p>
<div class="leiste">
  __KNOEPFE__ <span class="mu">Leertaste: weiter</span>
  <button id="bAlle">alle nebeneinander</button>
  <span class="mu">Lupe:</span><button data-z="0">aus</button><button data-z="2" class="an">2x</button><button data-z="4">4x</button>
</div>
__INHALT__
<h2>Einzelbilder</h2>
<p class="mu">1:1-Ausschnitte im Ordner <code>einzelbilder/</code>: je Fassung (<code>_ea</code>, <code>_neu</code> = Pilot 1,
<code>_neu2</code> = nachgebessert) und alle nebeneinander.</p>
<ul class="einzel">__EINZEL__</ul>
</main>
<script>
const FASSUNGEN = __FASSUNGEN__;
let fassung = FASSUNGEN[FASSUNGEN.length - 1][0], zoom = 2;
function setzen(f) {
  fassung = f;
  document.querySelectorAll('.spalte').forEach(s => s.classList.toggle('gewaehlt', s.dataset.f === f));
  document.querySelectorAll('button[data-f]').forEach(b => b.classList.toggle('an', b.dataset.f === f));
}
document.querySelectorAll('button[data-f]').forEach(b => b.onclick = () => setzen(b.dataset.f));
document.getElementById('bAlle').onclick = e => { document.body.classList.toggle('alle'); e.target.classList.toggle('an'); };
document.addEventListener('keydown', e => { if (e.code === 'Space') { e.preventDefault();
  const i = FASSUNGEN.findIndex(x => x[0] === fassung); setzen(FASSUNGEN[(i + 1) % FASSUNGEN.length][0]); } });
document.querySelectorAll('button[data-z]').forEach(b => b.onclick = () => {
  zoom = +b.dataset.z; document.querySelectorAll('button[data-z]').forEach(x => x.classList.toggle('an', x === b)); });
document.querySelectorAll('.bildrahmen').forEach(r => {
  const img = r.querySelector('img'), lupe = r.querySelector('.lupe');
  r.addEventListener('mousemove', e => {
    if (!zoom) { lupe.style.display = 'none'; return; }
    const b = r.getBoundingClientRect(), x = e.clientX - b.left, y = e.clientY - b.top;
    lupe.style.display = 'block';
    lupe.style.left = (x - 160) + 'px'; lupe.style.top = (y - 160) + 'px';
    lupe.style.backgroundImage = 'url(' + img.src + ')';
    lupe.style.backgroundSize = (b.width * zoom) + 'px ' + (b.height * zoom) + 'px';
    lupe.style.backgroundPosition = (160 - x * zoom) + 'px ' + (160 - y * zoom) + 'px';
  });
  r.addEventListener('mouseleave', () => lupe.style.display = 'none');
});
setzen(fassung);
</script></body></html>
'''


def schreibe_seite(teile, einzel):
    out = []
    for titel, text, figs in teile:
        out.append('<h2>%s</h2><p class="mu">%s</p>' % (html.escape(titel), html.escape(text)))
        for stamm, unter, vs in figs:
            spalten = []
            for v, etikett in FASSUNGEN:
                if v in vs:
                    spalten.append('<div class="spalte" data-f="%s"><div class="bildrahmen"><img loading="lazy" src="%s">'
                                   '<span class="etikett">%s</span><div class="lupe"></div></div></div>'
                                   % (v, vs[v], html.escape(etikett)))
                else:
                    spalten.append('<div class="spalte" data-f="%s"><div class="fehlt">%s: nicht aufgenommen</div></div>'
                                   % (v, html.escape(etikett)))
            out.append('<figure><figcaption>%s</figcaption><div class="reihe">%s</div></figure>'
                       % (html.escape(unter), ''.join(spalten)))
    knoepfe = ''.join('<button data-f="%s">%s</button>' % (v, html.escape(e)) for v, e in FASSUNGEN)
    liste = '\n'.join('<li><a href="einzelbilder/%s_nebeneinander.png">%s</a> (%s) -- %s</li>' % (
        n, n, ', '.join('<a href="einzelbilder/%s_%s.png">%s</a>' % (n, v, dict(FASSUNGEN)[v]) for v in vs),
        html.escape(t)) for n, t, vs in einzel)
    import json
    seite = (SEITE.replace('__INHALT__', '\n'.join(out)).replace('__EINZEL__', liste)
             .replace('__KNOEPFE__', knoepfe).replace('__FASSUNGEN__', json.dumps(FASSUNGEN)))
    with open(os.path.join(WURZEL, 'index.html'), 'w', encoding='utf-8') as f:
        f.write(seite)
    print('geschrieben', os.path.join(WURZEL, 'index.html'), sum(len(t[2]) for t in teile), 'Vergleiche,',
          len(einzel), 'Einzelbilder')


if __name__ == '__main__':
    main()
