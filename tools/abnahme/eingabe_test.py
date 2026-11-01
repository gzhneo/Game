#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Eingabe-Pruefung, vollautomatisch (nur Windows).

Startet das Spiel im Fenster mit einem Skirmish (lokaler Spieler Beobachter) und einem Testablauf. Die Befehle
ziehenEcht, radEcht und tasteEcht schicken echte Fenster-Nachrichten (WM_RBUTTONDOWN/WM_MOUSEMOVE/WM_RBUTTONUP,
WM_MOUSEWHEEL, WM_KEYDOWN/WM_KEYUP) durch den Eingabeweg von Win32Mouse und Win32Keyboard; bildlauf und rad speisen
Testeingaben hinter diesem Weg ein. Ausgewertet werden die Zeilen "TESTABLAUF ..." des Runner-Protokolls (-crcLog).

Laeufe:
  A  normal: Ziehen mit der rechten Taste nach rechts und links, Rad je Raste und fein, Umschalt+Pfeil, dazu die
     Bildlauf-Messung (Taste, Rand, Ziehen 100 Pixel)
  B  mit -echteEingabeAus: dieselben echten Eingaben duerfen nichts bewirken, Testeingaben (bildlauf, rad) schon
  C  nur mit --vergleich: dieselbe Bildlauf-Messung mit dem Bau von main (Exe) oder aus dessen Protokoll (.log)

Pruefungen, Tabelle OK/FEHLER/n/a in <out>/ergebnis.txt und auf der Konsole:
   1 Ziehen ueber den Rand    die Kamera faehrt beim Ziehen weiter als die sichtbare Bodenbreite (Fensterbreite) (n/a, wenn der
                              Fokus fehlte oder verloren ging)
   2 Zeiger versteckt         waehrend des Ziehens ist der Zeiger in jedem Durchlauf versteckt (n/a ohne Fokus)
   3 Zeiger zurueck           nach dem Loslassen steht der Zeiger am Ausgangspunkt, sichtbar, kein Ziehen mehr
   4 Rad je Raste             grobes Rad (120): fein=0, zwei Rasten = zwei Zoomschritte
   5 Rad stufenlos            feines Rad (30): fein=1, ein Teil eines Zoomschritts (0,1 bis 0,5 Schritte)
   6 Rad nach feinem          danach eine Raste (120) wieder etwa ein Zoomschritt (0,75 bis 1,25)
   7 Taste echt               Umschalt+Pfeil als Fenster-Nachricht bewegt die Kamera
   8 echteEingabeAus sperrt   Lauf B: Ziehen, Rad und Taste als Fenster-Nachricht bewegen nichts
   9 Testeingaben wirken      Lauf B: bildlauf und rad wirken weiter
  10 Bildlauf-Vorgabe         Kamerastrecke je Takt fuer Taste, Rand und Ziehen wie main (hoechstens 3 % daneben;
                              n/a ohne --vergleich)
  11 Ohne Absturz             Exit-Code 0 beider Laeufe, keine Zeile TESTABLAUF ERROR

Die echten Mauseingaben setzen den Mauszeiger fuer etwa zwei Sekunden in die Fenstermitte und danach zurueck.
Waehrend der Laeufe die Maus nicht benutzen und kein anderes Fenster anklicken: verliert das Spielfenster den Fokus,
bricht Windows das Ziehen ab; Pruefung 1 bis 3 melden dann n/a statt eines Ergebnisses.

Aufruf (aus der Wurzel des Repos):
  python tools/abnahme/eingabe_test.py --exe build-x64/GeneralsMD/Code/Main/Release/GZHNeoZH.exe
  python tools/abnahme/eingabe_test.py --exe ... --vergleich D:/main/build-x64/GeneralsMD/Code/Main/Release/GZHNeoZH.exe
