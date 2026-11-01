# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Vergleichsseite EA gegen CC0-Scan für die Naturobjekte -- lokal, nicht versioniert.
#
#   python tools/assets/blender/natur_vergleich.py <render-out/cc0-objekte> <arbeit>
#
# Erwartet <render-out/cc0-objekte>/laeufe/<lauf>/{ea,neu}/shot_*.bmp (tools/render/run_render_test.py, EA ohne und neu
# mit -mod) und die Blender-Ansichten in <arbeit>/<objekt>/ansicht/. Schreibt index.html (Umschalten EA/neu mit
# Knopf oder Leertaste, nebeneinander, Lupe 2x/4x, Helligkeitsregler), bilder/ (JPG) und einzelbilder/ (Ausschnitte
# je Fassung und nebeneinander).
import html
import json
import os
import sys

from PIL import Image, ImageDraw

WURZEL, ARBEIT = sys.argv[1], sys.argv[2]
FASSUNGEN = [('ea', 'EA'), ('neu', 'CC0-Scan')]
# Runde 2 (Auflösungsstufen): Spalten der Stufenvergleiche
ALLE_SPALTEN = [('ea', 'EA SD'), ('neu', 'neu Runde 1 (SD 256²)'), ('neu_sd', 'neu SD 256²'), ('ea_hd', 'EA HD'),
                ('neu_hd', 'neu HD 1024²'), ('neu_uhd', 'neu 2048² (Test unter HD)')]
# (Name, Titel, Text, {Spalte: Ordner unter laeufe/})
STUFEN_LAEUFE = [
    ('st_tl', 'Auflösungsstufen: Tournament Lake (1920x1080)',
     'Fahrt cc0_natur.txt, je Stufe ein eigenes -mod-Archiv: SD mit -textureQuality sd, HD und der 2048er-Test mit '
     '-textureQuality hd (EA-HD-Stufe Texturstufen\\HD darunter). Hauptvergleich: EA HD gegen neu HD. Kiefer: SD 256², '
     'HD 1280² (ZHTS2), Test 2560² (ZHTS4). Felsen neu gepackt (UV-Nutzung 0,69 statt 0,43).',
     {'ea': 'tl_natur/ea', 'neu_sd': 'tl_natur/neu2', 'ea_hd': 'hd_tl_natur/ea', 'neu_hd': 'hd_tl_natur/neu',
      'neu_uhd': 'hd_tl_natur/uhdtest'}),
    ('st_shell', 'Auflösungsstufen: Shell-Map nah (1920x1080)',
     'Fahrt cc0_shell.txt; Fass SD 128², HD 512², Test 1024²; Felsgruppe wie oben.',
     {'ea': 'shell_nah/ea', 'neu_sd': 'shell_nah/neu', 'ea_hd': 'hd_shell_nah/ea', 'neu_hd': 'hd_shell_nah/neu',
      'neu_uhd': 'hd_shell_nah/uhdtest'}),
]

LAEUFE = [
    ('tl_natur', 'Tournament Lake: Felsen und Kiefern',
     'Replay e0zh-tournament-lake-3ai-seed2 (Zero Hour x64), Fahrt tests/render/paths/cc0_natur.txt: Felsgruppe '
     '(Rocks1, Rocks3, RockClusterMedium01) in Spielsicht, näher, ganz nah und gedreht; Kiefernwald (TreePine, '
     'TreePine2) ebenso, zuletzt flacher geneigt; zweite Felsgruppe.'),
    ('tl_wind', 'Kiefern im Wind', 'Bilder alle 4 Frames bei nahem Zoom: der Wind lenkt die Kronen aus.'),
    ('tl_start', 'Tournament Lake: Standardansicht beim Start',
     'Skirmish Tournament Lake (3 KI, Seed 1), Frames 300 und 900, Standardkamera am Startpunkt.'),
    ('shell', 'Shell-Map (Hauptmenü Zero Hour)',
     'Hintergrundkarte des Hauptmenüs (ShellMapMD) mit Fässern (55GallonDrum) und Felsgruppen (RockClusterMedium01).'),
    ('shell_nah', 'Shell-Map: Fässer und Felsen nah', 'Kamerafahrt tests/render/paths/cc0_shell.txt über der Shell-Map.'),
    ('wald_spaet', 'Wälder später im Spiel',
     'Replay e0zh-tournament-lake-3ai-seed2, Frames 3000 bis 12000 alle 300 Frames über zwei Wäldern zwischen den '
     'Basen (Fahrt cc0_wald_spaet.txt) -- gesucht wurden umgefallene Kiefern; keine gefunden.'),
    ('alpine', 'Alpine Assault (Generals): Kiefern und Felsen',
     'Generals (GZHNeo.exe), Replay e0-alpine-assault-2ai-seed1, Fahrt cc0_alpine.txt: Kiefern (in Generals W3DModelDraw, Textur aus dem Modell) und Rocks1/Rocks3.'),
]

