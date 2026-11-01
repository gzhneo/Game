#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Stellt einen KI-Arbeitsordner (ki\\) fuer build_texture_tiers.py aus vorhandenen Ergebnissen zusammen, damit ein
neuer Lauf nur noch rechnet, was fehlt.

Die KI-Ausgaben sind nach der SHA-1 der Quelltextur benannt und haben immer den Faktor des Modells (4x); HD und UHD
lesen dieselben Bilder. Je Schluessel aus den Arten-Dateien:

  * liegt ein verfeinertes Bild in einem --fertig-Ordner (erster Treffer gewinnt), kommt es nach <ziel>\\aus;
  * sonst ein unverfeinertes (etwa x4plus aus einem Lauf ohne Verfeinerung) aus einem --roh-Ordner nach <ziel>\\gan --
    der Lauf mit --ai-refine-python verfeinert es dann, ohne ncnn noch einmal zu starten;
  * sonst nichts: der Lauf rechnet es ganz neu.

Uebernommen wird per Hardlink (gleiches Laufwerk), sonst als Kopie; Quellen werden nur gelesen. Die Arten-Datei
<ziel>\\arten.json entsteht immer als eigene Datei -- ein Hardlink auf die arten.json eines anderen Satzes wuerde
beim naechsten Lauf dessen Datei mit aendern. Ein schon vorhandenes <ziel>\\aus- oder gan-Bild bleibt stehen.

    python tools/assets/ki_satz.py --ziel <Asset>\\Texturstufen\\Arbeit\\UHD-KI3\\ki ^
        --arten <Asset>\\Texturstufen\\Arbeit\\UHD-KI2\\ki\\arten.json ^
        --fertig <Asset>\\Texturstufen\\Arbeit\\HD-KI2\\ki\\aus --roh <Asset>\\Texturstufen\\Arbeit\\UHD-KI2\\ki\\aus
    python tools/assets/ki_satz.py --selftest

