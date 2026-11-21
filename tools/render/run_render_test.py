#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Render tests for the Direct3D 11 backend.

Starts the game with a fixed time step, has it write back-buffer screenshots at given client frames and lets it
quit by itself. The pictures can be compared with a reference (compare_shots.py). A baseline run also measures
the frame times without the fixed time step.

Commands:

  run       Record a scene once: screenshots, runner log (run.log) and debug log end up in --out. With
            --reference the result is compared with a reference folder afterwards.
  twice     Record a scene twice (--out/a, --out/b) and compare the two runs. Shows how reproducible a scene
            is; the tolerances are derived from it.
  baseline  Run a scene without the fixed time step and without screenshots and record the frame times
            (FRAMES line, frames.csv).

Scenes (--scene):
  shellmap  Start menu with shell map (the real game engine; the most important first test)
  menu      Main menu without shell map (2D only)
  skirmish  Skirmish without menu: --map, --ai, --seed
  replay    Play a replay: --replay <file> (matching the architecture of the exe)
  mission   Start a map directly (-file, debug builds only): --map "Maps\\md_usa01.map" -- campaign
            missions including the cutscene

Camera path: --path <file> moves the camera along the file (-cameraPath) and takes a picture every --every
logic frames while the path runs (-shotEvery). --frames does not apply then; the expected pictures follow from
the file.

Examples:
  python tools/render/run_render_test.py run --exe build-x64/Generals/Code/Main/Debug/GZHNeo.exe ^
      --scene shellmap --frames 300,900 --out render-out/shellmap --extra -ignoreAsserts
  python tools/render/run_render_test.py twice --exe ... --scene shellmap --out render-out/repro
  python tools/render/run_render_test.py run --exe ... --out render-out/new --reference tests/render/shellmap
  python tools/render/run_render_test.py baseline --exe build-x64/.../Release/GZHNeo.exe --frames 1800
  python tools/render/run_render_test.py run --exe ... --scene replay ^
      --replay tests/replays/x64/e0-alpine-assault-2ai-seed1.rep --path tests/render/paths/quer.txt ^
      --out render-out/quer --extra -ignoreAsserts

Notes:
  - Only one game instance at a time (mutex). If GZHNeo.exe or GZHNeoZH.exe is already running the script
    stops (--force skips the check). Exception --multiInstance: starts the game with -multiInstance (no
    single-instance lock, logs prefixed m<PID>_); several runs may then run side by side; the mouse pointer
    is not parked (the game ignores the real mouse in picture comparison runs).
  - Do not minimise the window (without -fixedTimeStep the engine pauses then). Before every start the mouse
    pointer is parked at the left screen edge (not in a corner, see park_cursor): over the window it would
    trigger hover effects in the menu and edge scrolling of the shell-map camera (both end up in the
    picture). Do not move the mouse over the window during a run.
  - Frames count client frames (also in the menu); with -fixedTimeStep that is one logic tick per picture.
  - Every run has its own user folder (<out>/_user) and a fixed detail level (--level, default VeryHigh); the
    user's settings do not flow in. --level user starts with the user's settings.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_shots  # noqa: E402

GAME_PROCESSES = ("gzhneo.exe", "gzhneozh.exe")


def running_game_processes():
    try:
        out = subprocess.run(["tasklist", "/fo", "csv", "/nh"], capture_output=True, text=True,
                             errors="replace", timeout=30).stdout
    except (OSError, subprocess.SubprocessError):
        return []
    found = []
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if len(parts) >= 2 and parts[0].lower() in GAME_PROCESSES:
            found.append(f"{parts[0]} (PID {parts[1]})")
    return found


def scene_args(a):
    if a.scene == "shellmap":
        return []
    if a.scene == "menu":
        return ["-noshellmap"]
    if a.scene == "replay":
        if not a.replay:
            raise SystemExit("--scene replay braucht --replay")
        return ["-replay", os.path.abspath(a.replay)]
    if not a.map:
        raise SystemExit(f"--scene {a.scene} braucht --map")
    if a.scene == "mission":
        return ["-file", a.map]
    return ["-skirmish", a.map, "-skirmishAI", str(a.ai), "-skirmishSeed", str(a.seed)]


def parse_frames(text):
    frames = sorted({int(f) for f in text.split(",") if f.strip()})
    if not frames or frames[0] <= 0:
        raise SystemExit("--frames: positive Client-Frame-Nummern, z. B. 300,900")
    return frames


