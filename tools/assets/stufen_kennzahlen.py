#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Kennzahlen von Texturstufen gegen die SD-Texturen des Spiels.

Liest je Stufe das Manifest und die Archive, die build_texture_tiers.py geschrieben hat, und je Textur die SD-Fassung
aus dem Asset-Verzeichnis (erster Treffer wie im Spiel). Jede Stufentextur wird auf SD-Groesse gemittelt und auf den
sichtbaren Pixeln der SD-Textur verglichen:

  * Helligkeit: Luma (0,299 R + 0,587 G + 0,114 B) der Stufe zu SD, in Prozent Abweichung;
  * Saettigung: (max - min) / max je Pixel, Stufe zu SD, in Prozent Abweichung;
  * PSNR gegen SD;
  * Zahl der Texturen je Verfahren (KI, klassisch), Rueckfaelle, im Rueckvergleich verworfene KI-Ergebnisse.

Die Prozentwerte sind pixelgewichtet (Summe Stufe / Summe SD) ueber alle Texturen einer Gruppe; dazu der Median je
Textur. Ausgabe: <aus>\\kennzahlen.json und <aus>\\kennzahlen.md, mit --bilder N dazu eine Vergleichsseite
<aus>\\index.html (SD, dann jede Stufe; die N KI-Texturen mit der groessten Saettigungsabweichung in der ersten Stufe).

    python tools/assets/stufen_kennzahlen.py --sd <Asset-Verzeichnis> ^
        --stufe alt=<...\\Texturstufen\\Sicherung\\HD> --stufe neu=<...\\Texturstufen\\HD-KI3> --aus <Ordner> --bilder 40
    python tools/assets/stufen_kennzahlen.py --selftest

