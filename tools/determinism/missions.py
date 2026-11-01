#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Missionstest: Kampagnenmissionen ohne Eingabe laufen lassen und die Skriptspur vergleichen.

Hintergrund (2026-09-19): Die Referenzreplays sind KI-Skirmishes, Missionen
kommen darin nicht vor. Wenn Generals die Spiellogik von Zero Hour bekommt (Entscheidung A-2), koennen
Missionen leise brechen: eine Skriptaktion wird zum NO_OP, eine Bedingung rechnet anders, ein Ausloeser
feuert nie. Dieses Werkzeug startet jede Mission direkt (-file Maps\\<Name>.map, Einzelspieler, normal),
laesst sie N Logik-Frames ohne Eingabe laufen und schreibt ueber die Umgebungsvariable
GENERALS_SCRIPT_TRACE jede ausgefuehrte Skriptaktion mit (ScriptEngine::_appendMessage, beide Baeume).

Zwei Befehle:
  run      Missionen eines Spiels ablaufen lassen; je Mission <out>/<mission>.trace, .log (Auszug aus
           DebugLogFileD.txt) und eine Zusammenfassung <out>/missions.json.
  compare  Zwei run-Ergebnisse vergleichen: Skripte, die nur in einem Lauf feuern, deutlich verschobene
           erste Ausloesungen, neue Assertions und Meldungen zu ungueltigen Skripten.

Nur Debug-Builds: -file steht in der Parametertabelle im Debug-Block (Release spielt sonst die Shell-Map).

  python tools/determinism/missions.py run --exe build-x64/Generals/Code/Main/Debug/GZHNeo.exe --game generals --out render-out/missions/alt
  python tools/determinism/missions.py compare render-out/missions/alt render-out/missions/neu
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time

MISSIONS = {
    "generals": ["training01"]
                + ["usa%02d" % i for i in range(1, 9)]
                + ["gla01", "gla02"] + ["gla%02d" % i for i in range(4, 9)]
                + ["chi%02d" % i for i in range(1, 9)] + ["chifinal"],
    "zh": ["md_usa%02d" % i for i in range(1, 6)]
          + ["md_gla%02d" % i for i in range(1, 6)]
          + ["md_chi%02d" % i for i in range(1, 6)]
          + ["gc_airgeneral", "gc_chemgeneral", "gc_chinaboss", "gc_demolitiongeneral", "gc_lasergeneral",
             "gc_nukegeneral", "gc_stealth", "gc_superweaponsgeneral", "gc_tankgeneral"],
}

LOG_PATTERNS = {
    "assert": re.compile(r"ASSERTION FAILURE: (.*)"),
    "invalid_script": re.compile(r"(Invalid script|invalid script|script action|script condition).*", re.I),
}


def click_assert_dialogs(pid):
    """Klickt in Dialogen des Prozesses Ignorieren bzw. Ja (BM_CLICK). Liefert die Dialogtexte."""
    import ctypes
    from ctypes import wintypes
    user32 = ctypes.windll.user32
    found = []
    proto = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def cb(hwnd, lparam):
        p = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value == pid:
            cls = ctypes.create_unicode_buffer(64)
            user32.GetClassNameW(hwnd, cls, 64)
            # Dialogklasse #32770: erst "Assertion Failure" (Ignorieren), danach "Ignore this crash from now on?" (Ja)
            if cls.value == "#32770":
                title = ctypes.create_unicode_buffer(256)
                user32.GetWindowTextW(hwnd, title, 256)
                texts = []

                def cb2(child, lp):
                    k = ctypes.create_unicode_buffer(32)
                    user32.GetClassNameW(child, k, 32)
                    if k.value == "Static":
                        b2 = ctypes.create_unicode_buffer(1024)
                        user32.GetWindowTextW(child, b2, 1024)
                        if b2.value.strip():
                            texts.append(" ".join(b2.value.split()))
                    return True
                user32.EnumChildWindows(hwnd, proto(cb2), 0)
                for text in ("&Ignorieren", "Ignorieren", "&Ignore", "Ignore", "&Ja", "Ja", "&Yes", "Yes"):
                    b = user32.FindWindowExW(hwnd, None, "Button", text)
                    if b:
                        user32.PostMessageW(b, 0x00F5, 0, 0)
                        found.append((title.value + ": " + " | ".join(texts))[:400])
                        break
        return True
    user32.EnumWindows(proto(cb), 0)
    return found


