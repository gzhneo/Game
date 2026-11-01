# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Aufnahmen fuer die freien Baeume: Kamerafahrten zu Waeldern, Laeufe ueber die Warteschlange
(tools/abnahme/slot.py), Pixelvergleich zweier Fassungen und die Vergleichsseite mit Schieber.

Szenen: die drei Pilotkarten (ForgottenForestZH, Red Rock, Tournament Lake) und drei weitere fuer den Nachweis, dass
der Baum-Puffer ohne freie Baeume pixelgleich zeichnet (Alpine Assault, Fortress Avalanche, Shell-Map).

Aufruf:
  python tools/render/baum_aufnahmen.py lauf --fassung vorher --zh <GZHNeoZH.exe> [--nur wald,tl] [--stufe hd]
        [--mod <ordner>] [--extra ...]
  python tools/render/baum_aufnahmen.py vergleich vorher nachher      abweichende Pixel je Bild (0 = pixelgleich)
  python tools/render/baum_aufnahmen.py seite --links ea --rechts frei [--titel ...]
Bilder: render-out/baeume/raw/<szene>/<fassung>/shot_*.bmp, Seite render-out/baeume/seite/baeume_vergleich.html
"""
import argparse
import base64
import io
import json
import os
import subprocess
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HIER))
sys.path.insert(0, os.path.join(REPO, "tools", "abnahme"))
PFADE = os.path.join(REPO, "tests", "render", "paths")
AUS = os.path.join(REPO, "render-out", "baeume")

# Szene: (Szene des Runners, Karte, Pfad, Takt, Titel)
SZENEN = {
    "wald":     ("skirmish", "ForgottenForestZH",  "baum_wald.txt",    60,  "ForgottenForestZH (Hartriegel, Herbsteichen)"),
    "redrock":  ("skirmish", "Red Rock",           "baum_redrock.txt", 60,  "Red Rock (Tannen, Fichten, Ahorn; Hartriegel 2)"),
    "tl":       ("skirmish", "Tournament Lake",    "baum_tl.txt",      60,  "Tournament Lake (Hartriegel 4/5, Kiefern)"),
    "iron":     ("skirmish", "Iron Dragon",        "baum_iron.txt",    60,  "Iron Dragon (Herbstkarte, Herbsteichen)"),
    "alpine":   ("skirmish", "Alpine Assault",     "berge_alpine.txt", 120, "Alpine Assault (Schnee-Nadelbaeume)"),
    "fortress": ("skirmish", "Fortress Avalanche", "fels_fortress.txt", 120, "Fortress Avalanche"),
    "shell":    ("shellmap", None,                 "cc0_shell.txt",    60,  "Shell-Map"),
}
PILOT = ("tl", "wald", "redrock", "iron")
EFFEKTE = "fxaa,sharpen,softparticles,colorgrade,ssao,shadowmap=4096,terraindetail"
LEISTE = 0.78		# Anteil der Bildhoehe ueber der Befehlsleiste


def roh(name, fassung):
    return os.path.join(AUS, "raw", name, fassung)


def cmd_lauf(a):
    from slot import wait_free
    fehler = 0
    for name in (a.nur.split(",") if a.nur else list(SZENEN)):
        szene, karte, pfad, takt, _ = SZENEN[name]
        out = roh(name, a.fassung)
        wait_free(parallel=3)
        cmd = [sys.executable, os.path.join(HIER, "run_render_test.py"), "run", "--exe", a.zh, "--scene", szene,
               "--path", os.path.join(PFADE, pfad), "--every", str(takt), "--xres", str(a.xres), "--yres", str(a.yres),
               "--texture-quality", a.stufe, "--mehrfach", "--out", out]
        if karte:
            cmd += ["--map", karte, "--ai", "1", "--seed", "1"]
        extra = ["-ignoreAsserts", "-d3d11Effects", a.effekte]
        if a.mod:
            extra += ["-mod", a.mod]
        cmd += ["--extra"] + extra + (a.extra or [])
        r = subprocess.call(cmd)
        print("==", name, a.fassung, "Exit", r, flush=True)
        fehler += r != 0
    return 1 if fehler else 0


def cmd_vergleich(a):
    """Pixelvergleich zweier Fassungen je Szene (vorher gegen nachher ohne freie Baeume: muss 0 sein)."""
    import numpy as np
    from PIL import Image
    gesamt = 0
    bilder = 0
    for name in SZENEN:
        d1, d2 = roh(name, a.a), roh(name, a.b)
        if not (os.path.isdir(d1) and os.path.isdir(d2)):
            continue
        for f in sorted(x for x in os.listdir(d1) if x.startswith("shot_") and x.endswith(".bmp")):
            if not os.path.exists(os.path.join(d2, f)):
                print(name, f, "fehlt")
                gesamt += 1
                continue
            x = np.asarray(Image.open(os.path.join(d1, f)).convert("RGB")).astype(int)
            y = np.asarray(Image.open(os.path.join(d2, f)).convert("RGB")).astype(int)
            n = int((np.abs(x - y).max(axis=2) > 0).sum())
            gesamt += n
            bilder += 1
            print("%-9s %s  abweichende Pixel %d" % (name, f, n))
    print("GESAMT %d Bilder, abweichende Pixel %d" % (bilder, gesamt))
    return 0 if gesamt == 0 and bilder else 1


def jpeg(img, breite):
    from PIL import Image
    im = Image.fromarray(img)
    if im.width > breite:
        im = im.resize((breite, round(im.height * breite / im.width)), Image.LANCZOS)
    b = io.BytesIO()
    im.save(b, "JPEG", quality=88)
    return "data:image/jpeg;base64," + base64.b64encode(b.getvalue()).decode("ascii")


def cmd_seite(a):
    """Eine Datei mit eingebetteten Bildern: je Szene und Halt links/rechts waehlbar, Schieber dazwischen."""
    import numpy as np
    from PIL import Image
    fassungen = [f.split("=", 1) for f in a.fassungen.split(",")]
    daten, szenen = {}, []
    hoehe = breite = None
    for name in (a.nur.split(",") if a.nur else PILOT):
        dirs = {k: roh(name, k) for k, _ in fassungen}
        if not all(os.path.isdir(d) for d in dirs.values()):
            print("uebersprungen", name)
            continue
        shots = sorted(x for x in os.listdir(dirs[fassungen[0][0]]) if x.startswith("shot_") and x.endswith(".bmp"))
        shots = [s for s in shots if all(os.path.exists(os.path.join(d, s)) for d in dirs.values())]
        szenen.append([name, SZENEN[name][4], [s[5:11].lstrip("0") for s in shots]])
        for s in shots:
            for k, d in dirs.items():
                img = np.asarray(Image.open(os.path.join(d, s)).convert("RGB"))
                img = img[:int(img.shape[0] * LEISTE)]
                hoehe, breite = img.shape[:2]
                daten["%s_%s_%s" % (name, s[5:11].lstrip("0"), k)] = jpeg(img, a.breite)
    if not szenen:
        raise SystemExit("keine Bilder")
    tpl = open(os.path.join(HIER, "baum_seite.html"), encoding="utf-8").read()
    html = (tpl.replace("/*TITEL*/", a.titel).replace("/*TEXT*/", a.text or "")
            .replace("/*FUSS*/", a.fuss or "").replace("/*SEITENVERHAELTNIS*/", "%d/%d" % (breite, hoehe))
            .replace("/*DATEN*/null", json.dumps(daten)).replace("/*FASSUNGEN*/null", json.dumps(fassungen, ensure_ascii=False))
            .replace("/*SZENEN*/null", json.dumps(szenen, ensure_ascii=False)))
    ziel = os.path.join(AUS, "seite")
    os.makedirs(ziel, exist_ok=True)
    datei = os.path.join(ziel, a.datei)
    open(datei, "w", encoding="utf-8").write(html)
    print("Seite", datei, round(len(html) / 1e6, 1), "MB,", len(daten), "Bilder")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    l = sub.add_parser("lauf")
    l.add_argument("--fassung", required=True)
    l.add_argument("--zh", default=os.path.join(REPO, "build-x64", "GeneralsMD", "Code", "Main", "Release", "GZHNeoZH.exe"))
    l.add_argument("--stufe", default="hd", choices=("sd", "hd", "uhd", "auto"))
    l.add_argument("--mod", help="Ordner mit dem Testarchiv (-mod)")
    l.add_argument("--xres", type=int, default=1920)
    l.add_argument("--yres", type=int, default=1080)
    l.add_argument("--effekte", default=EFFEKTE)
    l.add_argument("--nur")
    l.add_argument("--extra", nargs=argparse.REMAINDER)
    v = sub.add_parser("vergleich")
    v.add_argument("a")
    v.add_argument("b")
    s = sub.add_parser("seite")
    s.add_argument("--fassungen", default="ea=EA,frei=frei", help="ordner=Anzeigename,... (erste zwei sind links/rechts)")
    s.add_argument("--nur")
    s.add_argument("--titel", default="Bäume frei: EA gegen frei")
    s.add_argument("--text")
    s.add_argument("--fuss")
    s.add_argument("--breite", type=int, default=1280)
    s.add_argument("--datei", default="baeume_vergleich.html")
    a = ap.parse_args()
    return {"lauf": cmd_lauf, "vergleich": cmd_vergleich, "seite": cmd_seite}[a.cmd](a) or 0


if __name__ == "__main__":
    sys.exit(main())
