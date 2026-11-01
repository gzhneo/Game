#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Dateisystem unter Windows mit EA-Daten: Index des Spiels gegen VfsDump (10.8).

Der alte Namensbaum und -altesDateisystem sind entfernt; die alte Vorrangregel bildet VfsDump nach (tools/dateisystem/pruefen.sh).
Teil A: Indexabzug (-dateiIndexAusgeben, -freeContent ea) gegen VfsDump ohne Mod, fuer einen Mod als BIG, einen Mod als ZIP
(je 40 Dateien aus Data/FreierBoden, die Dateien des freien Bodens ueberschreiben) und fuer -baseGame; vergleichen.py muss
"gleich" melden. VfsDump kennt das Texturpaket (Texturstufen\\Paket) nicht: liegt eines bei den Assets, --stufe sd nehmen.
Teil B: Ladezeit des Kartenladens (Zeile LOADTIME, Replay als Szene), Median ohne den kalten ersten Lauf; der Vergleichswert
des alten Dateisystems steht in 10.7.

  python tools/dateisystem/windows_vergleich.py --exe build-x64/GeneralsMD/Code/Main/Release/GZHNeoZH.exe
      --vfsdump build-x64/Code/Tools/VfsDump/Release/VfsDump.exe --basegame <Generals-Assets>
"""
import argparse
import glob
import os
import re
import shutil
import statistics
import subprocess
import sys
import zipfile

HIER = os.path.dirname(os.path.abspath(__file__))
WURZEL = os.path.abspath(os.path.join(HIER, "..", ".."))
sys.path.insert(0, os.path.join(WURZEL, "tools", "abnahme"))
sys.path.insert(0, HIER)
import slot  # noqa: E402
import beispiel  # noqa: E402


def win(pfad):
    return os.path.abspath(pfad).replace("/", "\\")


STUFE = "hd"


def lauf(exe, out, szene, extra):
    slot.wait_free()
    return subprocess.call([sys.executable, os.path.join(WURZEL, "tools", "render", "run_render_test.py"), "run", "--exe", exe,
                            "--out", out, "--frames", "60", "--texture-quality", STUFE] + szene + ["--extra"] + extra,
                           cwd=WURZEL, stdout=subprocess.DEVNULL)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--exe", required=True, help="GZHNeoZH.exe (Release), mit verlinkten Assets im selben Ordner")
    p.add_argument("--vfsdump", required=True, help="VfsDump.exe (build-x64/Code/Tools/VfsDump/<Konfiguration>)")
    p.add_argument("--stufe", choices=("sd", "hd", "uhd"), default="hd", help="Texturstufe fuer Teil A und B")
    p.add_argument("--basegame", help="Ordner mit Generals-Archiven fuer -baseGame (nur Zero Hour)")
    p.add_argument("--out", default="render-out/dateisystem_win")
    p.add_argument("--runden", type=int, default=6, help="Teil B: Laeufe je Weg und Replay")
    p.add_argument("--replay", action="append", help="Teil B: Replay (mehrfach), Standard: zwei Zero-Hour-Referenzreplays")
    a = p.parse_args()
    global STUFE
    STUFE = a.stufe
    exe = os.path.abspath(a.exe)
    spiel = os.path.dirname(exe)
    zh = os.path.basename(exe).lower().startswith("gzhneozh")
    out = os.path.abspath(a.out)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)

    quelle = sorted(glob.glob(os.path.join(WURZEL, "Data", "FreierBoden", "Art", "Terrain", "*.dds")))[:40]
    if not quelle:
        sys.exit("Data/FreierBoden/Art/Terrain/*.dds fehlt")
    inhalt = [(os.path.basename(f), open(f, "rb").read()) for f in quelle]
    os.makedirs(os.path.join(out, "mod_big"))
    big = os.path.join(out, "mod_big", "mod.big")
    beispiel.big(big, [("Art\\Terrain\\" + n, d) for n, d in inhalt])
    zip_pfad = os.path.join(out, "mod.zip")
    with zipfile.ZipFile(zip_pfad, "w", zipfile.ZIP_STORED) as z:
        for n, d in inhalt:
            z.writestr("Art/Terrain/" + n, d)

    # Spielschalter, VfsDump-Schalter; die Archive heissen in beiden Abzuegen wie auf der Befehlszeile des Spiels
    faelle = [("ohne_mod", [], []),
              ("mod_big", ["-mod", win(big)], ["--mod", big + "=" + win(big)]),
              ("mod_zip", ["-mod", win(zip_pfad)], ["--mod", zip_pfad + "=" + win(zip_pfad)])]
    if a.basegame:
        faelle.append(("basegame", ["-baseGame", a.basegame], ["--basegame", a.basegame + "=" + a.basegame]))
    ok = True
    print("== A: Indexabzug des Spiels gegen VfsDump")
    for name, mod, vfs in faelle:
        neu = win(os.path.join(out, name + "_spiel.txt"))
        lauf(exe, os.path.join(out, "lauf_" + name), ["--scene", "shellmap"], ["-dateiIndexAusgeben", neu, "-freeContent", "ea"] + mod)
        ref = os.path.join(out, name + "_vfsdump.txt")
        with open(ref, "w", encoding="latin-1") as f:
            subprocess.call([a.vfsdump, spiel, "--tier", STUFE] + (["--zh"] if zh else []) + vfs, stdout=f)
        r = subprocess.run([sys.executable, os.path.join(HIER, "vergleichen.py"), neu, ref],
                           cwd=WURZEL, capture_output=True, text=True)
        zeilen = [z for z in r.stdout.splitlines() if z.startswith(("Spiel ", "nur im")) or z in ("gleich", "ABWEICHUNG")]
        aus_mod = sum(1 for z in open(neu, encoding="latin-1")
                      if z.split("\t")[-1].strip().lower().endswith(("mod.big", "mod.zip")))
        print(f"{name:10} rc {r.returncode}  " + " | ".join(zeilen) + f"  | Pfade aus dem Mod: {aus_mod}", flush=True)
        ok = ok and r.returncode == 0

    print("== B: Ladezeit des Kartenladens")
    replays = a.replay or [os.path.join(WURZEL, "tests", "replays", "zh", "x64", n) for n in
                           ("e0zh-tournament-lake-3ai-seed2.rep", "e0zh-green-pastures-2ai-seed1.rep")]
    zeit = re.compile(r"LOADTIME (\d+) ms map")
    werte = {}
    for rep in replays:
        name = os.path.basename(rep)
        for runde in range(a.runden):
            for weg, zusatz in (("neu", []),):
                ziel = os.path.join(out, f"zeit_{name}_{weg}_{runde}")
                lauf(exe, ziel, ["--scene", "replay", "--replay", rep], ["-logTextureStats", "300"] + zusatz)
                ms = None
                try:
                    for z in open(os.path.join(ziel, "run.log"), encoding="latin-1"):
                        m = zeit.search(z)
                        if m:
                            ms = int(m.group(1))
                except OSError:
                    pass
                werte.setdefault((name, weg), []).append(ms)
                print(f"  {name} {weg} Runde {runde}: {ms} ms", flush=True)
    print("== Median ohne die erste Runde (kalter Dateicache)")
    for (name, weg), w in sorted(werte.items()):
        brauchbar = [x for x in w[1:] if x is not None]
        if brauchbar:
            print(f"{name:42} {weg}: {statistics.median(brauchbar):.0f} ms (min {min(brauchbar)}, max {max(brauchbar)}, n {len(brauchbar)})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