Schreibt <ziel>\\satz.json (Zahlen je Art und Herkunft) und meldet, was der Lauf noch rechnen muss.
"""
import argparse
import json
import os
import shutil
import sys
import tempfile
from collections import Counter


def load_kinds(paths):
    kinds = {}
    for p in paths:
        with open(p, "r", encoding="ascii") as f:
            for key, kind in json.load(f).items():
                kinds.setdefault(key, kind)
    return kinds


def take(src, dst):
    """Hard link, else copy. Returns how the file came over."""
    try:
        os.link(src, dst)
        return "link"
    except OSError:
        shutil.copy2(src, dst)
        return "kopie"


def assemble(target, kind_files, done_dirs, raw_dirs, dry_run=False):
    kinds = load_kinds(kind_files)
    shared = [p for p in kind_files if os.stat(p).st_nlink > 1]
    aus, gan = os.path.join(target, "aus"), os.path.join(target, "gan")
    if not dry_run:
        os.makedirs(aus, exist_ok=True)
        os.makedirs(gan, exist_ok=True)
    counts = Counter()
    how = Counter()
    for key in sorted(kinds):
        name = key + ".png"
        kind = kinds[key]
        if os.path.exists(os.path.join(aus, name)):
            counts[(kind, "schon fertig")] += 1
            continue
        src = next((os.path.join(d, name) for d in done_dirs if os.path.exists(os.path.join(d, name))), None)
        if src:
            counts[(kind, "verfeinert uebernommen")] += 1
            if not dry_run:
                how[take(src, os.path.join(aus, name))] += 1
            continue
        if os.path.exists(os.path.join(gan, name)):
            counts[(kind, "zu verfeinern")] += 1
            continue
        src = next((os.path.join(d, name) for d in raw_dirs if os.path.exists(os.path.join(d, name))), None)
        if src:
            counts[(kind, "zu verfeinern")] += 1
            if not dry_run:
                how[take(src, os.path.join(gan, name))] += 1
            continue
        counts[(kind, "neu zu rechnen")] += 1
    report = {
        "keys": len(kinds),
        "by_kind": {f"{k}|{s}": n for (k, s), n in sorted(counts.items())},
        "totals": dict(Counter(s for (_, s) in counts.elements())),
        "transfer": dict(how),
        "kind_files": kind_files, "done_dirs": done_dirs, "raw_dirs": raw_dirs,
        "shared_kind_files": shared,
    }
    if not dry_run:
        os.makedirs(target, exist_ok=True)
        tmp = os.path.join(target, "arten.json.tmp")
        with open(tmp, "w", encoding="ascii") as f:
            json.dump(kinds, f)
        os.replace(tmp, os.path.join(target, "arten.json"))	# never a link to another set's file
        with open(os.path.join(target, "satz.json"), "w", encoding="ascii") as f:
            json.dump(report, f, indent=1, sort_keys=True)
    return report


def print_report(r):
    print(f"Schluessel: {r['keys']}")
    for k, n in sorted(r["totals"].items()):
        print(f"  {k:24s} {n}")
    for k, n in sorted(r["by_kind"].items()):
        kind, state = k.split("|")
        print(f"    {kind:10s} {state:24s} {n}")
    if r["transfer"]:
        print(f"  uebernommen: {r['transfer']}")
    for p in r["shared_kind_files"]:
        print(f"  Hinweis: {p} ist ein Hardlink (mehrfach verknuepft); hier nur gelesen, das Ziel bekommt eine eigene Datei")


def selftest():
    fails = []

    def check(cond, what):
        if not cond:
            fails.append(what)
            print("  FEHLGESCHLAGEN:", what)

    with tempfile.TemporaryDirectory() as tmp:
        def put(path, data=b"png"):
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as f:
                f.write(data)

        hd, uhd, target = (os.path.join(tmp, n) for n in ("HD-KI2", "UHD-KI2", "UHD-KI3"))
        arten = os.path.join(uhd, "ki", "arten.json")
        put(arten, json.dumps({"a": "Modell", "b": "Gelaende", "c": "Baum", "d": "Modell"}).encode())
        hd_arten = os.path.join(hd, "ki", "arten.json")
        os.makedirs(os.path.dirname(hd_arten), exist_ok=True)
        try:
            os.link(arten, hd_arten)	# the trap: one file shared by two work folders
        except OSError:
            shutil.copy(arten, hd_arten)
        put(os.path.join(hd, "ki", "aus", "a.png"), b"fein-a")
        put(os.path.join(uhd, "ki", "aus", "a.png"), b"roh-a")
        put(os.path.join(uhd, "ki", "aus", "b.png"), b"roh-b")
        put(os.path.join(uhd, "ki", "aus", "c.png"), b"roh-c")
        put(os.path.join(target, "ki", "aus", "c.png"), b"schon")
        before = open(arten, "rb").read()
        r = assemble(os.path.join(target, "ki"), [arten], [os.path.join(hd, "ki", "aus")], [os.path.join(uhd, "ki", "aus")])
        t = r["totals"]
        check(t == {"verfeinert uebernommen": 1, "zu verfeinern": 1, "schon fertig": 1, "neu zu rechnen": 1},
              f"Satz: Zuordnung {t}")
        check(open(os.path.join(target, "ki", "aus", "a.png"), "rb").read() == b"fein-a", "Satz: verfeinert vor roh")
        check(open(os.path.join(target, "ki", "gan", "b.png"), "rb").read() == b"roh-b", "Satz: roh nach gan")
        check(open(os.path.join(target, "ki", "aus", "c.png"), "rb").read() == b"schon", "Satz: Vorhandenes bleibt")
        check(not os.path.exists(os.path.join(target, "ki", "gan", "d.png")), "Satz: Fehlendes wird neu gerechnet")
        own = os.path.join(target, "ki", "arten.json")
        check(json.load(open(own)) == json.loads(before) and os.stat(own).st_nlink == 1, "Satz: eigene arten.json")
        with open(own, "w") as f:
            f.write("{}")
        check(open(arten, "rb").read() == before and open(hd_arten, "rb").read() == before,
              "Satz: Aendern der neuen arten.json laesst die alten Saetze unberuehrt")
        r2 = assemble(os.path.join(target, "ki"), [arten], [os.path.join(hd, "ki", "aus")], [os.path.join(uhd, "ki", "aus")])
        check(r2["totals"].get("schon fertig") == 2 and r2["totals"].get("zu verfeinern") == 1, "Satz: zweiter Lauf idempotent")
        dry = assemble(os.path.join(tmp, "leer", "ki"), [arten], [], [], dry_run=True)
        check(dry["totals"] == {"neu zu rechnen": 4} and not os.path.exists(os.path.join(tmp, "leer")), "Satz: Probelauf schreibt nichts")
    print(f"Selbsttest: {'bestanden' if not fails else str(len(fails)) + ' Fehler'}")
    return 0 if not fails else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ziel", help="neuer ki-Ordner (darunter aus, gan, arten.json)")
    ap.add_argument("--arten", action="append", default=[], help="arten.json eines Satzes (mehrfach; erster gewinnt)")
    ap.add_argument("--fertig", action="append", default=[], help="Ordner mit verfeinerten Bildern (mehrfach)")
    ap.add_argument("--roh", action="append", default=[], help="Ordner mit unverfeinerten 4x-Bildern (mehrfach)")
    ap.add_argument("--probe", action="store_true", help="nur zaehlen, nichts schreiben")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.ziel or not a.arten:
        ap.error("--ziel und --arten sind noetig")
    for d in a.fertig + a.roh:
        if not os.path.isdir(d):
            raise SystemExit(f"Ordner fehlt: {d}")
    ziel = os.path.abspath(a.ziel)
    for d in a.fertig + a.roh:
        if os.path.abspath(d).startswith(ziel + os.sep):
            raise SystemExit(f"{d} liegt im Ziel; Quellen werden nur gelesen")
    print_report(assemble(ziel, a.arten, a.fertig, a.roh, a.probe))
    return 0


if __name__ == "__main__":
    sys.exit(main())
