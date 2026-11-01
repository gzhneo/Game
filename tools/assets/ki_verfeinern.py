#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""KI-Verfeinerung per Diffusion fuer build_texture_tiers.py.

Nimmt die 4x-Ergebnisse eines GAN-Hochskalierers (etwa realesrgan-x4plus) und laesst Stable Diffusion 1.5 mit dem
ControlNet "Tile" (lllyasviel/control_v11f1e_sd15_tile) plausible Oberflaechendetails ergaenzen -- das Verfahren
von "Ultimate SD Upscale": img2img mit geringer Staerke, das ControlNet haelt Form und Farbe am Ausgangsbild fest.
Gerechnet wird in ueberlappenden 512er-Kacheln, weich ueberblendet.

Laeuft NICHT mit dem System-Python, sondern in einer eigenen Umgebung mit PyTorch (ROCm oder CUDA) und diffusers,
z. B. <GZH_WERKZEUGE>\\ki-venv. Modelle laedt Hugging Face beim ersten Lauf in HF_HOME (Vorgabe hier
<GZH_WERKZEUGE>\\hf-cache): stable-diffusion-v1-5/stable-diffusion-v1-5 (CreativeML OpenRAIL-M) und
lllyasviel/control_v11f1e_sd15_tile (OpenRAIL). Das Werkzeug gibt nichts davon weiter.

    <ki-python> tools/assets/ki_verfeinern.py --ein <ordner> --aus <ordner> [--staerke 0.35] [--staerken datei.json]

Wiederaufnahme: jede fertige Datei wird atomar geschrieben und beim naechsten Lauf uebersprungen. --staerken nennt je
Dateiname eine eigene Staerke (0 = nur kopieren), sonst gilt --staerke.
"""
import argparse
import json
import os
import shutil
import sys
import time

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import pfade  # noqa: E402

PROMPT = "highly detailed game texture, sharp focus, realistic materials, fine surface detail, best quality"
NEGATIVE = "blurry, soft, lowres, jpeg artifacts, text, letters, watermark, cartoon, painting"


def tiles(n, tile, overlap):
    if n <= tile:
        return [0]
    step = tile - overlap
    pos = list(range(0, n - tile + 1, step))
    if pos[-1] + tile < n:
        pos.append(n - tile)
    return pos


def refine(pipe, gen, img, strength, steps, tile=512, overlap=64, seed=1):
    """img: HxWx3 uint8 (4x-Ergebnis). Ueberlappende Kacheln, Gewicht linear zum Kachelrand."""
    import torch
    h, w = img.shape[:2]
    if h < 64 or w < 64:
        return img
    th, tw = min(tile, h), min(tile, w)
    # SD braucht Vielfache von 8
    th, tw = th - th % 8, tw - tw % 8
    out = np.zeros((h, w, 3), np.float32)
    wsum = np.zeros((h, w, 1), np.float32)
    ramp = lambda n: np.minimum(np.minimum(np.arange(n) + 1, np.arange(n)[::-1] + 1), overlap).astype(np.float32)
    wgt = np.minimum(ramp(th)[:, None], ramp(tw)[None, :])[..., None]
    for y in tiles(h, th, overlap):
        for x in tiles(w, tw, overlap):
            crop = Image.fromarray(np.ascontiguousarray(img[y:y + th, x:x + tw]))
            gen.manual_seed(seed)
            with torch.no_grad():
                r = pipe(prompt=PROMPT, negative_prompt=NEGATIVE, image=crop, control_image=crop, strength=strength,
                         num_inference_steps=steps, guidance_scale=6.0, controlnet_conditioning_scale=1.0,
                         generator=gen, height=th, width=tw).images[0]
            out[y:y + th, x:x + tw] += np.asarray(r, np.float32) * wgt
            wsum[y:y + th, x:x + tw] += wgt
    res = img.astype(np.float32).copy()
    m = wsum[..., 0] > 0
    res[m] = out[m] / wsum[m]
    return np.clip(np.rint(res), 0, 255).astype(np.uint8)


def lesbar(path):
    """Fertig ist nur eine vollstaendig lesbare Ausgabe; ein Stromausfall kann trotz atomarem Umbenennen eine
    halb geschriebene Datei hinterlassen (so geschehen am 2026-09-24). Die wird neu gerechnet."""
    if not os.path.exists(path):
        return False
    try:
        with Image.open(path) as im:
            im.load()
        return True
    except Exception:
        os.remove(path)
        return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ein", required=True)
    ap.add_argument("--aus", required=True)
    ap.add_argument("--staerke", type=float, default=0.35)
    ap.add_argument("--staerken", help="JSON {dateiname: staerke}")
    ap.add_argument("--schritte", type=int, default=30, help="Schritte bei Staerke 1 (wirksam: schritte x staerke)")
    a = ap.parse_args()
    if pfade.werkzeuge(pflicht=False):
        os.environ.setdefault("HF_HOME", pfade.werkzeuge("hf-cache"))
    import torch
    from diffusers import ControlNetModel, StableDiffusionControlNetImg2ImgPipeline
    per = json.load(open(a.staerken)) if a.staerken else {}
    os.makedirs(a.aus, exist_ok=True)
    todo = [f for f in sorted(os.listdir(a.ein)) if f.lower().endswith(".png") and not lesbar(os.path.join(a.aus, f))]
    print(f"Verfeinern: {len(todo)} Bilder offen", flush=True)
    if not todo:
        return 0
    cn = ControlNetModel.from_pretrained("lllyasviel/control_v11f1e_sd15_tile", torch_dtype=torch.float16)
    pipe = StableDiffusionControlNetImg2ImgPipeline.from_pretrained(
        "stable-diffusion-v1-5/stable-diffusion-v1-5", controlnet=cn, variant="fp16", torch_dtype=torch.float16,
        safety_checker=None, requires_safety_checker=False).to("cuda" if torch.cuda.is_available() else "cpu")
    pipe.set_progress_bar_config(disable=True)
    gen = torch.Generator(pipe.device)
    t0 = time.time()
    for i, f in enumerate(todo):
        src, dst = os.path.join(a.ein, f), os.path.join(a.aus, f)
        s = float(per.get(f, a.staerke))
        if s <= 0:
            shutil.copyfile(src, dst + ".tmp")
        else:
            img = np.asarray(Image.open(src).convert("RGB"))
            Image.fromarray(refine(pipe, gen, img, s, a.schritte)).save(dst + ".tmp", format="PNG")
        os.replace(dst + ".tmp", dst)
        if (i + 1) % 50 == 0 or i + 1 == len(todo):
            el = time.time() - t0
            print(f"  {i + 1}/{len(todo)}, {el:.0f} s, Rest etwa {el / (i + 1) * (len(todo) - i - 1) / 60:.0f} min", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
