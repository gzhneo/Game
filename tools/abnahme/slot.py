# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Wait until no game and no WorldBuilder is running (parallel work of several agents and the main session).

Fair queue: every waiter puts a ticket into the queue folder (QUEUE below; name = arrival time and PID). Only the
oldest living ticket may start; tickets of dead processes are removed. Without it whoever happened to look at the
right moment won -- serial scripts that start one game after the other starved the others for hours.

Start lock: some seconds pass between "free" and the actual game start. wait_free() therefore takes a start lock
(spielstart_<time>_<PID>.lock in the queue) before it returns and keeps it for START_LOCK_S seconds; the game has
to be started by then, afterwards tasklist sees it.

The file names in the queue folder (tickets ending in .marke2, spielstart_*.lock, folder spiel_warteschlange,
environment variable CNC_WARTESCHLANGE) are a protocol shared by all sessions on a machine and stay as they are.

Usage: wait_free() right before the game start; from the shell
    python -c "import sys; sys.path.insert(0, 'tools/abnahme'); from slot import wait_free; wait_free()"
With parallel=3 for runs with -multiInstance.
"""
import os, subprocess, time

NAMES = ("gzhneo.exe", "gzhneozh.exe")
# One folder for the whole machine: all worktrees, agents and the main session must see the same queue,
# otherwise they start at the same time. The environment variable CNC_WARTESCHLANGE overrides the place.
QUEUE = os.environ.get("CNC_WARTESCHLANGE") or os.path.join(
    os.environ.get("LOCALAPPDATA") or os.path.expanduser("~"), "CnC_Modernisierung", "spiel_warteschlange")
START_LOCK_S = 90
TICKET_MAX_S = 3600	# older tickets count as orphaned


def _tasklist():
    try:
        return subprocess.run(["tasklist", "/fo", "csv", "/nh"], capture_output=True, text=True,
                              errors="replace", timeout=120).stdout
    except (OSError, subprocess.SubprocessError):
        return ""


def busy(out=None):
    """Running windowed games and WorldBuilder. Runner runs (command line with -runner, replay checks without a
    window) do not count: they bypass the single-instance lock, run from their own exe folder and do not disturb
    picture tests -- parallel replay checks used to stop the whole queue. `out` is only accepted for
    compatibility."""
    ps = ("Get-CimInstance Win32_Process -Filter \"name='GZHNeo.exe' or name='GZHNeoZH.exe' or name like 'WorldBuilder%'\" "
          "| ForEach-Object { $_.Name + '|' + $_.ProcessId + '|' + $_.CommandLine }")
    try:
        r = subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True, text=True,
                           errors="replace", timeout=120)
    except (OSError, subprocess.SubprocessError):
        return ["process query?"]
    if r.returncode != 0:
        return ["process query?"]
    found = []
    for line in r.stdout.splitlines():
        parts = line.split("|", 2)
        if len(parts) < 2:
            continue
        cmd = parts[2].lower() if len(parts) > 2 else ""
        if " -runner" in cmd:
            continue
        found.append(f"{parts[0]} ({parts[1]})")
    return found


def _living_pids(out):
    pids = set()
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if len(parts) > 1 and parts[1].isdigit():
            pids.add(int(parts[1]))
    return pids


def _ticket_parallel(m):
    """Number of simultaneous games the waiter allows (ticket name ..._p<N>.marke2); old tickets without it
    count as exclusive (1)."""
    rest = m[:-len(".marke2")].split("_")
    if len(rest) >= 3 and rest[2].startswith("p") and rest[2][1:].isdigit():
        return int(rest[2][1:])
    return 1


def _first_ticket(mine, out, occupied=None):
    """Name of the oldest living ticket that could start now; dead tickets are removed.
    A waiter that cannot start with the current occupancy anyway (it wants fewer simultaneous games than are
    running -- exclusive, for instance) no longer holds the queue up; it used to hang for over an hour behind an
    exclusive waiter because other agents keep playing in parallel. When the machine is free enough it is first
    again as the oldest."""
    living = _living_pids(out)
    # Only tickets of the current format count: waiters with an older slot.py in memory (tickets ".marke")
    # counted runner replays as busy, never got their turn and held everybody else up.
    tickets = sorted(m for m in os.listdir(QUEUE) if m.endswith(".marke2"))
    for m in tickets:
        try:
            pid = int(m.split("_")[1].split(".")[0])
        except (IndexError, ValueError):
            continue
        if m != mine and living and pid not in living:
            try:
                os.remove(os.path.join(QUEUE, m))
            except OSError:
                pass
            continue
        if m != mine and occupied is not None and occupied >= _ticket_parallel(m):
            continue
        # Orphaned: a waiter whose caller was terminated lives on but never starts (its output pipe is dead) and
        # held the queue up for over an hour. Tickets older than TICKET_MAX_S therefore no longer count as head;
        # they are not deleted (foreign files, sandbox).
        try:
            if m != mine and time.time() - float(m.split("_")[0]) > TICKET_MAX_S:
                continue
        except ValueError:
            pass
        return m
    return mine


def _take_lock():
    """Start lock without deleting foreign files: sandboxed callers must not delete foreign files, so an expired
    shared lock file used to block everybody. Every starter now creates its own file
    spielstart_<time>_<PID>.lock; locked is as long as one of them is younger than START_LOCK_S. Old files are
    only removed on a best-effort basis."""
    now = time.time()
    for f in os.listdir(QUEUE):
        if not f.startswith("spielstart_"):
            continue
        path = os.path.join(QUEUE, f)
        try:
            age = now - os.path.getmtime(path)
        except OSError:
            continue
        if age < START_LOCK_S:
            return False
        try:
            os.remove(path)
        except OSError:
            pass
    try:
        with open(os.path.join(QUEUE, f"spielstart_{now:017.3f}_{os.getpid()}.lock"), "w") as fh:
            fh.write(time.ctime())
    except OSError:
        return False
    return True


def _too_full(b, parallel):
    """Occupied? WorldBuilder always runs alone; otherwise up to `parallel` games may run."""
    if any(x.lower().startswith("worldbuilder") or x.endswith("?") for x in b):
        return bool(b)
    return len(b) >= parallel


def wait_free(log=print, parallel=1):
    """parallel > 1: only for runs with -multiInstance (run_render_test.py --multiInstance) that can run next to
    other instances. Default 1: no other game may run."""
    os.makedirs(QUEUE, exist_ok=True)
    mine = f"{time.time():017.3f}_{os.getpid()}_p{parallel}.marke2"
    open(os.path.join(QUEUE, mine), "w").close()
    n = 0
    try:
        while True:
            out = _tasklist()
            b = busy(out)
            first = _first_ticket(mine, out, len(b)) == mine
            if not _too_full(b, parallel) and first:
                time.sleep(15)
                if not _too_full(busy(), parallel) and _take_lock():
                    return
                continue
            if n % 20 == 0:
                log(f"waiting for {b if b else 'others in the queue'}")
            n += 1
            time.sleep(10)
    finally:
        try:
            os.remove(os.path.join(QUEUE, mine))
        except OSError:
            pass
