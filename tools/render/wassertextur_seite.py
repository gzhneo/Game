#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour (Electronic Arts).
# Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Vergleichsseite EA- gegen freie Wasserfarbe aus den Aufnahmen von
wassertextur_aufnahmen.py, mit Schieber, dazu Helligkeit je Bild.

  python tools/render/wassertextur_seite.py [--raw render-out/wassertextur/raw] [--out render-out/wassertextur/seite]

Helligkeit: mittlere Luminanz (0..255, Rec. 709 auf den sRGB-Werten) des ganzen Bildes ohne Befehlsleiste und der
Wasserflaeche. Wasserflaeche = Pixel, die sich zwischen EA und frei um mehr als 8 in einem Kanal unterscheiden, um
6 Pixel erweitert (nur dort wirkt die Textur). Abweichung in Prozent von EA. Ergebnis: <out>/wassertextur_vergleich.html
(Bilder eingebettet) und <out>/helligkeit.csv. Die Seite ist ein Pruefbericht aus Spielbildern mit EA-Daten und bleibt
lokal (render-out wird nicht eingecheckt).
"""
import argparse
import base64
import csv
import glob
import html
import io
import json
import os

import numpy as np
from PIL import Image
from scipy.ndimage import binary_dilation

WURZEL = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TITEL = {
    "z-insel": "Meer: Tournament Island (ZH)", "z-shell": "Meer: Shell-Map (ZH)", "z-see": "See: Tournament Lake (ZH)",
    "z-fluss": "Fluss: Winding River (ZH)", "g-fluss": "Fluss: Heartland Shield (Generals)",
    "z-winter": "Winter: Bitter Winter (ZH)", "z-whiteout": "Winter: Whiteout (ZH)", "z-oase": "Wüste: Golden Oasis (ZH)",
    "z-badlands": "Wüste: Barren Badlands (ZH)", "g-nacht": "Nacht: Silent River (Generals)",
}
LEISTE = 0.76		# Anteil der Bildhoehe oberhalb der Befehlsleiste


def lum(a):
    return a[..., 0] * 0.2126 + a[..., 1] * 0.7152 + a[..., 2] * 0.0722


def jpeg(a, breite=1280):
    im = Image.fromarray(a)
    im = im.resize((breite, int(round(breite * im.height / im.width))), Image.LANCZOS)
    b = io.BytesIO()
    im.save(b, "JPEG", quality=88)
    return "data:image/jpeg;base64," + base64.b64encode(b.getvalue()).decode("ascii")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--raw", default=os.path.join(WURZEL, "render-out", "wassertextur", "raw"))
    ap.add_argument("--out", default=os.path.join(WURZEL, "render-out", "wassertextur", "seite"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    bilder, eintraege, zeilen = {}, [], []
    summe = {}
    for stufe_dir in sorted(glob.glob(os.path.join(a.raw, "ea_q*"))):
        stufe = os.path.basename(stufe_dir)[3:]
        for szene in TITEL:
            for f_ea in sorted(glob.glob(os.path.join(stufe_dir, szene, "shot_*.bmp")))[1::2]:
                f_frei = f_ea.replace(os.sep + "ea_" + stufe + os.sep, os.sep + "frei_" + stufe + os.sep)
                if not os.path.exists(f_frei):
                    continue
                ea = np.asarray(Image.open(f_ea).convert("RGB"))
                fr = np.asarray(Image.open(f_frei).convert("RGB"))
                h = int(ea.shape[0] * LEISTE) if szene != "z-shell" else ea.shape[0]
                ea, fr = ea[:h], fr[:h]
                diff = np.abs(ea.astype(int) - fr.astype(int)).max(-1) > 8
                maske = binary_dilation(diff, iterations=6)
                l_ea, l_fr = lum(ea.astype(float)), lum(fr.astype(float))
                anteil = maske.mean()
                w_ea = l_ea[maske].mean() if anteil > 0 else float("nan")
                w_fr = l_fr[maske].mean() if anteil > 0 else float("nan")
                bild = os.path.basename(f_ea)[5:-4].lstrip("0")
                key = "%s_%s_%s" % (stufe, szene, bild)
                bilder[key + "_ea"] = jpeg(ea)
                bilder[key + "_frei"] = jpeg(fr)
                z = dict(stufe=stufe, szene=szene, bild=bild, wasseranteil=round(100 * anteil, 1),
                         bild_ea=round(l_ea.mean(), 2), bild_frei=round(l_fr.mean(), 2),
                         bild_proz=round(100 * (l_fr.mean() / l_ea.mean() - 1), 2),
                         wasser_ea=round(w_ea, 2), wasser_frei=round(w_fr, 2),
                         wasser_proz=round(100 * (w_fr / w_ea - 1), 2) if anteil > 0 else "")
                zeilen.append(z)
                eintraege.append([key, stufe, szene, bild])
                s = summe.setdefault((stufe, szene), [0, 0.0, 0.0, 0.0, 0.0, 0])
                s[0] += 1; s[1] += l_ea.mean(); s[2] += l_fr.mean()
                if anteil > 0:
                    s[3] += w_ea * anteil; s[4] += w_fr * anteil; s[5] += anteil
    with open(os.path.join(a.out, "helligkeit.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(zeilen[0].keys()), delimiter=";")
        w.writeheader()
        w.writerows(zeilen)
    tab = ["<tr><th>Stufe</th><th>Szene</th><th>Bilder</th><th>ganzes Bild EA → frei</th><th>Wasser EA → frei</th></tr>"]
    for (stufe, szene), s in sorted(summe.items()):
        we = "%.1f → %.1f (%+.1f %%)" % (s[3] / s[5], s[4] / s[5], 100 * (s[4] / s[3] - 1)) if s[5] > 0 else "–"
        tab.append("<tr><td>%s</td><td>%s</td><td>%d</td><td>%.1f → %.1f (%+.2f %%)</td><td>%s</td></tr>" % (
            "Hoch (2)" if stufe == "q2" else "Niedrig (0)", html.escape(TITEL[szene]), s[0], s[1] / s[0], s[2] / s[0],
            100 * (s[2] / s[1] - 1), we))
        print("%s %-12s ganzes Bild %+.2f %%, Wasser %s" % (stufe, szene, 100 * (s[2] / s[1] - 1), we.replace("→", "->")))
    seite = SEITE.replace("@BILDER@", json.dumps(bilder)).replace("@LISTE@", json.dumps(eintraege)) \
        .replace("@TITEL@", json.dumps(TITEL, ensure_ascii=False)).replace("@TABELLE@", "\n".join(tab))
    with open(os.path.join(a.out, "wassertextur_vergleich.html"), "w", encoding="utf-8") as f:
        f.write(seite)
    print("Seite:", os.path.join(a.out, "wassertextur_vergleich.html"))


SEITE = """<!doctype html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Wassertextur Vergleich</title>
<style>
:root{--bg:#f2f3f3;--panel:#fafbfb;--fg:#1f2426;--muted:#5f686c;--line:#d3d8da;--accent:#1f6f7a;--ink:#fff}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){--bg:#121617;--panel:#1b2022;--fg:#e4eaec;--muted:#98a3a7;--line:#323a3d;--accent:#5fb3bf;--ink:#121617;color-scheme:dark}}
:root[data-theme=dark]{--bg:#121617;--panel:#1b2022;--fg:#e4eaec;--muted:#98a3a7;--line:#323a3d;--accent:#5fb3bf;--ink:#121617;color-scheme:dark}
body{background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,Segoe UI,sans-serif;margin:0;padding:24px 16px}
.w{max-width:1280px;margin:auto;display:grid;gap:14px}h1{margin:0;font-size:1.5rem}p{margin:0;color:var(--muted)}
.row{display:flex;gap:6px;flex-wrap:wrap;align-items:center}
button{font:inherit;color:var(--fg);background:var(--panel);border:1px solid var(--line);border-radius:4px;padding:5px 9px;cursor:pointer}
button[aria-pressed=true]{background:var(--accent);color:var(--ink);border-color:var(--accent)}
.stage{position:relative;overflow:hidden;border:1px solid var(--line);border-radius:4px;background:#000;touch-action:none;user-select:none}
.stage img{display:block;width:100%;height:auto}.stage #il{position:absolute;inset:0}
.tag{position:absolute;top:8px;font:12px monospace;background:rgba(0,0,0,.7);color:#fff;padding:3px 7px;border-radius:3px;pointer-events:none}
.h{position:absolute;top:0;bottom:0;width:2px;background:#fff;box-shadow:0 0 0 1px #0007;pointer-events:none}
input[type=range]{width:100%;accent-color:var(--accent)}
table{border-collapse:collapse;font-size:14px}th,td{border-bottom:1px solid var(--line);padding:4px 14px 4px 0;text-align:left}
</style></head><body><div class="w">
<h1>Wasserfarbe: EA gegen frei</h1>
<p>Links die gemalte EA-Kachel, rechts die freie Kachel aus CC0-Fotos (<code>-waterTexture free</code>), sonst gleiche Einstellungen (1080p, HD, Wasserstufe wie gewählt). Schieber ziehen oder Taste Leertaste: ganz EA / ganz frei.</p>
<div class="row" id="stufen"></div><div class="row" id="szenen"></div><div class="row" id="bilder"></div>
<div class="stage" id="st"><img id="ir" alt="frei"><img id="il" alt="EA"><span class="tag" style="left:8px">EA</span><span class="tag" style="right:8px">frei</span><div class="h" id="h"></div></div>
<input type="range" id="reg" min="0" max="100" value="50" aria-label="Schieber">
<p id="wert"></p>
<table>@TABELLE@</table>
<p>Wasser = Fläche, auf der sich die Bilder unterscheiden (um 6 Pixel erweitert), gewichtet nach Fläche. Ganzes Bild ohne Befehlsleiste.</p>
</div><script>
const D=@BILDER@, L=@LISTE@, T=@TITEL@; const $=i=>document.getElementById(i);
let st=L[0][1], sz=L[0][2], bi=L[0][3];
function knoepfe(id,werte,akt,setz,name){const e=$(id);e.innerHTML="";for(const v of werte){const b=document.createElement("button");b.textContent=name(v);b.setAttribute("aria-pressed",String(v==akt));b.onclick=()=>{setz(v);z();};e.append(b);}}
function uniq(a){return [...new Set(a)];}
function z(){const stufen=uniq(L.map(x=>x[1]));knoepfe("stufen",stufen,st,v=>st=v,v=>v=="q2"?"Wasserstufe Hoch":"Wasserstufe Niedrig");
const szn=uniq(L.filter(x=>x[1]==st).map(x=>x[2]));if(!szn.includes(sz))sz=szn[0];knoepfe("szenen",szn,sz,v=>sz=v,v=>T[v]);
const bl=L.filter(x=>x[1]==st&&x[2]==sz).map(x=>x[3]);if(!bl.includes(bi))bi=bl[0];knoepfe("bilder",bl,bi,v=>bi=v,v=>"Bild "+v);
const k=st+"_"+sz+"_"+bi;$("il").src=D[k+"_ea"];$("ir").src=D[k+"_frei"];}
function s(){const p=$("reg").value;$("il").style.clipPath="inset(0 "+(100-p)+"% 0 0)";$("h").style.left=p+"%";}
$("reg").oninput=s;const sg=$("st");function mv(e){const r=sg.getBoundingClientRect();$("reg").value=Math.max(0,Math.min(100,(e.clientX-r.left)/r.width*100));s();}
sg.addEventListener("pointerdown",e=>{sg.setPointerCapture(e.pointerId);mv(e);sg.onpointermove=mv;});sg.addEventListener("pointerup",()=>sg.onpointermove=null);
document.addEventListener("keydown",e=>{if(e.code=="Space"){e.preventDefault();$("reg").value=$("reg").value>50?0:100;s();}});
z();s();
</script></body></html>"""


if __name__ == "__main__":
    main()