def run_one(exe, mission, frames, out, timeout):
    trace = os.path.join(out, mission + ".trace")
    if os.path.exists(trace):
        os.remove(trace)
    env = dict(os.environ, GENERALS_SCRIPT_TRACE=os.path.abspath(trace))
    exe_dir = os.path.dirname(os.path.abspath(exe))
    log = os.path.join(exe_dir, "DebugLogFileD.txt")
    cmd = [os.path.abspath(exe), "-win", "-xres", "800", "-yres", "600", "-nologo", "-noaudio", "-fixedTimeStep",
           "-quitAfter", str(frames), "-ignoreAsserts", "-file", "Maps\\%s.map" % mission]
    t0 = time.time()
    proc = subprocess.Popen(cmd, cwd=exe_dir, env=env, stdin=subprocess.DEVNULL,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    dialogs = []
    code = None
    while True:
        try:
            code = proc.wait(timeout=1)
            break
        except subprocess.TimeoutExpired:
            pass
        # Das Leck-Assert beim Beenden mitten in der Mission erscheint trotz -ignoreAsserts als Dialog
        # (TODO 39); weiterklicken und mitzaehlen.
        for x in click_assert_dialogs(proc.pid):
            if x not in dialogs[-1:]:
                dialogs.append(x)
        if time.time() - t0 > timeout:
            proc.kill()
            proc.wait()
            code = None
            break
    secs = time.time() - t0
    asserts, invalid = [], []
    loaded = False
    if os.path.isfile(log):
        text = open(log, encoding="latin-1", errors="replace").read()
        loaded = "failed to load" not in text.lower()
        for line in text.splitlines():
            m = LOG_PATTERNS["assert"].search(line)
            if m and "memory leaks" not in m.group(1):
                asserts.append(m.group(1).strip())
            if LOG_PATTERNS["invalid_script"].search(line) and "Run script" not in line:
                invalid.append(line.strip())
        with open(os.path.join(out, mission + ".log"), "w", encoding="utf-8") as f:
            f.write(text)
    events = []
    if os.path.isfile(trace):
        for line in open(trace, encoding="latin-1", errors="replace"):
            p = line.rstrip("\n").split(" ", 2)
            if len(p) == 3 and p[0].isdigit():
                events.append((int(p[0]), p[1], p[2]))
    return {"mission": mission, "exit": code, "seconds": round(secs, 1), "events": len(events), "dialogs": dialogs, "crt_asserts": [x for x in dialogs if "Runtime Library" in x],
            "last_frame": events[-1][0] if events else 0, "loaded": loaded,
            "asserts": sorted(set(asserts)), "invalid_script": sorted(set(invalid))}


def cmd_run(a):
    os.makedirs(a.out, exist_ok=True)
    missions = a.missions.split(",") if a.missions else MISSIONS[a.game]
    results = []
    for m in missions:
        r = run_one(a.exe, m, a.frames, a.out, a.timeout)
        if r["seconds"] < 5 and r["events"] == 0:
            print("%s: nach %.1f s ohne Skriptereignis beendet -- laeuft schon eine Spielinstanz? Abbruch." % (m, r["seconds"]))
            return 2
        results.append(r)
        print("%-24s exit %-4s %6.1f s  %4d Skriptereignisse bis Frame %5d  %d Asserts  %d Skriptmeldungen  %d CRT"
              % (m, r["exit"], r["seconds"], r["events"], r["last_frame"], len(r["asserts"]), len(r["invalid_script"]),
                 len(r["crt_asserts"])), flush=True)
        for x in r["crt_asserts"]:
            print("    CRT-Assert: %s" % x[:300], flush=True)
    with open(os.path.join(a.out, "missions.json"), "w", encoding="utf-8") as f:
        json.dump({"exe": a.exe, "frames": a.frames, "results": results}, f, indent=1)
    return 0


def read_trace(path):
    first = {}
    count = {}
    if os.path.isfile(path):
        for line in open(path, encoding="latin-1", errors="replace"):
            p = line.rstrip("\n").split(" ", 2)
            if len(p) != 3 or not p[0].isdigit():
                continue
            key = (p[1], p[2])
            first.setdefault(key, int(p[0]))
            count[key] = count.get(key, 0) + 1
    return first, count


def cmd_compare(a):
    ja = json.load(open(os.path.join(a.a, "missions.json"), encoding="utf-8"))
    jb = json.load(open(os.path.join(a.b, "missions.json"), encoding="utf-8"))
    rb = {r["mission"]: r for r in jb["results"]}
    bad = 0
    for ra in ja["results"]:
        m = ra["mission"]
        if m not in rb:
            continue
        r2 = rb[m]
        fa, ca = read_trace(os.path.join(a.a, m + ".trace"))
        fb, cb = read_trace(os.path.join(a.b, m + ".trace"))
        only_a = sorted(k for k in fa if k not in fb)
        only_b = sorted(k for k in fb if k not in fa)
        shifted = sorted((k, fa[k], fb[k]) for k in fa if k in fb and abs(fa[k] - fb[k]) > a.tolerance)
        new_asserts = [x for x in r2["asserts"] if x not in ra["asserts"]]
        new_invalid = [x for x in r2["invalid_script"] if x not in ra["invalid_script"]]
        new_invalid += ["CRT: " + x for x in r2.get("crt_asserts", []) if x not in ra.get("crt_asserts", [])]
        same = not (only_a or only_b or shifted or new_asserts or new_invalid) and ra["exit"] == r2["exit"]
        status = "gleich" if same and fa == fb else ("unauffaellig" if same else "ABWEICHUNG")
        if not same:
            bad += 1
        print("%-24s %s  (%d/%d Skripte, Exit %s/%s)" % (m, status, len(fa), len(fb), ra["exit"], r2["exit"]))
        for k in only_a[:a.show]:
            print("    nur A: %s %s (Frame %d)" % (k[0], k[1], fa[k]))
        for k in only_b[:a.show]:
            print("    nur B: %s %s (Frame %d)" % (k[0], k[1], fb[k]))
        for k, x, y in shifted[:a.show]:
            print("    verschoben: %s %s  Frame %d -> %d" % (k[0], k[1], x, y))
        for x in new_asserts[:a.show]:
            print("    neue Assertion: %s" % x)
        for x in new_invalid[:a.show]:
            print("    neue Skriptmeldung: %s" % x)
    print("%d Missionen mit Abweichung" % bad)
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--exe", required=True)
    r.add_argument("--game", choices=sorted(MISSIONS), required=True)
    r.add_argument("--out", required=True)
    r.add_argument("--frames", type=int, default=6000, help="Client-Frames je Mission (Standard 6000 = 200 s Spielzeit)")
    r.add_argument("--missions", help="kommagetrennt, sonst alle des Spiels")
    r.add_argument("--timeout", type=int, default=900)
    c = sub.add_parser("compare")
    c.add_argument("a")
    c.add_argument("b")
    c.add_argument("--tolerance", type=int, default=300, help="Frames, ab denen eine erste Ausloesung als verschoben gilt")
    c.add_argument("--show", type=int, default=8)
    a = ap.parse_args()
    return cmd_run(a) if a.cmd == "run" else cmd_compare(a)


if __name__ == "__main__":
    sys.exit(main())