Braucht numpy und Pillow (BC7 lesen ab Pillow 9).
"""
import argparse
import io
import json
import os
import shutil
import sys
import tempfile
from collections import Counter, defaultdict
from concurrent.futures import ProcessPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import texture_inventory as inv  # noqa: E402

import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402

GROUPS = ("alle", "ki", "klassisch")


def decode(data):
    img = Image.open(io.BytesIO(data))
    img.load()
    return np.array(img.convert("RGBA"), dtype=np.uint8)


def box_down(rgb, width, height):
    return np.stack([np.array(Image.fromarray(np.ascontiguousarray(rgb[..., c], dtype=np.float32), "F")
                              .resize((width, height), Image.BOX), dtype=np.float32) for c in range(3)], -1)


def luma(rgb):
    return rgb[..., 0] * 0.299 + rgb[..., 1] * 0.587 + rgb[..., 2] * 0.114


def saturation(rgb):
    mx = rgb.max(axis=-1)
    mn = rgb.min(axis=-1)
    return np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-6), 0.0)


def psnr(a, b):
    mse = float(((a.astype(np.float64) - b.astype(np.float64)) ** 2).mean())
    return 99.0 if mse <= 1e-9 else 10.0 * np.log10(255.0 * 255.0 / mse)


def measure(stage_rgba, sd_rgba):
    """Sums over the visible SD pixels: luma and saturation of stage and SD, pixel count, PSNR."""
    h, w = sd_rgba.shape[:2]
    mask = sd_rgba[..., 3] >= 8
    if not mask.any():
        return None
    small = box_down(stage_rgba[..., :3].astype(np.float32), w, h)[mask]
    ref = sd_rgba[..., :3].astype(np.float32)[mask]
    return {"n": int(mask.sum()), "luma": float(luma(small).sum()), "luma_sd": float(luma(ref).sum()),
            "sat": float(saturation(small).sum()), "sat_sd": float(saturation(ref).sum()),
            "psnr": round(psnr(small, ref), 2)}


_cache = {}


def _archive(path):
    if path not in _cache:
        _cache[path] = inv.BigArchive(path)
    return _cache[path]


def _texture_job(args):
    name, sd_path, stage_reads = args
    try:
        sd = decode(_archive(sd_path).read(name))
    except Exception as e:
        return name, {"error": f"SD: {type(e).__name__}: {e}"}
    out = {}
    for stage, (path, entry) in stage_reads.items():
        try:
            out[stage] = measure(decode(_archive(path).read(entry)), sd)
        except Exception as e:
            out[stage] = {"error": f"{type(e).__name__}: {e}"}
    return name, out


def load_stage(folder):
    with open(os.path.join(folder, "manifest.json"), "r", encoding="ascii") as f:
        manifest = json.load(f)
    by_entry = {}
    for a in manifest.get("archives", []):
        p = os.path.join(folder, a["archive"])
        if os.path.exists(p):
            for entry in inv.BigArchive(p).entries:
                by_entry[entry] = p
    return manifest, by_entry


def summarize_group(values):
    """values: list of measure() results. Pixel-weighted deviation in percent plus per-texture median."""
    vals = [v for v in values if v and "n" in v]
    if not vals:
        return {"texturen": 0}
    luma_dev = 100.0 * (sum(v["luma"] for v in vals) / max(sum(v["luma_sd"] for v in vals), 1e-6) - 1.0)
    sat_dev = 100.0 * (sum(v["sat"] for v in vals) / max(sum(v["sat_sd"] for v in vals), 1e-6) - 1.0)
    per_luma = [100.0 * (v["luma"] / v["luma_sd"] - 1.0) for v in vals if v["luma_sd"] > 0]
    per_sat = [100.0 * (v["sat"] / v["sat_sd"] - 1.0) for v in vals if v["sat_sd"] > 0]
    return {"texturen": len(vals), "helligkeit_prozent": round(luma_dev, 2), "saettigung_prozent": round(sat_dev, 2),
            "helligkeit_median": round(float(np.median(per_luma)), 2) if per_luma else None,
            "saettigung_median": round(float(np.median(per_sat)), 2) if per_sat else None,
            "psnr_median": round(float(np.median([v["psnr"] for v in vals])), 2)}


def counts(manifest):
    tex = manifest.get("textures", [])
    c = {"texturen": len(tex),
         "ok": sum(1 for m in tex if m.get("status") == "ok"),
         "ki": sum(1 for m in tex if m.get("status") == "ok" and m.get("method") == "ki"),
         "klassisch": sum(1 for m in tex if m.get("status") == "ok" and m.get("method") == "klassisch"),
         "rueckfall": sum(1 for m in tex if m.get("status") == "rueckfall"),
         "ki_verworfen": sum(1 for m in tex if m.get("ai_rejected")),
         "farbe_angeglichen": sum(1 for m in tex if m.get("ai_color"))}
    kinds = Counter(m["kind"] for m in tex if m.get("status") == "ok" and m.get("method") == "ki")
    c["ki_je_art"] = dict(sorted(kinds.items()))
    rejected = Counter(m["kind"] for m in tex if m.get("ai_rejected"))
    c["ki_verworfen_je_art"] = dict(sorted(rejected.items()))
    return c


def evaluate(sd_root, stages, jobs=1, names=None, limit=None, common=False):
    """stages: [(label, folder)]. Returns the report dict and the raw measurements."""
    files, _ = inv.effective_files(sd_root)
    loaded = {label: load_stage(folder) for label, folder in stages}
    metas = {label: {m["name"].lower(): m for m in man["textures"]} for label, (man, _) in loaded.items()}
    ok_sets = [{n for n, m in ms.items() if m.get("status") == "ok"} for ms in metas.values()]
    # common: only textures every stage has, so a sample run compares like with like
    wanted = sorted(set.intersection(*ok_sets) if common else set().union(*ok_sets))
    if names:
        wanted = [n for n in wanted if any(p in n for p in names)]
    if limit:
        wanted = wanted[:limit]
    tasks = []
    for name in wanted:
        if name not in files:
            continue
        reads = {}
        for label, (_, by_entry) in loaded.items():
            m = metas[label].get(name)
            if m and m.get("status") == "ok":
                entry = m["output"].lower().replace("/", "\\")
                if entry in by_entry:
                    reads[label] = (by_entry[entry], entry)
        if reads:
            tasks.append((name, os.path.join(sd_root, files[name][0]), reads))
    results = {}
    if jobs > 1:
        with ProcessPoolExecutor(max_workers=jobs) as pool:
            for name, r in pool.map(_texture_job, tasks, chunksize=8):
                results[name] = r
    else:
        for t in tasks:
            name, r = _texture_job(t)
            results[name] = r
    report = {"sd": sd_root, "stufen": {}}
    for label, folder in stages:
        man = loaded[label][0]
        groups = defaultdict(list)
        errors = 0
        for name, r in results.items():
            v = r.get(label)
            if v is None:
                continue
            if "error" in v:
                errors += 1
                continue
            m = metas[label][name]
            kind = m.get("kind", "?")
            method = "ki" if m.get("method") == "ki" else "klassisch"
            for g in ("alle", method, f"{method}|{kind}"):
                groups[g].append(v)
        report["stufen"][label] = {
            "ordner": folder, "stufe": man.get("stage"), "spiel": man.get("game"),
            "format": man.get("texture_format"),
            "ai_min_psnr": man.get("params", {}).get("ai_min_psnr"),
            "ai_match_color": bool(man.get("params", {}).get("ai_match_color")),
            "anzahl": counts(man), "lesefehler": errors,
            "gruppen": {g: summarize_group(v) for g, v in sorted(groups.items())},
        }
    return report, results


def write_markdown(report, path):
    labels = list(report["stufen"])
    lines = ["# Kennzahlen der Texturstufen gegen SD", "", f"SD: `{report['sd']}`", ""]
    lines.append("| | " + " | ".join(labels) + " |")
    lines.append("|---|" + "---|" * len(labels))

    def row(title, fn):
        lines.append(f"| {title} | " + " | ".join(fn(report["stufen"][l]) for l in labels) + " |")

    row("Stufe / Format", lambda s: f"{s['stufe']} / {s['format']}")
    row("Farbangleichung, KI-Schwelle", lambda s: f"{'ja' if s['ai_match_color'] else 'nein'}, {s['ai_min_psnr']} dB")
    row("Texturen ok / KI / klassisch", lambda s: f"{s['anzahl']['ok']} / {s['anzahl']['ki']} / {s['anzahl']['klassisch']}")
    row("KI je Art", lambda s: ", ".join(f"{k} {n}" for k, n in s["anzahl"]["ki_je_art"].items()) or "-")
    row("KI im Rueckvergleich verworfen", lambda s: f"{s['anzahl']['ki_verworfen']} ("
        + (", ".join(f"{k} {n}" for k, n in s["anzahl"]["ki_verworfen_je_art"].items()) or "-") + ")")
    row("Rueckfall auf SD", lambda s: str(s["anzahl"]["rueckfall"]))

    def fmt(g, key):
        return lambda s: ("-" if not s["gruppen"].get(g, {}).get("texturen")
                          else f"{s['gruppen'][g][key]:+.1f} % (Median {s['gruppen'][g][key.split('_')[0] + '_median']:+.1f})"
                          if key.endswith("prozent") else f"{s['gruppen'][g][key]:.1f}")

    for g in ("alle", "ki", "klassisch"):
        row(f"Helligkeit gegen SD ({g})", fmt(g, "helligkeit_prozent"))
        row(f"Saettigung gegen SD ({g})", fmt(g, "saettigung_prozent"))
        row(f"PSNR-Median gegen SD ({g}), dB", fmt(g, "psnr_median"))
    kinds = sorted({g for s in report["stufen"].values() for g in s["gruppen"] if g.startswith("ki|")})
    for g in kinds:
        row(f"Saettigung {g.replace('|', ' ')}", fmt(g, "saettigung_prozent"))
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def write_page(report, results, sd_root, stages, out, count):
    """Comparison page: SD (nearest-neighbour enlarged) and every stage side by side."""
    first = stages[0][0]
    files, _ = inv.effective_files(sd_root)
    loaded = {label: load_stage(folder) for label, folder in stages}
    metas = {label: {m["name"].lower(): m for m in man["textures"]} for label, (man, _) in loaded.items()}
    cand = []
    for name, r in results.items():
        v = r.get(first)
        if v and "n" in v and v["sat_sd"] > 0 and metas[first].get(name, {}).get("method") == "ki":
            cand.append((abs(v["sat"] / v["sat_sd"] - 1.0), name))
    pick = [n for _, n in sorted(cand, reverse=True)[:count]]
    img_dir = os.path.join(out, "bilder")
    os.makedirs(img_dir, exist_ok=True)
    rows = []
    for i, name in enumerate(pick):
        sd = decode(_archive(os.path.join(sd_root, files[name][0])).read(name))
        cells = []
        size = None
        for label, (man, by_entry) in loaded.items():
            m = metas[label].get(name)
            entry = m["output"].lower().replace("/", "\\") if m and m.get("status") == "ok" else None
            if entry and entry in by_entry:
                img = decode(_archive(by_entry[entry]).read(entry))
                size = size or (img.shape[1], img.shape[0])
                cells.append((label, img, m))
            else:
                cells.append((label, None, m))
        size = size or (sd.shape[1], sd.shape[0])
        scale = min(1.0, 512.0 / max(size))
        show = (max(1, int(size[0] * scale)), max(1, int(size[1] * scale)))
        tds = []
        fn = f"{i:03d}_sd.png"
        Image.fromarray(sd).resize(show, Image.NEAREST).save(os.path.join(img_dir, fn))
        tds.append(f"<td><img src='bilder/{fn}'><br>SD {sd.shape[1]}x{sd.shape[0]}</td>")
        for label, img, m in cells:
            if img is None:
                tds.append(f"<td>{label}: {m.get('status') if m else 'fehlt'}</td>")
                continue
            fn = f"{i:03d}_{label}.png"
            Image.fromarray(img).resize(show, Image.LANCZOS).save(os.path.join(img_dir, fn))
            v = results[name].get(label) or {}
            dev = (f"Hell. {100 * (v['luma'] / v['luma_sd'] - 1):+.1f} %, Saett. {100 * (v['sat'] / v['sat_sd'] - 1):+.1f} %, "
                   f"{v['psnr']:.1f} dB") if v.get("n") and v["luma_sd"] > 0 and v["sat_sd"] > 0 else ""
            tds.append(f"<td><img src='bilder/{fn}'><br>{label}: {m.get('method')}, {dev}</td>")
        rows.append(f"<tr><th colspan='{len(tds)}'>{name}</th></tr><tr>{''.join(tds)}</tr>")
    html = ("<!doctype html><meta charset='utf-8'><title>Texturstufen</title>"
            "<style>body{background:#222;color:#ddd;font:13px sans-serif}td{vertical-align:top;padding:4px}"
            "th{text-align:left;padding-top:12px}img{image-rendering:auto;max-width:512px}</style>"
            f"<h1>Texturstufen gegen SD</h1><p>{len(pick)} KI-Texturen mit der groessten Saettigungsabweichung in "
            f"'{first}'.</p><table>{''.join(rows)}</table>")
    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as f:
        f.write(html)
    return len(pick)


def selftest():
    import build_texture_tiers as btt
    fails = []

    def check(cond, what):
        if not cond:
            fails.append(what)
            print("  FEHLGESCHLAGEN:", what)

    rng = np.random.default_rng(3)
    with tempfile.TemporaryDirectory() as tmp:
        sd_root = os.path.join(tmp, "assets")
        os.makedirs(sd_root)
        sds, entries = {}, []
        for i in range(6):
            t = np.zeros((32, 32, 4), np.uint8)
            t[..., :3] = np.clip(rng.normal((140, 100, 60), (30, 20, 15), (32, 32, 3)), 0, 255).astype(np.uint8)
            t[..., 3] = 255
            name = f"art\\textures\\t{i}.tga"
            sds[name] = t
            entries.append((name, btt.encode_tga(t, "none")))
        btt.write_big(os.path.join(sd_root, "Textures.big"), entries)

        def stage(label, transform, method):
            folder = os.path.join(tmp, label)
            os.makedirs(folder)
            ents, tex = [], []
            for name, t in sds.items():
                up = np.repeat(np.repeat(t, 2, axis=0), 2, axis=1)
                big = transform(up, t)
                # uncompressed, so the test measures the tool and not the DXT error of a noise pattern
                out = name
                ents.append((out, btt.encode_tga(big, "none")))
                tex.append({"name": name, "output": out, "status": "ok", "method": method(name), "kind": "Modell"})
            tex.append({"name": "art\\textures\\fehlt.tga", "status": "rueckfall", "kind": "Modell"})
            btt.write_big(os.path.join(folder, "TexturesHD.big"), ents)
            with open(os.path.join(folder, "manifest.json"), "w") as f:
                json.dump({"stage": label, "game": "zh", "texture_format": "DXT1/DXT5", "params": {"ai_min_psnr": 20},
                           "archives": [{"archive": "TexturesHD.big"}], "textures": tex}, f)
            return folder

        def grey(up, _t):
            g = up.copy()
            g[..., :3] = np.clip(up[..., :3] * 0.8 + 0.2 * 128 - 6, 0, 255).astype(np.uint8)
            return g

        def matched(up, t):
            return btt.match_colors(grey(up, t), t)[0]

        ki = lambda n: "ki" if n[-5] in "0123" else "klassisch"
        alt = stage("alt", grey, ki)
        neu = stage("neu", matched, ki)
        report, results = evaluate(sd_root, [("alt", alt), ("neu", neu)], jobs=1)
        a, n = report["stufen"]["alt"], report["stufen"]["neu"]
        check(a["anzahl"]["ok"] == 6 and a["anzahl"]["ki"] == 4 and a["anzahl"]["rueckfall"] == 1,
              f"Kennzahlen: Zaehlung {a['anzahl']}")
        ga, gn = a["gruppen"]["ki"], n["gruppen"]["ki"]
        check(ga["saettigung_prozent"] < -10 and ga["helligkeit_prozent"] < -1.5,
              f"Kennzahlen: grauere Stufe erkannt ({ga['helligkeit_prozent']} %, {ga['saettigung_prozent']} %)")
        check(abs(gn["saettigung_prozent"]) < 2.0 and abs(gn["helligkeit_prozent"]) < 1.0,
              f"Kennzahlen: angeglichene Stufe wie SD ({gn['helligkeit_prozent']} %, {gn['saettigung_prozent']} %)")
        check(gn["psnr_median"] > ga["psnr_median"], "Kennzahlen: PSNR steigt mit der Angleichung")
        check(n["gruppen"]["alle"]["texturen"] == 6 and "ki|Modell" in n["gruppen"], "Kennzahlen: Gruppen")
        out = os.path.join(tmp, "aus")
        os.makedirs(out)
        write_markdown(report, os.path.join(out, "kennzahlen.md"))
        md = open(os.path.join(out, "kennzahlen.md"), encoding="utf-8").read()
        check("| Texturen ok / KI / klassisch | 6 / 4 / 2 | 6 / 4 / 2 |" in md, "Kennzahlen: Tabelle")
        shown = write_page(report, results, sd_root, [("alt", alt), ("neu", neu)], out, 3)
        check(shown == 3 and os.path.exists(os.path.join(out, "bilder", "000_neu.png")), "Kennzahlen: Vergleichsseite")
        r2, _ = evaluate(sd_root, [("alt", alt)], jobs=2)
        part = os.path.join(tmp, "probe")
        os.makedirs(part)
        man = json.load(open(os.path.join(neu, "manifest.json")))
        man["textures"] = man["textures"][:2]
        with open(os.path.join(part, "manifest.json"), "w") as f:
            json.dump(man, f)
        shutil.copy(os.path.join(neu, "TexturesHD.big"), part)
        r3, _ = evaluate(sd_root, [("alt", alt), ("probe", part)], common=True)
        check(r3["stufen"]["alt"]["gruppen"]["alle"]["texturen"] == 2, "Kennzahlen: nur gemeinsame Texturen")
        check(r2["stufen"]["alt"]["gruppen"]["ki"] == ga, "Kennzahlen: parallel gleich")
    print(f"Selbsttest: {'bestanden' if not fails else str(len(fails)) + ' Fehler'}")
    return 0 if not fails else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sd", help="Asset-Verzeichnis des Spiels (SD-Texturen)")
    ap.add_argument("--stufe", action="append", default=[], help="NAME=Ordner mit manifest.json (mehrfach, Reihenfolge der Spalten)")
    ap.add_argument("--aus", help="Ausgabeordner")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    ap.add_argument("--names", help="nur Texturen, deren Pfad einen dieser Teile enthaelt (kommagetrennt)")
    ap.add_argument("--limit", type=int)
    ap.add_argument("--bilder", type=int, default=0, help="Vergleichsseite mit N KI-Texturen")
    ap.add_argument("--gemeinsam", action="store_true",
                    help="nur Texturen, die jede Stufe hat (Probelauf gegen einen vollen Satz)")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.sd or not a.stufe or not a.aus:
        ap.error("--sd, --stufe und --aus sind noetig")
    stages = []
    for s in a.stufe:
        label, _, folder = s.partition("=")
        if not folder or not os.path.exists(os.path.join(folder, "manifest.json")):
            raise SystemExit(f"--stufe {s}: NAME=Ordner mit manifest.json erwartet")
        stages.append((label, folder))
    os.makedirs(a.aus, exist_ok=True)
    names = [n.lower() for n in a.names.split(",")] if a.names else None
    report, results = evaluate(a.sd, stages, a.jobs, names, a.limit, a.gemeinsam)
    with open(os.path.join(a.aus, "kennzahlen.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, indent=1, sort_keys=True)
    write_markdown(report, os.path.join(a.aus, "kennzahlen.md"))
    if a.bilder:
        write_page(report, results, a.sd, stages, a.aus, a.bilder)
    print(open(os.path.join(a.aus, "kennzahlen.md"), encoding="utf-8").read())
    return 0


if __name__ == "__main__":
    sys.exit(main())
