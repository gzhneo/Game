#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Spielweise menschlicher Spieler aus Replays lesen (Kauf-, Bau- und Angriffsbefehle).

Ein Replay enthaelt die Befehle der MENSCHLICHEN Spieler (KI-Plaetze rechnen lokal und senden nichts) mit
Frame, Spielernummer, Typ und Argumenten. Die Nummern der Vorlagen, Upgrades, Wissenschaften und
Spezialkraefte loest eine Liste auf, die das Spiel mit `-vorlagenAusgeben <Datei>` schreibt.

  spielanalyse.py --vorlagen-zh vorlagen-zh.txt --vorlagen-generals vorlagen-g.txt <Ordner oder .rep>... [--md Datei]

Grenzen (aus den Befehlen nicht lesbar): Gruppengroesse eines Angriffs (die Auswahl wird per Rahmen oder
Objektnummer gesetzt, die Zahl der Einheiten kennt nur der Spielzustand), Ziele von Angriffen und Verkaeufen
(Objektnummern), Einnahmen, Ausgaben mit Preisnachlaessen und Upgrade-Kosten, Ausgang der Partie.
Die Spielernummer eines Platzes ist 2 + Rang unter den belegten Plaetzen (0 neutral, 1 zivil).
"""
import argparse
import collections
import glob
import os
import re
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HIER, "..", "fuzz", "replaycmd"))
import replaycmd  # noqa: E402

TICKS = 30                      # Logik-Frames je Sekunde
T_UNIT, T_BUILD, T_UPGRADE, T_SCIENCE = 1047, 1049, 1045, 1044
T_SPECIAL = (1040, 1041, 1042)
T_ATTACK = (1036, 1059, 1060, 1061, 1069)
T_SELL = 1052


def lade_liste(pfad):
    d = {"THING": {}, "UPGRADE": {}, "SCIENCE": {}, "SPECIAL": {}, "PLAYERTEMPLATE": {}}
    for z in open(pfad, encoding="latin-1"):
        t = z.rstrip("\r\n").split(" ")
        if t[0] == "THING":
            d["THING"][int(t[1])] = (t[2], t[3], int(t[4]), (t[5].split(",") if len(t) > 5 else []))
        elif t[0] in ("UPGRADE", "SCIENCE", "SPECIAL"):
            d[t[0]][int(t[1])] = t[2]
        elif t[0] == "PLAYERTEMPLATE":
            d["PLAYERTEMPLATE"][int(t[1])] = (t[2], t[3])
    return d


def plaetze(kopf):
    """[(Spielernummer, Name, Vorlagennummer, ist_mensch)] der belegten Plaetze."""
    text = kopf.decode("latin-1")
    m = re.search(r";S=([^;]*);", text)
    if not m:
        return []
    out, rang = [], 0
    for slot in m.group(1).split(":"):
        if not slot or slot[0] in "OX":
            continue
        f = slot.split(",")
        if slot[0] == "H":
            out.append((2 + rang, f[0][1:], int(f[5]), True))
        else:
            out.append((2 + rang, "KI " + f[0][1:], int(f[2]), False))
        rang += 1
    return out


def int_arg(cmd, i):
    t, raw = cmd.args[i]
    return int.from_bytes(raw[:4], "little", signed=True)


def analysiere(pfad, listen):
    daten = open(pfad, "rb").read()
    kopf, befehle, _ = replaycmd.parse(daten)
    pf = os.path.normcase(pfad)
    spiel = "zh" if "generalszh" in pf else "generals"
    liste = listen[spiel]
    sp = plaetze(kopf)
    menschen = {p for p, _, _, h in sp if h}
    hat_ki = any(not h for _, _, _, h in sp)
    ende = max((c.frame for c in befehle), default=0)
    ergebnis = {"datei": os.path.basename(pfad), "spiel": spiel, "minuten": ende / TICKS / 60.0,
                "gegen_ki": hat_ki, "spieler": {}}
    for p, name, tmpl, human in sp:
        if not human:
            continue
        seite = liste["PLAYERTEMPLATE"].get(tmpl, ("?", "?"))
        ergebnis["spieler"][p] = {"name": name, "vorlage": seite[0], "seite": seite[1], "ereignisse": [],
                                  "seiten": collections.Counter()}
    for c in befehle:
        if c.player not in ergebnis["spieler"]:
            continue
        ev = ergebnis["spieler"][c.player]["ereignisse"]
        seiten = ergebnis["spieler"][c.player]["seiten"]
        minute = c.frame / TICKS / 60.0
        if c.type == T_UNIT and c.args:
            n, s, kosten, art = liste["THING"].get(int_arg(c, 0), ("?", "?", 0, []))
            ev.append((minute, "einheit", n, kosten, art))
            seiten[basisseite(s)] += 1
        elif c.type == T_BUILD and c.args:
            n, s, kosten, art = liste["THING"].get(int_arg(c, 0), ("?", "?", 0, []))
            ev.append((minute, "gebaeude", n, kosten, art))
            seiten[basisseite(s)] += 1
        elif c.type == T_UPGRADE and c.args:
            ev.append((minute, "upgrade", liste["UPGRADE"].get(int_arg(c, len(c.args) - 1), "?"), 0, []))
        elif c.type == T_SCIENCE and c.args:
            ev.append((minute, "wissenschaft", liste["SCIENCE"].get(int_arg(c, 0), "?"), 0, []))
        elif c.type in T_SPECIAL and c.args:
            ev.append((minute, "spezial", liste["SPECIAL"].get(int_arg(c, 0), "?"), 0, []))
        elif c.type in T_ATTACK:
            ev.append((minute, "angriff", "", 0, []))
        elif c.type == T_SELL:
            ev.append((minute, "verkauf", "", 0, []))
    return ergebnis


def basisseite(s):
    for b in ("America", "China", "GLA"):
        if s.startswith(b):
            return b
    return "unbekannt"


def kategorie(art):
    if "STRUCTURE" in art:
        return "Gebaeude"
    if "AIRCRAFT" in art:
        return "Luft"
    if "INFANTRY" in art:
        return "Infanterie"
    if "VEHICLE" in art:
        return "Fahrzeug"
    return "Sonstiges"


def seite_von(v):
    """Seite aus der Spielervorlage, sonst die haeufigste Seite der bestellten Einheiten und Gebaeude."""
    b = basisseite(v["seite"])
    if b != "unbekannt":
        return b
    zaehler = collections.Counter({k: n for k, n in v["seiten"].items() if k != "unbekannt"})
    return zaehler.most_common(1)[0][0] if zaehler else "unbekannt"


def bericht(ergebnisse, md):
    z = []
    w = z.append
    w("# Spielweise menschlicher Spieler aus Replays")
    w("")
    w("Erzeugt von `tools/replay/spielanalyse.py` aus %d Replays (Quelle: Community-Sammlung, MIT-Lizenz). "
      "Es sind nur Befehle **menschlicher** Spieler gelesen (KI-Plätze senden keine). Zeiten in Spielminuten (30 Logik-Frames je Sekunde)." % len(ergebnisse))
    w("")
    w("Nicht aus den Befehlen lesbar: Gruppengröße eines Angriffs, Ziele von Angriffen und Verkäufen (nur Objektnummern), Einnahmen, "
      "Preisnachlässe, Upgrade-Kosten, Sieg oder Niederlage. Die Kosten der Einheiten und Gebäude sind die Grundkosten aus den Spieldaten.")
    w("")
    w("## Partien")
    w("")
    w("| Datei | Spiel | Dauer (min) | Gegen KI | Menschen (Fraktion) |")
    w("|---|---|---|---|---|")
    for e in ergebnisse:
        mens = ", ".join("%s (%s)" % (v["name"], seite_von(v)) for v in e["spieler"].values())
        w("| %s | %s | %.1f | %s | %s |" % (e["datei"][:48], e["spiel"], e["minuten"], "ja" if e["gegen_ki"] else "nein", mens))
    # je Spieler und Seite sammeln
    zeilen = []   # (spiel, seite, gegen_ki, minuten, ereignisse)
    for e in ergebnisse:
        for p, v in e["spieler"].items():
            if len(v["ereignisse"]) < 30:
                continue
            zeilen.append((e["spiel"], seite_von(v), e["gegen_ki"], e["minuten"], v["ereignisse"], v["name"], e["datei"]))
    w("")
    w("## Zusammenfassung (Mittelwerte über Spieler mit mindestens 30 Befehlen)")
    w("")
    w("Median in Spielminuten, in Klammern die Zahl der Spieler, die das Ereignis überhaupt hatten.")
    w("")
    w("| Kennzahl | gegen KI | gegen Menschen |")
    w("|---|---|---|")
    def med(werte):
        werte = sorted(werte)
        return ("%.1f (%d)" % (werte[len(werte) // 2], len(werte))) if werte else "-"
    def sammle(ki, f):
        out = []
        for spiel, seite, k, minuten, ev, name, datei in zeilen:
            if k != ki:
                continue
            x = f(ev, minuten)
            if x is not None:
                out.append(x)
        return out
    def erstes(art, marke, n=1):
        def f(ev, minuten):
            l = sorted(m for m, kd, nm, c, a in ev if kd == art and marke in a)
            return l[n - 1] if len(l) >= n else None
        return f
    def erster_angriff(ev, minuten):
        l = sorted(m for m, kd, nm, c, a in ev if kd == "angriff")
        return l[0] if l else None
    def rate_10_20(ev, minuten):
        if minuten < 15:
            return None
        return sum(c for m, kd, nm, c, a in ev if kd == "einheit" and 10 <= m < 20) / min(10.0, minuten - 10.0)
    for titel, f in (("2. Versorgungszentrum", erstes("gebaeude", "FS_SUPPLY_CENTER", 2)),
                     ("3. Versorgungszentrum", erstes("gebaeude", "FS_SUPPLY_CENTER", 3)),
                     ("1. Waffenfabrik", erstes("gebaeude", "FS_WARFACTORY")),
                     ("1. Angriffsbefehl", erster_angriff)):
        w("| %s | %s | %s |" % (titel, med(sammle(True, f)), med(sammle(False, f))))
    ra = sammle(True, rate_10_20)
    rb = sammle(False, rate_10_20)
    w("| Ausgaben für Einheiten 10–20 min ($/min, Grundkosten) | %s | %s |" % (
        "%d (%d)" % (sorted(ra)[len(ra) // 2], len(ra)) if ra else "-", "%d (%d)" % (sorted(rb)[len(rb) // 2], len(rb)) if rb else "-"))
    w("")
    w("## a) Wirtschaft")
    w("")
    w("Zeitpunkt des n-ten Versorgungszentrums, des ersten Hackers, der Abwurfzone und des Schwarzmarkts (Minute des Bau- bzw. Kaufbefehls), "
      "Versorger (Sammler) und Bauer (Dozer) bestellt in den ersten 10 Minuten.")
    w("")
    w("| Spieler | Spiel | Seite | gegen KI | 1. Zentrum | 2. | 3. | 4. | 1. Hacker | Abwurfzone | Schwarzmarkt | Sammler ≤10 min | Bauer ≤10 min |")
    w("|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    fmt = lambda x: "%.1f" % x if x is not None else "-"
    for spiel, seite, ki, minuten, ev, name, datei in zeilen:
        zentren = sorted(m for m, k, n, c, a in ev if k == "gebaeude" and "FS_SUPPLY_CENTER" in a)
        hacker = [m for m, k, n, c, a in ev if k == "einheit" and "hacker" in n.lower()]
        abwurf = [m for m, k, n, c, a in ev if k == "gebaeude" and "FS_SUPPLY_DROPZONE" in a]
        markt = [m for m, k, n, c, a in ev if k == "gebaeude" and "FS_BLACK_MARKET" in a]
        sammler = sum(1 for m, k, n, c, a in ev if k == "einheit" and "HARVESTER" in a and m <= 10)
        bauer = sum(1 for m, k, n, c, a in ev if k == "einheit" and "DOZER" in a and m <= 10)
        zz = zentren + [None] * 4
        w("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %d | %d |" % (
            name, spiel, seite, "ja" if ki else "nein", fmt(zz[0]), fmt(zz[1]), fmt(zz[2]), fmt(zz[3]),
            fmt(min(hacker) if hacker else None), fmt(min(abwurf) if abwurf else None), fmt(min(markt) if markt else None), sammler, bauer))
    w("")
    w("## b) Produktion")
    w("")
    w("Erste Waffenfabrik, Zahl der Fabriken und Kasernen nach 10 und 20 Minuten (Bauaufträge, ohne Abzug von Verkäufen), "
      "Ausgaben je Minute für Einheiten und Gebäude (Grundkosten, ohne Upgrades) in den Zeitfenstern 0–10, 10–20 und ab 20 Minuten.")
    w("")
    w("| Spieler | Seite | 1. Waffenfabrik | Fabriken 10/20 | Kasernen 10/20 | Einheiten $/min 0–10 | 10–20 | ab 20 | Gebäude $/min 0–10 | 10–20 | ab 20 |")
    w("|---|---|---|---|---|---|---|---|---|---|---|")
    for spiel, seite, ki, minuten, ev, name, datei in zeilen:
        fab = sorted(m for m, k, n, c, a in ev if k == "gebaeude" and ("FS_WARFACTORY" in a or "FS_AIRFIELD" in a))
        kas = sorted(m for m, k, n, c, a in ev if k == "gebaeude" and "FS_BARRACKS" in a)
        def bis(lst, t):
            return sum(1 for m in lst if m <= t)
        def rate(art, lo, hi):
            summe = sum(c for m, k, n, c, a in ev if k == art and lo <= m < hi)
            dauer = max(0.0, min(hi, minuten) - lo)
            return "%d" % (summe / dauer) if dauer >= 1.0 else "-"
        w("| %s | %s | %s | %d/%d | %d/%d | %s | %s | %s | %s | %s | %s |" % (
            name, seite, fmt(fab[0] if fab else None), bis(fab, 10), bis(fab, 20), bis(kas, 10), bis(kas, 20),
            rate("einheit", 0, 10), rate("einheit", 10, 20), rate("einheit", 20, 999),
            rate("gebaeude", 0, 10), rate("gebaeude", 10, 20), rate("gebaeude", 20, 999)))
    w("")
    w("## c) Angriffe")
    w("")
    w("Zeitpunkt des ersten Angriffsbefehls und Zahl der Angriffsbefehle je 5 Minuten (Angriff auf Objekt, erzwungener Angriff, Angriffsbewegung, Angriff der Gruppe). "
      "Die Gruppengröße steht nicht im Befehl.")
    w("")
    w("| Spieler | Seite | gegen KI | Dauer | 1. Angriff | Befehle je 5 min (0–5, 5–10, 10–15, 15–20, 20–25, 25–30) |")
    w("|---|---|---|---|---|---|")
    for spiel, seite, ki, minuten, ev, name, datei in zeilen:
        ang = sorted(m for m, k, n, c, a in ev if k == "angriff")
        je5 = [sum(1 for m in ang if i * 5 <= m < i * 5 + 5) for i in range(6)]
        w("| %s | %s | %s | %.1f | %s | %s |" % (name, seite, "ja" if ki else "nein", minuten, fmt(ang[0] if ang else None),
                                                 " / ".join(str(x) for x in je5)))
    w("")
    w("## d) Einheitenmix und die ersten 10 Einheiten")
    w("")
    w("Anteil der bestellten Einheiten nach Art, je Zeitabschnitt (0–10, 10–20, ab 20 Minuten), zusammengefasst über alle Partien der Seite.")
    w("")
    gesamt = collections.defaultdict(lambda: collections.defaultdict(lambda: collections.Counter()))
    for spiel, seite, ki, minuten, ev, name, datei in zeilen:
        for m, k, n, c, a in ev:
            if k != "einheit":
                continue
            ab = "0-10" if m < 10 else ("10-20" if m < 20 else "20+")
            gesamt[seite][ab][kategorie(a)] += 1
    w("| Seite | Abschnitt | Infanterie | Fahrzeug | Luft | Sonstiges | Summe |")
    w("|---|---|---|---|---|---|---|")
    for seite in sorted(gesamt):
        for ab in ("0-10", "10-20", "20+"):
            cnt = gesamt[seite][ab]
            summe = sum(cnt.values())
            if summe == 0:
                continue
            w("| %s | %s | %s | %s | %s | %s | %d |" % (seite, ab, *("%d %%" % round(100 * cnt[k] / summe) for k in ("Infanterie", "Fahrzeug", "Luft", "Sonstiges")), summe))
    w("")
    w("Die ersten 10 bestellten Einheiten (ohne Bauer und Sammler zu trennen) je Spieler:")
    w("")
    for spiel, seite, ki, minuten, ev, name, datei in zeilen:
        ersten = [n for m, k, n, c, a in sorted(ev) if k == "einheit"][:10]
        w("- **%s** (%s): %s" % (name, seite, ", ".join(ersten)))
    w("")
    w("## e) Verkäufe")
    w("")
    w("Zahl der Verkaufsbefehle je Spieler und der Zeitraum, in dem sie fallen. Was verkauft wird, steht nicht im Befehl (nur die Objektnummer).")
    w("")
    w("| Spieler | Seite | Verkäufe | davon in den letzten 20 % der Partie | erster Verkauf (min) |")
    w("|---|---|---|---|---|")
    for spiel, seite, ki, minuten, ev, name, datei in zeilen:
        v = sorted(m for m, k, n, c, a in ev if k == "verkauf")
        spaet = sum(1 for m in v if m >= 0.8 * minuten)
        w("| %s | %s | %d | %d | %s |" % (name, seite, len(v), spaet, fmt(v[0] if v else None)))
    w("")
    w("## f) Partien gegen HardAI")
    w("")
    w("Aus den Befehlen lässt sich der Ausgang nicht lesen. Die Dauer ist die Zeit bis zum letzten Befehl; bei Partien gegen die KI endet das Replay mit dem Ende der Partie.")
    w("")
    w("| Datei | Menschen | KI-Plätze | Dauer (min) |")
    w("|---|---|---|---|")
    for e in ergebnisse:
        if not e["gegen_ki"]:
            continue
        w("| %s | %d | siehe Dateiname | %.1f |" % (e["datei"][:60], len(e["spieler"]), e["minuten"]))
    text = "\n".join(z) + "\n"
    if md:
        open(md, "w", encoding="utf-8", newline="\n").write(text)
    else:
        sys.stdout.write(text)


def analysiere_ki_logs(ordner, liste):
    """Kennzahlen der Computerspieler aus Protokollen der KI-Serien (KIAUFTRAG, KISTAT, KIANGRIFF; -aiStats -crcLog).
    Ergebnis: {art: [Spieler]} mit art 'neue KI' und 'klassische KI'."""
    nach_name = {v[0]: v for v in liste["THING"].values()}
    out = {"neue KI": [], "klassische KI": []}
    for pfad in sorted(glob.glob(os.path.join(ordner, "**", "*.log"), recursive=True)):
        arten, ereignisse, angriffe, letzter = {}, collections.defaultdict(list), collections.defaultdict(list), 0
        for z in open(pfad, encoding="latin-1", errors="replace"):
            if z.startswith("KISTAT "):
                t = z.split()
                spieler = int(t[3])
                arten.setdefault(spieler, int(t[t.index("art") + 1]))
                letzter = max(letzter, int(t[1]))
            elif z.startswith("KIAUFTRAG "):
                t = z.split()
                frame, spieler = int(t[1]), int(t[3])
                art, name, kosten = t[5], t[7], int(t[9])
                kinds = nach_name.get(name, ("?", "?", 0, []))[3]
                ereignisse[spieler].append((frame / TICKS / 60.0, "einheit" if art == "einheit" else "gebaeude", name, kosten, kinds))
            elif z.startswith("KIANGRIFF ") and " los " in z:
                t = z.split()
                angriffe[int(t[3])].append(int(t[1]) / TICKS / 60.0) if int(t[3]) in angriffe else angriffe.__setitem__(int(t[3]), [int(t[1]) / TICKS / 60.0])
        minuten = letzter / TICKS / 60.0
        for spieler, art in arten.items():
            ev = ereignisse.get(spieler, [])
            if len(ev) < 10:
                continue
            ang = sorted(angriffe.get(spieler, []))
            ev = list(ev)
            if ang:
                ev.append((ang[0], "angriff", "", 0, []))
            out["neue KI" if art == 1 else "klassische KI"].append({"datei": os.path.basename(pfad), "spieler": spieler, "minuten": minuten, "ev": ev})
    return out


def bericht_ki(ki, md):
    z = []
    w = z.append
    w("# Kennzahlen der Computerspieler (aus Protokollen der KI-Serien)")
    w("")
    w("Kennzahlen aus den Zeilen `KIAUFTRAG` (Einheiten- und Gebäudeaufträge), `KISTAT` und `KIANGRIFF` "
      "(erster Angriff nur für die neue KI). Minuten sind Spielminuten. Median je Spieler und Partie, in Klammern die Zahl der Spieler mit dem Ereignis.")
    w("")
    w("| Kennzahl | " + " | ".join(ki) + " |")
    w("|---|" + "---|" * len(ki))
    def med(werte):
        werte = sorted(werte)
        return ("%.1f (%d)" % (werte[len(werte) // 2], len(werte))) if werte else "-"
    def spalte(f):
        zeile = []
        for art in ki:
            werte = []
            for sp in ki[art]:
                x = f(sp["ev"], sp["minuten"])
                if x is not None:
                    werte.append(x)
            zeile.append(med(werte))
        return " | ".join(zeile)
    def erstes(marke, n=1):
        def f(ev, minuten):
            l = sorted(m for m, kd, nm, c, a in ev if kd == "gebaeude" and marke in a)
            return l[n - 1] if len(l) >= n else None
        return f
    def anzahl(marke, bis):
        def f(ev, minuten):
            if minuten < bis:
                return None
            return float(sum(1 for m, kd, nm, c, a in ev if kd == "gebaeude" and marke in a and m <= bis))
        return f
    def rate(lo, hi):
        def f(ev, minuten):
            if minuten < hi:
                return None
            return sum(c for m, kd, nm, c, a in ev if kd == "einheit" and lo <= m < hi) / (hi - lo)
        return f
    def erster_angriff(ev, minuten):
        l = sorted(m for m, kd, nm, c, a in ev if kd == "angriff")
        return l[0] if l else None
    zeilen = (("2. Versorgungszentrum (min)", erstes("FS_SUPPLY_CENTER", 2)), ("3. Versorgungszentrum (min)", erstes("FS_SUPPLY_CENTER", 3)),
              ("1. Waffenfabrik (min)", erstes("FS_WARFACTORY")), ("Waffenfabriken nach 10 min", anzahl("FS_WARFACTORY", 10)),
              ("Kasernen nach 10 min", anzahl("FS_BARRACKS", 10)), ("Kasernen nach 20 min", anzahl("FS_BARRACKS", 20)),
              ("Einheiten $/min 10-20 (Grundkosten)", rate(10, 20)), ("1. Angriff (min, nur neue KI)", erster_angriff))
    for titel, f in zeilen:
        w("| %s | %s |" % (titel, spalte(f)))
    w("")
    w("Spieler je Art: " + ", ".join("%s %d" % (a, len(ki[a])) for a in ki))
    text = "\n".join(z) + "\n"
    if md:
        open(md, "w", encoding="utf-8", newline="\n").write(text)
    else:
        sys.stdout.write(text)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pfade", nargs="*")
    ap.add_argument("--ki-logs", help="Ordner mit Protokollen der KI-Serien (KIAUFTRAG-Zeilen); schreibt die Kennzahlen der Computerspieler")
    ap.add_argument("--md-ki")
    ap.add_argument("--vorlagen-zh", required=True)
    ap.add_argument("--vorlagen-generals", required=True)
    ap.add_argument("--md")
    a = ap.parse_args()
    listen = {"zh": lade_liste(a.vorlagen_zh), "generals": lade_liste(a.vorlagen_generals)}
    if a.ki_logs:
        bericht_ki(analysiere_ki_logs(a.ki_logs, listen["zh"]), a.md_ki)
        if not a.pfade:
            return
    dateien = []
    for p in a.pfade:
        dateien += sorted(glob.glob(os.path.join(p, "**", "*.rep"), recursive=True)) if os.path.isdir(p) else [p]
    ergebnisse = [analysiere(f, listen) for f in dateien]
    bericht(ergebnisse, a.md)


if __name__ == "__main__":
    main()
