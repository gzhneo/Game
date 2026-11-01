#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Boden-Detail: Detailtexturen aus fotografischen CC0-Scans.

Der D3D11-Gelaendeshader legt je Bodenart eine feine, kachelnde Detailstruktur ueber die Originaltextur
(Helligkeit, Mittelwert 1, multiplikativ; beim Heranzoomen eingeblendet). Dieses Werkzeug holt die Scans,
baut daraus die beiden Detailtexturen und die Zuordnung Gelaendetextur -> Detailmaterial.

Befehle:
  laden       CC0-Scans (Farbe, Umgebungsverdeckung, Normalen) von Poly Haven und ambientCG nach
              <GZH_WERKZEUGE>/cc0/download/<quelle>/<id>/ laden, samt meta.json (Autor, Lizenz, URL)
  bauen       Detailtexturen Data/Textures/BodenDetail/detail_a.dds und detail_b.dds (256x256, RGBA =
              vier Materialien je Datei, volle Mip-Kette) und Vorschaubilder nach --arbeit
  zuordnung   Data/INI/BodenDetail.ini: alle Gelaendetexturen aus Terrain.ini (Zero Hour und Generals) mit
              ihrem Detailmaterial -- nach Namen, Klasse und Farbstatistik der TGA
  kacheln     Vorschau: Originaltextur, Detail und beides zusammen (3x3 gekachelt) fuer einige TGAs

Kodierung der Detailtexturen: Byte b steht fuer den Faktor b/128 (128 = 1,0, neutral). Der Mittelwert jeder
Mip-Stufe und jedes Kanals ist auf 128 gezogen -- die Detailebene aendert die mittlere Helligkeit nicht.

Beispiel:
  python tools/assets/boden_detail.py laden
  python tools/assets/boden_detail.py bauen --arbeit render-out/boden-detail/arbeit
  python tools/assets/boden_detail.py zuordnung
