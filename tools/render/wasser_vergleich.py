#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Vergleichsseite fuer das Wasser aus den Aufnahmen von wasser_aufnahmen.py.

  python tools/render/wasser_vergleich.py [--raw render-out/wasser2/raw] [--out render-out/wasser2] [--alt alt] [--neu neu]
      [--extra stufe1=Mittel ...]

Schreibt:
  <out>/einzelbilder/<szene>_<frame>_alt.png und _neu.png (zum Nebeneinanderhalten),
  <out>/bewegung_alt.gif, bewegung_neu.gif (aus der Szene g-bewegung, Ausschnitt am Ufer),
  <out>/helligkeit.csv und <out>/index.html: je Bild Umschalten alt/neu (Leertaste), Lupe 2x/4x (Taste L),
  nebeneinander (Taste N), Helligkeitstabelle.

Helligkeit: mittlere Luminanz (Rec. 709, 0..255) des ganzen Bildes, der geaenderten Flaeche (Pixel mit einer
Abweichung ueber 8 in einem Kanal, um 6 Pixel erweitert -- praktisch das Wasser) und des Rests. Aendert sich der Rest,
wirkt die Aenderung ueber das Wasser hinaus (auch schwache, flaechige Verschiebungen unter der Schwelle fallen dort auf).
"""
import argparse
import csv
import glob
import html
import os

import numpy as np
from PIL import Image, ImageFilter

TEXT_ALT = "alt"
TEXT_NEU = "neu"
WURZEL = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TITEL = {
    "g-see": "Generals, Tournament Lake (Kratersee)",
    "z-see": "Zero Hour, Tournament Lake (Kratersee)",
    "g-nacht": "Generals, Silent River bei Nacht",
    "z-nacht": "Zero Hour, Silent River bei Nacht",
    "g-fluss": "Generals, Heartland Shield (Fluss)",
    "g-shell": "Generals, Shell-Map",
    "z-shell": "Zero Hour, Shell-Map (Meer)",
}


def lum(a):
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


def messen(alt, neu):
    a = np.asarray(alt, dtype=np.float32)
    n = np.asarray(neu, dtype=np.float32)
    diff = np.abs(a - n).max(axis=-1) > 8
    maske = np.asarray(Image.fromarray((diff * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(13))) > 0
    la, ln = lum(a), lum(n)
    rest = ~maske
    def m(x, k):
        return float(x[k].mean()) if k.any() else float("nan")
    return {
        "bild_alt": float(la.mean()), "bild_neu": float(ln.mean()),
        "wasser_alt": m(la, maske), "wasser_neu": m(ln, maske),
        "rest_alt": m(la, rest), "rest_neu": m(ln, rest),
        "anteil_geaendert": float(maske.mean()),
    }


def prozent(a, b):
    return (b - a) / a * 100.0 if a and a == a and b == b else float("nan")


def gif(bilder, ziel, ausschnitt):
    rahmen = [Image.open(b).convert("RGB").crop(ausschnitt).resize(((ausschnitt[2] - ausschnitt[0]) // 2 * 2 // 2,
              (ausschnitt[3] - ausschnitt[1]) // 2)) for b in bilder]
    rahmen = [r.convert("P", palette=Image.ADAPTIVE, colors=255) for r in rahmen]
    rahmen[0].save(ziel, save_all=True, append_images=rahmen[1:], duration=66, loop=0, optimize=False)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--raw", default=os.path.join(WURZEL, "render-out", "wasser2", "raw"))
    ap.add_argument("--out", default=os.path.join(WURZEL, "render-out", "wasser2"))
    ap.add_argument("--alt", default="alt")
    ap.add_argument("--neu", default="neu")
    ap.add_argument("--varianten", nargs="*", default=[], help="weitere Stufen label=Beschriftung (z. B. stufe1=Mittel)")
    ap.add_argument("--referenz", help="Ordner mit Referenzfotos (CC0/gemeinfrei) samt QUELLEN.md; Auswahl --referenz-bilder")
    ap.add_argument("--referenz-bilder", nargs="*", default=[])
    ap.add_argument("--beschriftung", default="alt=alt,neu=neu", help="Knopftexte der beiden Hauptfassungen")
    ap.add_argument("--gif", default="1100,40,1900,680", help="Ausschnitt x0,y0,x1,y1 der Bewegungsbilder")
    a = ap.parse_args()
    einzel = os.path.join(a.out, "einzelbilder")
    os.makedirs(einzel, exist_ok=True)
    varianten = [v.split("=", 1) for v in a.varianten]
    global TEXT_ALT, TEXT_NEU
    texte = dict(t.split("=", 1) for t in a.beschriftung.split(","))
    TEXT_ALT, TEXT_NEU = html.escape(texte.get("alt", "alt")), html.escape(texte.get("neu", "neu"))

    zeilen = []
    abschnitte = []
    for szene in TITEL:
        alt_dir = os.path.join(a.raw, a.alt, szene)
        neu_dir = os.path.join(a.raw, a.neu, szene)
        bilder = sorted(os.path.basename(p) for p in glob.glob(os.path.join(neu_dir, "shot_*.bmp")))
        bilder = [b for b in bilder if os.path.exists(os.path.join(alt_dir, b))]
        if not bilder:
            continue
        karten = []
        for b in bilder:
            frame = int(b[5:-4])
            name = "%s_%04d" % (szene, frame)
            alt = Image.open(os.path.join(alt_dir, b)).convert("RGB")
            neu = Image.open(os.path.join(neu_dir, b)).convert("RGB")
            alt.save(os.path.join(einzel, name + "_alt.png"))
            neu.save(os.path.join(einzel, name + "_neu.png"))
            extra = []
            for label, text in varianten:
                p = os.path.join(a.raw, label, szene, b)
                if os.path.exists(p):
                    Image.open(p).convert("RGB").save(os.path.join(einzel, "%s_%s.png" % (name, label)))
                    extra.append((label, text))
            w = messen(alt, neu)
            w.update(szene=szene, frame=frame)
            zeilen.append(w)
            karten.append((name, frame, w, extra))
        abschnitte.append((szene, karten))

    with open(os.path.join(a.out, "helligkeit.csv"), "w", newline="", encoding="utf-8") as f:
        feld = ["szene", "frame", "bild_alt", "bild_neu", "wasser_alt", "wasser_neu", "rest_alt", "rest_neu", "anteil_geaendert"]
        wr = csv.DictWriter(f, fieldnames=feld)
        wr.writeheader()
        for z in zeilen:
            wr.writerow({k: (round(z[k], 3) if isinstance(z[k], float) else z[k]) for k in feld})

    # Bewegung
    gifs = []
    x0, y0, x1, y1 = (int(v) for v in a.gif.split(","))
    for label in [a.alt, a.neu] + [l for l, _ in varianten]:
        fs = sorted(glob.glob(os.path.join(a.raw, label, "g-bewegung", "shot_*.bmp")))
        if fs:
            ziel = "bewegung_%s.gif" % label
            gif(fs, os.path.join(a.out, ziel), (x0, y0, x1, y1))
            gifs.append((label, ziel))

    # Seite
    teile = []
    tabelle = ["<table><tr><th>Szene</th><th>Bilder</th><th>ganzes Bild</th><th>geaenderte Flaeche (Wasser)</th>"
               "<th>Rest</th><th>Anteil geaendert</th></tr>"]
    for szene, karten in abschnitte:
        ws = [k[2] for k in karten]
        mittel = lambda key: float(np.nanmean([w[key] for w in ws]))
        tabelle.append("<tr><td>%s</td><td>%d</td><td>%.1f &rarr; %.1f (%+.2f %%)</td><td>%.1f &rarr; %.1f (%+.1f %%)</td>"
                       "<td>%.1f &rarr; %.1f (%+.2f %%)</td><td>%.0f %%</td></tr>" % (
                           html.escape(TITEL[szene]), len(ws), mittel("bild_alt"), mittel("bild_neu"),
                           prozent(mittel("bild_alt"), mittel("bild_neu")), mittel("wasser_alt"), mittel("wasser_neu"),
                           prozent(mittel("wasser_alt"), mittel("wasser_neu")), mittel("rest_alt"), mittel("rest_neu"),
                           prozent(mittel("rest_alt"), mittel("rest_neu")), mittel("anteil_geaendert") * 100))
        karten_html = []
        for name, frame, w, extra in karten:
            knoepfe = "".join('<button data-v="%s">%s</button>' % (lab, html.escape(txt)) for lab, txt in extra)
            karten_html.append(
                ('<figure class="karte" data-name="%s"><div class="knoepfe"><button data-v="alt">' + TEXT_ALT + '</button>'
                '<button data-v="neu" class="an">' + TEXT_NEU + '</button>%s<span class="info">Frame %d &middot; Wasser %+.1f %% &middot; Rest %+.2f %%</span></div>'
                '<div class="bild"><img class="haupt" src="einzelbilder/%s_neu.png" loading="lazy">'
                '<img class="neben" src="einzelbilder/%s_alt.png" loading="lazy"></div></figure>') % (
                    name, knoepfe, frame, prozent(w["wasser_alt"], w["wasser_neu"]),
                    prozent(w["rest_alt"], w["rest_neu"]), name, name))
        teile.append('<section><h2>%s</h2>%s</section>' % (html.escape(TITEL[szene]), "".join(karten_html)))
    tabelle.append("</table>")
    gif_html = "".join('<figure><figcaption>%s</figcaption><img src="%s"></figure>' % (html.escape(l), g) for l, g in gifs)

    ref_html = ""
    if a.referenz and a.referenz_bilder:
        import shutil, re
        ziel = os.path.join(a.out, "referenz")
        os.makedirs(ziel, exist_ok=True)
        quellen = open(os.path.join(a.referenz, "QUELLEN.md"), encoding="utf-8").read()
        teile_r = []
        for name in a.referenz_bilder:
            shutil.copy(os.path.join(a.referenz, name), os.path.join(ziel, name))
            m = re.search(r"\| `" + re.escape(name) + r"` \| <([^>]+)> \| ([^|]*) \| ([^|]*) \|", quellen)
            quelle = ('<a href="%s">%s</a>, %s' % (m.group(1), html.escape(m.group(2).strip()), html.escape(m.group(3).strip()))) if m else ""
            teile_r.append('<figure><img src="referenz/%s" loading="lazy"><figcaption>%s<br>%s</figcaption></figure>' % (name, html.escape(name), quelle))
        ref_html = '<h2>Referenzfotos (CC0 / gemeinfrei, Wikimedia Commons)</h2><div class="gifs">' + "".join(teile_r) + "</div>"
    seite = SEITE.replace("%REFERENZ%", ref_html).replace("%TABELLE%", "".join(tabelle)).replace("%GIFS%", gif_html).replace("%TEILE%", "".join(teile))
    with open(os.path.join(a.out, "index.html"), "w", encoding="utf-8") as f:
        f.write(seite)
    print("Seite:", os.path.join(a.out, "index.html"), "|", len(zeilen), "Bildpaare")


SEITE = """<!doctype html>
<html lang="de"><head><meta charset="utf-8"><title>Wasser 2: alt und neu</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
:root { --bg:#f4f4f1; --fg:#1d1d1b; --mut:#6b6b66; --line:#d8d8d2; --acc:#1f6f8b; }
@media (prefers-color-scheme: dark) { :root { --bg:#161716; --fg:#e8e8e3; --mut:#9a9a93; --line:#33332f; --acc:#6cb6cf; } }
body { background:var(--bg); color:var(--fg); font:15px/1.45 system-ui,sans-serif; margin:0 auto; max-width:1500px; padding:16px; }
h1 { font-size:22px; margin:8px 0 4px; } h2 { font-size:18px; margin:28px 0 8px; border-bottom:1px solid var(--line); }
p, li { color:var(--mut); max-width:95ch; }
table { border-collapse:collapse; margin:8px 0 16px; font-variant-numeric:tabular-nums; }
td, th { border-bottom:1px solid var(--line); padding:4px 10px; text-align:left; }
.karte { margin:0 0 18px; } .knoepfe { display:flex; gap:6px; align-items:center; margin:4px 0; flex-wrap:wrap; }
button { background:transparent; color:var(--fg); border:1px solid var(--line); border-radius:4px; padding:3px 10px; cursor:pointer; }
button.an { border-color:var(--acc); color:var(--acc); font-weight:600; }
.info { color:var(--mut); font-size:13px; margin-left:8px; }
.bild { position:relative; display:flex; gap:6px; overflow:hidden; }
.bild img { width:100%; height:auto; display:block; }
.neben { display:none; }
body.nebeneinander .neben { display:block; } body.nebeneinander .bild img { width:50%; }
.lupe { position:fixed; width:360px; height:360px; border:2px solid var(--acc); border-radius:6px; pointer-events:none;
  background-repeat:no-repeat; display:none; z-index:9; box-shadow:0 2px 12px rgba(0,0,0,.4); }
.gifs { display:flex; gap:10px; flex-wrap:wrap; } .gifs img { max-width:100%; } .gifs figure { margin:0; max-width:460px; } .gifs figcaption { font-size:12px; color:var(--mut); }
.hilfe { position:sticky; top:0; background:var(--bg); padding:6px 0; border-bottom:1px solid var(--line); z-index:5; font-size:13px; }
</style></head><body>
<h1>Wasser (TODO 61): EA, Fassung 1, Fassung 2, Runde 3</h1>
<div class="hilfe"><b>Leertaste</b>: alle Bilder alt/neu umschalten &middot; <b>L</b>: Lupe aus/2x/4x (Maus ueber dem Bild)
&middot; <b>N</b>: nebeneinander (links neu, rechts alt) &middot; Knoepfe je Bild</div>
<p>Aufnahmen 1920x1080, Debug x64, Direct3D 11, <code>-fixedTimeStep</code>, Texturstufe SD, Detailstufe Hoch
(<code>-waterQuality 2</code>, D3D11-Effekte wie Hoch). EA = <code>-waterQuality 0</code>; Fassung 1 = origin/main vor dem Branch; Fassung 2 = zweite Runde; Runde 3 = nach
den Referenzfotos (Branch <code>wasser2</code>). Leertaste wechselt zwischen Fassung 1 und Runde 3. Einzelbilder als PNG unter <code>einzelbilder/</code>.</p>
<h2>Helligkeit</h2>
<p>Mittlere Luminanz (0..255). &bdquo;Geaenderte Flaeche&ldquo;: Pixel mit einer Abweichung ueber 8 in einem Kanal, um 6 Pixel
erweitert -- praktisch das Wasser. &bdquo;Rest&ldquo;: alles andere; dort zeigte sich jede flaechige Wirkung ueber das
Wasser hinaus.</p>
%TABELLE%
%REFERENZ%
<h2>Bewegung (feste Pose am Kratersee, 3 s)</h2>
<div class="gifs">%GIFS%</div>
%TEILE%
<div class="lupe" id="lupe"></div>
<script>
(function () {
  var stufe = 0, lupe = document.getElementById('lupe');
  function setze(karte, v) {
    var img = karte.querySelector('.haupt');
    img.src = 'einzelbilder/' + karte.dataset.name + '_' + v + '.png';
    karte.dataset.v = v;
    karte.querySelectorAll('.knoepfe button').forEach(function (b) { b.classList.toggle('an', b.dataset.v === v); });
  }
  document.querySelectorAll('.karte').forEach(function (k) {
    k.dataset.v = 'neu';
    k.querySelectorAll('.knoepfe button').forEach(function (b) { b.addEventListener('click', function () { setze(k, b.dataset.v); }); });
  });
  document.addEventListener('keydown', function (e) {
    if (e.target.tagName === 'INPUT') return;
    if (e.code === 'Space') {
      e.preventDefault();
      document.querySelectorAll('.karte').forEach(function (k) { setze(k, k.dataset.v === 'alt' ? 'neu' : 'alt'); });
    } else if (e.key === 'l' || e.key === 'L') {
      stufe = (stufe + 1) % 3; if (!stufe) lupe.style.display = 'none';
    } else if (e.key === 'n' || e.key === 'N') {
      document.body.classList.toggle('nebeneinander');
    }
  });
  document.addEventListener('mousemove', function (e) {
    var img = e.target;
    if (!stufe || !img.matches || !img.matches('.bild img')) { lupe.style.display = 'none'; return; }
    var r = img.getBoundingClientRect(), f = stufe === 1 ? 2 : 4;   // 2x bzw. 4x der Originalpixel
    var x = (e.clientX - r.left) / r.width * img.naturalWidth, y = (e.clientY - r.top) / r.height * img.naturalHeight;
    lupe.style.display = 'block';
    lupe.style.left = Math.min(window.innerWidth - 370, e.clientX + 20) + 'px';
    lupe.style.top = Math.max(0, e.clientY - 180) + 'px';
    lupe.style.backgroundImage = 'url(' + img.src + ')';
    lupe.style.backgroundSize = (img.naturalWidth * f) + 'px auto';
    lupe.style.backgroundPosition = (180 - x * f) + 'px ' + (180 - y * f) + 'px';
  });
})();
</script>
</body></html>
"""

if __name__ == "__main__":
    main()
