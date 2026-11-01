# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Texturen fuer das Wasser erzeugen:
#
#   python tools/assets/wasser_texturen.py [--roh <GZH_WERKZEUGE>\cc0\hdri] [--ziel Data/Textures/Wasser] [--vorschau <ordner>]
#
# 1. wellen.dds (256x256, volle Mip-Kette): zwei kachelbare Wellenfelder, prozedural aus einem Seegangsspektrum
#    (Phillips-Spektrum mit Windrichtung, zufaellige Phasen mit festem Startwert) per FFT gerechnet. Ganzzahlige
#    Wellenzahlen, dadurch nahtlos. Gespeichert ist die Neigung (dh/dx, dh/dy), nicht die Normale: gemittelt in den
#    Mip-Stufen bleibt sie richtig (glatter in der Ferne). R,G = Lage A (Windwellen), B,A = Lage B (Kraeuselung).
# 2. schaum.dds (256x256): R = Schaummuster (Zellrauschen und 1/f-Rauschen, kachelbar, gleichverteilt), G =
#    grossraeumige Variation (kachelbares 1/f-Rauschen, gegen sichtbare Kachelung der Wellen), B = zweites
#    Schaummuster (feiner), A = 255.
# 3. himmel_<tageszeit>.dds (256x256, obere Halbkugel stereografisch, volle Mip-Kette): Umgebungskarte fuer die
#    Spiegelung aus je einem CC0-HDRI von Poly Haven (Liste HDRIS unten, Herkunft in ASSET_CREDITS.md). Die Sonne
#    wird gekappt (Sonnenglanz rechnet der Shader aus der Lichtrichtung der Karte), das Bild so gedreht, dass die
#    Sonne in Richtung +x steht (der Shader dreht sie auf die Lichtrichtung der Karte), auf mittlere Himmelshelligkeit 1
#    zwischen 10 und 70 Grad Hoehe normiert und als Gamma 2,2 mit Faktor 0,5 gespeichert (Mittel ~ 0,5, Spielraum bis 2).
#    Unter dem Horizont: Horizontfarbe (die Spiegelung schaut nie dorthin).
#
# 4. wind.dds (256x256): grossraeumige Felder fuer Boeen, Wellenmischung, Kraeuselflecken und Farbe (Runde 3,
#    nach Referenzfotos: echtes Wasser ist fleckig, nicht gleichmaessig bewegt).
#
# Alle Ausgaben sind DDS mit 32 Bit A8R8G8B8 (BGRA im Speicher) und voller Mip-Kette -- gelesen von
# W3DWater.cpp (loadWaterTexture). Das Ergebnis ist fest (Startwert), zweiter Lauf erzeugt dieselben Dateien.
import argparse
import json
import os
import sys
import struct
import urllib.request

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import pfade  # noqa: E402

WURZEL = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
API = 'https://api.polyhaven.com'
KOPF = {'User-Agent': 'cnc-generals-modernisierung (TODO 61, Einzelabruf)'}

# Tageszeit des Spiels -> Poly-Haven-HDRI (CC0) und Hoehe in Grad, ab der das Bild freier Himmel ist
HDRIS = {
    'morgen': ('citrus_orchard_road_puresky', 4.0),
    # Nutzervorschlag war sundowner_overlook: dort reichen Terrasse und ein kahler Baum bis rund 40 Grad Hoehe, sie
    # stuenden als dunkler Fleck in der Spiegelung. Statt dessen ein Himmel mit Wolken.
    'tag': ('kloofendal_48d_partly_cloudy_puresky', 4.0),
    'abend': ('belfast_sunset_puresky', 4.0),
    'nacht': ('kloppenheim_02_puresky', 4.0),
}


# --- DDS -----------------------------------------------------------------------------------------------------------

