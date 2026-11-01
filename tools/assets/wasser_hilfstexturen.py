#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour (Electronic Arts).
# Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Free helper maps of the simple water path, procedural (no foreign or EA image is read).

The engine uses them as follows (Data/Shaders/sm4/riverwater.ps.hlsl, trapezoidwater.ps.hlsl, W3DWaterTracks.cpp):
  wasser_glitzern.dds    sparkle x noise is added to the water colour: small bright specks
  wasser_rauschen.dds    the noise is a mask in camera space (repeat 1/16): where the specks show
  wasser_flusskante.dds  rivers: alpha cuts the river across its width (u across, v along, wrapped), rgb adds a
                         faint light seam at the banks; constant along v, otherwise bands cross every river
  wasser_brandung.dds    breaker of the water tracks on beaches: 256 x 128, wave front near the bottom edge, spray
                         fading upwards, transparent top and bottom rows

Target values (measured against the replaced files, only as numbers): sparkle mean 0.04, 5.5 % above 0.3, 1.3 % above
0.7, specks of 1 to 2 texels; noise mean 0.18, 17.5 % above 0.5, blobs of about 5 % of the tile; edge alpha 0 at the
border, 1 from 16 % inwards on both sides, light seam about 0.03; breaker alpha rising from the top to about 0.35 at
90 % of the height, 0 in the last rows.

Example:
  python tools/assets/wasser_hilfstexturen.py --ziel Data/Textures/Wasser --vorschau render-out/wasser-hilfstexturen