def path_frames(path, every):
    """Pictures of a camera path: -shotEvery records between the first and the last key point, at the first
    frame of each new multiple of every, named after the multiple."""
    if every <= 0:
        raise SystemExit("--every: positive Anzahl Logik-Frames")
    keys = []
    with open(path, "r", encoding="ascii", errors="replace") as f:
        for line in f:
            fields = line.split("#", 1)[0].split()
            if fields:
                keys.append(int(fields[0]))
    if not keys:
        raise SystemExit(f"--path: keine Stuetzpunkte in {path}")
    first, last = keys[0], keys[-1]
    return [n * every for n in range(max(first // every, 1), last // every + 1)], last


def shot_plan(a):
    """Expected screenshot frames and -quitAfter for this run."""
    if a.path:
        frames, last = path_frames(a.path, a.every)
        return frames, a.quit or last + 30
    frames = parse_frames(a.frames)
    return frames, a.quit or frames[-1] + 30


def read_log(path):
    if not os.path.isfile(path):
        return []
    with open(path, "r", encoding="ascii", errors="replace") as f:
        return [line.strip() for line in f if line.strip()]


def park_cursor():
    """Park the mouse pointer outside the game window: over the window it triggers hover effects in the main
    menu and edge scrolling of the shell-map camera, both end up in the picture and make two runs differ.
    NOT in the lower right corner -- the "show desktop" button is there; it minimised all windows at game
    start, the game waited minimised until the time limit and kept the initial size 800x600 afterwards.
    Not in the upper corners either (close button or system menu of maximised windows). Middle of the left
    screen edge."""
    try:
        import ctypes
        u = ctypes.windll.user32
        u.SetCursorPos(0, u.GetSystemMetrics(1) // 2)
    except (AttributeError, OSError):
        pass


CDB = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "WindowsApps", "cdbX64.exe")


def dump_hung_process(pid, out):
    """If the game hangs until the time limit, save a memory dump and the call stacks of all threads before
    aborting (cdb attached non-invasively, -pv). Result in out: hang.dmp and hang_stacks.txt."""
    if not os.path.isfile(CDB):
        print(f"No cdb at {CDB} -- no memory dump")
        return
    dmp = os.path.join(out, "hang.dmp")
    stacks = os.path.join(out, "hang_stacks.txt")
    cmds = f'.dump /ma /o "{dmp}"; ~*kb 40; qd'
    try:
        r = subprocess.run([CDB, "-pv", "-p", str(pid), "-c", cmds], capture_output=True, text=True,
                           errors="replace", timeout=300)
        with open(stacks, "w", encoding="utf-8") as f:
            f.write(r.stdout)
        print(f"Haenger: Speicherauszug {dmp}, Aufrufstapel {stacks}")
    except (OSError, subprocess.SubprocessError) as e:
        print(f"Speicherauszug fehlgeschlagen: {e}")


def run_game(a, out, fixed, frames, quit_after):
    exe = os.path.abspath(a.exe)
    if not os.path.isfile(exe):
        raise SystemExit(f"exe not found: {exe}")
    os.makedirs(out, exist_ok=True)
    log = os.path.join(out, "run.log")
    for stale in [log, log + ".frames.csv", log + ".ticks.csv"] + \
            [os.path.join(out, n) for n in os.listdir(out) if n.startswith("shot_") and n.endswith(".bmp")]:
        if os.path.isfile(stale):
            os.remove(stale)
    # Reference pictures always use the original textures (SD); "Auto" depends on the resolution, the video
    # memory and the tiers present. --texture-quality overrides this.
    cmd = [exe, "-win", "-xres", str(a.xres), "-yres", str(a.yres), "-nologo", "-noaudio",
           "-textureQuality", a.texture_quality,
           "-gamma", str(a.gamma),
           "-msaa", str(a.msaa),
           "-crcLog", log, "-quitAfter", str(quit_after)] + scene_args(a)
    if fixed:
        cmd += ["-fixedTimeStep", "-shotDir", out]
        if getattr(a, "path", None):
            cmd += ["-cameraPath", os.path.abspath(a.path), "-shotEvery", str(a.every)]
        else:
            for f in frames:
                cmd += ["-shotAfter", str(f)]
    if getattr(a, "multiInstance", False):
        cmd += ["-multiInstance"]
    extra = list(a.extra or [])
    extra_lower = [e.lower() for e in extra]
    # Own user folder and fixed detail level: otherwise level, effect list and further options would come from the
    # user's Settings.ini and the pictures would depend on the machine. The almost empty Settings.ini keeps the first
    # start from taking over the original game's Options.ini.
    if a.level != "user":
        if "-userdata" not in extra_lower:
            user_dir = os.path.join(out, "_user")
            if os.path.isdir(user_dir):
                shutil.rmtree(user_dir)
            os.makedirs(user_dir)
            with open(os.path.join(user_dir, "Settings.ini"), "w", encoding="utf-8", newline="\r\n") as f:
                f.write("[General]\n")
            cmd += ["-userData", user_dir + os.sep, "-settingsReadOnly"]
        if "-staticlod" not in extra_lower:
            cmd += ["-staticLOD", a.level]
    # -d3d11 explicitly, so an old entry "RenderBackend = D3D9" in Options.ini raises no message.
    if not any(e.lower() in ("-d3d9", "-d3d11", "-vulkan") for e in extra):
        cmd += ["-d3d11"]
    cmd += extra
    print("Start:", " ".join(f'"{c}"' if " " in c else c for c in cmd))
    if not getattr(a, "multiInstance", False):
        park_cursor()
    start = time.time()
    proc = subprocess.Popen(cmd, cwd=os.path.dirname(exe))
    try:
        code = proc.wait(timeout=a.timeout)
    except subprocess.TimeoutExpired:
        code = None
        dump_hung_process(proc.pid, out)
        proc.kill()
        proc.wait()
    secs = time.time() - start
    debug_log = os.path.join(os.path.dirname(exe), (f"m{proc.pid}_" if getattr(a, "multiInstance", False) else "")
                             + "DebugLogFileD.txt")
    if os.path.isfile(debug_log) and os.path.getmtime(debug_log) >= start - 1:
        shutil.copy2(debug_log, os.path.join(out, "DebugLogFileD.txt"))
    lines = read_log(log)
    print(f"End after {secs:.1f} s, exit code {code}")
    for line in lines:
        if line.startswith(("SHOT", "FRAMES", "BENCH", "END", "EXIT", "ERROR", "CAMERAPATH", "SHADERFAILURE")):
            print("  " + line)
    return code, lines


def check_run(a, out, frames, code, lines):
    ok = True
    if code is None:
        print(f"ERROR: time limit of {a.timeout} s exceeded")
        ok = False
    elif code != 0:
        # A non-zero exit code after a regular end (END quitAfter, all screenshots present) comes from the
        # leak report or the assert box at shutdown -- a warning, not an abort, otherwise the picture
        # comparison would be skipped although the pictures are complete.
        print(f"WARNING: exit code {code} (shutdown path); the pictures are compared anyway")
    for line in lines:
        # A shader that did not compile drops its effect silently; the game names it in the release build too.
        if line.startswith(("CAMERAPATH ERROR", "SHADERFAILURE")):
            print("ERROR: " + line)
            ok = False
    if not any(line.startswith("END quitAfter") for line in lines):
        print("ERROR: the run did not end through -quitAfter (crash or early end? check DebugLogFileD.txt)")
        ok = False
    for f in frames:
        path = os.path.join(out, f"shot_{f:06d}.bmp")
        if not os.path.isfile(path):
            print(f"ERROR: screenshot missing: {path}")
            ok = False
    return ok


def compare_dirs(ref, new, diff, a):
    args = argparse.Namespace(reference=ref, candidate=new, diff=diff, threshold=a.threshold,
                              min_psnr=a.min_psnr, max_diff_ratio=a.max_diff_ratio,
                              ignore=getattr(a, "ignore", None) or [],
                              json=os.path.join(new, "compare.json"))
    return compare_shots.run_compare(args)


def guard_mutex(a):
    if a.force or getattr(a, "multiInstance", False):
        return
    others = running_game_processes()
    if others:
        raise SystemExit("Abort: already running: " + ", ".join(others) +
                         " (mutex; another start ends at once). --force skips the check.")


def cmd_run(a):
    guard_mutex(a)
    frames, quit_after = shot_plan(a)
    out = os.path.abspath(a.out)
    code, lines = run_game(a, out, True, frames, quit_after)
    if not check_run(a, out, frames, code, lines):
        return 2
    if a.reference:
        return compare_dirs(os.path.abspath(a.reference), out, os.path.join(out, "diff"), a)
    return 0


def cmd_twice(a):
    guard_mutex(a)
    frames, quit_after = shot_plan(a)
    base = os.path.abspath(a.out)
    runs = []
    for name in ("a", "b"):
        out = os.path.join(base, name)
        code, lines = run_game(a, out, True, frames, quit_after)
        if not check_run(a, out, frames, code, lines):
            return 2
        runs.append(out)
    print("Comparison run a against run b:")
    return compare_dirs(runs[0], runs[1], os.path.join(base, "diff"), a)


def cmd_baseline(a):
    guard_mutex(a)
    out = os.path.abspath(a.out)
    code, lines = run_game(a, out, False, [], a.frames_total)
    frames_line = next((line for line in lines if line.startswith("FRAMES")), None)
    if code != 0 or frames_line is None:
        print("ERROR: no FRAMES line (exit code %s)" % code)
        return 2
    print(f"Baseline: {frames_line}")
    print(f"Single values: {os.path.join(out, 'run.log.frames.csv')}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--exe", required=True, help="GZHNeo.exe (with linked assets in the same folder)")
    common.add_argument("--scene", choices=("shellmap", "menu", "skirmish", "replay", "mission"), default="shellmap")
    common.add_argument("--replay", help="replay file for --scene replay")
    common.add_argument("--map", help='skirmish map, e.g. "Alpine Assault"; mission: map path, e.g. "Maps\\md_usa01.map"')
    common.add_argument("--ai", type=int, default=3, help="skirmish: number of AI opponents")
    common.add_argument("--seed", type=int, default=1, help="skirmish: game seed")
    # Brightness slider neutral; under D3D11 it also acts in a window and would otherwise come from the user's
    # Options.ini (reference pictures would then depend on the slider).
    common.add_argument("--gamma", type=int, default=50, help="brightness slider position 0..100 (-gamma), default 50 = neutral")
    common.add_argument("--texture-quality", default="sd", choices=("sd", "hd", "uhd", "auto"),
                        help="texture tier, default sd for reproducible references")
    # The anti-aliasing would otherwise come from the user's Settings.ini (8 samples on one machine, 1 on another),
    # and with multisampling the depth effects took another path.
    common.add_argument("--msaa", type=int, default=1, help="multisampling, samples (-msaa), default 1 = off; "
                        "independent of the user's Settings.ini")
    common.add_argument("--level", default="VeryHigh", choices=("Low", "Medium", "High", "VeryHigh", "user"),
                        help="detail level (-staticLOD) with its own user folder under --out, default VeryHigh; "
                             "user = level and settings from the user's own user folder")
    common.add_argument("--xres", type=int, default=1280)
    common.add_argument("--yres", type=int, default=720)
    common.add_argument("--timeout", type=int, default=600, help="seconds per run")
    common.add_argument("--force", action="store_true", help="skip the check for running game instances")
    common.add_argument("--multiInstance", action="store_true",
                        help="start the game with -multiInstance: several instances side by side, one log per instance")
    common.add_argument("--extra", nargs=argparse.REMAINDER, help="further game parameters (give them last)")

    shots = argparse.ArgumentParser(add_help=False)
    shots.add_argument("--frames", default="300,900", help="client frames for screenshots, comma-separated")
    shots.add_argument("--path", help="camera path (-cameraPath), replaces --frames")
    shots.add_argument("--every", type=int, default=60, help="with --path: one picture every N logic frames")
    shots.add_argument("--quit", type=int, help="quit after N client frames (default: last screenshot + 30)")
    shots.add_argument("--threshold", type=int, default=compare_shots.DEFAULT_THRESHOLD)
    shots.add_argument("--min-psnr", type=float, default=compare_shots.DEFAULT_MIN_PSNR)
    shots.add_argument("--max-diff-ratio", type=float, default=compare_shots.DEFAULT_MAX_DIFF_RATIO)
    shots.add_argument("--ignore", action="append", type=compare_shots.Rect, metavar="X,Y,B,H", default=[],
                       help="exclude an image region from the comparison (pixels or percent), may be given several times. "
                            "Useful for the shell map because the menu fade-in is not deterministic")

    r = sub.add_parser("run", parents=[common, shots], help="record a scene, optionally compare it with a reference")
    r.add_argument("--out", required=True, help="target folder")
    r.add_argument("--reference", help="reference folder with shot_*.bmp")

    t = sub.add_parser("twice", parents=[common, shots], help="record a scene twice and compare")
    t.add_argument("--out", required=True, help="target folder (subfolders a, b, diff)")

    b = sub.add_parser("baseline", parents=[common], help="measure frame times without the fixed time step")
    b.add_argument("--out", default="render-out/baseline", help="target folder")
    b.add_argument("--frames", dest="frames_total", type=int, default=1800,
                   help="run time in client frames (default 1800, about 60 s)")

    a = ap.parse_args([arg for arg in sys.argv[1:] if arg != ""])
    return {"run": cmd_run, "twice": cmd_twice, "baseline": cmd_baseline}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
