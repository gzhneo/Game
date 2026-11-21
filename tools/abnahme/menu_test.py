#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Menu check: does a test script get through the main menu? (Windows only)

Starts the game in a window with the shell map and the main menu (without -quickstart) and a test script that
clicks through the menus like a player. For every test script that goes through the main menu (lan_lokal.py,
zuschauen_test.py, campaign and skirmish scripts) the main menu has to appear without the real mouse moving --
test scripts and -noRealInput ignore it.

Runs (per exe):
  LAN       hauptmenue, klick ButtonMultiplayer, klick ButtonNetwork, fenster LanLobbyMenu.wnd:ButtonHost
  LAN_no_real_input   the same with -noRealInput
  Skirmish  hauptmenue, klick ButtonSinglePlayer, klick ButtonSkirmish, fenster SkirmishGameOptionsMenu.wnd:ButtonStart

A run is OK when the exit code is 0, every command has a line "TESTSCRIPT OK line ..." and no line
"TESTSCRIPT ERROR" occurs. Table OK/ERROR in <out>/result.txt and on the console.

Usage (from the repository root), also for both games at once:
  python tools/abnahme/menu_test.py --exe build-x64/GeneralsMD/Code/Main/Release/GZHNeoZH.exe
  python tools/abnahme/menu_test.py --exe build-x64/GeneralsMD/Code/Main/Release/GZHNeoZH.exe \\
      --exe build-x64/Generals/Code/Main/Release/GZHNeo.exe
Exit code 0 when no run reports ERROR.
"""
import argparse, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

LAN = ["mainMenu", "click MainMenu.wnd:ButtonMultiplayer 900", "click MainMenu.wnd:ButtonNetwork 900",
       "window LanLobbyMenu.wnd:ButtonHost 900", "quit"]
SKIRMISH = ["mainMenu", "click MainMenu.wnd:ButtonSinglePlayer 900", "click MainMenu.wnd:ButtonSkirmish 900",
            "window SkirmishGameOptionsMenu.wnd:ButtonStart 900", "quit"]
RUNS = [("LAN", LAN, []), ("LAN_no_real_input", LAN, ["-noRealInput"]), ("Skirmish", SKIRMISH, [])]


def log_lines(log):
    try:
        with open(log, encoding="latin-1") as f:
            return [z.rstrip("\r\n") for z in f if "TESTSCRIPT" in z]
    except OSError:
        return []


def start_run(a, exe, name, commands, extra, out):
    script = os.path.join(out, f"{name}.txt")
    with open(script, "w", encoding="ascii", newline="\r\n") as f:
        f.write("\n".join(commands) + "\n")
    log = os.path.join(out, f"{name}.log")
    if os.path.isfile(log):
        os.remove(log)
    xres, yres = a.res.split("x")
    cmd = [exe, "-win", "-xres", xres, "-yres", yres, "-noaudio", "-settingsReadOnly",
           "-crcLog", log, "-testScript", script] + extra
    from slot import wait_free
    wait_free()
    print(f"Run {name}:", " ".join(f'"{c}"' if " " in c else c for c in cmd), flush=True)
    proc = subprocess.Popen(cmd, cwd=os.path.dirname(exe))
    try:
        code = proc.wait(timeout=a.timeout)
    except subprocess.TimeoutExpired:
        code = None
        print(f"time limit of {a.timeout} s exceeded, PID {proc.pid} is still running -- please check by hand")
    return code, log


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--exe", required=True, action="append", help="GZHNeo.exe or GZHNeoZH.exe (may be given several times)")
    p.add_argument("--res", default="1280x720")
    p.add_argument("--timeout", type=int, default=300, help="seconds per run")
    p.add_argument("--out", default="render-out/menu_test")
    a = p.parse_args()

    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    results = []
    for exe in a.exe:
        exe = os.path.abspath(exe)
        game = os.path.splitext(os.path.basename(exe))[0]
        for name, commands, extra in RUNS:
            code, log = start_run(a, exe, f"{game}_{name}", commands, extra, out)
            z = log_lines(log)
            errors = [x for x in z if "TESTSCRIPT ERROR" in x]
            ok_count = sum(1 for x in z if "TESTSCRIPT OK line" in x)
            ok = code == 0 and not errors and ok_count >= len(commands)
            text = f"exit code {code}, {ok_count} of {len(commands)} commands OK"
            if errors:
                text += f"; {errors[0].split('TESTSCRIPT ', 1)[-1]}"
            results.append((game, name, "OK" if ok else "ERROR", text))

    text = [f"Menu check {time.strftime('%Y-%m-%d %H:%M')}", "",
            f"{'Game':12}  {'Run':9}  Result    Details"]
    text += [f"{s:12}  {n:9}  {e:8}  {t}" for s, n, e, t in results]
    count = sum(1 for e in results if e[2] == "ERROR")
    text += ["", f"{count} ERROR, {len(results) - count} OK"]
    with open(os.path.join(out, "result.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(text) + "\n")
    print("\n".join(text))
    print(f"Result: {os.path.join(out, 'result.txt')}")
    return 1 if count else 0


if __name__ == "__main__":
    sys.exit(main())