Braucht numpy und Pillow.
"""
import argparse
import io
import json
import os
import re
import struct
import sys
import urllib.request
import zipfile

import numpy as np
from PIL import Image

HIER = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HIER))
sys.path.insert(0, HIER)

sys.path.insert(0, os.path.join(HIER, '..'))
import pfade  # noqa: E402
DOWNLOAD = pfade.werkzeuge("cc0", "download", pflicht=False)
ZIEL_TEX = os.path.join(REPO, "Data", "Textures", "BodenDetail")
ZIEL_INI = os.path.join(REPO, "Data", "INI", "BodenDetail.ini")
def root_zh():
    return pfade.assets("zh")


def root_g():
    return pfade.assets("generals")
UA = {"User-Agent": "Mozilla/5.0 (CnC-Generals-Modernisierung; Boden-Detail)"}

GROESSE = 512			# Kantenlaenge der Detailtextur; sie deckt eine Kachel (64 Atlaspixel = 20 Welteinheiten)
AUSGABE_STUFE = 1		# geschrieben wird ab dieser Mip-Stufe der Berechnung: 256 Pixel Kantenlaenge fuer alle Texturstufen

# Materialien: Nummer (1..8), Name, Quelle, Id, Parameter.
#   n        Wiederholungen des Scans je Kachel (1, 2 oder 4). Eine Kachel sind 20 Welteinheiten, rund 5 m
#            (Panzer ~ 25 Einheiten ~ 7 m); ein 2-m-Scan bei n = 1 erscheint also rund 2,5-fach vergroessert --
#            so grob sind auch die Fotos der EA-Texturen (Grashalme mehrere Texel lang). Mit n = 2 lag die
#            Struktur so fein, dass die Mip-Stufen sie in der Nahansicht wegmittelten.
#   streu    Streuung des Faktors (Standardabweichung um 1,0), gemessen in Mip-Stufe MIP_NORM (128 Texel je
#            Kachel -- so dicht liest die nahe Kamera, 150 bis 220 Bildpunkte je Kachel)
#   ao, rel  Anteil Umgebungsverdeckung (Exponent) und Reliefschattierung aus der Normalenkarte
#   grenze   Hochpass: Strukturen groesser als Kachel/(n*grenze) werden entfernt
# Kanaele: detail_a.dds RGBA = Material 1..4, detail_b.dds RGBA = Material 5..8.
MIP_NORM = 2
MATERIALIEN = [
    (1, "gras",        "polyhaven", "leafy_grass",     {"n": 1, "streu": 0.14, "ao": 0.6, "rel": 0.3, "grenze": 4}),
    (2, "trockengras", "polyhaven", "withered_grass",  {"n": 1, "streu": 0.14, "ao": 0.6, "rel": 0.35, "grenze": 6}),
    # Erde: trockener, fein gerissener Boden; traegt vor allem die Rissboeden (RISSBODEN), brown_mud_dry bis 2026-10-01
    (3, "erde",        "polyhaven", "dry_ground_01",   {"n": 1, "streu": 0.13, "ao": 0.8, "rel": 0.5, "grenze": 3}),
    # Kies: einzelne Steine verschiedener Groesse auf Erde; gravel_floor war feines Rauschen ueber den EA-Steinen
    (4, "kies",        "polyhaven", "rocky_trail",     {"n": 1, "streu": 0.13, "ao": 0.8, "rel": 0.5, "grenze": 3}),
    (5, "sand",        "polyhaven", "sandy_gravel_02", {"n": 1, "streu": 0.10, "ao": 0.6, "rel": 0.5, "grenze": 3}),
    (6, "fels",        "polyhaven", "rock_face",       {"n": 1, "streu": 0.18, "ao": 0.8, "rel": 0.6, "grenze": 3}),
    (7, "schnee",      "ambientcg", "Snow006",         {"n": 1, "streu": 0.06, "ao": 0.5, "rel": 0.6, "grenze": 4}),
    (8, "asphalt",     "ambientcg", "Asphalt015",      {"n": 1, "streu": 0.08, "ao": 0.5, "rel": 0.3, "grenze": 3}),
]
NAMEN = {m[1]: m[0] for m in MATERIALIEN}
NAMEN["keins"] = 0


# -------------------------------------------------------------------------------------------------
# laden
# -------------------------------------------------------------------------------------------------
def hole(url):
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=120) as r:
        return r.read()


def lade_polyhaven(aid, ziel):
    info = json.loads(hole("https://api.polyhaven.com/info/" + aid))
    files = json.loads(hole("https://api.polyhaven.com/files/" + aid))
    karten = {"Diffuse": "farbe", "AO": "ao", "nor_gl": "normal", "Displacement": "hoehe", "Rough": "rauheit"}
    dateien = {}
    for key, name in karten.items():
        if key not in files:
            continue
        url = files[key]["2k"]["png"]["url"]
        pfad = os.path.join(ziel, name + ".png")
        if not os.path.exists(pfad):
            with open(pfad, "wb") as f:
                f.write(hole(url))
        dateien[name] = {"datei": name + ".png", "url": url}
    return {
        "quelle": "Poly Haven", "id": aid, "name": info.get("name", aid),
        "autoren": info.get("authors", {}), "masse_mm": info.get("dimensions"),
        "lizenz": "CC0 1.0 Universal (Public Domain Dedication)",
        "seite": "https://polyhaven.com/a/" + aid, "dateien": dateien,
    }


def lade_ambientcg(aid, ziel):
    meta = json.loads(hole("https://ambientcg.com/api/v2/full_json?id=%s&include=dimensionsData" % aid))
    a = meta["foundAssets"][0]
    url = "https://ambientcg.com/get?file=%s_2K-PNG.zip" % aid
    zpfad = os.path.join(ziel, aid + "_2K-PNG.zip")
    if not os.path.exists(zpfad):
        with open(zpfad, "wb") as f:
            f.write(hole(url))
    karten = {"_Color": "farbe", "_AmbientOcclusion": "ao", "_NormalGL": "normal", "_Displacement": "hoehe",
              "_Roughness": "rauheit"}
    dateien = {}
    with zipfile.ZipFile(zpfad) as z:
        for n in z.namelist():
            for key, name in karten.items():
                if key in n and n.lower().endswith(".png"):
                    with open(os.path.join(ziel, name + ".png"), "wb") as f:
                        f.write(z.read(n))
                    dateien[name] = {"datei": name + ".png", "url": url + " (" + n + ")"}
    return {
        "quelle": "ambientCG", "id": aid, "name": a.get("displayName", aid),
        "autoren": {"Lennart Demes (ambientCG)": "All"},
        "masse_mm": [10 * a.get("dimensionX", 0), 10 * a.get("dimensionY", 0)],
        "lizenz": "CC0 1.0 Universal (Public Domain Dedication)",
        "seite": "https://ambientcg.com/a/" + aid, "dateien": dateien,
    }


def cmd_laden(a):
    ids = a.ids or [(m[2], m[3]) for m in MATERIALIEN]
    for eintrag in ids:
        quelle, aid = eintrag if isinstance(eintrag, tuple) else eintrag.split(":", 1)
        ziel = os.path.join(a.download, quelle, aid)
        os.makedirs(ziel, exist_ok=True)
        meta = lade_polyhaven(aid, ziel) if quelle == "polyhaven" else lade_ambientcg(aid, ziel)
        with open(os.path.join(ziel, "meta.json"), "w", encoding="utf-8") as f:
            json.dump(meta, f, indent=1, ensure_ascii=False)
        print("%-10s %-18s %s  %s" % (quelle, aid, meta["masse_mm"], ", ".join(meta["autoren"])))


# -------------------------------------------------------------------------------------------------
# bauen
# -------------------------------------------------------------------------------------------------
def lies(pfad, grau=False):
    im = Image.open(pfad)
    im.load()
    if im.mode in ("I;16", "I;16B", "I"):
        x = np.asarray(im, dtype=np.float64) / 65535.0
        return x if grau or x.ndim == 2 else x
    if grau:
        return np.asarray(im.convert("L"), dtype=np.float64) / 255.0
    return np.asarray(im.convert("RGB"), dtype=np.float64) / 255.0


def box(x, f):
    h, w = x.shape[:2]
    return x.reshape(h // f, f, w // f, f, *x.shape[2:]).mean(axis=(1, 3))


def gauss_periodisch(x, sigma):
    """Gauss-Weichzeichner mit periodischem Rand (ueber die FFT)."""
    h, w = x.shape
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.fftfreq(w)[None, :]
    g = np.exp(-2.0 * (np.pi ** 2) * (sigma ** 2) * (fx ** 2 + fy ** 2))
    return np.real(np.fft.ifft2(np.fft.fft2(x) * g))


def material_detail(quelle, aid, p, download):
    """Faktorbild (Mittel 1) einer Kachel in GROESSE x GROESSE."""
    d = os.path.join(download, quelle, aid)
    farbe = lies(os.path.join(d, "farbe.png"))
    lum = 0.299 * farbe[..., 0] + 0.587 * farbe[..., 1] + 0.114 * farbe[..., 2]
    roh = np.maximum(lum, 1e-3)
    if p.get("ao", 0) > 0 and os.path.exists(os.path.join(d, "ao.png")):
        ao = lies(os.path.join(d, "ao.png"), grau=True)
        roh = roh * np.power(np.clip(ao, 0.05, 1.0), p["ao"])
    if p.get("rel", 0) > 0 and os.path.exists(os.path.join(d, "normal.png")):
        n = lies(os.path.join(d, "normal.png")) * 2.0 - 1.0
        n = n / np.maximum(np.linalg.norm(n, axis=2, keepdims=True), 1e-6)
        licht = np.array([-0.45, 0.45, 0.77])		# von links oben (OpenGL-Normalen: +y oben)
        licht = licht / np.linalg.norm(licht)
        nl = np.clip((n * licht).sum(axis=2), 0.0, 1.0) / licht[2]
        roh = roh * np.power(np.clip(nl, 0.05, 2.0), p["rel"])
    kante = GROESSE // p["n"]
    f = roh.shape[0] // kante
    klein = box(roh[:f * kante, :f * kante], f)
    # Hochpass im Logarithmus: Verhaeltnis zum weichgezeichneten Bild, nahtlos (periodisch)
    lg = np.log(klein)
    hp = lg - gauss_periodisch(lg, kante / (p["grenze"] * 2.0))
    fak = np.exp(hp)
    fak = fak / fak.mean()
    gemessen = box(fak, 2 ** MIP_NORM) if MIP_NORM else fak
    fak = 1.0 + (fak - 1.0) * (p["streu"] / max(gemessen.std(), 1e-6))
    fak = np.clip(fak, 0.05, 1.95)
    fak = fak / fak.mean()
    return np.tile(fak, (p["n"], p["n"]))


def quantisiere(x):
    """Faktor -> Byte (b/128), Mittel exakt 128 (Rundungsfehler ueber die haeufigsten Werte verteilt)."""
    b = np.clip(np.rint(x * 128.0), 1, 255).astype(np.int64)
    diff = int(round(128.0 * b.size - b.sum()))
    if diff:
        flat = b.reshape(-1)
        rest = (x.reshape(-1) * 128.0) - flat
        order = np.argsort(-rest if diff > 0 else rest, kind="stable")
        schritt = 1 if diff > 0 else -1
        k = 0
        for idx in order:
            if k == abs(diff):
                break
            v = flat[idx] + schritt
            if 1 <= v <= 255:
                flat[idx] = v
                k += 1
    return b.astype(np.uint8)


def mipkette(ebene0):
    """Liste der Mip-Stufen (float), je Stufe Mittel 1 -- bis 1x1."""
    out = [ebene0]
    x = ebene0
    while x.shape[0] > 1:
        x = box(x, 2)
        out.append(x / x.mean())
    return out


def schreibe_dds(pfad, stufen_rgba):
    """Unkomprimiertes 32-Bit-DDS (R8G8B8A8, Masken wie DXGI_FORMAT_R8G8B8A8_UNORM) mit Mip-Kette."""
    h, w = stufen_rgba[0].shape[:2]
    DDSD = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000
    kopf = struct.pack("<4sIIIIIII", b"DDS ", 124, DDSD, h, w, w * 4, 0, len(stufen_rgba))
    kopf += b"\0" * 44
    kopf += struct.pack("<IIIIIIII", 32, 0x41, 0, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000)
    kopf += struct.pack("<IIIII", 0x1000 | 0x8 | 0x400000, 0, 0, 0, 0)
    with open(pfad, "wb") as f:
        f.write(kopf)
        for s in stufen_rgba:
            f.write(np.ascontiguousarray(s).tobytes())


def cmd_bauen(a):
    os.makedirs(ZIEL_TEX, exist_ok=True)
    os.makedirs(a.arbeit, exist_ok=True)
    kanaele = {}
    for nr, name, quelle, aid, p in MATERIALIEN:
        fak = material_detail(quelle, aid, p, a.download)
        kanaele[nr] = mipkette(fak)
        vor = np.clip(fak * 128.0, 0, 255).astype(np.uint8)
        Image.fromarray(np.tile(vor, (2, 2))).save(os.path.join(a.arbeit, "detail_%d_%s.png" % (nr, name)))
        print("%d %-12s %-16s Streuung %.3f  Min %.2f Max %.2f" % (nr, name, aid, fak.std(), fak.min(), fak.max()))
    for datei, nrs in (("detail_a.dds", (1, 2, 3, 4)), ("detail_b.dds", (5, 6, 7, 8))):
        stufen = []
        for lvl in range(AUSGABE_STUFE, len(kanaele[nrs[0]])):
            s = np.stack([quantisiere(kanaele[n][lvl]) for n in nrs], axis=-1)
            stufen.append(s)
        schreibe_dds(os.path.join(ZIEL_TEX, datei), stufen)
        m = [float(stufen[0][..., c].mean()) for c in range(4)]
        print("geschrieben", datei, "Mittel je Kanal", ["%.3f" % v for v in m])


# -------------------------------------------------------------------------------------------------
# zuordnung
# -------------------------------------------------------------------------------------------------
def terrain_ini(root):
    import texture_inventory as inv
    files, _ = inv.effective_files(root)
    k = "data\\ini\\terrain.ini"
    ini = files[k][1].read(k).decode("latin-1")
    out = {}
    for m in re.finditer(r"^\s*Terrain\s+(\S+)(.*?)^\s*End", ini, re.S | re.M | re.I):
        tex = re.search(r"Texture\s*=\s*(\S+)", m.group(2))
        cls = re.search(r"Class\s*=\s*(\S+)", m.group(2))
        if tex:
            out.setdefault(tex.group(1).lower(), (m.group(1), cls.group(1) if cls else "-"))
    return files, out


def statistik(files, tga):
    key = "art\\terrain\\" + tga
    if key not in files:
        return None
    im = Image.open(io.BytesIO(files[key][1].read(key)))
    im.load()
    rgb = np.asarray(im.convert("RGB"), dtype=np.float64) / 255.0
    lum = 0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2]
    fein = lum - gauss_periodisch(lum, 2.0)
    mx, mn = rgb.max(axis=2), rgb.min(axis=2)
    sat = float(((mx - mn) / np.maximum(mx, 1e-3)).mean())
    return {"mittel": rgb.reshape(-1, 3).mean(axis=0), "lum": float(lum.mean()), "fein": float(fein.std()), "sat": sat}


# Texturen mit deutlichem Rissnetz (trockener Lehm); die Farbstatistik trennt sie nicht von hellem Sand mit Steinchen
RISSBODEN = {"tmdirt07a.tga", "tmdirt07b.tga", "tmdirt07d.tga", "tmdirt07e.tga"}


def material_fuer(tga, klasse, typ, st):
    """(Material, Begruendung) nach Name, Klasse und Farbstatistik."""
    # Reihenfolge: zuerst der Dateiname (TM = Medium, TL = Light, TX = Extra ... + Gras/Dirt/Sand/Cliff ...), er
    # beschreibt das Motiv; die INI-Klasse (Class = MOUNTAIN_RUGGED ...) ist nur die Gruppe im WorldBuilder und
    # passt oft nicht (TMGras12a steht in MOUNTAIN_RUGGED). Innerhalb einer Namensgruppe entscheidet die
    # Farbstatistik der TGA. Schwellen geeicht an Tournament Lake: TMDirt05a (Kies)
    # fein 0,138, TMDirt03a (Sand mit wenig Steinen) 0,082; TMGras12a (gruenes Gras) 0,48/0,48/0,25,
    # TMGras12b (trocken) 0,44/0,39/0,30.
    n = tga.lower()
    t = typ.upper()
    r, g, b = st["mittel"] if st else (0.5, 0.5, 0.5)
    gruen = st is not None and g >= r * 0.97 and g > b * 1.3
    fein = st["fein"] if st else 0.0
    hell = st["lum"] if st else 0.5
    if "snow" in n:
        if hell < 0.5:
            return "fels", "snow, dark (rock with snow)"
        return "schnee", "snow"
    if "wood" in n:
        return "keins", "wood (jetties, planks)"
    # verschneite Varianten anderer Boeden (DirtMediumType15Snow = TMDirt15a, 70 % von Whiteout): hell und grau
    if "snow" in klasse.lower() and "cliff" not in n and "rock" not in n and st and hell > 0.7 and st["sat"] < 0.12:
        return "schnee", "class snowy, texture light and grey"
    if any(k in n for k in ("asph", "asflt", "pklt", "conc", "road")):
        return "asphalt", "asphalt/concrete"
    if "cobb" in n:
        return "asphalt", "pavement (fine grain)"
    if n in RISSBODEN:
        return "erde", "cracked ground (dry clay)"
    if "cliff" in n or "rock" in n:
        if gruen and fein < 0.12:
            return "gras", "cliff, green and smooth (overgrown)"
        if "rock" in n and "cliff" not in n and fein > 0.2:
            return "kies", "scree (rock texture with much fine structure)"
        return "fels", "rock/cliff"
    if "crops" in n:
        return "erde", "field"
    if "gras" in n or "grass" in n:
        if gruen:
            return "gras", "grass, green"
        return "trockengras", "grass, yellow/brown"
    if "dirt" in n or "mud" in n:
        if fein > 0.11:
            return "kies", "earth with much fine structure (gravel)"
        if hell > 0.58:
            return "sand", "earth, light (sandy)"
        return "erde", "earth"
    if "sand" in n or "desert" in n or "beach" in n:
        if gruen:
            return "gras", "sand, green"
        if fein > 0.12:
            return "kies", "sand with much fine structure (scree)"
        return "sand", "sand/desert"
    # Name ohne Hinweis: INI-Klasse, dann Farbe
    if t.startswith("SNOW"):
        return ("schnee", "class snow") if hell >= 0.5 else ("fels", "class snow, dark")
    if t in ("ASPHALT", "CONCRETE", "RESIDENTIAL"):
        return "asphalt", "class asphalt/concrete/urban"
    if st is None:
        return "keins", "unknown, TGA missing"
    if gruen:
        return "gras", "colour green"
    if hell > 0.75 and st["sat"] < 0.12:
        return "schnee", "colour light, grey"
    if st["sat"] < 0.12:
        return "asphalt", "colour grey"
    if t.startswith("DESERT") or t.startswith("BEACH") or t == "SAND_ACCENT":
        return "sand", "class desert/beach"
    return "erde", "colour brown"


def cmd_zuordnung(a):
    files_zh, zh = terrain_ini(root_zh())
    files_g, gen = terrain_ini(root_g())
    alle = dict(gen)
    alle.update(zh)
    zeilen = []
    zaehler = {}
    for tga in sorted(alle):
        klasse, typ = alle[tga]
        st = statistik(files_zh, tga) or statistik(files_g, tga)
        mat, grund = material_fuer(tga, klasse, typ, st)
        zaehler[mat] = zaehler.get(mat, 0) + 1
        info = ("mean %.2f %.2f %.2f, fine %.3f" % (st["mittel"][0], st["mittel"][1], st["mittel"][2], st["fein"])) if st else "TGA missing"
        zeilen.append("%-24s = %-12s ; %s, %s, %s; %s" % (tga, mat, klasse, typ, grund, info))
    kopf = [
        "; BodenDetail.ini -- terrain detail layer, generated by tools/assets/boden_detail.py zuordnung",
        "; Terrain texture (Terrain.ini \"Texture\", Zero Hour and Generals) = detail material. Materials: keins (none), gras,",
        "; trockengras, erde, kies, sand, fels, schnee, asphalt. By name and class, otherwise by the colour statistics of the TGA.",
        "; Textures not listed get no detail. Carry hand-edited lines over when regenerating.",
        "",
    ]
    os.makedirs(os.path.dirname(ZIEL_INI), exist_ok=True)
    with open(ZIEL_INI, "w", encoding="latin-1", newline="\r\n") as f:
        f.write("\n".join(kopf + zeilen) + "\n")
    print(len(zeilen), "Texturen:", ", ".join("%s %d" % kv for kv in sorted(zaehler.items(), key=lambda kv: -kv[1])))


# -------------------------------------------------------------------------------------------------
# bestand (Zellenanteile je Gelaendetextur ueber alle Karten)
# -------------------------------------------------------------------------------------------------
def klassen_tga(root):
    """Terrain.ini: Klassenname (klein) -> TGA (klein)."""
    import texture_inventory as inv
    files, _ = inv.effective_files(root)
    k = "data\\ini\\terrain.ini"
    ini = files[k][1].read(k).decode("latin-1")
    out = {}
    for m in re.finditer(r"^\s*Terrain\s+(\S+)(.*?)^\s*End", ini, re.S | re.M | re.I):
        tex = re.search(r"Texture\s*=\s*(\S+)", m.group(2))
        if tex:
            out[m.group(1).lower()] = tex.group(1).lower()
    return out


def karten_zellen(daten):
    """Zellen je Texturklasse (Grundlage der Zelle, ohne Rand) aus BlendTileData einer Karte."""
    sys.path.insert(0, os.path.join(REPO, "tools", "render"))
    import map_props
    names, top = map_props.parse_chunks(map_props.unpack_map(daten))
    hver, hb = top["HeightMapData"]
    p = 0
    w, h = struct.unpack("<ii", hb[p:p + 8]); p += 8
    rand = struct.unpack("<i", hb[p:p + 4])[0] if hver >= 3 else 0
    ver, b = top["BlendTileData"]
    p = 0
    n, = struct.unpack("<i", b[p:p + 4]); p += 4
    tiles = np.frombuffer(b[p:p + 2 * n], dtype="<i2").reshape(h, w); p += 2 * n
    p += 2 * n							# Ueberblendung
    if ver >= 6:
        p += 2 * n						# Dreifach-Ueberblendung
    if ver >= 5:
        p += 2 * n						# Klippen
    if ver >= 7:
        p += h * ((w + 1) // 8 if ver == 7 else (w + 7) // 8)
    p += 4 + 4							# Kacheln, Ueberblendungen
    if ver >= 5:
        p += 4
    ncls, = struct.unpack("<i", b[p:p + 4]); p += 4
    klassen = []
    for _ in range(ncls):
        first, num, _wd, _leg = struct.unpack("<iiii", b[p:p + 16]); p += 16
        ln, = struct.unpack("<H", b[p:p + 2]); p += 2
        klassen.append((first, num, b[p:p + ln].decode("latin-1").lower())); p += ln
    innen = tiles[rand:h - 1 - rand, rand:w - 1 - rand] >> 2
    zaehl = {}
    for first, num, name in klassen:
        c = int(((innen >= first) & (innen < first + num)).sum())
        if c:
            zaehl[name] = zaehl.get(name, 0) + c
    return zaehl, int(innen.size)


def cmd_bestand(a):
    sys.path.insert(0, os.path.join(REPO, "tools", "render"))
    import map_props
    zuo = {}
    for z in open(ZIEL_INI, encoding="latin-1"):
        z = z.split(";")[0].strip()
        if "=" in z:
            k, v = [s.strip() for s in z.split("=", 1)]
            zuo[k.lower()] = v
    gesamt = {}				# tga -> [Summe der Kartenanteile, Zellen, Karten]
    karten = 0
    zeilen_karte = []
    for spiel, archiv, root in (("zh", "ZeroHour", root_zh()), ("generals", "Generals", root_g())):
        k2t = klassen_tga(root)
        f, ents = map_props.big_entries(map_props.ARCHIVES[archiv])
        for nm, off, size in ents:
            if not nm.endswith(".map") or nm.count("/") < 2:
                continue
            f.seek(off)
            try:
                zaehl, alle = karten_zellen(f.read(size))
            except Exception as e:
                print("uebersprungen", spiel, nm, e)
                continue
            karten += 1
            karte = nm.split("/")[-2]
            anteile = []
            for kl, c in zaehl.items():
                tga = k2t.get(kl, kl + "?")
                g = gesamt.setdefault(tga, [0.0, 0, set()])
                g[0] += c / alle
                g[1] += c
                g[2].add(spiel + ":" + karte)
                anteile.append((c / alle, tga))
            anteile.sort(reverse=True)
            zeilen_karte.append("%s;%s;%s" % (spiel, karte, " ".join("%s=%.1f%%(%s)" % (t, 100 * s, zuo.get(t, "-"))
                                                                    for s, t in anteile[:6])))
    zeilen = sorted(gesamt.items(), key=lambda kv: -kv[1][0])
    proMat = {}
    for tga, (s, c, ks) in zeilen:
        m = zuo.get(tga, "-")
        proMat[m] = proMat.get(m, 0.0) + s
    os.makedirs(a.arbeit, exist_ok=True)
    with open(os.path.join(a.arbeit, "bestand.csv"), "w", encoding="utf-8") as fo:
        fo.write("tga;material;mittlerer_anteil_prozent;zellen;karten\n")
        for tga, (s, c, ks) in zeilen:
            fo.write("%s;%s;%.3f;%d;%d\n" % (tga, zuo.get(tga, "-"), 100 * s / karten, c, len(ks)))
    with open(os.path.join(a.arbeit, "bestand_karten.csv"), "w", encoding="utf-8") as fo:
        fo.write("\n".join(zeilen_karte) + "\n")
    print(karten, "Karten. Mittlerer Zellenanteil je Material:")
    for m, s in sorted(proMat.items(), key=lambda kv: -kv[1]):
        print("  %-12s %5.1f %%" % (m, 100 * s / karten))
    print("Haeufigste Texturen:")
    for tga, (s, c, ks) in zeilen[:a.anzahl]:
        print("  %-22s %-12s %5.2f %%  in %3d Karten" % (tga, zuo.get(tga, "-"), 100 * s / karten, len(ks)))


# -------------------------------------------------------------------------------------------------
# kacheln (Vorschau)
# -------------------------------------------------------------------------------------------------
def lies_dds(pfad):
    with open(pfad, "rb") as f:
        d = f.read()
    h, w = struct.unpack("<II", d[12:20])
    return np.frombuffer(d[128:128 + w * h * 4], dtype=np.uint8).reshape(h, w, 4)


def cmd_kacheln(a):
    import texture_inventory as inv
    files, _ = inv.effective_files(root_zh())
    zuo = {}
    for z in open(ZIEL_INI, encoding="latin-1"):
        z = z.split(";")[0].strip()
        if "=" in z:
            k, v = [s.strip() for s in z.split("=", 1)]
            zuo[k.lower()] = NAMEN.get(v, 0)
    det = [lies_dds(os.path.join(ZIEL_TEX, "detail_a.dds")), lies_dds(os.path.join(ZIEL_TEX, "detail_b.dds"))]
    os.makedirs(a.arbeit, exist_ok=True)
    for tga in a.tga:
        key = "art\\terrain\\" + tga.lower()
        im = Image.open(io.BytesIO(files[key][1].read(key)))
        im.load()
        rgb = np.asarray(im.convert("RGB"), dtype=np.float64) / 255.0
        mat = zuo.get(tga.lower(), 0)
        s = a.vergr
        gross = np.asarray(Image.fromarray((rgb * 255).astype(np.uint8)).resize((rgb.shape[1] * s, rgb.shape[0] * s),
                                                                                  Image.BICUBIC), dtype=np.float64) / 255.0
        # Detail: eine Kachel (64 px Original) = GROESSE px Detail
        kachelpx = 64 * s
        d = np.ones(gross.shape[:2])
        if mat:
            kanal = det[(mat - 1) // 4][..., (mat - 1) % 4].astype(np.float64) / 128.0
            kl = np.asarray(Image.fromarray(kanal.astype(np.float32), mode="F").resize((kachelpx, kachelpx), Image.BILINEAR))
            reps = gross.shape[0] // kachelpx
            d = np.tile(kl, (reps, reps))
        mit = np.clip(gross * (1.0 + (d[..., None] - 1.0) * a.staerke), 0, 1)
        bild = np.concatenate([gross, mit], axis=1)
        Image.fromarray((bild * 255).astype(np.uint8)).save(os.path.join(a.arbeit, "kachel_%s.png" % tga.lower().replace(".tga", "")))
        print(tga, "Material", mat, "Helligkeit ohne/mit %.4f %.4f" % (gross.mean(), mit.mean()))


# -------------------------------------------------------------------------------------------------
# seite (Vergleichsseite und Einzelbilder aus den Spielaufnahmen)
# -------------------------------------------------------------------------------------------------
# alt = Filter und Boden wie bisher; _af = Gelaendefilter anisotrop 16x (-terrainAnisotropy 16); _det = Detailebene
FASSUNGEN = [("sd", "SD alt"), ("sd_af", "SD nur Filter"), ("sd_af_det", "SD Filter + Detail"), ("sd_det", "SD nur Detail"),
             ("hd", "HD-KI2 alt"), ("hd_af", "HD-KI2 nur Filter"), ("hd_af_det", "HD-KI2 Filter + Detail"),
             ("hd_det", "HD-KI2 nur Detail")]
SZENEN_TITEL = {
    "lake": "Tournament Lake, Skirmish, Standardansicht",
    "lake_nah": "Tournament Lake, Bodenfahrt boden_nah.txt (Zoom 0,35)",
    "lake_zoom": "Tournament Lake, Zoomfahrt zoom.txt",
    "alpine": "Alpine Assault, boden_detail.txt",
    "wueste": "Tournament Desert, boden_detail.txt",
    "schnee": "Winter Wolf (Schnee), boden_detail.txt",
}
LEISTE = 0.68		# Bildanteil oberhalb der Befehlsleiste (1080p: die Leiste beginnt bei rund 745 px)


def luminanz709(img):
    x = img.astype(np.float64)
    return 0.2126 * x[..., 0] + 0.7152 * x[..., 1] + 0.0722 * x[..., 2]


def cmd_seite(a):
    roh = a.roh
    ziel = a.aus
    os.makedirs(os.path.join(ziel, "bilder"), exist_ok=True)
    daten = {"fassungen": FASSUNGEN, "szenen": [], "fuss": a.fuss or ""}
    tabelle = []
    for szene in [s for s in SZENEN_TITEL if os.path.isdir(os.path.join(roh, s))]:
        basis = os.path.join(roh, szene, "sd")
        if not os.path.isdir(basis):
            continue
        shots = sorted(f for f in os.listdir(basis) if f.startswith("shot_") and f.endswith(".bmp"))
        eintraege = []
        for shot in shots:
            stem = shot[:-4]
            hell, vorh = {}, []
            for key, _ in FASSUNGEN:
                pfad = os.path.join(roh, szene, key, shot)
                if not os.path.exists(pfad):
                    continue
                img = np.asarray(Image.open(pfad).convert("RGB"))
                lum = luminanz709(img)
                hell[key] = [round(float(lum.mean()), 2), round(float(lum[:int(lum.shape[0] * LEISTE)].mean()), 2)]
                vorh.append(key)
                os.makedirs(os.path.join(ziel, "bilder", szene), exist_ok=True)
                Image.fromarray(img).save(os.path.join(ziel, "bilder", szene, "%s_%s.jpg" % (stem, key)), quality=94)
            eintraege.append({"shot": stem, "fassungen": vorh, "hell": hell})
            for k in ("sd", "hd", "sd_af", "hd_af"):
                if k in hell and k + "_det" in hell:
                    tabelle.append((szene, stem, k, hell[k][0], hell[k + "_det"][0], hell[k][1], hell[k + "_det"][1]))
        daten["szenen"].append({"name": szene, "titel": SZENEN_TITEL[szene], "beschreibung": SZENEN_TITEL[szene],
                                "shots": eintraege})
    tpl = open(os.path.join(HIER, "boden_detail_seite.html"), encoding="utf-8").read()
    open(os.path.join(ziel, "index.html"), "w", encoding="utf-8").write(
        tpl.replace("/*DATEN*/null", json.dumps(daten, ensure_ascii=False)))
    with open(os.path.join(ziel, "helligkeit.csv"), "w", encoding="utf-8") as f:
        f.write("szene;bild;stufe;ohne;mit;ohne_leiste;mit_leiste;abweichung_prozent\n")
        for s, b, k, o, m, ol, ml in tabelle:
            f.write("%s;%s;%s;%.2f;%.2f;%.2f;%.2f;%.2f\n" % (s, b, k, o, m, ol, ml, (ml / ol - 1) * 100 if ol else 0))
    # Einzelbilder: die wichtigsten Paare als verlustfreie PNG, Dateinamen so, dass A und B nebeneinander liegen
    einzeln = os.path.join(ziel, "einzelbilder")
    os.makedirs(einzeln, exist_ok=True)
    for eintrag in a.einzeln or []:
        szene, stem = eintrag.split(":")
        for k, _t in FASSUNGEN:
            pfad = os.path.join(roh, szene, k, stem + ".bmp")
            if os.path.exists(pfad):
                Image.open(pfad).convert("RGB").save(os.path.join(einzeln, "%s_%s_%s.png" % (szene, stem.replace("shot_", ""), k)))
    werte = [(ml / ol - 1) * 100 for *_x, ol, ml in tabelle if ol]
    print("Seite:", os.path.join(ziel, "index.html"), "--", len(tabelle), "Paare, Helligkeit mit/ohne Detail (ohne Leiste):",
          "min %.2f %% max %.2f %% Mittel %.2f %%" % (min(werte), max(werte), sum(werte) / len(werte)) if werte else "-")


# -------------------------------------------------------------------------------------------------
# nachweis (Zeilen fuer ASSET_CREDITS.md)
# -------------------------------------------------------------------------------------------------
def cmd_nachweis(a):
    print("| Nr. | Material | Kanal | Scan | Quelle | Autoren | Masse | Lizenz |")
    print("|---|---|---|---|---|---|---|---|")
    for nr, name, quelle, aid, p in MATERIALIEN:
        with open(os.path.join(a.download, quelle, aid, "meta.json"), encoding="utf-8") as f:
            m = json.load(f)
        masse = m.get("masse_mm") or [0, 0]
        masse = ("%.1f x %.1f m" % (masse[0] / 1000.0, masse[1] / 1000.0)) if masse[0] else "k. A."
        autoren = ", ".join("%s (%s)" % (k, v) if v not in ("All",) else k for k, v in m["autoren"].items())
        kanal = ("detail_a.dds", "detail_b.dds")[(nr - 1) // 4] + " " + "RGBA"[(nr - 1) % 4]
        print("| %d | %s | %s | %s (`%s`) | %s, <%s> | %s | %s | CC0 1.0 |" % (
            nr, name, kanal, m["name"], aid, m["quelle"], m["seite"], autoren, masse))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--download", default=DOWNLOAD, required=DOWNLOAD is None)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("laden")
    s.add_argument("ids", nargs="*", help="quelle:id (polyhaven:gravel_floor, ambientcg:Snow006); leer = alle Materialien")
    s = sub.add_parser("bauen")
    s.add_argument("--arbeit", default=os.path.join(REPO, "render-out", "boden-detail", "arbeit"))
    sub.add_parser("zuordnung")
    s = sub.add_parser("bestand", help="Zellenanteile je Gelaendetextur ueber alle Karten beider Spiele")
    s.add_argument("--arbeit", default=os.path.join(REPO, "render-out", "boden-detail2"))
    s.add_argument("--anzahl", type=int, default=60)
    sub.add_parser("nachweis", help="Tabelle Herkunft/Lizenz der Scans fuer ASSET_CREDITS.md")
    s = sub.add_parser("seite", help="Vergleichsseite aus <roh>/<szene>/<fassung>/shot_*.bmp")
    s.add_argument("--roh", default=os.path.join(REPO, "render-out", "boden-detail", "raw"))
    s.add_argument("--aus", default=os.path.join(REPO, "render-out", "boden-detail"))
    s.add_argument("--einzeln", nargs="*", help="szene:shot_000240 ... als PNG-Einzelbilder")
    s.add_argument("--fuss", help="Hinweistext unter der Tabelle")
    s = sub.add_parser("kacheln")
    s.add_argument("tga", nargs="+")
    s.add_argument("--arbeit", default=os.path.join(REPO, "render-out", "boden-detail", "arbeit"))
    s.add_argument("--vergr", type=int, default=4)
    s.add_argument("--staerke", type=float, default=1.0)
    a = ap.parse_args()
    {"laden": cmd_laden, "bauen": cmd_bauen, "zuordnung": cmd_zuordnung, "bestand": cmd_bestand, "kacheln": cmd_kacheln,
     "nachweis": cmd_nachweis, "seite": cmd_seite}[a.cmd](a)


if __name__ == "__main__":
    main()
