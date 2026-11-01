#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""KI-gegen-KI-Serien messen.

Startet Skirmish-Partien ohne Menue und ohne Zeichnen (-runner), in denen die neue KI
(AICommanderPlayer) gegen die klassische KI (AISkirmishPlayer) spielt, und wertet die Zeilen
KISTAT, KISIEG und KIHILFE aus dem Runner-Protokoll aus (-crcLog, -aiStats).

Jeder Seed wird gespiegelt gespielt: einmal "N1,N1,K2,K2" und einmal "K1,K1,N2,N2". Startplaetze,
Fraktionen und Farben haengen nur am Seed, die beiden Partien unterscheiden sich also nur darin,
welches Team die neue KI fuehrt. So hebt sich der Vorteil einer Startposition oder Fraktion auf.

Befehle:
  serie       mehrere Karten und Seeds, gespiegelt; schreibt je Partie eine JSON-Zeile in
              <out>/ergebnisse.jsonl und am Ende die Zusammenfassung
  lauf        eine Partie mit frei gewaehlter Belegung (--ki "K1,K1,K2,K2")
  auswertung  Zusammenfassung aus einer vorhandenen ergebnisse.jsonl
  elo         Bradley-Terry-Wertung (Elo-Skala) aus einer oder mehreren ergebnisse.jsonl; die klassische
              KI ist der Nullpunkt (ihr Code bleibt ueber alle Staende gleich), Staende der neuen KI
              unterscheiden sich ueber --label der Serie
  planvergleich  KIPLAN-Zeilen zweier Protokolle vergleichen (gleiche Partie, etwa -workers 0 und 8):
              der Planer muss in jedem Lauf dasselbe sehen und rechnen

Beispiele:
  python tools/ki/ki_serie.py serie --exe build-x64/GeneralsMD/Code/Main/Release/GZHNeoZH.exe ^
      --karten "Tournament Desert" "Golden Oasis" --seeds 1 2 3 --out ki-runs/serie1
  python tools/ki/ki_serie.py lauf --exe ... --karte "Tournament Desert" --ki "K1,K1,K2,K2" --seed 1 --out ki-runs/beob
  python tools/ki/ki_serie.py auswertung ki-runs/serie1/ergebnisse.jsonl
  python tools/ki/ki_serie.py elo ki-runs/serie1/ergebnisse.jsonl ki-runs/serie2/ergebnisse.jsonl
  python tools/ki/ki_serie.py planvergleich ki-runs/w0/partie.log ki-runs/w8/partie.log

Belegung (--ki, wie -skirmishAI): je Platz ein Buchstabe (K = klassische KI, N = neue KI) und die
Teamnummer 1 bis 4; ohne Ziffer spielt der Platz ohne Team. Platz 0 ist der Beobachter.