def mip_kette(bild, wrap_u=True):
    """Box-Verkleinerung bis 1x1. bild: float32 HxWxC in 0..1 (oder Neigungen), Mittel ueber 2x2."""
    stufen = [bild]
    while bild.shape[0] > 1 or bild.shape[1] > 1:
        h, w = bild.shape[:2]
        if h > 1:
            bild = 0.5 * (bild[0::2] + bild[1::2])
        if w > 1:
            bild = 0.5 * (bild[:, 0::2] + bild[:, 1::2])
        stufen.append(bild)
    return stufen


def schreibe_dds(pfad, stufen_bgra):
    """stufen_bgra: Liste uint8-Felder HxWx4 in der Reihenfolge B,G,R,A (A8R8G8B8)."""
    h, w = stufen_bgra[0].shape[:2]
    flags = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000		# CAPS HEIGHT WIDTH PITCH PIXELFORMAT MIPMAPCOUNT
    kopf = struct.pack('<4sIIIIIII', b'DDS ', 124, flags, h, w, w * 4, 0, len(stufen_bgra))
    kopf += b'\0' * 44
    kopf += struct.pack('<II4sIIIII', 32, 0x41, b'\0\0\0\0', 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    kopf += struct.pack('<IIIII', 0x1000 | 0x8 | 0x400000, 0, 0, 0, 0)
    assert len(kopf) == 128
    with open(pfad, 'wb') as f:
        f.write(kopf)
        for s in stufen_bgra:
            f.write(np.ascontiguousarray(s, dtype=np.uint8).tobytes())


def zu_bgra(rgba):
    q = np.clip(np.rint(rgba * 255.0), 0, 255).astype(np.uint8)
    return q[..., [2, 1, 0, 3]]


# --- Wellen --------------------------------------------------------------------------------------------------------

def wellenfeld(n, rng, wind_deg, peak_lambda, klein_lambda, streuung):
    """Kachelbares Hoehenfeld aus einem Phillips-Spektrum; Rueckgabe Neigung (sx, sy) je Texel in Hoehe/Kachelbreite."""
    k = np.fft.fftfreq(n, 1.0 / n)							# ganzzahlige Wellenzahlen je Kachel
    kx, ky = np.meshgrid(k, k)
    kl = np.hypot(kx, ky)
    kl[0, 0] = 1.0
    w = np.array([np.cos(np.radians(wind_deg)), np.sin(np.radians(wind_deg))])
    kpeak = n / peak_lambda
    cosw = (kx * w[0] + ky * w[1]) / kl
    richtung = streuung + (1.0 - streuung) * cosw ** 2			# etwas Seegang auch quer zum Wind
    spektrum = np.exp(-(kpeak / kl) ** 2) / kl ** 4 * richtung
    spektrum *= np.exp(-(kl / (n / klein_lambda)) ** 2)			# kleinste Wellen daempfen (Mip 0 ohne Flimmern)
    spektrum[0, 0] = 0.0
    amp = np.sqrt(spektrum) * (rng.standard_normal((n, n)) + 1j * rng.standard_normal((n, n)))
    sx = np.real(np.fft.ifft2(1j * 2 * np.pi * kx * amp))
    sy = np.real(np.fft.ifft2(1j * 2 * np.pi * ky * amp))
    s = np.sqrt(np.mean(sx ** 2 + sy ** 2))
    return sx / s, sy / s									# Einheit: mittlere Neigung 1


def rauschen_1f(n, rng, beta, kmin, kmax):
    """Kachelbares Rauschen mit Leistungsspektrum 1/k^beta zwischen kmin und kmax (Wellenzahl je Kachel), 0..1."""
    k = np.fft.fftfreq(n, 1.0 / n)
    kx, ky = np.meshgrid(k, k)
    kl = np.hypot(kx, ky)
    maske = (kl >= kmin) & (kl <= kmax)
    kl[0, 0] = 1.0
    amp = np.where(maske, kl ** (-beta / 2.0), 0.0)
    f = np.real(np.fft.ifft2(amp * (rng.standard_normal((n, n)) + 1j * rng.standard_normal((n, n)))))
    f = (f - f.mean()) / (f.std() + 1e-9)
    return np.clip(0.5 + 0.18 * f, 0.0, 1.0)


def zellrauschen(n, rng, zellen):
    """Kachelbares Zellrauschen (Worley): F2 - F1 der Abstaende zu zufaelligen Punkten, ergibt Schaumlinien."""
    punkte = (np.arange(zellen)[:, None] + rng.random((zellen, 2)) * 0.0)
    gx, gy = np.meshgrid(np.arange(zellen), np.arange(zellen))
    px = (gx + rng.random((zellen, zellen))) / zellen
    py = (gy + rng.random((zellen, zellen))) / zellen
    px, py = px.ravel(), py.ravel()
    del punkte
    u = (np.arange(n) + 0.5) / n
    uu, vv = np.meshgrid(u, u)
    f1 = np.full((n, n), 9.0)
    f2 = np.full((n, n), 9.0)
    for i in range(px.size):
        dx = np.abs(uu - px[i]); dx = np.minimum(dx, 1.0 - dx)
        dy = np.abs(vv - py[i]); dy = np.minimum(dy, 1.0 - dy)
        d = np.hypot(dx, dy)
        f2 = np.where(d < f1, f1, np.minimum(f2, d))
        f1 = np.minimum(f1, d)
    return (f2 - f1) * zellen


def zellrauschen_f1(n, rng, zellen):
    """Kachelbares Zellrauschen: Abstand zum naechsten zufaelligen Punkt (F1), 0 am Punkt, etwa 1 am Zellrand."""
    gx, gy = np.meshgrid(np.arange(zellen), np.arange(zellen))
    px = ((gx + rng.random((zellen, zellen))) / zellen).ravel()
    py = ((gy + rng.random((zellen, zellen))) / zellen).ravel()
    u = (np.arange(n) + 0.5) / n
    uu, vv = np.meshgrid(u, u)
    f1 = np.full((n, n), 9.0)
    for i in range(px.size):
        dx = np.abs(uu - px[i]); dx = np.minimum(dx, 1.0 - dx)
        dy = np.abs(vv - py[i]); dy = np.minimum(dy, 1.0 - dy)
        f1 = np.minimum(f1, np.hypot(dx, dy))
    return np.clip(f1 * zellen / 0.75, 0.0, 1.0)


def gleichverteilt(feld):
    """Werte auf ihren Rang abbilden (0..1 gleichverteilt)."""
    flach = feld.ravel()
    rang = np.empty(flach.size)
    rang[np.argsort(flach, kind='stable')] = (np.arange(flach.size) + 0.5) / flach.size
    return rang.reshape(feld.shape)


def wellen_und_schaum(ziel, vorschau):
    n = 256
    rng = np.random.default_rng(61)
    ax, ay = wellenfeld(n, rng, 30.0, 40.0, 3.0, 0.15)		# Lage A: Windwellen, Spitze bei 1/40 der Kachel
    bx, by = wellenfeld(n, rng, 110.0, 14.0, 2.0, 0.35)		# Lage B: Kraeuselung, andere Richtung
    skala = 3.0													# +-3 Standardabweichungen auf 0..1
    rgba = np.stack([ax, ay, bx, by], axis=-1) / (2.0 * skala) + 0.5
    rgba = np.clip(rgba, 0.0, 1.0).astype(np.float32)
    schreibe_dds(os.path.join(ziel, 'wellen.dds'), [zu_bgra(s) for s in mip_kette(rgba)])

    # Schaum: blasige Zellen (1 - F1 des Zellrauschens, weich) mal 1/f-Rauschen, dann auf Gleichverteilung gebracht
    # (Rang). Der Shader vergleicht mit einer Schwelle: der Anteil Schaum ist dann gleich der gewuenschten Dichte, auch
    # wenn die Mip-Stufen das Muster in der Ferne mitteln.
    z1 = zellrauschen_f1(n, rng, 7)
    z2 = zellrauschen_f1(n, rng, 17)
    schaum1 = gleichverteilt((1.0 - z1) * 0.55 + rauschen_1f(n, rng, 1.6, 2, 48) * 0.9)
    schaum2 = gleichverteilt((1.0 - z2) * 0.5 + rauschen_1f(n, rng, 1.3, 6, 96) * 0.9)
    variation = rauschen_1f(n, rng, 3.0, 1, 12)
    rgba = np.stack([schaum1, variation, schaum2, np.ones((n, n))], axis=-1).astype(np.float32)
    schreibe_dds(os.path.join(ziel, 'schaum.dds'), [zu_bgra(s) for s in mip_kette(rgba)])
    if vorschau:
        from PIL import Image
        wl = np.clip((np.stack([ax, ay, np.zeros_like(ax)], -1) / 6 + 0.5) * 255, 0, 255).astype(np.uint8)
        Image.fromarray(wl).save(os.path.join(vorschau, 'wellen_a.png'))
        wl = np.clip((np.stack([bx, by, np.zeros_like(bx)], -1) / 6 + 0.5) * 255, 0, 255).astype(np.uint8)
        Image.fromarray(wl).save(os.path.join(vorschau, 'wellen_b.png'))
        Image.fromarray((schaum1 * 255).astype(np.uint8)).save(os.path.join(vorschau, 'schaum1.png'))
        Image.fromarray((schaum2 * 255).astype(np.uint8)).save(os.path.join(vorschau, 'schaum2.png'))
        Image.fromarray((variation * 255).astype(np.uint8)).save(os.path.join(vorschau, 'variation.png'))


def rauschen_gestreckt(n, rng, beta, kmin, kmax, wind_deg, streckung):
    """Wie rauschen_1f, aber entlang der Windrichtung gestreckt (Boeenfelder sind laenglich), normiert auf Mittel 0,
    Standardabweichung 1."""
    k = np.fft.fftfreq(n, 1.0 / n)
    kx, ky = np.meshgrid(k, k)
    c, s_ = np.cos(np.radians(wind_deg)), np.sin(np.radians(wind_deg))
    k_laengs = kx * c + ky * s_
    k_quer = -kx * s_ + ky * c
    kl = np.hypot(k_laengs * streckung, k_quer)
    maske = (np.hypot(kx, ky) >= kmin) & (kl <= kmax)
    kl[0, 0] = 1.0
    amp = np.where(maske, kl ** (-beta / 2.0), 0.0)
    f = np.real(np.fft.ifft2(amp * (rng.standard_normal((n, n)) + 1j * rng.standard_normal((n, n)))))
    return (f - f.mean()) / (f.std() + 1e-9)


def wind(ziel, vorschau):
    """wind.dds (256x256, Runde 3 nach Referenzfotos): R Boeenfelder (laenglich in Windrichtung 30 Grad, grob),
    G Mischung der Wellenlagen (Richtung und Massstab, sehr grob), B feine Kraeuselflecken ("cat's paws"),
    A Farbvariation (Trübung, grob). Eigener Startwert, aendert die uebrigen Texturen nicht."""
    n = 256
    rng = np.random.default_rng(62)
    boeen = rauschen_gestreckt(n, rng, 2.6, 2, 24, 30.0, 2.5)
    mischung = rauschen_gestreckt(n, rng, 3.2, 1, 8, 30.0, 1.0)
    flecken = rauschen_gestreckt(n, rng, 1.6, 10, 70, 30.0, 1.8)
    farbe = rauschen_gestreckt(n, rng, 3.0, 1, 10, 0.0, 1.0)
    kanal = lambda f, k: np.clip(0.5 + k * f, 0.0, 1.0)
    rgba = np.stack([kanal(boeen, 0.22), kanal(mischung, 0.25), kanal(flecken, 0.2), kanal(farbe, 0.2)], axis=-1).astype(np.float32)
    schreibe_dds(os.path.join(ziel, 'wind.dds'), [zu_bgra(t) for t in mip_kette(rgba)])
    if vorschau:
        from PIL import Image
        Image.fromarray((rgba[..., :3] * 255).astype(np.uint8)).save(os.path.join(vorschau, 'wind.png'))


# --- Himmel --------------------------------------------------------------------------------------------------------

def hole(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=KOPF), timeout=600) as r:
        return r.read()


