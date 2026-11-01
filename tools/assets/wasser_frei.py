#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour (Electronic Arts).
# Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Freie Wasserfarbtexturen aus CC0-HDRIs von Poly Haven.

    python tools/assets/wasser_frei.py laden  [--roh <GZH_WERKZEUGE>\\cc0\\wasser_roh]
    python tools/assets/wasser_frei.py bauen  [--roh ...] [--ziel Data/Textures/Wasser] [--vorschau <ordner>]

Ersetzt (Vorgabe, `-waterTexture ea` waehlt die EA-Kachel) die gemalten EA-Kacheln der Wasserfarbe (StandingWaterTexture
aus Water.ini bzw. map.ini, WaterTexture der WaterSets). Die Erzeugung liest KEINE EA-Datei: Farbe, Streuung und
Deckkraft jeder Textur stehen als eigene feste Werte in der Tabelle TEXTUREN unten.

Verfahren je Textur:
  1. Quelle ist ein Panorama (equirektangulaer, 8k, Radiance .hdr) mit Wasser unter dem Horizont. Ein rechteckiger
     Ausschnitt der Wasserebene wird senkrecht von oben gesehen (Ebene eine Kamerahoehe unter dem Objektiv,
     Koordinaten in Kamerahoehen; x zeigt zur Bildmitte des Panoramas, y nach links), ohne Steg, Gelaender, Ufer.
  2. Nur die Wellenstruktur wird uebernommen: Logarithmus der Helligkeit minus seine starke Unschaerfe (entfernt den
     Verlauf von Spiegelung und Licht ueber den Ausschnitt), auf Streuung 1 gebracht, Glanzspitzen weich gekappt.
  3. Nahtlos: der Ausschnitt wird auf 640 Pixel gebracht, die Kachel hat 512; die 128 Pixel Ueberhang werden an den
     gegenueberliegenden Rand ueberblendet (Gewichte mit w1^2 + w2^2 = 1, damit die Streuung im Saum gleich bleibt).
     Erst waagerecht, dann senkrecht.
  4. Farbe: eigener Zielton (sRGB-Mittelwert), eigene Streuung der Helligkeit (sRGB, 0..255), dazu eine grossflaechige
     Fleckigkeit aus eigenem kachelbarem 1/f-Rauschen (fester Startwert) und eine leichte Aufhellung der Wellenkaemme
     zum Weiss hin. Mittelwert und Streuung werden nachgeregelt, bis sie die Zielwerte treffen.
  5. Ausgabe 512 x 512, A8R8G8B8 mit voller Mip-Kette (wie die uebrigen Wassertexturen, W3DWater.cpp
     loadWater2Dds), Alpha fest (eigener Wert: Deckkraft der Fluesse, beim stehenden Wasser ohne Wirkung).