Parallelbetrieb: Vor jeder Partie wartet das Skript, bis kein GZHNeo.exe, GZHNeoZH.exe oder
WorldBuilder laeuft (zwei freie Pruefungen im Abstand von 20 s, wie scratchpad/slot.py der
Haupt-Session). Das Spiel laeuft im Fenster (-win) und zeichnet mit -runner nach dem ersten Bild nicht mehr.
"""
import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys
import threading
import time

WINDOW_ARGS = ["-win", "-xres", "800", "-yres", "600"]
BUSY_NAMES = ("gzhneo.exe", "gzhneozh.exe")

KISTAT = re.compile(r"^KISTAT (\d+) (.*)$")
KISIEG = re.compile(r"^KISIEG (\d+) ende (\d+) team (-?\d+) sieger\(spieler/platz/art\)(.*)$")
KIHILFE = re.compile(r"^KIHILFE (\d+) spieler (\d+) (\S+)(.*)$")
KIANGRIFF = re.compile(r"^KIANGRIFF (\d+) spieler (\d+) (\S+)(.*)$")
KIBAU = re.compile(r"^KIBAU (\d+) spieler (\d+) (einheit|upgrade) (\S+) kosten (\d+)(?:.* wahl (\S+))?")
KIVERLUST = re.compile(r"^KIVERLUST (\d+) spieler (\d+) vorlage (\S+) wert (\d+) gebaeude (\d) von_spieler (\d+) "
                       r"von_vorlage (\S+) von_gebaeude (\d) ort (\S+)")
KIVERTEIDIGUNG = re.compile(r"^KIVERTEIDIGUNG (\d+) spieler (\d+) bedrohung (\d+) einheiten (\d+) wert (\d+)")
KIOHNE = re.compile(r"^KIOHNE (\d+) spieler (\d+) teile (-?\d+)")
KIFABRIK = re.compile(r"^KIBAU (\d+) spieler (\d+) fabrik (\S+) bestand (\d+) geld (\d+)")
KIRUECKZUG = re.compile(r"^KIRUECKZUG (\d+) spieler (\d+) einheiten (\d+) wert (\d+) werkstatt (\d+) heilung (\d+) heim (\d+)")
KIWIRTSCHAFT = re.compile(r"^KIWIRTSCHAFT (\d+) spieler (\d+) (\S+)(.*)$")
KIPLAN = re.compile(r"^KIPLAN (\d+) spieler (\d+) (.*)$")
KIZEIT = re.compile(r"^KIZEIT (\d+) spieler (\d+) (.*)$")
KIZIEL = re.compile(r"^KIZIEL (\d+) spieler (\d+) ")
KIUEBERFALL = re.compile(r"^KIUEBERFALL (\d+) spieler (\d+) (\S+)(.*)$")
KIGEFECHT = re.compile(r"^KIGEFECHT (\d+) spieler (\d+) (\S+)(.*)$")
KISUPERWAFFE = re.compile(r"^KISUPERWAFFE (\d+) spieler (\d+) ")
KIFOKUS = re.compile(r"^KIFOKUS (\d+) spieler (\d+) gegner (-?\d+) vorher (-?\d+) grund (\S+)")
SLOT = re.compile(r"^SLOT (\d+) state (\d+) ai (\d+) template (-?\d+) start (-?\d+) color (-?\d+) team (-?\d+)(?: kind (-?\d+))?")


# ------------------------------------------------------------------------------------------------
# Parallelbetrieb
def busy():
    try:
        out = subprocess.run(["tasklist", "/fo", "csv", "/nh"], capture_output=True, text=True,
                             errors="replace", timeout=120).stdout
    except (OSError, subprocess.SubprocessError):
        return ["tasklist?"]
    if "System" not in out:
        return ["tasklist leer?"]
    found = []
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if parts and (parts[0].lower() in BUSY_NAMES or parts[0].lower().startswith("worldbuilder")):
            found.append(parts[0])
    return found


def wait_free(log=print):
    n = 0
    while True:
        b = busy()
        if not b:
            time.sleep(20)
            if not busy():
                return
            continue
        if n % 10 == 0:
            log(f"warte auf {b}")
        n += 1
        time.sleep(60)


# ------------------------------------------------------------------------------------------------
# Eine Partie
def run_match(a, karte, ki, seed, out_dir, name):
    os.makedirs(out_dir, exist_ok=True)
    log = os.path.abspath(os.path.join(out_dir, name + ".log"))
    if os.path.exists(log):
        os.remove(log)
    exe = os.path.abspath(a.exe)
    cmd = [exe] + WINDOW_ARGS + ["-runner", "-skirmish", karte, "-skirmishAI", ki, "-skirmishSeed", str(seed),
                                 "-maxFrames", str(a.frames), "-crcLog", log, "-crcLogInterval", "100000",
                                 "-aiStats", str(a.intervall)] + list(a.extra or [])
    if not a.nicht_warten:
        warte = wait_free
        if a.slot:
            # Fremdes Warte-Modul mit Startsperre (Parallelbetrieb mehrerer Sitzungen): bei jedem Start neu
            # laden, damit Aenderungen am Modul waehrend einer langen Serie greifen.
            import importlib.util
            spec = importlib.util.spec_from_file_location("ki_slot", a.slot)
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            warte = mod.wait_free
        warte()
    start = time.time()
    try:
        code = subprocess.run(cmd, cwd=os.path.dirname(exe), timeout=a.timeout).returncode
    except subprocess.TimeoutExpired:
        code = None
    secs = time.time() - start
    res = parse_log(log)
    res.update({"karte": karte, "ki": ki, "seed": seed, "exit": code, "sekunden": round(secs, 1), "log": log})
    return res


def parse_kv(text):
    toks = text.split()
    d = {}
    for i in range(0, len(toks) - 1, 2):
        v = toks[i + 1]
        try:
            d[toks[i]] = int(v)
        except ValueError:
            d[toks[i]] = v
    return d


def parse_log(path):
    res = {"sieg": None, "endframe": None, "letzter_frame": 0, "spieler": {}, "hilfe": [], "slots": {},
           "angriff": [], "fokus": [], "gleich": {}}
    if not os.path.isfile(path):
        res["fehler"] = "kein Protokoll"
        return res
    by_frame = {}	# Frame -> Zeilen (fuer "gleicher Gegner im Buendnis")
    with open(path, "r", encoding="ascii", errors="replace") as f:
        for raw in f:
            line = raw.strip()
            m = KISTAT.match(line)
            if m:
                frame = int(m.group(1))
                d = parse_kv(m.group(2))
                d["frame"] = frame
                res["spieler"][str(d["spieler"])] = d	# letzter Stand je Spieler
                res["letzter_frame"] = max(res["letzter_frame"], frame)
                by_frame.setdefault(frame, []).append(d)
                continue
            m = KIANGRIFF.match(line)
            if m:
                res["angriff"].append({"frame": int(m.group(1)), "spieler": int(m.group(2)), "was": m.group(3),
                                       "rest": m.group(4).strip()})
                continue
            m = KIBAU.match(line)
            if m:
                res.setdefault("bau", []).append({"frame": int(m.group(1)), "spieler": int(m.group(2)), "was": m.group(3),
                                                  "name": m.group(4), "kosten": int(m.group(5)), "wahl": m.group(6)})
                continue
            m = KIVERTEIDIGUNG.match(line)
            if m:
                res["verteidigung"] = res.get("verteidigung", 0) + 1
                continue
            m = KIOHNE.match(line)
            if m:
                res["ohne"] = int(m.group(3))  # -aiOhne: switched-off parts (bit mask, negative = unknown name)
                continue
            m = KIFABRIK.match(line)
            if m:
                res.setdefault("fabrik", []).append({"frame": int(m.group(1)), "spieler": int(m.group(2)), "name": m.group(3),
                                                     "geld": int(m.group(5))})
                continue
            m = KIWIRTSCHAFT.match(line)
            if m:
                if m.group(3) != "lage":
                    res.setdefault("wirtschaft", []).append({"frame": int(m.group(1)), "spieler": int(m.group(2)),
                                                             "was": m.group(3), "rest": m.group(4).strip()})
                continue
            m = KIRUECKZUG.match(line)
            if m:
                res.setdefault("rueckzug", []).append({"frame": int(m.group(1)), "spieler": int(m.group(2)),
                                                       "einheiten": int(m.group(3)), "wert": int(m.group(4)),
                                                       "werkstatt": int(m.group(5)), "heilung": int(m.group(6)),
                                                       "heim": int(m.group(7))})
                continue
            m = KIZEIT.match(line)
            if m:
                d = parse_kv(m.group(3))
                d["frame"] = int(m.group(1))
                res.setdefault("zeit", {}).setdefault(m.group(2), []).append(d)
                continue
            m = KIPLAN.match(line)
            if m:
                res["plaene"] = res.get("plaene", 0) + 1
                continue
            if KIZIEL.match(line):
                res["ziele"] = res.get("ziele", 0) + 1
                continue
            if KISUPERWAFFE.match(line):
                res["superwaffe"] = res.get("superwaffe", 0) + 1
                continue
            m = KIUEBERFALL.match(line)
            if m:
                was = m.group(3)
                rest = m.group(4).split()
                key = was + ("_" + rest[0] if was in ("rueckzug", "ende") and rest else "")
                u = res.setdefault("ueberfall", {})
                u[key] = u.get(key, 0) + 1
                continue
            m = KIGEFECHT.match(line)
            if m:
                g = res.setdefault("gefecht", {"fokus": 0, "abstand": 0, "verteilt": 0, "verteilen": 0})
                if m.group(3) == "lage":
                    d = parse_kv(m.group(4))
                    for k in ("fokus", "abstand", "verteilt"):
                        g[k] += d.get(k, 0)
                elif m.group(3) == "verteilen":
                    g["verteilen"] += 1
                continue
            m = KIFOKUS.match(line)
            if m:
                res["fokus"].append({"frame": int(m.group(1)), "spieler": int(m.group(2)), "gegner": int(m.group(3)),
                                     "vorher": int(m.group(4)), "grund": m.group(5)})
                continue
            m = KISIEG.match(line)
            if m:
                winners = []
                for w in m.group(4).split():
                    if w == "keiner":
                        continue
                    p, s, k = w.split("/")
                    winners.append({"spieler": int(p), "platz": int(s), "art": int(k)})
                res["sieg"] = {"frame": int(m.group(1)), "team": int(m.group(3)), "sieger": winners}
                res["endframe"] = int(m.group(2))
                continue
            m = KIHILFE.match(line)
            if m:
                res["hilfe"].append({"frame": int(m.group(1)), "spieler": int(m.group(2)), "was": m.group(3),
                                     "rest": m.group(4).strip()})
                continue
            m = SLOT.match(line)
            if m:
                res["slots"][m.group(1)] = {"template": int(m.group(4)), "start": int(m.group(5)),
                                            "team": int(m.group(7)), "art": int(m.group(8) or 0)}
    # Einnahmen je KI-Art zwischen Minute 25 und 30 (7.13: Wirtschaft im spaeten Spiel), nur wenn beide
    # Messpunkte da sind; besiegte Spieler zaehlen mit (ihre Einnahmen bleiben stehen).
    e25 = {str(d["spieler"]): d for d in by_frame.get(45000, [])}
    e30 = {str(d["spieler"]): d for d in by_frame.get(54000, [])}
    if e25 and e30:
        spaet = {}
        for p, d in e30.items():
            if p in e25:
                k = str(d.get("art"))
                spaet[k] = spaet.get(k, 0) + d["einnahmen"] - e25[p]["einnahmen"]
        res["einnahmen_25_30"] = spaet
    # Einnahmen je Minute ab Minute 30 bis zum letzten Messpunkt (Haertetest, 7.13)
    if e30 and res["letzter_frame"] > 54000 + 1800:
        last = {str(d["spieler"]): d for d in by_frame.get(res["letzter_frame"], [])}
        minutes = (res["letzter_frame"] - 54000) / 1800.0
        ab30 = {}
        for p, d in last.items():
            if p in e30:
                k = str(d.get("art"))
                ab30[k] = ab30.get(k, 0) + (d["einnahmen"] - e30[p]["einnahmen"]) / minutes
        res["einnahmen_ab_30"] = ab30
    # Anteil der Messpunkte, an denen alle lebenden KIs eines Buendnisses denselben Gegner verfolgen,
    # je KI-Art des Buendnisses (nur Buendnisse mit mindestens zwei Lebenden gleicher Art).
    for rows in by_frame.values():
        teams = {}
        for d in rows:
            if d.get("besiegt") or d.get("team", -1) < 0:
                continue
            teams.setdefault((d.get("team"), d.get("art")), []).append(d.get("gegner"))
        for (team, art), enemies in teams.items():
            if len(enemies) < 2:
                continue
            e = res["gleich"].setdefault(str(art), [0, 0])
            e[1] += 1
            if len(set(enemies)) == 1:
                e[0] += 1
    return res


def team_of_kind(res, art):
    """Team (Slot-Teamnummer), das die KI-Art art spielt; None, wenn gemischt oder unbekannt."""
    teams = {d.get("team") for d in res["spieler"].values() if d.get("art") == art}
    return teams.pop() if len(teams) == 1 else None


def planner_times(res):
    """Mittlere Zeiten des Planers je Partie (Mikrosekunden je Plan und KI; Echtzeit, nur Messung)."""
    rows = [d for lst in res.get("zeit", {}).values() for d in lst]
    if not rows:
        return None
    n = sum(d.get("plaene", 0) for d in rows) or 1

    def mean(k):
        return sum(d.get(k, 0) * d.get("plaene", 0) for d in rows) / n
    return {"plaene": res.get("plaene", 0), "objekte": round(mean("objekte"), 1),
            "schnappschuss_us": round(mean("schnappschuss_us"), 1), "rechnen_us": round(mean("rechnen_us"), 1),
            "warten_us": round(mean("warten_us"), 1),
            "warten_max_us": max(d.get("warten_max", 0) for d in rows),
            "worker": rows[-1].get("worker")}


def attack_durations(angriffe):
    """Je Ende-Grund: Zahl, Dauer (s) und Wert beim Ende im Mittel, aus den Zeilen 'KIANGRIFF ... ende'.
    Abbrüche beim Sammeln (phase 1) zählen getrennt als '<grund>@sammeln'."""
    out = {}
    for h in angriffe:
        if h["was"] != "ende" or not h["rest"]:
            continue
        toks = h["rest"].split()
        d = parse_kv(" ".join(toks[1:]))
        # Phase 1 = still gathering at the own base; the duration then counts from the gather start.
        key = toks[0] + ("@sammeln" if d.get("phase") == 1 else "")
        e = out.setdefault(key, [0, 0, 0])
        e[0] += 1
        e[1] += d.get("dauer", 0)
        e[2] += d.get("wert", 0) if isinstance(d.get("wert", 0), int) else 0
    return out


def summarize_match(res):
    """Kennzahlen je KI-Art (0 klassisch, 1 neu) fuer eine Partie."""
    out = {}
    for art in (0, 1):
        rows = [d for d in res["spieler"].values() if d.get("art") == art]
        if not rows:
            continue
        agg = {k: sum(r.get(k, 0) for r in rows) for k in
               ("einnahmen", "ausgaben", "verloren_e", "verloren_g", "zerstoert_e", "zerstoert_g", "gebaut_e",
                "einheiten", "armeewert", "geld", "gebaeude")}
        agg["spieler"] = len(rows)
        agg["besiegt"] = sum(r.get("besiegt", 0) for r in rows)
        out[str(art)] = agg
    winner_art = None
    if res.get("sieg") and res["sieg"]["sieger"]:
        arts = {w["art"] for w in res["sieg"]["sieger"]}
        winner_art = arts.pop() if len(arts) == 1 else "gemischt"
    starts = [h for h in res["hilfe"] if h["was"] == "start"]
    enden = {}
    for h in res["hilfe"]:
        if h["was"] == "ende" and h["rest"]:
            grund = h["rest"].split()[0]
            enden[grund] = enden.get(grund, 0) + 1
    angriffe = res.get("angriff", [])
    los = {}
    a_enden = {}
    for h in angriffe:
        if h["was"] == "los" and h["rest"]:
            g = h["rest"].split()[0]
            los[g] = los.get(g, 0) + 1
        if h["was"] == "ende" and h["rest"]:
            g = h["rest"].split()[0]
            a_enden[g] = a_enden.get(g, 0) + 1
    fokus = {}
    for h in res.get("fokus", []):
        fokus[h["grund"]] = fokus.get(h["grund"], 0) + 1
    return {"je_art": out, "sieger_art": winner_art, "hilfe_einsaetze": len(starts), "hilfe_enden": enden,
            "hilfe_verworfen": sum(1 for h in res["hilfe"] if h["was"] == "verworfen"),
            "hilfe_verstaerkung": sum(1 for h in res["hilfe"] if h["was"] == "verstaerkung"),
            "angriffe": sum(1 for h in angriffe if h["was"] == "sammeln"), "angriff_los": los, "angriff_enden": a_enden,
            "angriff_nachschub": sum(1 for h in angriffe if h["was"] == "nachschub"),
            "angriff_erst_ab": sum(1 for h in angriffe if h["was"] == "erst_ab"), "fokus": fokus,
            "bau_einheiten": sum(1 for b in res.get("bau", []) if b["was"] == "einheit"),
            "bau_upgrades": sum(1 for b in res.get("bau", []) if b["was"] == "upgrade"),
            "bau_fabriken": len(res.get("fabrik", [])),
            "verteidigung": res.get("verteidigung", 0),
            "bau_kosten": sum(b["kosten"] for b in res.get("bau", [])),
            "bau_konter": sum(1 for b in res.get("bau", []) if b.get("wahl") == "konter"),
            "rueckzug": [sum(r[k] for r in res.get("rueckzug", [])) for k in ("einheiten", "wert", "werkstatt", "heilung", "heim")],
            "wirtschaft": dict({w: sum(1 for x in res.get("wirtschaft", []) if x["was"] == w)
                                for w in ("ausbau", "bau", "hacker", "vorlauf", "technik", "technik_upgrade")},
                               eingenommen=sum(1 for x in res.get("wirtschaft", [])
                                               if x["was"] == "technik_ende" and x["rest"].startswith("eingenommen"))),
            "planer": planner_times(res),
            "einnahmen_25_30": res.get("einnahmen_25_30"),
            "einnahmen_ab_30": res.get("einnahmen_ab_30"),
            "gleich": res.get("gleich", {}),
            "angriff_dauer": attack_durations(angriffe),
            "ziele": res.get("ziele", 0), "superwaffe": res.get("superwaffe", 0),
            "ueberfall": res.get("ueberfall", {}), "gefecht": res.get("gefecht", {})}


# ------------------------------------------------------------------------------------------------
# Zusammenfassung
def summary(rows):
    lines = []
    n = len(rows)
    decided = [r for r in rows if r["kennzahlen"]["sieger_art"] in (0, 1)]
    neu = sum(1 for r in decided if r["kennzahlen"]["sieger_art"] == 1)
    alt = sum(1 for r in decided if r["kennzahlen"]["sieger_art"] == 0)
    offen = n - len(decided)
    lines.append(f"Partien: {n}, entschieden {len(decided)} (neue KI {neu}, klassische KI {alt}), offen/Zeitlimit {offen}")
    if decided:
        lines.append(f"Siegquote neue KI: {100.0 * neu / len(decided):.1f} % der entschiedenen Partien")
        dur = [r["endframe"] / 30.0 / 60.0 for r in decided if r.get("endframe")]
        if dur:
            dur.sort()
            lines.append(f"Spieldauer (entschieden): Mittel {sum(dur) / len(dur):.1f} min, Median {dur[len(dur) // 2]:.1f} min, "
                         f"min {dur[0]:.1f}, max {dur[-1]:.1f}")
    for art, label in (("1", "neue KI"), ("0", "klassische KI")):
        vals = [r["kennzahlen"]["je_art"][art] for r in rows if art in r["kennzahlen"]["je_art"]]
        if not vals:
            continue
        def avg(k):
            return sum(v[k] for v in vals) / len(vals)
        lines.append(f"{label} (Mittel je Partie und Team): Einnahmen {avg('einnahmen'):.0f}, Ausgaben {avg('ausgaben'):.0f}, "
                     f"gebaut {avg('gebaut_e'):.0f} Einheiten, verloren {avg('verloren_e'):.0f} Einheiten/"
                     f"{avg('verloren_g'):.0f} Gebaeude, zerstoert {avg('zerstoert_e'):.0f}/{avg('zerstoert_g'):.0f}, "
                     f"Geld am Ende {avg('geld'):.0f}")
    hilfe = [r["kennzahlen"]["hilfe_einsaetze"] for r in rows]
    if hilfe:
        lines.append(f"Hilfe-Einsaetze der neuen KI: {sum(hilfe)} ({sum(hilfe) / len(hilfe):.1f} je Partie), verworfen "
                     f"{sum(r['kennzahlen']['hilfe_verworfen'] for r in rows)}, Verstaerkungen "
                     f"{sum(r['kennzahlen']['hilfe_verstaerkung'] for r in rows)}")
        enden = {}
        for r in rows:
            for g, c in r["kennzahlen"].get("hilfe_enden", {}).items():
                enden[g] = enden.get(g, 0) + c
        if enden:
            lines.append("Ende der Hilfe-Einsaetze: " + ", ".join(f"{g} {c}" for g, c in sorted(enden.items(), key=lambda x: -x[1])))

    def merge(key):
        acc = {}
        for r in rows:
            for g, c in r["kennzahlen"].get(key, {}).items():
                acc[g] = acc.get(g, 0) + c
        return ", ".join(f"{g} {c}" for g, c in sorted(acc.items(), key=lambda x: -x[1])) or "-"
    angriffe = [r["kennzahlen"].get("angriffe", 0) for r in rows]
    if any(angriffe):
        lines.append(f"Angriffe der neuen KI: {sum(angriffe)} ({sum(angriffe) / len(angriffe):.1f} je Partie), Nachschub "
                     f"{sum(r['kennzahlen'].get('angriff_nachschub', 0) for r in rows)}, erst stärker werden "
                     f"{sum(r['kennzahlen'].get('angriff_erst_ab', 0) for r in rows)}")
        lines.append("  losgeschlagen: " + merge("angriff_los"))
        lines.append("  Ende: " + merge("angriff_enden"))
    vt = [r["kennzahlen"].get("verteidigung", 0) for r in rows]
    if any(vt):
        lines.append(f"Basisverteidigung der neuen KI: {sum(vt) / len(vt):.1f} Befehle je Partie")
    bau = [r["kennzahlen"].get("bau_einheiten", 0) for r in rows]
    if any(bau) or any(r["kennzahlen"].get("bau_upgrades", 0) for r in rows):
        lines.append(f"Spaetes Spiel (neue KI, je Partie): {sum(bau) / len(bau):.1f} Einheiten und "
                     f"{sum(r['kennzahlen'].get('bau_upgrades', 0) for r in rows) / len(rows):.1f} Upgrades selbst gekauft, "
                     f"{sum(r['kennzahlen'].get('bau_kosten', 0) for r in rows) / len(rows):.0f} Geld, davon nach Konter-Wertung "
                     f"{sum(r['kennzahlen'].get('bau_konter', 0) for r in rows)} von {sum(bau)} Einheiten; "
                     f"{sum(r['kennzahlen'].get('bau_fabriken', 0) for r in rows) / len(rows):.1f} Fabriken zusätzlich")
    wi = [r["kennzahlen"].get("wirtschaft", {}) for r in rows]
    if any(sum(w.values()) for w in wi):
        lines.append("Wirtschaft der neuen KI (gesamt): " + ", ".join(
            f"{k} {sum(w.get(k, 0) for w in wi)}" for k in ("ausbau", "bau", "hacker", "vorlauf", "technik", "eingenommen", "technik_upgrade")))
    sp = [r["kennzahlen"].get("einnahmen_25_30") for r in rows if r["kennzahlen"].get("einnahmen_25_30")]
    if sp:
        for art, label in (("1", "neue KI"), ("0", "klassische KI")):
            vals = [s[art] for s in sp if art in s]
            if vals:
                lines.append(f"Einnahmen Minute 25 bis 30 ({label}, je Buendnis, {len(vals)} Partien ueber 30 min): "
                             f"Mittel {sum(vals) / len(vals):.0f}")
    ab = [r["kennzahlen"].get("einnahmen_ab_30") for r in rows if r["kennzahlen"].get("einnahmen_ab_30")]
    if ab:
        for art, label in (("1", "neue KI"), ("0", "klassische KI")):
            vals = [s[art] for s in ab if art in s]
            if vals:
                lines.append(f"Einnahmen je Minute ab Minute 30 ({label}, je Buendnis, {len(vals)} Partien): "
                             f"Mittel {sum(vals) / len(vals):.0f}")
    rz = [r["kennzahlen"].get("rueckzug", [0, 0, 0, 0, 0]) for r in rows]
    if any(x[0] for x in rz):
        s = [sum(x[i] for x in rz) for i in range(5)]
        lines.append(f"Rueckzug beschaedigter Einheiten (neue KI, je Partie): {s[0] / len(rows):.1f} Einheiten, Wert "
                     f"{s[1] / len(rows):.0f}; zur Werkstatt {s[2]}, zur Heilung {s[3]}, heim {s[4]} (gesamt)")
    pt = [r["kennzahlen"].get("planer") for r in rows if r["kennzahlen"].get("planer")]
    if pt:
        def pm(k):
            return sum(p[k] for p in pt) / len(pt)
        lines.append(f"Planer der neuen KI (je Plan und KI, Mittel ueber {len(pt)} Partien): {pm('objekte'):.0f} Objekte, "
                     f"Schnappschuss {pm('schnappschuss_us'):.0f} us, Rechnen {pm('rechnen_us'):.0f} us, "
                     f"Warten bei der Uebernahme {pm('warten_us'):.0f} us")
    ad = {}
    for r in rows:
        for g, (n, dauer, wert) in (r["kennzahlen"].get("angriff_dauer") or {}).items():
            e = ad.setdefault(g, [0, 0, 0])
            e[0] += n
            e[1] += dauer
            e[2] += wert
    if ad:
        lines.append("Angriffe nach Ende (Zahl, mittlere Dauer s, mittlerer Wert beim Ende): " + ", ".join(
            f"{g} {e[0]} ({e[1] / e[0]:.0f} s, {e[2] / e[0]:.0f})" for g, e in sorted(ad.items(), key=lambda x: -x[1][0])))
    zi = sum(r["kennzahlen"].get("ziele", 0) for r in rows)
    sw = sum(r["kennzahlen"].get("superwaffe", 0) for r in rows)
    if zi or sw:
        lines.append(f"Zielwechsel nach Planer (KIZIEL): {zi} ({zi / len(rows):.1f} je Partie); Superwaffen-Ziele (KISUPERWAFFE): {sw}")
    uf = {}
    for r in rows:
        for k, v in (r["kennzahlen"].get("ueberfall") or {}).items():
            uf[k] = uf.get(k, 0) + v
    if uf:
        lines.append("Ueberfaelle (KIUEBERFALL, gesamt): " + ", ".join(f"{k} {v}" for k, v in sorted(uf.items(), key=lambda x: -x[1])))
    gf = {}
    for r in rows:
        for k, v in (r["kennzahlen"].get("gefecht") or {}).items():
            gf[k] = gf.get(k, 0) + v
    if any(gf.values()):
        lines.append(f"Gefechtsfuehrung (KIGEFECHT, je Partie): Fokusbefehle {gf.get('fokus', 0) / len(rows):.0f}, "
                     f"Abstand {gf.get('abstand', 0) / len(rows):.0f}, verteilte Einheiten {gf.get('verteilt', 0) / len(rows):.0f} "
                     f"({gf.get('verteilen', 0)} Mal verteilt)")
    if any(r["kennzahlen"].get("fokus") for r in rows):
        lines.append("Gegnerwahl der neuen KI (Wechsel nach Grund): " + merge("fokus"))
    for art, label in (("1", "neue KI"), ("0", "klassische KI")):
        same = [0, 0]
        for r in rows:
            g = r["kennzahlen"].get("gleich", {}).get(art)
            if g:
                same[0] += g[0]
                same[1] += g[1]
        if same[1]:
            lines.append(f"Gleicher Gegner im Buendnis ({label}): {100.0 * same[0] / same[1]:.0f} % der Messpunkte ({same[0]}/{same[1]})")
    # gespiegelte Paare: gleiche Karte und gleicher Seed
    pairs = {}
    for r in rows:
        pairs.setdefault((r["karte"], r["seed"]), []).append(r)
    both = sum(1 for p in pairs.values() if len(p) == 2 and all(x["kennzahlen"]["sieger_art"] == 1 for x in p))
    none = sum(1 for p in pairs.values() if len(p) == 2 and all(x["kennzahlen"]["sieger_art"] == 0 for x in p))
    split = sum(1 for p in pairs.values() if len(p) == 2) - both - none
    lines.append(f"Gespiegelte Paare: neue KI gewinnt beide {both}, klassische beide {none}, geteilt/offen {split}")
    lines.append("")
    lines.append("Karte                  Seed  Belegung      Sieger  Dauer   Hilfe  Angr  Exit")
    for r in sorted(rows, key=lambda x: (x["karte"], x["seed"], x["ki"])):
        k = r["kennzahlen"]
        sieger = {1: "neu", 0: "klass.", None: "-"}.get(k["sieger_art"], str(k["sieger_art"]))
        dauer = f"{r['endframe'] / 1800.0:5.1f}m" if r.get("endframe") else f">{r['letzter_frame'] / 1800.0:4.1f}m"
        lines.append(f"{r['karte'][:22]:22} {r['seed']:4}  {r['ki']:13} {sieger:7} {dauer:7} {k['hilfe_einsaetze']:5} "
                     f"{k.get('angriffe', 0):5}  {r['exit']}")
    return "\n".join(lines)


# ------------------------------------------------------------------------------------------------
def cmd_lauf(a):
    res = run_match(a, a.karte, a.ki, a.seed, a.out, f"{safe(a.karte)}-s{a.seed}-{safe(a.ki)}")
    res["kennzahlen"] = summarize_match(res)
    with open(os.path.join(a.out, "ergebnisse.jsonl"), "a", encoding="utf-8") as f:
        f.write(json.dumps(res) + "\n")
    print(summary([res]))
    return 0 if res["exit"] == 0 else 1


def cmd_serie(a):
    os.makedirs(a.out, exist_ok=True)
    jsonl = os.path.join(a.out, "ergebnisse.jsonl")
    done = set()
    rows = []
    if os.path.isfile(jsonl):
        for line in open(jsonl, encoding="utf-8"):
            r = json.loads(line)
            rows.append(r)
            done.add((r["karte"], r["seed"], r["ki"]))
    todo = [(karte, seed, ki) for karte in a.karten for seed in a.seeds for ki in a.belegungen
            if (karte, seed, ki) not in done]
    lock = threading.Lock()

    def one(job):
        karte, seed, ki = job
        with lock:
            print(f"[{time.strftime('%H:%M:%S')}] {karte} Seed {seed} {ki} ...", flush=True)
        res = run_match(a, karte, ki, seed, a.out, f"{safe(karte)}-s{seed}-{safe(ki)}")
        res["kennzahlen"] = summarize_match(res)
        res["label"] = a.label
        k = res["kennzahlen"]
        with lock:
            print(f"    {karte} Seed {seed} {ki}: Sieger {k['sieger_art']}, Ende {res['endframe']}, letzter Frame "
                  f"{res['letzter_frame']}, Hilfe {k['hilfe_einsaetze']}, Angriffe {k['angriffe']}, Exit {res['exit']}, "
                  f"{res['sekunden']} s", flush=True)
            with open(jsonl, "a", encoding="utf-8") as f:
                f.write(json.dumps(res) + "\n")
            rows.append(res)

    if a.parallel > 1:
        # Parallel ohne Warten auf eine freie Maschine: -runner-Laeufe sperren sich nicht gegenseitig.
        a.nicht_warten = True
        with concurrent.futures.ThreadPoolExecutor(max_workers=a.parallel) as pool:
            list(pool.map(one, todo))
    else:
        for job in todo:
            one(job)
    text = summary(rows)
    with open(os.path.join(a.out, "zusammenfassung.txt"), "w", encoding="utf-8") as f:
        f.write(text + "\n")
    print(text)
    return 0


def cmd_auswertung(a):
    rows = [json.loads(l) for l in open(a.datei, encoding="utf-8") if l.strip()]
    for r in rows:
        if a.neu_einlesen and os.path.isfile(r.get("log", "")):
            keep = {k: r[k] for k in ("karte", "ki", "seed", "exit", "sekunden", "log") if k in r}
            r.clear()
            r.update(parse_log(keep["log"]))
            r.update(keep)
            r.pop("kennzahlen", None)
        if "kennzahlen" not in r:
            r["kennzahlen"] = summarize_match(r)
    print(summary(rows))
    return 0


def cmd_verlauf(a):
    """Zeitverlauf je Spieler aus einem oder mehreren Protokollen (Beobachtung des spaeten Spiels)."""
    step = a.minuten * 1800
    agg = {}	# minute -> Listen
    for path in a.logs:
        rows = []
        with open(path, "r", encoding="ascii", errors="replace") as f:
            for raw in f:
                m = KISTAT.match(raw.strip())
                if m:
                    d = parse_kv(m.group(2))
                    d["frame"] = int(m.group(1))
                    rows.append(d)
        frames = sorted({r["frame"] for r in rows})
        if not a.nur_summe:
            print(f"== {os.path.basename(path)}")
            print("  min spieler seite                art team geld   armeewert einheiten leerlauf gebaeude gegner")
        for fr in frames:
            if fr % step:
                continue
            cur = [r for r in rows if r["frame"] == fr and not r.get("besiegt")]
            # gleicher Gegner im Team?
            by_team = {}
            for r in cur:
                by_team.setdefault(r.get("team"), []).append(r.get("gegner"))
            same = sum(1 for t, g in by_team.items() if t is not None and t >= 0 and len(g) >= 2 and len(set(g)) == 1)
            teams2 = sum(1 for t, g in by_team.items() if t is not None and t >= 0 and len(g) >= 2)
            e = agg.setdefault(fr // 1800, {"geld": [], "armeewert": [], "einheiten": [], "leer": [], "same": 0, "teams": 0})
            e["same"] += same
            e["teams"] += teams2
            for r in cur:
                e["geld"].append(r["geld"])
                e["armeewert"].append(r["armeewert"])
                e["einheiten"].append(r["einheiten"])
                e["leer"].append(r["leerlauf"] / r["einheiten"] if r["einheiten"] else 0.0)
                if not a.nur_summe:
                    print(f"  {fr // 1800:3} {r['spieler']:7} {str(r['seite'])[:20]:20} {r['art']:3} {r['team']:4} {r['geld']:6} "
                          f"{r['armeewert']:9} {r['einheiten']:9} {r['leerlauf']:8} {r['gebaeude']:8} {r.get('gegner', '?')}")
    print("Mittel ueber alle lebenden Spieler je Zeitpunkt:")
    print("  min  n  geld   armeewert einheiten leerlauf% teams_mit_gleichem_gegner")
    for minute in sorted(agg):
        e = agg[minute]
        n = len(e["geld"])
        if not n:
            continue
        print(f"  {minute:3} {n:3} {sum(e['geld']) / n:6.0f} {sum(e['armeewert']) / n:9.0f} {sum(e['einheiten']) / n:9.1f} "
              f"{100.0 * sum(e['leer']) / n:8.0f} {e['same']}/{e['teams']}")
    return 0


URSACHEN_MINUTEN = (3, 5, 8, 10, 15, 20, 25, 30)


def match_series(path, kills=None):
    """KISTAT je Frame: {frame: [Zeilen]} aus einem Protokoll; KIVERLUST-Zeilen nach kills, wenn angegeben."""
    series = {}
    with open(path, "r", encoding="ascii", errors="replace") as f:
        for raw in f:
            line = raw.strip()
            m = KISTAT.match(line)
            if m:
                d = parse_kv(m.group(2))
                d["frame"] = int(m.group(1))
                series.setdefault(d["frame"], []).append(d)
                continue
            if kills is not None:
                m = KIVERLUST.match(line)
                if m:
                    kills.append({"frame": int(m.group(1)), "opfer": int(m.group(2)), "wert": int(m.group(4)),
                                  "gebaeude": m.group(5) == "1", "taeter": int(m.group(6)),
                                  "taeter_gebaeude": m.group(8) == "1", "ort": m.group(9)})
    return series


def alliance_sums(rows, art):
    """Summen eines Buendnisses (alle Spieler der KI-Art art) an einem Messpunkt."""
    keys = ("armeewert", "einheiten", "gebaeude", "einnahmen", "verloren_e", "verloren_g", "zerstoert_e", "zerstoert_g")
    out = {k: 0 for k in keys}
    for d in rows:
        if d.get("art") == art:
            for k in keys:
                out[k] += d.get(k, 0) if isinstance(d.get(k, 0), int) else 0
    return out


def analyse_match(row, neu=1, alt=0):
    """Verlauf einer Partie neue gegen klassische KI: Verhaeltnisse je Minute, Kipppunkt, wer zuerst faellt."""
    kills = []
    series = match_series(row["log"], kills)
    if not series:
        return None
    sieg = row.get("sieg")
    arts = {w["art"] for w in sieg["sieger"]} if sieg and sieg.get("sieger") else set()
    ausgang = "sieg" if arts == {neu} else ("niederlage" if arts == {alt} else "offen")
    frames = sorted(series)
    res = {"ausgang": ausgang, "karte": row.get("karte"), "seed": row.get("seed"), "ki": row.get("ki"),
           "ende_min": round(frames[-1] / 1800.0, 1), "minuten": {}}
    ratio_by_frame = []
    first_fall = None
    first_building_lost = {neu: None, alt: None}
    for fr in frames:
        rows = series[fr]
        n, k = alliance_sums(rows, neu), alliance_sums(rows, alt)
        ratio = (n["armeewert"] + 1.0) / (k["armeewert"] + 1.0)
        ratio_by_frame.append((fr, ratio))
        for a, sums in ((neu, n), (alt, k)):
            if first_building_lost[a] is None and sums["verloren_g"] > 0:
                first_building_lost[a] = round(fr / 1800.0, 1)
        if first_fall is None:
            fallen = [d for d in rows if d.get("besiegt")]
            if fallen:
                first_fall = {"min": round(fr / 1800.0, 1), "art": fallen[0].get("art")}
        if fr % 1800 == 0 and fr // 1800 in URSACHEN_MINUTEN:
            res["minuten"][fr // 1800] = {
                "armee": round(ratio, 2),
                "einnahmen": round((n["einnahmen"] + 1.0) / (k["einnahmen"] + 1.0), 2),
                "gebaeude": round((n["gebaeude"] + 1.0) / (k["gebaeude"] + 1.0), 2),
                "einheiten_tausch": round((n["zerstoert_e"] + 1.0) / (n["verloren_e"] + 1.0), 2)}
    # Kipppunkt: ab wann das Armee-Verhaeltnis dauerhaft auf der Seite des spaeteren Verlierers bleibt
    kipp = None
    if ausgang != "offen":
        good = (lambda r: r < 1.0) if ausgang == "niederlage" else (lambda r: r > 1.0)
        for fr, r in reversed(ratio_by_frame):
            if not good(r):
                break
            kipp = round(fr / 1800.0, 1)
    res["kipppunkt_min"] = kipp
    # Verluste und Abschuesse der neuen KI nach Ort und Taeter (Einheiten, nach Baukosten), aus KIVERLUST
    art_of = {}
    for rows in series.values():
        for d in rows:
            art_of[d["spieler"]] = d.get("art")
    # Getrennt vor und nach dem Kipppunkt: Verluste danach sind oft Folge des verlorenen Spiels (der Sieger
    # steht in der Basis des Verlierers), Verluste davor eher die Ursache. Ohne Kipppunkt zaehlt Minute 12.
    grenze = (kipp if kipp is not None else 12.0) * 1800
    verluste = {}
    for k in kills:
        if k["gebaeude"]:
            continue
        if art_of.get(k["opfer"]) == neu:
            key = ("verloren", k["ort"], "gebaeude" if k["taeter_gebaeude"] else "einheit")
        elif art_of.get(k["taeter"]) == neu and art_of.get(k["opfer"]) == alt:
            key = ("zerstoert", k["ort"], "gebaeude" if k["taeter_gebaeude"] else "einheit")
        else:
            continue
        zeit = "vorher" if k["frame"] < grenze else "nachher"
        verluste[key] = verluste.get(key, 0) + k["wert"]
        verluste[key + (zeit,)] = verluste.get(key + (zeit,), 0) + k["wert"]
    res["verluste"] = verluste
    res["zuerst_besiegt"] = first_fall
    res["erstes_gebaeude_verloren"] = {"neu": first_building_lost[neu], "klassisch": first_building_lost[alt]}
    return res


def cmd_ursachen(a):
    """Warum gewinnt oder verliert die neue KI? Verlaeufe aus den Protokollen vorhandener Serien (keine neuen Partien)."""
    import statistics
    games = []
    for datei in a.dateien:
        for line in open(datei, encoding="utf-8"):
            if not line.strip():
                continue
            r = json.loads(line)
            if not os.path.isfile(r.get("log", "")):
                continue
            g = analyse_match(r)
            if g:
                games.append(g)
    if not games:
        print("keine Protokolle gefunden")
        return 1
    by = {}
    for g in games:
        by.setdefault(g["ausgang"], []).append(g)
    print(f"Partien: {len(games)} -- " + ", ".join(f"{k} {len(v)}" for k, v in sorted(by.items())))

    def med(vals):
        vals = [v for v in vals if v is not None]
        return f"{statistics.median(vals):5.2f}" if vals else "    -"

    for key, title in (("armee", "Armeewert neue/klassische KI"), ("einnahmen", "Einnahmen (Summe) neue/klassische KI"),
                       ("gebaeude", "Gebaeude neue/klassische KI"), ("einheiten_tausch", "Austausch der neuen KI (zerstoert/verloren)")):
        print(f"\n{title}, Median je Minute:")
        print("  Ausgang     " + " ".join(f"{m:>5}" for m in URSACHEN_MINUTEN))
        for ausgang in ("sieg", "niederlage", "offen"):
            gs = by.get(ausgang, [])
            if gs:
                print(f"  {ausgang:11} " + " ".join(med([g["minuten"].get(m, {}).get(key) for g in gs]) for m in URSACHEN_MINUTEN))
    for ausgang in ("sieg", "niederlage"):
        gs = by.get(ausgang, [])
        if not gs:
            continue
        print(f"\n{ausgang}: Dauer Median {med([g['ende_min'] for g in gs])} min, Kipppunkt Median "
              f"{med([g['kipppunkt_min'] for g in gs])} min")
        falls = [g["zuerst_besiegt"] for g in gs if g["zuerst_besiegt"]]
        neu_zuerst = sum(1 for f in falls if f["art"] == 1)
        print(f"  zuerst besiegt: neue KI {neu_zuerst}, klassische KI {len(falls) - neu_zuerst} (Median Minute "
              f"{med([f['min'] for f in falls])})")
        print(f"  erstes eigenes Gebaeude verloren (Median Minute): neue KI "
              f"{med([g['erstes_gebaeude_verloren']['neu'] for g in gs])}, klassische KI "
              f"{med([g['erstes_gebaeude_verloren']['klassisch'] for g in gs])}")
    if any(g.get("verluste") for g in games):
        for zeit, titel in ((None, "gesamt"), ("vorher", "vor dem Kipppunkt (ohne Kipppunkt: vor Minute 12)"),
                            ("nachher", "ab dem Kipppunkt")):
            print(f"\nEinheiten der neuen KI nach Ort, {titel} (Baukosten je Partie, Mittel; Ort aus Sicht des Opfers):")
            print("  Ausgang     Ort           verloren(durch Einheit/Gebaeude)  zerstoert(der Gegner)  Austausch")
            suffix = (zeit,) if zeit else ()
            for ausgang in ("sieg", "niederlage"):
                gs = by.get(ausgang, [])
                if not gs:
                    continue
                for ort in ("eigene_basis", "feld", "feindbasis"):
                    lu = sum(g["verluste"].get(("verloren", ort, "einheit") + suffix, 0) for g in gs) / len(gs)
                    lb = sum(g["verluste"].get(("verloren", ort, "gebaeude") + suffix, 0) for g in gs) / len(gs)
                    # Fuer die Abschuesse der neuen KI ist der Ort aus Sicht des Gegners gespiegelt.
                    gegen = {"eigene_basis": "feindbasis", "feindbasis": "eigene_basis", "feld": "feld"}[ort]
                    z = sum(g["verluste"].get(("zerstoert", gegen, t) + suffix, 0) for g in gs for t in ("einheit", "gebaeude")) / len(gs)
                    q = f"{z / (lu + lb):5.2f}" if lu + lb else "    -"
                    print(f"  {ausgang:11} {ort:13} {lu:8.0f} / {lb:8.0f}             {z:9.0f}            {q}")

    print("\nNach Karte und Belegung (Siege/Niederlagen/offen der neuen KI):")
    cells = {}
    for g in games:
        c = cells.setdefault((g["karte"], g["ki"]), [0, 0, 0])
        c[("sieg", "niederlage", "offen").index(g["ausgang"])] += 1
    for (karte, ki), c in sorted(cells.items()):
        print(f"  {karte:30} {ki:14} {c[0]:3} / {c[1]:3} / {c[2]:3}")
    if a.einzeln:
        print("\nJe Partie (Ausgang, Karte, Seed, Ende, Kipppunkt, zuerst besiegt, Armee-Verhaeltnis bei Minute 5/10/15):")
        for g in games:
            arm = "/".join(str(g["minuten"].get(m, {}).get("armee", "-")) for m in (5, 10, 15))
            f = g["zuerst_besiegt"]
            print(f"  {g['ausgang']:10} {g['karte']} s{g['seed']} {g['ki']} ende {g['ende_min']} kipp {g['kipppunkt_min']} "
                  f"zuerst {('neu' if f and f['art'] == 1 else 'klassisch') + ' ' + str(f['min']) if f else '-'} armee {arm}")
    return 0


# ------------------------------------------------------------------------------------------------
# Wertung (Bradley-Terry, Elo-Skala)
def agent_of(letter, label):
    """Spieler einer Belegung: die klassische KI ist ueber alle Staende dieselbe, die neue KI je Stand eine eigene."""
    if letter == "K":
        return "K"
    return f"{label}:{letter}" if label else letter


def match_teams(row):
    """(Teams als {Teamnummer: [Spieler]}, Siegerteam oder None) aus Belegung und KISIEG."""
    teams = {}
    for i, tok in enumerate(row["ki"].split(",")):
        tok = tok.strip()
        if not tok:
            continue
        team = tok[1:].rstrip("emb") or f"frei{i}"
        # Schwierigkeitsanhang (e/m/b, etwa "K2e") gehoert zum Spieler: klassisch Leicht ist nicht der Anker K.
        stufe = tok[-1] if len(tok) > 1 and tok[-1] in "em" else ""
        teams.setdefault(team, []).append(agent_of(tok[0], row.get("label", "")) + stufe)
    sieg = row.get("sieg")
    if not sieg or not sieg.get("sieger"):
        return teams, None
    # Siegerteam ueber den Platz des ersten Siegers (Platz 1 ist der erste Eintrag der Belegung)
    platz = sieg["sieger"][0]["platz"]
    toks = [t.strip() for t in row["ki"].split(",")]
    if not 1 <= platz <= len(toks):
        return teams, None
    tok = toks[platz - 1]
    return teams, (tok[1:].rstrip("emb") or f"frei{platz - 1}")


def bt_fit(games, agents, anchor, iters=2000, rate=0.05, prior=0.01):
    """games: Liste (Sieger-Spieler, Verlierer-Spieler) als Listen; Teamstaerke = Mittel der Wertungen."""
    import math
    r = {g: 0.0 for g in agents}
    for _ in range(iters):
        grad = {g: -2.0 * prior * r[g] for g in agents}
        for win, lose in games:
            d = sum(r[x] for x in win) / len(win) - sum(r[x] for x in lose) / len(lose)
            p = 1.0 / (1.0 + math.exp(-d))
            for x in win:
                grad[x] += (1.0 - p) / len(win)
            for x in lose:
                grad[x] -= (1.0 - p) / len(lose)
        for g in agents:
            r[g] += rate * grad[g]
        shift = r[anchor] if anchor in r else sum(r.values()) / len(r)
        for g in agents:
            r[g] -= shift
    return {g: v * 400.0 / math.log(10.0) for g, v in r.items()}


def cmd_elo(a):
    import random
    rows = []
    for path in a.dateien:
        rows += [json.loads(l) for l in open(path, encoding="utf-8") if l.strip()]
    games = []
    offen = 0
    for row in rows:
        teams, winner = match_teams(row)
        if winner is None or winner not in teams:
            offen += 1
            continue
        for team, members in teams.items():
            if team != winner:
                games.append((teams[winner], members))
    agents = sorted({x for g in games for side in g for x in side})
    if not games:
        print(f"keine entschiedenen Partien ({offen} offen)")
        return 1
    anchor = "K" if "K" in agents else agents[0]
    rating = bt_fit(games, agents, anchor)
    rng = random.Random(a.seed)
    boots = {g: [] for g in agents}
    for _ in range(a.bootstrap):
        sample = [games[rng.randrange(len(games))] for _ in games]
        present = sorted({x for g in sample for side in g for x in side})
        rb = bt_fit(sample, present, anchor if anchor in present else present[0], iters=600)
        for g in present:
            boots[g].append(rb[g])
    print(f"Partien: {len(rows)}, entschieden {len(rows) - offen}, Vergleiche {len(games)}, Nullpunkt {anchor}")
    print("Spieler                         Elo     95 %-Intervall   Siege/Vergleiche")
    for g in sorted(agents, key=lambda x: -rating[x]):
        b = sorted(boots[g])
        lo = b[int(0.025 * len(b))] if b else float("nan")
        hi = b[min(len(b) - 1, int(0.975 * len(b)))] if b else float("nan")
        won = sum(1 for w, l in games if g in w)
        total = sum(1 for w, l in games if g in w or g in l)
        print(f"{g:30} {rating[g]:6.0f}   [{lo:6.0f}, {hi:6.0f}]   {won}/{total}")
    return 0


def cmd_planvergleich(a):
    """Die KIPLAN-Zeilen zweier Laeufe derselben Partie muessen gleich sein (Planer deterministisch)."""
    def plans(path):
        with open(path, "r", encoding="ascii", errors="replace") as f:
            return [l.strip() for l in f if l.startswith("KIPLAN ")]
    x, y = plans(a.a), plans(a.b)
    n = min(len(x), len(y))
    for i in range(n):
        if x[i] != y[i]:
            print(f"Unterschied in Zeile {i + 1}:\n  {x[i]}\n  {y[i]}")
            return 1
    print(f"KIPLAN: {len(x)} und {len(y)} Zeilen, die ersten {n} gleich")
    if n == 0:
        return 1
    return 0 if len(x) == len(y) else 1


def safe(s):
    return re.sub(r"[^A-Za-z0-9]+", "_", s).strip("_")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--exe", required=True, help="GZHNeoZH.exe oder GZHNeo.exe (Assets im Exe-Ordner verlinkt)")
    common.add_argument("--out", default="ki-runs", help="Ordner fuer Protokolle und Ergebnisse")
    common.add_argument("--frames", type=int, default=108000, help="Zeitlimit in Logik-Frames (30 = 1 s; Standard 60 min)")
    common.add_argument("--intervall", type=int, default=900, help="KISTAT alle N Logik-Frames (Standard 30 s)")
    common.add_argument("--timeout", type=int, default=3600, help="Sekunden je Partie")
    common.add_argument("--nicht-warten", action="store_true", help="nicht auf eine freie Maschine warten")
    common.add_argument("--slot", help="Python-Datei mit wait_free() (etwa mit Startsperre), statt der eingebauten Wartefunktion")
    common.add_argument("--extra", nargs=argparse.REMAINDER, help="weitere Spielparameter (am Ende angeben)")

    s = sub.add_parser("serie", parents=[common], help="gespiegelte Serie")
    s.add_argument("--karten", nargs="+", default=["Tournament Desert"])
    s.add_argument("--seeds", nargs="+", type=int, default=[1, 2, 3])
    s.add_argument("--belegungen", nargs="+", default=["N1,N1,K2,K2", "K1,K1,N2,N2"])
    s.add_argument("--label", default="", help="Name des Stands der neuen KI fuer die Wertung (elo)")
    s.add_argument("--parallel", type=int, default=1,
                   help="so viele Partien gleichzeitig (-runner, ohne Warten auf eine freie Maschine); Rechner nicht voll belegen")

    l = sub.add_parser("lauf", parents=[common], help="eine Partie")
    l.add_argument("--karte", required=True)
    l.add_argument("--ki", default="K1,K1,K2,K2")
    l.add_argument("--seed", type=int, default=1)

    w = sub.add_parser("auswertung", help="Zusammenfassung aus ergebnisse.jsonl")
    w.add_argument("datei")
    w.add_argument("--neu-einlesen", action="store_true", help="Protokolle neu auswerten (nach Aenderungen am Skript)")

    v = sub.add_parser("verlauf", help="Zeitverlauf je Spieler aus Protokollen (KISTAT)")
    v.add_argument("logs", nargs="+")
    v.add_argument("--minuten", type=int, default=5, help="Schrittweite in Spielminuten")
    v.add_argument("--nur-summe", action="store_true", help="nur die Mittelwerte ueber alle Protokolle")

    u = sub.add_parser("ursachen", help="Verlaeufe gewonnener und verlorener Partien (neue gegen klassische KI)")
    u.add_argument("dateien", nargs="+", help="ergebnisse.jsonl einer oder mehrerer Serien")
    u.add_argument("--einzeln", action="store_true", help="zusaetzlich eine Zeile je Partie")
    e = sub.add_parser("elo", help="Bradley-Terry-Wertung aus ergebnisse.jsonl (klassische KI = 0)")
    e.add_argument("dateien", nargs="+")
    e.add_argument("--bootstrap", type=int, default=200, help="Stichproben fuer das 95-%%-Intervall")
    e.add_argument("--seed", type=int, default=1)

    pv = sub.add_parser("planvergleich", help="KIPLAN-Zeilen zweier Protokolle vergleichen")
    pv.add_argument("a")
    pv.add_argument("b")

    a = ap.parse_args([x for x in sys.argv[1:] if x != ""])
    return {"serie": cmd_serie, "lauf": cmd_lauf, "auswertung": cmd_auswertung, "verlauf": cmd_verlauf,
            "elo": cmd_elo, "planvergleich": cmd_planvergleich, "ursachen": cmd_ursachen}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