def hdri_laden(asset, roh):
    ordner = os.path.join(roh, asset)
    os.makedirs(ordner, exist_ok=True)
    datei = os.path.join(ordner, asset + '_1k.hdr')
    if not os.path.exists(datei):
        info = json.loads(hole('%s/info/%s' % (API, asset)))
        dateien = json.loads(hole('%s/files/%s' % (API, asset)))
        e = dateien['hdri']['1k']['hdr']
        daten = hole(e['url'])
        with open(datei, 'wb') as f:
            f.write(daten)
        info['_datei'] = e['url']
        json.dump(info, open(os.path.join(ordner, 'info.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
        print('geladen', asset, len(daten))
    return datei


def lies_hdr(pfad):
    """Radiance-RGBE (.hdr) mit neuer Lauflaengenkodierung -> float32 HxWx3 (linear)."""
    daten = open(pfad, 'rb').read()
    p = 0
    while True:
        e = daten.index(b'\n', p)
        zeile = daten[p:e]
        p = e + 1
        if zeile.startswith(b'-Y') or zeile.startswith(b'+Y'):
            teile = zeile.split()
            h, w = int(teile[1]), int(teile[3])
            break
    bild = np.zeros((h, w, 4), np.uint8)
    for y in range(h):
        if daten[p] == 2 and daten[p + 1] == 2 and ((daten[p + 2] << 8) | daten[p + 3]) == w:
            p += 4
            for c in range(4):
                x = 0
                while x < w:
                    z = daten[p]; p += 1
                    if z > 128:
                        z -= 128
                        bild[y, x:x + z, c] = daten[p]; p += 1
                    else:
                        bild[y, x:x + z, c] = np.frombuffer(daten, np.uint8, z, p); p += z
                    x += z
        else:
            bild[y] = np.frombuffer(daten, np.uint8, w * 4, p).reshape(w, 4); p += w * 4
    e = bild[..., 3].astype(np.int32)
    f = np.where(e > 0, np.ldexp(1.0, e - 136), 0.0)
    return (bild[..., :3].astype(np.float64) * f[..., None]).astype(np.float32)


def lum(c):
    return 0.2126 * c[..., 0] + 0.7152 * c[..., 1] + 0.0722 * c[..., 2]


def himmel(asset, klar_ab, roh, ziel, name, vorschau):
    hdr = lies_hdr(hdri_laden(asset, roh))
    h, w = hdr.shape[:2]
    # auf 512x256 mitteln (Rauschen der Nachtbilder weg), dann weiter wie unten
    hdr = hdr.reshape(256, h // 256, 512, w // 512, 3).mean(axis=(1, 3))
    h, w = hdr.shape[:2]
    hoehe = 90.0 - (np.arange(h) + 0.5) / h * 180.0			# Grad ueber dem Horizont je Zeile
    L = lum(hdr)
    # Sonne: hellster Punkt ueber dem Horizont
    y0, x0 = np.unravel_index(np.argmax(np.where(hoehe[:, None] > 0, L, 0)), L.shape)
    sonne_u = (x0 + 0.5) / w
    band = (hoehe > 10) & (hoehe < 70)
    mittel = L[band].mean()
    median = np.median(L[band])
    # Sonne und Sonnenhof kappen (Farbton bleibt): hoechstens das 2,5-fache des Medians
    grenze = 2.5 * median
    faktor = np.minimum(1.0, grenze / np.maximum(L, 1e-9))
    hdr = hdr * faktor[..., None]
    L = lum(hdr)
    mittel = L[band].mean()
    hdr = hdr / mittel
    # Unter der Hoehe klar_ab (Gelaende, Baeume, gestreckter Horizont der Pure-Sky-Bilder) die Zeile bei klar_ab,
    # quer weich gemittelt (ein Achtel der Breite, rundum): die Spiegelung schaut fast nie dorthin, soll dann aber
    # keinen Streifen und keine Baumsilhouette zeigen.
    zeile = hdr[np.argmin(np.abs(hoehe - klar_ab))]
    breite = w // 8
    kern = np.ones(breite) / breite
    glatt = np.stack([np.convolve(np.concatenate([zeile[-breite:, c], zeile[:, c], zeile[:breite, c]]), kern, 'same')[breite:-breite]
                      for c in range(3)], axis=-1)
    hdr[hoehe < klar_ab] = glatt
    # Sonne nach u = 0 (Richtung +x) drehen
    hdr = np.roll(hdr, -int(round(sonne_u * w)), axis=1)
    # Obere Halbkugel stereografisch auf 256x256 (Mitte = Zenit, Rand = Horizont): der Shader braucht dafuer keine
    # Winkelfunktionen (uv = R.xy / (1 + R.z) * 0.5 + 0.5), anders als bei der abstandstreuen Karte.
    n = 256
    x = (np.arange(n) + 0.5) / n * 2.0 - 1.0
    X, Y = np.meshgrid(x, x)
    r2 = np.minimum(X * X + Y * Y, 1.0)
    dx, dy, dz = 2.0 * X / (1.0 + r2), 2.0 * Y / (1.0 + r2), (1.0 - r2) / (1.0 + r2)
    az = np.mod(np.arctan2(dy, dx), 2.0 * np.pi)
    el = np.degrees(np.arcsin(np.clip(dz, -1.0, 1.0)))
    fu = az / (2.0 * np.pi) * w - 0.5
    fv = (90.0 - el) / 180.0 * h - 0.5
    u0 = np.floor(fu).astype(int); v0 = np.clip(np.floor(fv).astype(int), 0, h - 2)
    au = (fu - u0)[..., None]; av = np.clip(fv - v0, 0.0, 1.0)[..., None]
    u0 %= w; u1 = (u0 + 1) % w
    halb = (hdr[v0, u0] * (1 - au) + hdr[v0, u1] * au) * (1 - av) + (hdr[v0 + 1, u0] * (1 - au) + hdr[v0 + 1, u1] * au) * av
    ldr = np.clip(np.power(np.maximum(halb, 0.0), 1.0 / 2.2) * 0.5, 0.0, 1.0)
    rgba = np.concatenate([ldr, np.ones(ldr.shape[:2] + (1,))], axis=-1).astype(np.float32)
    schreibe_dds(os.path.join(ziel, 'himmel_%s.dds' % name), [zu_bgra(s) for s in mip_kette(rgba)])
    print('himmel_%s: %s, Sonne bei u=%.3f Hoehe %.1f Grad, Himmelsmittel %.4g (Median %.4g), max nach Kappung %.2f' % (
        name, asset, sonne_u, hoehe[y0], mittel, median, float(ldr.max())))
    if vorschau:
        from PIL import Image
        Image.fromarray((ldr * 255).astype(np.uint8)).save(os.path.join(vorschau, 'himmel_%s.png' % name))


def main():
    ap = argparse.ArgumentParser()
    vorgabe = pfade.werkzeuge('cc0', 'hdri', pflicht=False)
    ap.add_argument('--roh', default=vorgabe, required=vorgabe is None)
    ap.add_argument('--ziel', default=os.path.join(WURZEL, 'Data', 'Textures', 'Wasser'))
    ap.add_argument('--vorschau')
    a = ap.parse_args()
    os.makedirs(a.ziel, exist_ok=True)
    if a.vorschau:
        os.makedirs(a.vorschau, exist_ok=True)
    wellen_und_schaum(a.ziel, a.vorschau)
    wind(a.ziel, a.vorschau)
    for name, (asset, klar_ab) in HDRIS.items():
        himmel(asset, klar_ab, a.roh, a.ziel, name, a.vorschau)


if __name__ == '__main__':
    main()