Exit-Code 0, wenn keine Pruefung FEHLER meldet.
"""
import argparse, ctypes, os, re, subprocess, sys, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

BILDLAUF = ["bildlauf taste rechts 30", "logik 10", "bildlauf taste links 30", "logik 10",
            "bildlauf rand rechts 30", "logik 10", "bildlauf rand links 30", "logik 10",
            "bildlauf ziehen rechts 30 100", "logik 10", "bildlauf ziehen links 30 100", "logik 10"]
VORSPANN = ["partie 30000", "logik 90", "bildlauf taste rechts 1", "logik 10"]

LAUF_A = VORSPANN + BILDLAUF + [
    "ziehenEcht rechts 300 60", "logik 10", "ziehenEcht links 300 60", "logik 10",
    "radEcht 120 2", "radEcht -120 2", "radEcht 30 1", "radEcht -30 1", "radEcht 120 1", "radEcht -120 1",
    "tasteEcht rechts 60", "logik 10", "tasteEcht links 60", "beenden"]
LAUF_B = VORSPANN + [
    "ziehenEcht rechts 150 40", "logik 10", "radEcht 120 2", "tasteEcht rechts 60", "logik 10",
    "bildlauf taste rechts 30", "logik 10", "rad 2", "beenden"]
LAUF_C = VORSPANN + BILDLAUF + ["beenden"]


def werte(zeile, kennung):
    i = zeile.find(kennung + " ")
    if i < 0:
        return None
    paare = (kv.split("=", 1) for kv in zeile[i + len(kennung) + 1:].split() if "=" in kv)
    return {k: v for k, v in paare}


def zeilen(log):
    if not os.path.isfile(log):
        return []
    with open(log, encoding="latin-1", errors="replace") as f:
        return [z.rstrip() for z in f if "TESTABLAUF" in z]


def alle(log, kennung):
    return [w for w in (werte(z, kennung) for z in zeilen(log)) if w is not None]


def vordergrund(pid):
    """Bittet Windows, das Spielfenster nach vorn zu holen. Erzwungen wird nichts (kein Fokusraub beim
    Nutzer); lehnt Windows ab, bleiben Pruefung 2 und 3 n/a."""
    try:
        from tastatur_test import fenster_von
        hwnd = fenster_von(pid, frist=60)
        ctypes.windll.user32.SetForegroundWindow(hwnd)
    except Exception:
        pass


def starte(a, exe, name, befehle, extra, out):
    skript = os.path.join(out, f"{name}.txt")
    with open(skript, "w", encoding="ascii", newline="\r\n") as f:
        f.write("# Eingabe-Pruefung (tools/abnahme/eingabe_test.py)\n" + "\n".join(befehle) + "\n")
    log = os.path.join(out, f"{name}.log")
    if os.path.isfile(log):
        os.remove(log)
    exe = os.path.abspath(exe)
    xres, yres = a.res.split("x")
    cmd = [exe, "-win", "-xres", xres, "-yres", yres, "-quickstart", "-noaudio", "-einstellungenNurLesen",
           "-crcLog", log, "-testablauf", skript, "-skirmish", a.map, "-skirmishAI", "2", "-skirmishSeed", "1"] + extra
    from slot import wait_free
    wait_free()
    print("Bitte waehrend des Laufs die Maus nicht benutzen (das Spielfenster braucht den Fokus).", flush=True)
    print(f"Lauf {name}:", " ".join(f'"{c}"' if " " in c else c for c in cmd), flush=True)
    proc = subprocess.Popen(cmd, cwd=os.path.dirname(exe))
    threading.Thread(target=vordergrund, args=(proc.pid,), daemon=True).start()
    try:
        code = proc.wait(timeout=a.frist)
    except subprocess.TimeoutExpired:
        code = None
        print(f"Zeitlimit {a.frist} s ueberschritten, PID {proc.pid} laeuft noch -- bitte von Hand pruefen")
    return code, log


def je_takt(log):
    """Kamerastrecke je Takt fuer jede Bildlauf-Art (Mittel aus beiden Richtungen; der Aufwaermlauf zaehlt nicht)."""
    summe = {}
    for w in alle(log, "BILDLAUF"):
        if "art" not in w or w.get("ticks") == "1":
            continue
        art = w["art"] + (w["drag"] if w["art"] == "ziehen" else "")
        summe.setdefault(art, []).append(float(w["world"]) / float(w["ticks"]))
    return {k: sum(v) / len(v) for k, v in summe.items()}


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--exe", required=True, help="GZHNeo.exe oder GZHNeoZH.exe dieses Zweigs")
    p.add_argument("--vergleich", help="Exe von main (Lauf C) oder ein Runner-Protokoll (.log) mit seinen BILDLAUF-Zeilen")
    p.add_argument("--map", default="Alpine Assault")
    p.add_argument("--res", default="1280x720")
    p.add_argument("--frist", type=int, default=600)
    p.add_argument("--out", default="render-out/eingabe_test")
    a = p.parse_args()
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    ergebnis = []

    def pruefe(nummer, name, ok, text):
        ergebnis.append((nummer, name, "n/a" if ok is None else ("OK" if ok else "FEHLER"), text))

    code_a, log_a = starte(a, a.exe, "A_normal", LAUF_A, [], out)
    code_b, log_b = starte(a, a.exe, "B_echteEingabeAus", LAUF_B, ["-echteEingabeAus"], out)

    ziehen = alle(log_a, "ZIEHENECHT")
    if ziehen:
        weiter = all(int(w["dragged"]) and float(w["world"]) > float(w["across"]) for w in ziehen)
        fokus = all(int(w["focus"]) for w in ziehen)
        pruefe(1, "Ziehen ueber den Rand", weiter if (weiter or fokus) else None,
               "; ".join(f"{w['dir']}: gezogen {w['dragged']}, {float(w['world']):.0f} gegen sichtbar {float(w['across']):.0f}" for w in ziehen)
               + ("" if fokus else " (Fenster ohne Fokus oder Fokus verloren: ohne Mausbenutzung neu starten)"))
        pruefe(2, "Zeiger versteckt", all(int(w["hidden"]) for w in ziehen) if fokus else None,
               "; ".join(f"{w['dir']}: versteckt {w['hidden']}, Fokus {w['focus']}" for w in ziehen) + ("" if fokus else " (Fenster ohne Fokus: von Hand pruefen)"))
        pruefe(3, "Zeiger zurueck", all(int(w["back"]) and int(w["visible"]) and not int(w["still_dragging"]) for w in ziehen) if fokus else None,
               "; ".join(f"{w['dir']}: zurueck {w['back']} (Abstand {w['cursor']}), sichtbar {w['visible']}, zieht noch {w['still_dragging']}" for w in ziehen) + ("" if fokus else " (Fenster ohne Fokus: von Hand pruefen)"))
    else:
        for n, name in ((1, "Ziehen ueber den Rand"), (2, "Zeiger versteckt"), (3, "Zeiger zurueck")):
            pruefe(n, name, False, "keine Zeile ZIEHENECHT")

    rad = alle(log_a, "RADECHT")
    if len(rad) >= 6:
        d = [abs(float(w["zoom_after"]) - float(w["zoom_before"])) for w in rad]
        schritt = d[0] / 2 if d[0] > 0 else 0
        pruefe(4, "Rad je Raste", rad[0]["fine"] == "0" and schritt > 0,
               f"fein {rad[0]['fine']}, zwei Rasten {d[0]:.4f} (Schritt {schritt:.4f})")
        anteil = d[2] / schritt if schritt else 0
        pruefe(5, "Rad stufenlos", rad[2]["fine"] == "1" and 0.1 <= anteil <= 0.5,
               f"fein {rad[2]['fine']}, Delta 30: {d[2]:.4f} = {anteil:.2f} Schritte")
        anteil = d[4] / schritt if schritt else 0
        pruefe(6, "Rad nach feinem", 0.75 <= anteil <= 1.25, f"eine Raste danach: {d[4]:.4f} = {anteil:.2f} Schritte")
    else:
        for n, name in ((4, "Rad je Raste"), (5, "Rad stufenlos"), (6, "Rad nach feinem")):
            pruefe(n, name, False, f"{len(rad)} von 6 Zeilen RADECHT")

    taste = alle(log_a, "TASTEECHT")
    pruefe(7, "Taste echt", bool(taste) and all(float(w["world"]) > 1 for w in taste),
           "; ".join(f"{w['dir']}: {float(w['world']):.0f}" for w in taste) or "keine Zeile TASTEECHT")

    zb, rb, tb = alle(log_b, "ZIEHENECHT"), alle(log_b, "RADECHT"), alle(log_b, "TASTEECHT")
    gesperrt = bool(zb and rb and tb) and float(zb[0]["world"]) < 1 and not int(zb[0]["dragged"]) \
        and abs(float(rb[0]["zoom_after"]) - float(rb[0]["zoom_before"])) < 1e-4 and float(tb[0]["world"]) < 1
    pruefe(8, "echteEingabeAus sperrt", gesperrt,
           (f"Ziehen {float(zb[0]['world']):.1f}, Rad {rb[0]['zoom_before']} -> {rb[0]['zoom_after']}, Taste {float(tb[0]['world']):.1f}")
           if zb and rb and tb else "Zeilen fehlen")
    lauf_b = [w for w in alle(log_b, "BILDLAUF") if "art" in w and w.get("ticks") != "1"]
    rad_ok = [z for z in zeilen(log_b) if " rad 2 zoom " in z]
    zoom_b = float(rad_ok[0].split(" zoom ")[1].split()[0]) if rad_ok else None
    wirkt = bool(lauf_b) and float(lauf_b[0]["world"]) > 1 and zoom_b is not None and rb and abs(zoom_b - float(rb[0]["zoom_after"])) > 1e-4
    pruefe(9, "Testeingaben wirken", wirkt,
           (f"bildlauf {float(lauf_b[0]['world']):.0f}, rad Zoom {rb[0]['zoom_after'] if rb else '?'} -> {zoom_b}") if lauf_b else "keine Zeile BILDLAUF")

    jetzt = je_takt(log_a)
    if a.vergleich:
        if a.vergleich.lower().endswith(".log"):
            log_c, code_c = a.vergleich, 0
        else:
            code_c, log_c = starte(a, a.vergleich, "C_main", LAUF_C, [], out)
        vorher = je_takt(log_c)
        teile, ok = [], bool(vorher) and code_c == 0
        for art in sorted(jetzt):
            if art not in vorher or vorher[art] == 0:
                ok = False
                teile.append(f"{art}: main fehlt")
                continue
            abw = jetzt[art] / vorher[art] - 1
            ok = ok and abs(abw) <= 0.03
            teile.append(f"{art}: {jetzt[art]:.2f} gegen {vorher[art]:.2f} ({abw * 100:+.1f} %)")
        pruefe(10, "Bildlauf-Vorgabe", ok, "; ".join(teile))
    else:
        pruefe(10, "Bildlauf-Vorgabe", None, "ohne --vergleich; je Takt: " + ", ".join(f"{k} {v:.2f}" for k, v in sorted(jetzt.items())))

    fehler = [z for z in zeilen(log_a) + zeilen(log_b) if "TESTABLAUF ERROR" in z]
    pruefe(11, "Ohne Absturz", code_a == 0 and code_b == 0 and not fehler,
           f"Exit-Codes {code_a}, {code_b}; ERROR-Zeilen {len(fehler)}" + (f" ({fehler[0]})" if fehler else ""))

    text = [f"Eingabe-Pruefung {time.strftime('%Y-%m-%d %H:%M')}, {os.path.abspath(a.exe)}", "",
            f"{'Nr':>2}  {'Pruefung':24}  Ergebnis  Einzelheiten"]
    text += [f"{n:2}  {name:24}  {zustand:8}  {t}" for n, name, zustand, t in ergebnis]
    zahl = sum(1 for e in ergebnis if e[2] == "FEHLER")
    text += ["", f"{zahl} FEHLER, {sum(1 for e in ergebnis if e[2] == 'OK')} OK, {sum(1 for e in ergebnis if e[2] == 'n/a')} n/a",
             "Nur von Hand: Gefuehl der Maus (Beschleunigung, Ruckeln), Touchpad und Seitenrad, mehrere Bildschirme,",
             "Vollbild und rahmenlos (Zeiger fangen), DPI-Wechsel, Zeigergroesse, IME."]
    with open(os.path.join(out, "ergebnis.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(text) + "\n")
    print("\n".join(text))
    print(f"Ergebnis: {os.path.join(out, 'ergebnis.txt')}")
    return 1 if zahl else 0


if __name__ == "__main__":
    sys.exit(main())
