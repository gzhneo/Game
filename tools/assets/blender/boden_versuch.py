#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Versuch "bessere Bodentexturen mit Blender".

Neu erzeugt (prozedural in Blender, boden_backen.py) werden die Gelaendetexturen, die auf einer Karte die
meiste Flaeche decken, in der Anmutung des Originals: Farbrampe aus den Helligkeitsquantilen der
Originaltextur, Mittelwert und Streuung je Kanal angeglichen (gemessen in Originalaufloesung, damit die
Karte aus der Ferne dieselbe Farbe hat), nahtlos kachelbar, Kachelaufteilung wie das Original (eine
256er-TGA = 4x4 Kacheln zu 64 Pixeln, zusammen eine kachelnde Flaeche von 8x8 Kartenzellen).

Befehle:
  auswahl   Gelaendeklassen einer Karte nach Zellen zaehlen (BlendTileData), Terrain.ini aufloesen
  backen    Originale holen, Farbrampen messen, Blender je Textur starten (parallel)
  stufe     Relief schattieren, Farben angleichen, TGA mit Kennung ZHTS<s> schreiben und in BIG packen;
            dazu die Variante "Original + Blender-Detail" und die Kachelansichten (3x3)
  seite     Vergleichsseite aus den Spielaufnahmen (Umschalten der Fassungen, 3x-Ausschnitte, Helligkeit)

Beispiel (Zero Hour, Tournament Lake):
  python tools/assets/blender/boden_versuch.py auswahl --map "tournament lake"
  python tools/assets/blender/boden_versuch.py backen --arbeit render-out/blender-versuch/arbeit
  python tools/assets/blender/boden_versuch.py stufe  --arbeit render-out/blender-versuch/arbeit ^
      --ziel <ZH-Assets>/Texturstufen/Blender-Versuch --kacheln render-out/blender-versuch/kacheln

Die Stufe landet in Unterordnern (je einer fuer -mod): Neu2 (128er-Kacheln, ZHTS2), Neu4 (256er, ZHTS4),
Detail2 (Original hochskaliert + Blender-Feindetail, ZHTS2). Geladen mit
  -textureQuality hd -mod <Asset-Verzeichnis>/Texturstufen/Blender-Versuch/Neu2