"""
import argparse
import os

import numpy as np

from wasser_texturen import mip_kette, schreibe_dds, zu_bgra


def rauschen(h, w, sigma_y, sigma_x, saat):
    """Periodic smooth noise (mean 0, std 1); sigma in texels per axis."""
    rng = np.random.default_rng(saat)
    x = rng.standard_normal((h, w))
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.fftfreq(w)[None, :]
    g = np.exp(-2.0 * np.pi ** 2 * ((sigma_y * fy) ** 2 + (sigma_x * fx) ** 2))
    x = np.real(np.fft.ifft2(np.fft.fft2(x) * g))
    return (x - x.mean()) / max(x.std(), 1e-9)


def schwelle_fuer_anteil(feld, anteil):
    return float(np.quantile(feld, 1.0 - anteil))


def glatt(x, weich):
    return np.clip(x / weich + 0.5, 0.0, 1.0)


def glitzern(n=256):
    """Small irregular specks, clustered a little (glints come in groups on real water)."""
    fein = rauschen(n, n, 0.7, 0.7, 9301)
    gruppe = rauschen(n, n, 14, 14, 9302)
    feld = fein + 0.55 * gruppe
    t = schwelle_fuer_anteil(feld, 0.055)
    hell = np.clip((feld - t) / (feld.max() - t), 0, 1)
    wert = np.where(feld > t, 0.32 + 0.68 * hell ** 0.35, 0.0)
    wert += 0.012 * np.clip(fein + 1.0, 0, None)
    return np.clip(wert, 0, 1)


def maske(n=128):
    """Blobs of light on dark: where the specks may show."""
    feld = rauschen(n, n, 3.1, 3.1, 9311) + 0.35 * rauschen(n, n, 1.0, 1.0, 9312)
    t = schwelle_fuer_anteil(feld, 0.175)
    return np.clip(glatt(feld - t, 0.35) * 0.92 + 0.05 * glatt(feld - t + 0.8, 0.8), 0, 1)


def flusskante(n=256):
    """Alpha across the river (u), constant along it (v); a faint light seam at the banks."""
    u = (np.arange(n) + 0.5) / n
    d = np.minimum(u, 1.0 - u)
    t = np.clip(d / 0.16, 0, 1)
    alpha = t * t * (3 - 2 * t)
    a = np.broadcast_to(alpha[None, :], (n, n)).copy()
    # seam: sparse light flecks near both banks, periodic along v
    flecken = rauschen(n, n, 1.6, 1.2, 9321)
    saum = np.clip(1.0 - d / 0.2, 0, 1)[None, :] ** 0.7
    rgb = np.clip((flecken - 0.6) * 0.2, 0, 0.45) * saum
    return np.stack([rgb, rgb, rgb, a], axis=-1)


def brandung(w=256, h=128):
    """Breaker: a thin bright crest line near the bottom, fine foam above it, spires of spray fading upwards."""
    y = (np.arange(h) + 0.5) / h
    front_lage = np.clip(0.86 + 0.035 * rauschen(1, w, 1, 9, 9331)[0], 0.78, 0.92)
    abstand = front_lage[None, :] - y[:, None]			# > 0 above the crest
    streifen = rauschen(h, w, 10, 3.2, 9332)				# spires
    fein = rauschen(h, w, 0.7, 0.7, 9333)				# fine foam grain
    fasern = rauschen(h, w, 3.0, 0.8, 9335)				# short vertical fibres
    hoehe = np.clip(abstand / 0.8, 0, 1)
    breit = 0.75 + 0.5 * glatt(rauschen(1, w, 1, 20, 9334)[0], 1.6)[None, :]	# spray higher in places
    spray = (1.0 - hoehe) ** 1.2 * glatt(streifen + 0.8 - 1.1 * hoehe / breit, 0.9) * breit
    spray *= np.clip(0.35 + 0.4 * glatt(fein + 0.2, 1.4) + 0.3 * glatt(fasern, 1.4), 0, 1.1)
    spray += 0.5 * glatt(fein - 2.2, 0.3) * (1.0 - hoehe) * glatt(streifen + 1.2, 1.0)	# droplets
    # crest: a thin bright line along the front, frayed by the foam grain
    kamm = glatt(0.016 + 0.006 * fein - np.abs(abstand - 0.012), 0.012)
    alpha = np.maximum(spray * 0.66, kamm * 0.9)
    alpha *= glatt(abstand + 0.002, 0.008)				# nothing below the crest
    alpha[:2] = 0
    alpha[-3:] = 0
    rgb = np.ones((h, w))
    return np.stack([rgb, rgb, rgb, np.clip(alpha, 0, 1)], axis=-1)


def grau(x):
    return np.stack([x, x, x, np.ones_like(x)], axis=-1)


def schreibe(ordner, name, bild):
    schreibe_dds(os.path.join(ordner, name), [zu_bgra(stufe) for stufe in mip_kette(bild.astype(np.float32))])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ziel", default="Data/Textures/Wasser")
    ap.add_argument("--vorschau", default="render-out/wasser-hilfstexturen")
    a = ap.parse_args()
    os.makedirs(a.ziel, exist_ok=True)
    karten = {
        "wasser_glitzern.dds": grau(glitzern()),
        "wasser_rauschen.dds": grau(maske()),
        "wasser_flusskante.dds": flusskante(),
        "wasser_brandung.dds": brandung(),
    }
    for name, bild in karten.items():
        schreibe(a.ziel, name, bild)
        print("%-22s %dx%d  rgb %.3f  alpha %.3f" % (name, bild.shape[1], bild.shape[0], bild[..., 0].mean(), bild[..., 3].mean()))
    if a.vorschau:
        from PIL import Image
        os.makedirs(a.vorschau, exist_ok=True)
        for name, bild in karten.items():
            stamm = name[:-4]
            Image.fromarray(np.clip(np.rint(bild[..., 0] * 255), 0, 255).astype(np.uint8)).save(
                os.path.join(a.vorschau, stamm + "_rgb.png"))
            Image.fromarray(np.clip(np.rint(bild[..., 3] * 255), 0, 255).astype(np.uint8)).save(
                os.path.join(a.vorschau, stamm + "_alpha.png"))


if __name__ == "__main__":
    main()