Das Ergebnis ist fest: zweiter Lauf mit denselben Rohdateien ergibt dieselben Dateien.
"""
import argparse
import json
import os
import sys
import urllib.request

import numpy as np
from scipy.ndimage import gaussian_filter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wasser_texturen import lies_hdr, mip_kette, schreibe_dds, zu_bgra, rauschen_1f  # noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import pfade  # noqa: E402

WURZEL = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
API = 'https://api.polyhaven.com'
KOPF = {'User-Agent': 'gzh-neo (TODO 92, Einzelabruf)'}
AUFLOESUNG = '8k'
KACHEL = 512
UEBERHANG = 128

# Datei -> Quelle (Poly-Haven-HDRI, Ausschnitt x0, x1, y0, y1 in Kamerahoehen), eigener Zielton (sRGB), eigene
# Streuung der Helligkeit (sRGB 0..255), Anteil der Fleckigkeit, wahlweise Hochpass (Pixel), Alpha (0..255), Startwert
# der Fleckigkeit.
# Quellen: See/Meer die windbewegte Seeflaeche neben dem Steg (Lake Pier), Fluss und trueb die ruhigere, fein
# gekraeuselte Flaeche am Hafen (Small Harbour Morning).
# Zieltoene: See/Meer blaugruen, Fluss gedaempftes Blaugruen (dunkler und trueber als der See, aber Wasserton), trueb
# lehmbraun (fuer TSWater03, Schlammwasser).
# Die Helligkeit liegt bei 85 bis 90 von 255, damit das Wasser auf den Karten weder heller noch dunkler wirkt als
# gewohnt; geprueft wird das im Spiel (Vergleichsseite), nicht an EA-Dateien.
TEXTUREN = {
    'wasser_see.dds': dict(quelle='lake_pier', ausschnitt=(-1.2, 0.3, 2.5, 4.0), ton=(40, 98, 112), streu=19.0,
                           flecken=0.10, alpha=192, saat=921),
    'wasser_fluss.dds': dict(quelle='small_harbour_morning', ausschnitt=(-2.8, -0.8, 0.8, 2.8), ton=(50, 90, 94),
                             streu=14.0, flecken=0.03, hochpass=24.0, alpha=192, saat=922),
    'wasser_trueb.dds': dict(quelle='small_harbour_morning', ausschnitt=(-2.8, -0.8, 0.8, 2.8), ton=(106, 88, 62),
                             streu=12.0, flecken=0.04, hochpass=24.0, alpha=224, saat=923),
}


# --- Laden ---------------------------------------------------------------------------------------------------------

def hole(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=KOPF), timeout=900) as r:
        return r.read()


def hdr_datei(asset, roh):
    return os.path.join(roh, asset, '%s_%s.hdr' % (asset, AUFLOESUNG))


def laden(roh):
    for asset in sorted({t['quelle'] for t in TEXTUREN.values()}):
        datei = hdr_datei(asset, roh)
        if os.path.exists(datei):
            continue
        os.makedirs(os.path.dirname(datei), exist_ok=True)
        info = json.loads(hole('%s/info/%s' % (API, asset)))
        e = json.loads(hole('%s/files/%s' % (API, asset)))['hdri'][AUFLOESUNG]['hdr']
        daten = hole(e['url'])
        with open(datei, 'wb') as f:
            f.write(daten)
        info['_datei'] = e['url']
        json.dump(info, open(os.path.join(roh, asset, 'info.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
        print('geladen', asset, len(daten))


# --- Draufsicht auf die Wasserebene --------------------------------------------------------------------------------

def draufsicht(hdr, x0, x1, y0, y1, n):
    """n x n Abtastung der Ebene z = -1 (Kamerahoehe 1) im Rechteck [x0,x1] x [y0,y1], y1 oben im Bild."""
    H, W = hdr.shape[:2]
    xs = np.linspace(x0, x1, n)
    ys = np.linspace(y1, y0, n)
    xx, yy = np.meshgrid(xs, ys)
    tiefe = np.arctan2(1.0, np.hypot(xx, yy))					# Winkel unter dem Horizont
    phi = np.arctan2(yy, xx)
    u = (0.5 + phi / (2 * np.pi)) % 1.0 * W - 0.5
    v = (0.5 + tiefe / np.pi) * H - 0.5
    u0 = np.floor(u).astype(int)
    v0 = np.clip(np.floor(v).astype(int), 0, H - 2)
    fu = (u - u0)[..., None]
    fv = np.clip(v - v0, 0.0, 1.0)[..., None]
    u0 %= W
    u1 = (u0 + 1) % W
    return ((hdr[v0, u0] * (1 - fu) + hdr[v0, u1] * fu) * (1 - fv) +
            (hdr[v0 + 1, u0] * (1 - fu) + hdr[v0 + 1, u1] * fu) * fv)


def wellenstruktur(rgb, hochpass=None):
    """Hochpass des Helligkeitslogarithmus (Grenze in Pixeln, Vorgabe ein Achtel der Breite), Streuung 1, Spitzen
    weich auf etwa 2,5 gekappt. Ein engerer Hochpass nimmt grosse Flecken der Quelle heraus, die beim Kacheln auffielen."""
    lum = 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]
    lg = np.log(np.maximum(lum, 1e-6))
    d = lg - gaussian_filter(lg, hochpass or rgb.shape[0] / 8.0, mode='nearest')
    d = (d - d.mean()) / (d.std() + 1e-9)
    return 2.5 * np.tanh(d / 2.5)


def nahtlos(d, n, m):
    """(n+m)^2 -> n^2, periodisch: der Ueberhang m wird an den Anfang ueberblendet (Streuung bleibt erhalten)."""
    t = (np.arange(m) + 0.5) / m
    w_neu = np.sin(0.5 * np.pi * t)							# Anteil des Inneren
    w_alt = np.cos(0.5 * np.pi * t)							# Anteil des Ueberhangs
    a = d.copy()
    a[:, :m] = d[:, :m] * w_neu[None, :] + d[:, n:n + m] * w_alt[None, :]
    a = a[:, :n]
    b = a.copy()
    b[:m, :] = a[:m, :] * w_neu[:, None] + a[n:n + m, :] * w_alt[:, None]
    return b[:n, :]


# --- Farbe ---------------------------------------------------------------------------------------------------------

def zu_linear(s):
    s = np.asarray(s, np.float64) / 255.0
    return np.where(s <= 0.04045, s / 12.92, ((s + 0.055) / 1.055) ** 2.4)


def zu_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, 12.92 * c, 1.055 * c ** (1 / 2.4) - 0.055)


def einfaerben(d, flecken_feld, ton, streu, flecken):
    """Zielton und Streuung (sRGB 0..255) auf die Struktur d legen; Rueckgabe sRGB 0..1."""
    ton = np.asarray(ton, np.float64)
    basis = zu_linear(ton)
    k = 0.2
    gain = np.ones(3)
    for _ in range(40):
        lin = basis[None, None, :] * gain[None, None, :] * np.exp(k * d + flecken * flecken_feld)[..., None]
        kamm = np.clip(d - 1.0, 0.0, None)[..., None] * 0.06			# Kaemme leicht zum Weiss
        lin = lin + kamm * (1.0 - lin)
        s = zu_srgb(lin) * 255.0
        mittel = s.reshape(-1, 3).mean(0)
        lum = s @ np.array([0.2126, 0.7152, 0.0722])
        gain *= (ton / np.maximum(mittel, 1e-3)) ** 2.2
        k *= streu / max(lum.std(), 1e-3)
    return s / 255.0


def bauen(roh, ziel, vorschau):
    os.makedirs(ziel, exist_ok=True)
    if vorschau:
        os.makedirs(vorschau, exist_ok=True)
    cache = {}
    for name, t in TEXTUREN.items():
        if t['quelle'] not in cache:
            cache.clear()
            cache[t['quelle']] = np.minimum(lies_hdr(hdr_datei(t['quelle'], roh)), 60000.0)
        hdr = cache[t['quelle']]
        rgb = draufsicht(hdr, *t['ausschnitt'], KACHEL + UEBERHANG)
        d = nahtlos(wellenstruktur(rgb, t.get('hochpass')), KACHEL, UEBERHANG)
        d = (d - d.mean()) / (d.std() + 1e-9)
        rng = np.random.default_rng(t['saat'])
        fl = rauschen_1f(KACHEL, rng, 3.0, 1, 6)
        fl = (fl - fl.mean()) / (fl.std() + 1e-9)
        srgb = einfaerben(d, fl, t['ton'], t['streu'], t['flecken'])
        rgba = np.concatenate([srgb, np.full(srgb.shape[:2] + (1,), t['alpha'] / 255.0)], axis=2)
        schreibe_dds(os.path.join(ziel, name), [zu_bgra(s) for s in mip_kette(rgba)])
        q = np.clip(np.rint(srgb * 255.0), 0, 255)
        lum = q @ np.array([0.2126, 0.7152, 0.0722])
        print('%-18s Mittel RGB %s, Helligkeit %.1f, Streuung %.1f' % (name, np.round(q.reshape(-1, 3).mean(0), 1), lum.mean(), lum.std()))
        if vorschau:
            from PIL import Image
            Image.fromarray(q.astype(np.uint8)).save(os.path.join(vorschau, name.replace('.dds', '.png')))
            Image.fromarray(np.tile(q, (2, 2, 1)).astype(np.uint8)).save(os.path.join(vorschau, name.replace('.dds', '_2x2.png')))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('befehl', choices=['laden', 'bauen'])
    vorgabe = pfade.werkzeuge('cc0', 'wasser_roh', pflicht=False)
    ap.add_argument('--roh', default=vorgabe, required=vorgabe is None)
    ap.add_argument('--ziel', default=os.path.join(WURZEL, 'Data', 'Textures', 'Wasser'))
    ap.add_argument('--vorschau')
    a = ap.parse_args()
    if a.befehl == 'laden':
        laden(a.roh)
    else:
        bauen(a.roh, a.ziel, a.vorschau)


if __name__ == '__main__':
    main()