Nichts davon gehoert ins Repo: die Vorlagen sind EA-Texturen, die Ergebnisse bleiben lokal.
Braucht numpy und Pillow; Blender 5.2 (Pfad --blender).
"""
import argparse
import io
import json
import os
import re
import struct
import subprocess
import sys
from collections import Counter
from concurrent.futures import ThreadPoolExecutor

HIER = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HIER))						# tools/assets
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HIER)), "render"))	# tools/render
import texture_inventory as inv  # noqa: E402

import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.dirname(HIER)))					# tools
import pfade  # noqa: E402
ROOT_ZH = pfade.assets("zh", pflicht=False)
BLENDER = os.environ.get('BLENDER', 'blender')
HDKI2 = os.path.join(ROOT_ZH, "Texturstufen", "HD-KI2") if ROOT_ZH else None

# Auswahl fuer Tournament Lake (Anteil der Zellen laut `auswahl`), Art des Materials und Parameter
TEXTUREN = [
    ("TMGras12a", "gras", {"boden": 0.05}),
    ("TMDirt05a", "erde", {"kies": [[55, 0.8, 0.45], [120, 0.85, 0.42], [260, 0.7, 0.4]], "flecken": 0.35}),
    ("TMDirt03a", "erde", {"kies": [[45, 0.1, 0.45], [120, 0.25, 0.4]]}),
    ("TMGras02a", "gras", {"halme_u": 160, "halme_v": 40, "stroh": False, "boden": 0.03, "boden_f": 30,
                           "flecken": 0.45}),
    ("TMGras12b", "gras", {"boden": 0.05}),
    ("TMCliff12a", "fels", {"platten": [6, 12]}),
]
RELIEF = {"gras": 0.12, "erde": 0.8, "fels": 1.4}	# Staerke der Reliefschattierung je Art
QUANTILE = [0.02, 0.1, 0.25, 0.4, 0.55, 0.7, 0.85, 0.98]


# -------------------------------------------------------------------------------------------------
# Karte lesen
# -------------------------------------------------------------------------------------------------
def terrain_klassen(files, kartenname):
    """[(Klasse, Zellen, TGA)] einer Karte, nach Zellen absteigend."""
    import map_props as mp
    key = next(k for k in files if k.endswith(".map") and kartenname.lower() in k)
    data = mp.unpack_map(files[key][1].read(key))
    _names, top = mp.parse_chunks(data)
    ver, body = top["BlendTileData"]
    w, h, rand = struct.unpack("<iii", top["HeightMapData"][1][:12])
    p = 0
    n, = struct.unpack("<i", body[p:p + 4]); p += 4
    tiles = np.frombuffer(body[p:p + 2 * n], dtype="<i2") >> 2; p += 2 * n
    p += 2 * n						# blendTileNdxes
    if ver >= 6:
        p += 2 * n					# extraBlendTileNdxes
    if ver >= 5:
        p += 2 * n					# cliffInfoNdxes
    if ver >= 7:
        p += h * ((w + 1) // 8 if ver == 7 else (w + 7) // 8)
    p += 8 + (4 if ver >= 5 else 0)
    ncl, = struct.unpack("<i", body[p:p + 4]); p += 4
    ini_key = "data\\ini\\terrain.ini"
    ini = files[ini_key][1].read(ini_key).decode("latin-1")
    tex = {}
    for m in re.finditer(r"^\s*Terrain\s+(\S+)(.*?)^\s*End", ini, re.S | re.M | re.I):
        t = re.search(r"Texture\s*=\s*(\S+)", m.group(2))
        tex[m.group(1).lower()] = t.group(1) if t else None
    out = []
    for _ in range(ncl):
        first, num, _width, _legacy = struct.unpack("<iiii", body[p:p + 16]); p += 16
        ln, = struct.unpack("<H", body[p:p + 2]); p += 2
        name = body[p:p + ln].decode("latin-1"); p += ln
        maske = (tiles >= first) & (tiles < first + num)
        out.append((name, int(np.count_nonzero(maske)), tex.get(name.lower()), dichteste_stelle(maske, w, h, rand)))
    return sorted(out, key=lambda r: -r[1])


def dichteste_stelle(maske, w, h, rand, k=16):
    """Mitte des k x k-Zellen-Fensters mit dem hoechsten Anteil der Klasse, relativ zum Spielfeld ohne Rand
    (x, y wie in den Kamerafahrten, tests/render/paths). Achtung: y ist die Zeile der Karte; im Versuch lag
    ein Ort damit im See -- die Richtung von y ist nicht geprueft."""
    m = maske.reshape(h, w)[rand:h - rand, rand:w - rand].astype(np.float64)
    H, W = m.shape
    if m.sum() == 0 or H < k or W < k:
        return None
    ii = np.pad(m, ((1, 0), (1, 0))).cumsum(0).cumsum(1)
    summe = ii[k:, k:] - ii[:-k, k:] - ii[k:, :-k] + ii[:-k, :-k]
    y, x = np.unravel_index(np.argmax(summe), summe.shape)
    return round(float(x + k / 2) / W, 3), round(float(y + k / 2) / H, 3), round(float(summe.max()) / (k * k), 2)


def cmd_auswahl(a):
    files, _ = inv.effective_files(a.root)
    rows = terrain_klassen(files, a.map)
    total = sum(r[1] for r in rows) or 1
    print(f"{'Klasse':28} {'Zellen':>7} {'Anteil':>7}  {'TGA':18} dichteste Stelle (x, y, Dichte)")
    for name, cells, tga, ort in rows:
        print(f"{name:28} {cells:7} {100.0 * cells / total:6.1f}%  {str(tga):18} {ort if ort else '-'}")


# -------------------------------------------------------------------------------------------------
# Vorlagen und Farbrampen
# -------------------------------------------------------------------------------------------------
def lade_original(files, name):
    key = "art\\terrain\\" + name.lower() + ".tga"
    im = Image.open(io.BytesIO(files[key][1].read(key)))
    im.load()
    return np.asarray(im.convert("RGB"), dtype=np.float32) / 255.0


def lade_hdki2(name):
    key = "art\\terrain\\" + name.lower() + ".tga"
    for fn in sorted(os.listdir(HDKI2)):
        if fn.lower().endswith(".big"):
            big = inv.BigArchive(os.path.join(HDKI2, fn))
            if key in big.entries:
                im = Image.open(io.BytesIO(big.read(key)))
                im.load()
                return np.asarray(im.convert("RGB"), dtype=np.float32) / 255.0
    return None


def luminanz(rgb):
    return rgb[..., 0] * 0.299 + rgb[..., 1] * 0.587 + rgb[..., 2] * 0.114


def rampe(rgb):
    """Farben an den Helligkeitsquantilen (Mittel eines schmalen Bands um jedes Quantil)."""
    px = rgb.reshape(-1, 3)
    order = np.argsort(luminanz(px), kind="stable")
    n = len(order)
    stops = []
    for q in QUANTILE:
        lo, hi = int(max(0, (q - 0.04) * n)), int(min(n, (q + 0.04) * n))
        stops.append([q, [round(float(x), 4) for x in px[order[lo:hi]].mean(0)]])
    return stops


def saettigung(rgb):
    mx, mn = rgb.max(-1), rgb.min(-1)
    return (mx - mn) / np.maximum(mx, 1e-4)


def auftrag(name, art, param, orig, aus, groesse, samples, seed):
    """Auftrag fuer boden_backen.py: Grundrampe und Farbschichten aus Pixelgruppen des Originals."""
    job = {"name": name, "art": art, "param": param, "groesse": groesse, "samples": samples, "seed": seed,
           "aus": aus, "rampe": rampe(orig), "schichten": []}
    px = orig.reshape(-1, 3)
    sat = saettigung(px)
    if art == "fels":
        # Moos (gelbbraun, gesaettigt) und Stein (grau) getrennt
        moos = (sat > 0.28) & (px[:, 0] > px[:, 2] + 0.06)
        job["rampe"] = rampe(px[~moos])
        job["schichten"].append({"maske": "moos", "rampe": rampe(px[moos]), "anteil": round(float(moos.mean()), 3),
                                 "weich": 0.03, "hoehe": 0.15})
    elif art == "gras":
        # offener Boden = die am wenigsten gesaettigten Pixel; Stroh = gelblich (Rot >= Gruen); Rest gruen
        anteil_boden = param.get("boden", 0.0)
        dunkel = luminanz(px) < np.quantile(luminanz(px), 0.6)
        boden = (sat < np.quantile(sat[dunkel], anteil_boden)) & dunkel if anteil_boden > 0 else np.zeros(len(px), bool)
        stroh = (px[:, 0] >= px[:, 1] * 0.97) & ~boden
        gruen = ~stroh & ~boden
        if gruen.mean() > 0.05:
            job["rampe"] = rampe(px[gruen])
        if param.get("stroh", True) and 0.03 < stroh.mean() < 0.97:
            job["schichten"].append({"maske": "stroh", "rampe": rampe(px[stroh]), "anteil": round(float(stroh.mean()), 3),
                                     "weich": 0.05})
        if anteil_boden > 0:
            job["schichten"].append({"maske": "boden", "rampe": rampe(px[boden]), "anteil": anteil_boden,
                                     "weich": 0.03, "hoehe": 0.3})
    return job


def cmd_backen(a):
    files, _ = inv.effective_files(a.root)
    os.makedirs(a.arbeit, exist_ok=True)
    jobs = []
    for i, (name, art, param) in enumerate(TEXTUREN):
        if a.nur and name not in a.nur:
            continue
        orig = lade_original(files, name)
        d = os.path.join(a.arbeit, name)
        os.makedirs(d, exist_ok=True)
        Image.fromarray(np.uint8(np.rint(orig * 255))).save(os.path.join(d, "original.png"))
        groesse = orig.shape[0] * a.faktor
        job = auftrag(name, art, param, orig, os.path.abspath(d), groesse, a.samples, 1000 + i)
        pfad = os.path.join(d, "auftrag.json")
        json.dump(job, open(pfad, "w", encoding="utf-8"), indent=1)
        jobs.append((name, pfad))

    def lauf(item):
        name, pfad = item
        cmd = [a.blender, "--background", "--factory-startup", "--python", os.path.join(HIER, "boden_backen.py"),
               "--", os.path.abspath(pfad)]
        import time
        t0 = time.time()
        r = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
        fertig = [ln for ln in r.stdout.splitlines() if "BODEN_BACKEN" in ln]
        if r.returncode != 0 or not fertig:
            return f"{name}: FEHLER {r.returncode}\n{r.stdout[-3000:]}\n{r.stderr[-2000:]}"
        return f"{name}: {time.time() - t0:.0f} s  {fertig[-1]}"

    with ThreadPoolExecutor(a.parallel) as ex:
        for msg in ex.map(lauf, jobs):
            print(msg, flush=True)


# -------------------------------------------------------------------------------------------------
# Nachbearbeitung
# -------------------------------------------------------------------------------------------------
def box(rgb, n):
    """Auf n x n mitteln (ganzzahliger Faktor)."""
    f = rgb.shape[0] // n
    return rgb.reshape(n, f, n, f, -1).mean((1, 3))


def relief(hoehe, staerke):
    """Periodische Hangschattierung (Licht von links oben), flach = 1."""
    n = hoehe.shape[0]
    gx = (np.roll(hoehe, -1, 1) - np.roll(hoehe, 1, 1)) * 0.5 * n / 64.0
    gy = (np.roll(hoehe, -1, 0) - np.roll(hoehe, 1, 0)) * 0.5 * n / 64.0
    nx, ny, nz = -staerke * gx, -staerke * gy, np.ones_like(hoehe)
    ln = np.sqrt(nx * nx + ny * ny + nz * nz)
    lx, ly, lz = -0.45, -0.55, 0.70
    ll = np.sqrt(lx * lx + ly * ly + lz * lz)
    return (nx * lx + ny * ly + nz * lz) / (ln * ll) / (lz / ll)


def angleichen(bild, orig):
    """Je Kanal a*x+b so, dass das auf Originalgroesse gemittelte Bild Mittelwert und Streuung des Originals
    hat. Feindetail oberhalb der Originalaufloesung bleibt erhalten, die Fernwirkung stimmt."""
    klein = box(bild, orig.shape[0])
    out = np.empty_like(bild)
    for c in range(3):
        m, s = klein[..., c].mean(), klein[..., c].std()
        mo, so = orig[..., c].mean(), orig[..., c].std()
        out[..., c] = (bild[..., c] - m) * (so / max(s, 1e-4)) + mo
    return np.clip(out, 0.0, 1.0)


def hochskalieren(orig, n):
    """Periodisch (Rand durch Wiederholung) mit Lanczos auf n x n."""
    k = orig.shape[0]
    pad = 8
    big = np.pad(orig, ((pad, pad), (pad, pad), (0, 0)), mode="wrap")
    f = n // k
    out = np.stack([np.asarray(Image.fromarray(big[..., c].astype(np.float32), "F").resize(
        (big.shape[1] * f, big.shape[0] * f), Image.LANCZOS)) for c in range(3)], -1)
    return np.clip(out[pad * f:pad * f + n, pad * f:pad * f + n], 0, 1)


def hochpass(x, grenze):
    """Periodischer Hochpass: alles oberhalb von `grenze` Schwingungen je Texturbreite (weiche Kante)."""
    n = x.shape[0]
    fy = np.fft.fftfreq(n)[:, None] * n
    fx = np.fft.fftfreq(n)[None, :] * n
    r = np.sqrt(fx * fx + fy * fy)
    maske = 1.0 - np.exp(-(r / grenze) ** 4)
    return np.real(np.fft.ifft2(np.fft.fft2(x) * maske))


def tga(rgb, s):
    import build_texture_tiers as btt
    rgba = np.concatenate([np.uint8(np.rint(np.clip(rgb, 0, 1) * 255)),
                           np.full(rgb.shape[:2] + (1,), 255, np.uint8)], -1)
    return btt.encode_tga(rgba, "none", tile_scale=s)


def kachel3(rgb, kante):
    """3x3 gekachelt, jede Kachel auf kante Pixel gebracht."""
    im = Image.fromarray(np.uint8(np.rint(np.clip(rgb, 0, 1) * 255)))
    if im.size[0] != kante:
        im = im.resize((kante, kante), Image.LANCZOS if im.size[0] > kante else Image.BILINEAR)
    out = Image.new("RGB", (3 * kante, 3 * kante))
    for y in range(3):
        for x in range(3):
            out.paste(im, (x * kante, y * kante))
    return out


def mittel(x):
    return [round(float(v), 3) for v in x.reshape(-1, 3).mean(0)]


def streuung(x):
    return [round(float(v), 3) for v in x.reshape(-1, 3).std(0)]


def cmd_stufe(a):
    import build_texture_tiers as btt
    files, _ = inv.effective_files(a.root)
    stufen = {"Neu2": [], "Neu4": [], "Detail2": []}
    bericht = {}
    if a.kacheln:
        os.makedirs(a.kacheln, exist_ok=True)
    for name, art, _param in TEXTUREN:
        d = os.path.join(a.arbeit, name)
        if not os.path.exists(os.path.join(d, "albedo.npy")):
            print(f"{name}: kein Backergebnis, uebersprungen")
            continue
        orig = lade_original(files, name)
        alb = np.load(os.path.join(d, "albedo.npy"))
        hoehe = np.load(os.path.join(d, "hoehe.npy"))[..., 0]
        n4 = alb.shape[0]
        k = orig.shape[0]
        schatten = relief(hoehe, RELIEF[art])
        neu4 = angleichen(alb * schatten[..., None], orig)
        neu2 = box(neu4, 2 * k)
        # Variante Detail: Original hochskaliert, darueber das prozedurale Feindetail (nur Frequenzen, die
        # das Original nicht hat), als Helligkeitsfaktor
        up2 = hochskalieren(orig, 2 * k)
        lum = luminanz(box(neu4, 2 * k))
        detail = hochpass(lum, k / 2.0) / max(lum.mean(), 1e-3)
        det2 = np.clip(up2 * (1.0 + a.detail * detail)[..., None], 0, 1)
        tga_name = "Art\\Terrain\\" + name + ".tga"
        stufen["Neu2"].append((tga_name, tga(neu2, 2)))
        stufen["Neu4"].append((tga_name, tga(neu4, 4)))
        stufen["Detail2"].append((tga_name, tga(det2, 2)))
        for tag, img in (("blender2", neu2), ("blender4", neu4), ("detail2", det2)):
            Image.fromarray(np.uint8(np.rint(img * 255))).save(os.path.join(d, tag + ".png"))
        hd = lade_hdki2(name)
        eintrag = {"art": art, "original": mittel(orig), "original_std": streuung(orig),
                   "blender_klein": mittel(box(neu4, k)), "blender_klein_std": streuung(box(neu4, k)),
                   "blender4_std": streuung(neu4)}
        if hd is not None:
            Image.fromarray(np.uint8(np.rint(hd * 255))).save(os.path.join(d, "hdki2.png"))
            eintrag["hdki2"] = mittel(hd)
            eintrag["hdki2_std"] = streuung(hd)
        # Naht: mittlere Differenz ueber die Wiederholungskante gegen die im Innern
        innen = np.abs(np.diff(neu4, axis=1)).mean()
        kante = np.abs(neu4[:, 0] - neu4[:, -1]).mean()
        eintrag["naht_verhaeltnis"] = round(float(kante / innen), 3)
        bericht[name] = eintrag
        if a.kacheln:
            kante_px = 512
            tafeln = [("Original (SD)", orig), ("HD-KI2", hd), ("Blender s=2", neu2), ("Blender s=4", neu4),
                      ("Original + Blender-Detail s=2", det2)]
            for i, (titel, img) in enumerate(tafeln):
                if img is None:
                    continue
                kachel3(img, kante_px).save(os.path.join(a.kacheln, f"{name}_{i}.jpg"), quality=92)
        print(f"{name}: fertig, Naht {eintrag['naht_verhaeltnis']}", flush=True)
    if a.ziel:
        for stufe, entries in stufen.items():
            if not entries:
                continue
            d = os.path.join(a.ziel, stufe)
            os.makedirs(d, exist_ok=True)
            btt.write_big(os.path.join(d, f"TexturesBlender{stufe}.big"), entries)
            print(f"{stufe}: {len(entries)} Texturen -> {d}")
    json.dump(bericht, open(os.path.join(a.arbeit, "bericht.json"), "w", encoding="utf-8"), indent=1)



# -------------------------------------------------------------------------------------------------
# Vergleichsseite (lokal, nicht versioniert)
# -------------------------------------------------------------------------------------------------
FASSUNGEN = [("sd", "SD (Original)"), ("hdki2", "HD-KI2"), ("blender2", "Blender s=2"), ("blender4", "Blender s=4"),
             ("detail2", "Original + Blender-Detail s=2")]
KACHEL_TITEL = ["Original (SD)", "HD-KI2", "Blender s=2", "Blender s=4", "Original + Blender-Detail s=2"]


def ausschnitte(bilder, anzahl=2, w=320, h=180, ui_rand=0.68):
    """Die Fenster, in denen sich SD und Blender s=4 am staerksten unterscheiden (dort ist Boden zu sehen), ohne
    die Befehlsleiste unten. Nicht ueberlappend."""
    a = bilder.get("sd")
    b = bilder.get("blender4") if "blender4" in bilder else bilder.get("blender2")
    if a is None or b is None:
        return []
    d = np.abs(a.astype(np.float32) - b.astype(np.float32)).mean(-1)
    H, W = d.shape
    ii = np.pad(d, ((1, 0), (1, 0))).cumsum(0).cumsum(1)
    raster = []
    for y in range(0, int(H * ui_rand) - h, 20):
        for x in range(0, W - w, 20):
            raster.append((ii[y + h, x + w] - ii[y, x + w] - ii[y + h, x] + ii[y, x], x, y))
    raster.sort(reverse=True)
    out = []
    for _, x, y in raster:
        if all(abs(x - ox) >= w or abs(y - oy) >= h for ox, oy in out):
            out.append((x, y))
        if len(out) == anzahl:
            break
    return [(x, y, w, h) for x, y in out]


def cmd_seite(a):
    import html
    wurzel = a.aus
    daten = {"szenen": [], "kacheln": [], "fassungen": FASSUNGEN}
    for szene in sorted(d for d in os.listdir(wurzel) if os.path.isdir(os.path.join(wurzel, d, "sd"))):
        sd_dir = os.path.join(wurzel, szene, "sd")
        shots = sorted(f for f in os.listdir(sd_dir) if f.startswith("shot_") and f.endswith(".bmp"))
        eintraege = []
        for shot in shots:
            bilder = {}
            for key, _ in FASSUNGEN:
                pfad = os.path.join(wurzel, szene, key, shot)
                if os.path.exists(pfad):
                    bilder[key] = np.asarray(Image.open(pfad).convert("RGB"))
            if len(bilder) < 2:
                continue
            ziel = os.path.join(wurzel, "seite", szene)
            os.makedirs(ziel, exist_ok=True)
            stem = shot[:-4]
            hell = {}
            fenster = ausschnitte(bilder)
            for key, img in bilder.items():
                Image.fromarray(img).save(os.path.join(ziel, f"{stem}_{key}.jpg"), quality=93)
                hell[key] = round(float(luminanz(img.astype(np.float32)).mean()), 1)
                for i, (x, y, w, h) in enumerate(fenster):
                    Image.fromarray(img[y:y + h, x:x + w]).resize((w * 3, h * 3), Image.NEAREST).save(
                        os.path.join(ziel, f"{stem}_{key}_a{i}.png"))
            eintraege.append({"shot": stem, "fassungen": sorted(bilder), "hell": hell, "fenster": fenster})
        daten["szenen"].append({"name": szene, "shots": eintraege})
    bericht_pfad = os.path.join(a.arbeit, "bericht.json") if a.arbeit else None
    bericht = json.load(open(bericht_pfad, encoding="utf-8")) if bericht_pfad and os.path.exists(bericht_pfad) else {}
    kdir = os.path.join(wurzel, "kacheln")
    for name, art, _ in TEXTUREN:
        tafeln = [(KACHEL_TITEL[i], f"kacheln/{name}_{i}.jpg") for i in range(5)
                  if os.path.exists(os.path.join(kdir, f"{name}_{i}.jpg"))]
        daten["kacheln"].append({"name": name, "art": art, "tafeln": tafeln, "bericht": bericht.get(name, {})})
    tpl = open(os.path.join(HIER, "vergleich_vorlage.html"), encoding="utf-8").read()
    open(os.path.join(wurzel, "index.html"), "w", encoding="utf-8").write(
        tpl.replace("/*DATEN*/null", json.dumps(daten, ensure_ascii=False)))
    print("Seite:", os.path.join(wurzel, "index.html"), sum(len(s["shots"]) for s in daten["szenen"]), "Aufnahmen")

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=ROOT_ZH, required=ROOT_ZH is None, help="Asset-Verzeichnis (nur lesend)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("auswahl")
    s.add_argument("--map", default="tournament lake")
    s = sub.add_parser("backen")
    s.add_argument("--arbeit", required=True)
    s.add_argument("--blender", default=BLENDER)
    s.add_argument("--faktor", type=int, default=4, help="Backgroesse = Originalgroesse * Faktor (4 = Stufe s=4)")
    s.add_argument("--samples", type=int, default=16)
    s.add_argument("--parallel", type=int, default=3)
    s.add_argument("--nur", nargs="*")
    s = sub.add_parser("stufe")
    s.add_argument("--arbeit", required=True)
    s.add_argument("--ziel", help="Ordner der Versuchsstufe (Unterordner Neu2, Neu4, Detail2)")
    s.add_argument("--kacheln", help="Ordner fuer die 3x3-Kachelansichten")
    s.add_argument("--detail", type=float, default=0.6, help="Staerke des Feindetails in der Variante Detail")
    s = sub.add_parser("seite", help="Vergleichsseite aus den Aufnahmen <aus>/<Szene>/<Fassung>/shot_*.bmp")
    s.add_argument("--aus", required=True, help="Ordner mit den Szenen (z. B. render-out/blender-versuch)")
    s.add_argument("--arbeit", help="Arbeitsordner von backen/stufe (fuer die Farbwerte)")
    a = ap.parse_args()
    {"auswahl": cmd_auswahl, "backen": cmd_backen, "stufe": cmd_stufe, "seite": cmd_seite}[a.cmd](a)


if __name__ == "__main__":
    main()