# Einzelbilder: (Name, Lauf, Frame, Ausschnitt x, y, b, h im 1920x1080-Bild, Beschreibung) -- Liste in einzelbilder.json
EINZEL_DATEI = os.path.join(WURZEL, 'einzelbilder.json')

BLENDER = [
    ('PMRocks01', 'Rocks1 (PMRocks01), Blender', 'pmrocks01'),
    ('PMRocks03', 'Rocks3 (PMRocks03), Blender', 'pmrocks03'),
    ('PMmcluster', 'RockClusterMedium01 (PMmcluster), Blender', 'pmmcluster'),
    ('PMgaldrum', '55GallonDrum (PMgaldrum), Blender', 'pmgaldrum'),
]


def bild(pfad):
    return Image.open(pfad).convert('RGB')


def jpg(quelle, ziel, zuschnitt=None):
    if not os.path.exists(ziel) or os.path.getmtime(ziel) < os.path.getmtime(quelle):
        b = bild(quelle)
        if zuschnitt:
            b = b.crop(zuschnitt)
        b.save(ziel, quality=92)


def main():
    os.makedirs(os.path.join(WURZEL, 'bilder'), exist_ok=True)
    os.makedirs(os.path.join(WURZEL, 'einzelbilder'), exist_ok=True)
    teile = []
    for lauf, titel, text in LAEUFE:
        ordner = {v: os.path.join(WURZEL, 'laeufe', lauf, v) for v, _ in FASSUNGEN}
        if not all(os.path.isdir(o) for o in ordner.values()):
            continue
        shots = sorted(f for f in os.listdir(ordner['neu']) if f.startswith('shot_') and f.endswith('.bmp'))
        figs = []
        for f in shots:
            stamm = '%s_%s' % (lauf, f[5:11])
            vs = {}
            for v, _ in FASSUNGEN:
                q = os.path.join(ordner[v], f)
                if os.path.exists(q):
                    jpg(q, os.path.join(WURZEL, 'bilder', '%s_%s.jpg' % (stamm, v)))
                    vs[v] = 'bilder/%s_%s.jpg' % (stamm, v)
            figs.append((stamm, 'Frame %d' % int(f[5:11]), vs))
        teile.append((titel, text, figs))
    for lauf, titel, text, spalten in STUFEN_LAEUFE:
        ordner = {v: os.path.join(WURZEL, 'laeufe', o) for v, o in spalten.items()
                  if os.path.isdir(os.path.join(WURZEL, 'laeufe', o))}
        if 'neu_hd' not in ordner:
            continue
        shots = sorted(f for f in os.listdir(ordner['neu_hd']) if f.startswith('shot_') and f.endswith('.bmp'))
        figs = []
        for f in shots:
            stamm = '%s_%s' % (lauf, f[5:11])
            vs = {}
            for v, o in ordner.items():
                q = os.path.join(o, f)
                if os.path.exists(q):
                    jpg(q, os.path.join(WURZEL, 'bilder', '%s_%s.jpg' % (stamm, v)))
                    vs[v] = 'bilder/%s_%s.jpg' % (stamm, v)
            figs.append((stamm, 'Frame %d' % int(f[5:11]), vs))
        teile.append((titel, text, figs))
    figs = []
    for objekt, unter, praefix in BLENDER:
        for stufe in ('hd', 'uhd'):
            q = os.path.join(ARBEIT, objekt, 'ansicht', '%s_%s_schraeg.png' % (praefix, stufe))
            if os.path.exists(q):
                b = bild(q)
                w = b.size[0] // 2
                jpg(q, os.path.join(WURZEL, 'bilder', 'bl_%s_schraeg_neu_%s.jpg' % (objekt, stufe)),
                    (w, 0, 2 * w, b.size[1]))
        for ansicht in ('schraeg', 'seite', 'rts'):
            q = os.path.join(ARBEIT, objekt, 'ansicht', '%s_%s.png' % (praefix, ansicht))
            if os.path.exists(q):
                b = bild(q)
                w = b.size[0] // 2
                # objekt_ersetzen.py ansicht: EA links, neu rechts in einem Bild
                for v, box in (('ea', (0, 0, w, b.size[1])), ('neu', (w, 0, 2 * w, b.size[1]))):
                    jpg(q, os.path.join(WURZEL, 'bilder', 'bl_%s_%s_%s.jpg' % (objekt, ansicht, v)), box)
                vs = {v: 'bilder/bl_%s_%s_%s.jpg' % (objekt, ansicht, v) for v, _ in FASSUNGEN}
                vs['neu_sd'] = vs.pop('neu')        # Blender-Ansichten zeigen den aktuellen Stand (Runde 2)
                if ansicht == 'schraeg':
                    for stufe in ('hd', 'uhd'):
                        if os.path.exists(os.path.join(WURZEL, 'bilder', 'bl_%s_schraeg_neu_%s.jpg' % (objekt, stufe))):
                            vs['neu_' + stufe] = 'bilder/bl_%s_schraeg_neu_%s.jpg' % (objekt, stufe)
                figs.append(('bl_%s_%s' % (objekt, ansicht), '%s, %s' % (unter, ansicht), vs))
    for ansicht in ('rts', 'seite'):
        vs = {}
        for v, _ in FASSUNGEN:
            q = os.path.join(ARBEIT, 'PTPine01', 'ansicht', '%s_%s.png' % (v, ansicht))
            if os.path.exists(q):
                jpg(q, os.path.join(WURZEL, 'bilder', 'bl_baum_%s_%s.jpg' % (ansicht, v)))
                vs[v] = 'bilder/bl_baum_%s_%s.jpg' % (ansicht, v)
        if vs:
            figs.append(('bl_baum_%s' % ansicht, 'Kiefer (PTPine01), Blender/Cycles, %s' % ansicht, vs))
    if figs:
        teile.append(('Modelle in Blender', 'Das exportierte W3D und das EA-Original, mit dem Add-on io_mesh_w3d geladen, '
                      'ohne Spielbeleuchtung (Felsen und Fass: Workbench mit Texturfarbe; Kiefer: Cycles mit Alphatest '
                      'und Sonne).', figs))
    einzel = []
    for eintrag in (json.load(open(EINZEL_DATEI, encoding='utf-8')) if os.path.exists(EINZEL_DATEI) else []):
        name, lauf, frame, (x, y, b, h), txt = eintrag[:5]
        # Runde 2: optional {Spalte: Ordner unter laeufe/} statt <lauf>/ea und <lauf>/neu
        spalten = eintrag[5] if len(eintrag) > 5 else {v: lauf + '/' + v for v, _ in FASSUNGEN}
        etiketten = dict(ALLE_SPALTEN) if len(eintrag) > 5 else dict(FASSUNGEN)
        bilder = []
        for v, o in spalten.items():
            etikett = etiketten[v]
            p = os.path.join(WURZEL, 'laeufe', o, 'shot_%06d.bmp' % frame)
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
        einzel.append((name, txt, list(spalten)))
    schreibe_seite(teile, einzel)


SEITE = r'''<!doctype html>
<html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>CC0-Naturobjekte</title>
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
.bildrahmen img { display:block; width:100%; height:auto; filter:brightness(var(--hell, 1)); }
.etikett { position:absolute; top:6px; left:6px; background:#000b; color:#fff; font-size:13px; padding:1px 7px; border-radius:4px; pointer-events:none; }
.lupe { position:absolute; width:320px; height:320px; border:2px solid #fff; border-radius:50%; box-shadow:0 2px 10px #000a;
        pointer-events:none; display:none; background-repeat:no-repeat; filter:brightness(var(--hell, 1)); }
.einzel a { color:var(--ak); }
input[type=range] { width:160px; }
</style></head><body><main>
<h1>CC0-Naturobjekte: EA gegen Poly-Haven-Scan</h1>
<p class="mu">1920x1080, D3D11 mit fxaa, sharpen, softparticles, colorgrade, ssao,
shadowmap=4096, Texturstufe SD, -gamma 50. EA und neu mit derselben Exe und demselben Replay bzw. Seed, neu mit
<code>-mod &lt;GZH_WERKZEUGE&gt;/cc0/mod</code> (Felsen Rocks1/Rocks3/RockClusterMedium01, Kiefer TreePine/TreePine2,
Fass 55GallonDrum). Die Szene ist Bild für Bild dieselbe; nur die Modelle unterscheiden sich.</p>
<div class="leiste">
  __KNOEPFE__ <span class="mu">Leertaste: umschalten</span>
  <button id="bAlle">nebeneinander</button>
  <span class="mu">Lupe:</span><button data-z="0">aus</button><button data-z="2" class="an">2x</button><button data-z="4">4x</button>
  <span class="mu">Helligkeit:</span><input id="hell" type="range" min="0.6" max="2" step="0.05" value="1"><span id="hellw" class="mu">1,00</span>
</div>
__INHALT__
<h2>Einzelbilder</h2>
<p class="mu">1:1-Ausschnitte im Ordner <code>einzelbilder/</code>: je Fassung (<code>_ea</code>, <code>_neu</code>) und nebeneinander.</p>
<ul class="einzel">__EINZEL__
<li>Wind: <a href="einzelbilder/14_wind_ea.gif">14_wind_ea.gif</a>, <a href="einzelbilder/14_wind_neu.gif">14_wind_neu.gif</a>
-- Bildfolge alle 4 Frames (Fahrt cc0_baum.txt), die Kronen schwanken im Wind (Vertex-Shader Trees.vs)</li></ul>
</main>
<script>
const FASSUNGEN = __FASSUNGEN__;
let fassung = 'neu_hd', zoom = 2;
function setzen(f) {
  fassung = f;
  document.querySelectorAll('.spalte').forEach(s => s.classList.toggle('gewaehlt', s.dataset.f === f));
  document.querySelectorAll('button[data-f]').forEach(b => b.classList.toggle('an', b.dataset.f === f));
  // Abbildungen ohne diese Fassung: die nächstliegende Spalte zeigen
  const ersatz = {neu_hd: ['neu_sd', 'neu'], neu_uhd: ['neu_hd', 'neu_sd', 'neu'], neu_sd: ['neu'], ea_hd: ['ea'], neu: ['neu_sd']};
  document.querySelectorAll('.reihe').forEach(r => {
    if (r.querySelector('.spalte.gewaehlt')) return;
    for (const e of (ersatz[f] || [])) { const s = r.querySelector('.spalte[data-f="' + e + '"]'); if (s) { s.classList.add('gewaehlt'); return; } }
    const s = r.querySelector('.spalte'); if (s) s.classList.add('gewaehlt');
  });
}
document.querySelectorAll('button[data-f]').forEach(b => b.onclick = () => setzen(b.dataset.f));
document.getElementById('bAlle').onclick = e => { document.body.classList.toggle('alle'); e.target.classList.toggle('an'); };
document.addEventListener('keydown', e => { if (e.code === 'Space') { e.preventDefault();
  const i = FASSUNGEN.findIndex(x => x[0] === fassung); setzen(FASSUNGEN[(i + 1) % FASSUNGEN.length][0]); } });
document.querySelectorAll('button[data-z]').forEach(b => b.onclick = () => {
  zoom = +b.dataset.z; document.querySelectorAll('button[data-z]').forEach(x => x.classList.toggle('an', x === b)); });
const hell = document.getElementById('hell');
hell.oninput = () => { document.body.style.setProperty('--hell', hell.value);
  document.getElementById('hellw').textContent = (+hell.value).toFixed(2).replace('.', ','); };
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
            for v, etikett in ALLE_SPALTEN:
                if v in vs:
                    spalten.append('<div class="spalte" data-f="%s"><div class="bildrahmen"><img loading="lazy" src="%s">'
                                   '<span class="etikett">%s</span><div class="lupe"></div></div></div>'
                                   % (v, vs[v], html.escape(etikett)))
            out.append('<figure><figcaption>%s</figcaption><div class="reihe">%s</div></figure>'
                       % (html.escape(unter), ''.join(spalten)))
    knoepfe = ''.join('<button data-f="%s">%s</button>' % (v, html.escape(e)) for v, e in ALLE_SPALTEN)
    liste = '\n'.join('<li><a href="einzelbilder/%s_nebeneinander.png">%s</a> (einzeln: %s) -- %s</li>' % (
        n, n, ', '.join('<a href="einzelbilder/%s_%s.png">%s</a>' % (n, v, v) for v in vs), html.escape(t))
        for n, t, vs in einzel)
    seite = (SEITE.replace('__INHALT__', '\n'.join(out)).replace('__EINZEL__', liste)
             .replace('__KNOEPFE__', knoepfe).replace('__FASSUNGEN__', json.dumps(ALLE_SPALTEN)))
    with open(os.path.join(WURZEL, 'index.html'), 'w', encoding='utf-8') as f:
        f.write(seite)
    print('geschrieben', os.path.join(WURZEL, 'index.html'), sum(len(t[2]) for t in teile), 'Vergleiche,',
          len(einzel), 'Einzelbilder')


if __name__ == '__main__':
    main()
