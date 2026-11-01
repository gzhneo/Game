#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Befehle aus fremder Hand ausfuehren: Replays mit veraenderten Befehlsargumenten.

Die Befehle eines Replays sind dieselben, die im Netzspiel von Mitspielern kommen; das Spiel fuehrt
sie beim Abspielen aus (GameLogic::logicMessageDispatcher). Die Referenzreplays (KI gegen KI, lokaler
Spieler Beobachter) enthalten nur Kamera- und CRC-Befehle; das Werkzeug fuegt deshalb im Namen der
KI-Spieler Befehle aller Netzwerk-Befehlstypen ein (aus MessageStream.h, meist nach einer Auswahl
von Objektnummern), mit zufaelligen Argumentlisten aus ueblichen und grenzwertigen Werten, und
veraendert daneben vorhandene Befehle. Der Aufbau der Datei bleibt intakt, damit die Wiedergabe die
Befehle erreicht.

  replaycmd.py selftest <rep>...                      lesen und unveraendert schreiben ergibt dieselbe Datei
  replaycmd.py dump <rep>                             Befehle auflisten
  replaycmd.py types                                  Nummern und Namen der Netzwerk-Befehle
  replaycmd.py mutate <rep> <Ordner> [--count N] [--seed S] [--from-frame F]
                                                      N Varianten und manifest.txt (was geaendert ist)
  replaycmd.py fremdversion <rep> <ziel>              Kopie mit veraenderter ini-CRC im Kopf: das Wiedergabe-Menue
                                                      fragt beim Laden nach (Dialog "andere Version")
  replaycmd.py play --exe <GZHNeo.exe|GZHNeoZH.exe> <Ordner> [--timeout S] [--parallel N] [--extra ...]
                                                      (Windows) jede Variante mit -runner abspielen;
                                                      Absturz, Haenger, Exit-Code in bericht.txt

Abweichende CRCs sind bei veraenderten Befehlen erwartet und zaehlen nicht; gesucht sind Abstuerze,
Assertions (Debug-Build ohne -ignoreAsserts zeigt den Dialog: mit --extra -ignoreAsserts laufen
lassen und das Debug-Log lesen) und Haenger."""
import argparse
import concurrent.futures
import os
import random
import re
import threading
import time
import struct
import sys

# Argumenttypen (GameMessageArgumentDataType) und ihre Groesse in der Datei (RecorderClass::readArgument)
INTEGER, REAL, BOOLEAN, OBJECTID, DRAWABLEID, TEAMID, LOCATION, PIXEL, PIXELREGION, TIMESTAMP, WIDECHAR = range(11)
ARG_SIZE = {INTEGER: 4, REAL: 4, BOOLEAN: 1, OBJECTID: 4, DRAWABLEID: 4, TEAMID: 4, LOCATION: 12,
            PIXEL: 8, PIXELREGION: 16, TIMESTAMP: 4, WIDECHAR: 2}
ARG_NAME = ["int", "real", "bool", "objectID", "drawableID", "teamID", "location", "pixel", "pixelRegion",
            "timestamp", "wchar"]
MAX_SLOTS = 8
HERE = os.path.dirname(os.path.abspath(__file__))
MESSAGE_HEADER = os.path.join(HERE, "..", "..", "..", "Code", "GameEngine", "Common", "MessageStream.h")


def network_messages():
    """{Nummer: Name} der Befehle, die ueber das Netz gehen (GameMessage::Type aus MessageStream.h)."""
    text = open(MESSAGE_HEADER, encoding="latin-1").read()
    text = re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S)   # in einem Durchgang: "//***" ist kein Blockanfang
    body = text[text.index("MSG_BEGIN_NETWORK_MESSAGES"):text.index("MSG_BEGIN_DEBUG_NETWORK_MESSAGES")]
    names = re.findall(r"\b(MSG_\w+)\s*(?:=\s*(\d+))?\s*,", body)
    result, value = {}, None
    for name, explicit in names:
        value = int(explicit) if explicit else value + 1
        if name != "MSG_BEGIN_NETWORK_MESSAGES":
            result[value] = name
    return result


class Command:
    def __init__(self, frame, type_, player, groups, args):
        self.frame, self.type, self.player = frame, type_, player
        self.groups = groups        # [(Argumenttyp, Anzahl)]
        self.args = args            # [(Argumenttyp, Bytes)]

    def encode(self):
        out = struct.pack("<IiiB", self.frame, self.type, self.player, len(self.groups))
        for t, n in self.groups:
            out += struct.pack("<BB", t, n)
        for _, raw in self.args:
            out += raw
        return out


def skip_cstring(d, pos, width):
    while pos + width <= len(d):
        if d[pos:pos + width] == b"\0" * width:
            return pos + width
        pos += width
    raise ValueError("Zeichenkette ohne Ende")


def parse(data):
    """Kopf (Bytes), Befehle, Rest (was nicht als Befehl lesbar ist)."""
    if data[:6] != b"GENREP":
        raise ValueError("kein Replay (GENREP fehlt)")
    pos = 6 + 8 + 8 + 4 + 1 + 1 + MAX_SLOTS          # Zeiten (time_t, 64 Bit), Framedauer, Flags
    pos = skip_cstring(data, pos, 2)                 # Name
    pos += 16                                        # SYSTEMTIME
    pos = skip_cstring(data, pos, 2)                 # Version
    pos = skip_cstring(data, pos, 2)                 # Versionszeit
    pos += 12                                        # Versionsnummer, exe-CRC, ini-CRC
    pos = skip_cstring(data, pos, 1)                 # Spieloptionen
    pos = skip_cstring(data, pos, 1)                 # lokaler Slot
    pos += 16                                        # Schwierigkeit, Spielart, Rangpunkte, max. FPS
    header = data[:pos]
    commands = []
    while pos + 13 <= len(data):
        start = pos
        frame, type_, player, ngroups = struct.unpack_from("<IiiB", data, pos)
        pos += 13
        groups, args = [], []
        ok = pos + 2 * ngroups <= len(data)
        if ok:
            for _ in range(ngroups):
                groups.append(struct.unpack_from("<BB", data, pos))
                pos += 2
            for t, n in groups:
                if t not in ARG_SIZE:
                    ok = False
                    break
                for _ in range(n):
                    if pos + ARG_SIZE[t] > len(data):
                        ok = False
                        break
                    args.append((t, data[pos:pos + ARG_SIZE[t]]))
                    pos += ARG_SIZE[t]
        if not ok:
            pos = start
            break
        commands.append(Command(frame, type_, player, groups, args))
    return header, commands, data[pos:]


def build(header, commands, rest):
    return header + b"".join(c.encode() for c in commands) + rest


def interesting(t, rng):
    """Ein Wert, der Grenzen prueft, als Bytes des Argumenttyps."""
    if t in (INTEGER, TIMESTAMP):
        v = rng.choice([0, 1, -1, 2, 7, 8, 16, 255, 256, 1000, 65535, 0x7FFFFFFF, -0x80000000, rng.randrange(-5000, 5000)])
        return struct.pack("<i", v)
    if t == REAL:
        return struct.pack("<f", rng.choice([0.0, -1.0, 1e30, -1e30, float("inf"), float("-inf"), float("nan"), 1e-30]))
    if t == BOOLEAN:
        return bytes([rng.choice([0, 1, 2, 255])])
    if t in (OBJECTID, DRAWABLEID, TEAMID):
        return struct.pack("<I", rng.choice([0, 1, 2, 0xFFFFFFFF, 0x7FFFFFFF, rng.randrange(1, 4000), rng.randrange(1, 100)]))
    if t == LOCATION:
        c = [0.0, -1.0, 1e9, -1e9, float("nan"), float("inf"), rng.uniform(-100, 5000)]
        return struct.pack("<fff", rng.choice(c), rng.choice(c), rng.choice(c))
    if t == PIXEL:
        c = [0, -1, 100000, -100000, 0x7FFFFFFF, -0x80000000]
        return struct.pack("<ii", rng.choice(c), rng.choice(c))
    if t == PIXELREGION:
        c = [0, -1, 100000, -100000, 0x7FFFFFFF, -0x80000000]
        return struct.pack("<iiii", *(rng.choice(c) for _ in range(4)))
    if t == WIDECHAR:
        return struct.pack("<H", rng.choice([0, 0xFFFF, 0xD800, 0x41]))
    raise ValueError(t)


def regroup(args):
    groups = []
    for t, _ in args:
        if groups and groups[-1][0] == t and groups[-1][1] < 255:
            groups[-1] = (t, groups[-1][1] + 1)
        else:
            groups.append((t, 1))
    return groups


def mutate_command(c, rng):
    """Veraendert einen Befehl; liefert die Beschreibung."""
    kind = rng.random()
    if c.args and kind < 0.6:
        i = rng.randrange(len(c.args))
        t = c.args[i][0]
        c.args[i] = (t, interesting(t, rng))
        return f"Argument {i} ({ARG_NAME[t]}) = {c.args[i][1].hex()}"
    if kind < 0.7:
        old = c.player
        c.player = rng.choice([-1, 0, 1, 2, 7, 8, 15, 16, 0x7FFFFFFF])
        return f"Spieler {old} -> {c.player}"
    if kind < 0.8 and c.args:
        n = rng.randrange(len(c.args))
        c.args = c.args[:n]
        c.groups = regroup(c.args)
        return f"Argumente gekuerzt auf {n}"
    if kind < 0.9:
        t = rng.choice(list(ARG_SIZE))
        c.args.append((t, interesting(t, rng)))
        c.groups = regroup(c.args)
        return f"Argument angehaengt ({ARG_NAME[t]})"
    # Argument eines anderen Typs an derselben Stelle (Handler lesen Argumente nach Position)
    if c.args:
        i = rng.randrange(len(c.args))
        t = rng.choice(list(ARG_SIZE))
        c.args[i] = (t, interesting(t, rng))
        c.groups = regroup(c.args)
        return f"Argument {i} als {ARG_NAME[t]}"
    return "unveraendert"


def plausible(t, rng):
    """Ein Wert im ueblichen Bereich: kleine Zahlen, Objektnummern einer laufenden Partie, Orte auf der Karte."""
    if t in (INTEGER, TIMESTAMP):
        return struct.pack("<i", rng.randrange(0, 64))
    if t == REAL:
        return struct.pack("<f", rng.uniform(0, 1000))
    if t == BOOLEAN:
        return bytes([rng.randrange(2)])
    if t in (OBJECTID, DRAWABLEID, TEAMID):
        return struct.pack("<I", rng.randrange(1, 3000))
    if t == LOCATION:
        return struct.pack("<fff", rng.uniform(0, 3000), rng.uniform(0, 3000), rng.uniform(0, 50))
    if t == PIXEL:
        return struct.pack("<ii", rng.randrange(0, 800), rng.randrange(0, 600))
    if t == PIXELREGION:
        return struct.pack("<iiii", 0, 0, rng.randrange(0, 800), rng.randrange(0, 600))
    return struct.pack("<H", rng.randrange(0x20, 0x7F))


def synthetic(frame, type_, player, rng):
    """Ein Befehl mit zufaelliger Argumentliste; die Handler lesen die Argumente nach Position."""
    args = []
    for _ in range(rng.choice([0, 1, 1, 2, 2, 3, 4, 6])):
        t = rng.choice([INTEGER, INTEGER, OBJECTID, OBJECTID, LOCATION, BOOLEAN, REAL, TEAMID, PIXEL,
                        PIXELREGION, TIMESTAMP, DRAWABLEID, WIDECHAR])
        args.append((t, interesting(t, rng) if rng.random() < 0.3 else plausible(t, rng)))
    return Command(frame, type_, player, regroup(args), args)


def inject(cmds, messages, rng, from_frame):
    """Fuegt vor einem vorhandenen Befehl meist eine Auswahl und dann einen Befehl eines KI-Spielers ein."""
    i = rng.choice([k for k, c in enumerate(cmds) if c.frame >= from_frame])
    frame = cmds[i].frame
    player = rng.choice([1, 2, 3, 4, 5, 2, 3]) if rng.random() < 0.9 else rng.choice([-1, 0, 15, 16, 100])
    type_ = rng.choice(list(messages))
    new = []
    if rng.random() < 0.7:
        ids = [(OBJECTID, plausible(OBJECTID, rng)) for _ in range(rng.randrange(1, 12))]
        select = [(BOOLEAN, bytes([1]))] + ids
        select_type = next(v for v, n in messages.items() if n == "MSG_CREATE_SELECTED_GROUP")
        new.append(Command(frame, select_type, player, regroup(select), select))
    new.append(synthetic(frame, type_, player, rng))
    cmds[i:i] = new
    return f"Frame {frame}, Spieler {player}: {messages[type_]} mit {len(new[-1].args)} Argumenten" + \
        (" (nach Auswahl)" if len(new) == 2 else "")


def cmd_selftest(a):
    bad = 0
    for p in a.reps:
        data = open(p, "rb").read()
        header, commands, rest = parse(data)
        same = build(header, commands, rest) == data
        print(f"{'OK  ' if same and not rest else 'FEHL'} {p}: {len(commands)} Befehle, Kopf {len(header)} Byte, "
              f"Rest {len(rest)} Byte")
        bad += not same or bool(rest)
    return 1 if bad else 0


def cmd_dump(a):
    header, commands, rest = parse(open(a.rep, "rb").read())
    for c in commands:
        args = " ".join(f"{ARG_NAME[t]}:{raw.hex()}" for t, raw in c.args)
        print(f"{c.frame:6d} typ {c.type:5d} spieler {c.player:2d}  {args}")
    print(f"{len(commands)} Befehle, Rest {len(rest)} Byte")
    return 0


def cmd_types(a):
    for v, n in sorted(network_messages().items()):
        print(v, n)
    return 0


def cmd_mutate(a):
    rng = random.Random(a.seed)
    data = open(a.rep, "rb").read()
    header, commands, rest = parse(data)
    # CRC-Befehle (Typ MSG_LOGIC_CRC) zaehlen mit; veraendert wird, was nach --from-frame kommt
    candidates = [i for i, c in enumerate(commands) if c.frame >= a.from_frame]
    if not candidates:
        print("keine Befehle ab diesem Frame")
        return 1
    messages = {v: n for v, n in network_messages().items()
                if n not in ("MSG_SET_REPLAY_CAMERA", "MSG_LOGIC_CRC")}
    os.makedirs(a.out, exist_ok=True)
    base = os.path.splitext(os.path.basename(a.rep))[0]
    with open(os.path.join(a.out, "manifest.txt"), "w", encoding="utf-8") as manifest:
        for n in range(a.count):
            cmds = [Command(c.frame, c.type, c.player, list(c.groups), list(c.args)) for c in commands]
            notes = []
            # zuerst die vorhandenen Befehle veraendern (die Indizes gelten noch), dann einfuegen
            for _ in range(rng.randint(0, 2)):
                i = rng.choice(candidates)
                notes.append(f"Befehl {i} (Frame {cmds[i].frame}, Typ {cmds[i].type}): {mutate_command(cmds[i], rng)}")
            for _ in range(rng.randint(1, 8)):
                notes.append(inject(cmds, messages, rng, a.from_frame))
            name = f"{base}-m{n:04d}.rep"
            with open(os.path.join(a.out, name), "wb") as f:
                f.write(build(header, cmds, rest))
            manifest.write(f"{name}\t" + "; ".join(notes) + "\n")
    print(f"{a.count} Varianten in {a.out} (manifest.txt)")
    return 0


def cmd_play(a):
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "determinism"))
    import runner  # run_game: Fenster, Zeitlimit
    notes = {}
    manifest = os.path.join(a.dir, "manifest.txt")
    if os.path.isfile(manifest):
        for line in open(manifest, encoding="utf-8"):
            name, _, text = line.rstrip("\n").partition("\t")
            notes[name] = text
    reps = sorted(n for n in os.listdir(a.dir) if n.lower().endswith(".rep"))
    lock = threading.Lock()
    started = time.time()
    # Exit code 2 is a replay desync: expected, the commands changed the game. Every other non-zero code is a
    # crash or an abort. The runner's crash-folder check cannot tell parallel games apart (a crash would mark
    # every game running at that moment), so with --parallel only the exit code counts.
    expected = (0, 2)

    def one(name, report):
        rep = os.path.abspath(os.path.join(a.dir, name))
        code, secs, crashes = runner.run_game(a.exe, ["-runner", "-replay", rep, "-mehrfach"], a.timeout, a.extra)
        if code is None:
            result = f"HAENGER (Zeitlimit {a.timeout} s)"
        elif code not in expected:
            result = f"ABSTURZ (Exit-Code {code})" + (" " + ", ".join(crashes) if a.parallel <= 1 and crashes else "")
        elif a.parallel <= 1 and crashes:
            result = "ABSTURZ " + ", ".join(crashes)
        else:
            result = "OK" if code == 0 else "Abweichung (erwartet)"
        line = f"{name}\t{result}\t{secs:.0f} s\t{notes.get(name, '')}"
        with lock:
            print(line, flush=True)
            report.write(line + "\n")
            report.flush()
        return result.startswith(("ABSTURZ", "HAENGER"))

    # --parallel N: N games at once, the report lines come in finishing order
    with open(os.path.join(a.dir, "bericht.txt"), "w", encoding="utf-8") as report:
        with concurrent.futures.ThreadPoolExecutor(max(1, a.parallel)) as pool:
            failed = sum(pool.map(lambda n: one(n, report), reps))

    # Assertions of the Debug build (with -ignoreAsserts the game goes on): one log per game, m<pid>_DebugLogFileD.txt,
    # matched to the variant by the replay path in its command line.
    asserts = {}
    game_dir = os.path.dirname(os.path.abspath(a.exe))
    for log in os.listdir(game_dir):
        if not (log.startswith("m") and log.endswith("_DebugLogFileD.txt")):
            continue
        path = os.path.join(game_dir, log)
        if os.path.getmtime(path) < started:
            continue
        with open(path, encoding="latin-1") as f:
            text = f.read()
        m = re.search(r"-replay\s+\S*[\\/]" + re.escape(os.path.basename(os.path.normpath(a.dir))) + r"[\\/]([^\s\\/]+\.rep)", text) or \
            re.search(r"-replay\s+(\S+)", text)
        if not m:
            continue
        variant = os.path.basename(m.group(1))
        lines = [l.strip() for l in text.splitlines() if "ASSERTION FAILURE" in l]
        if lines:
            asserts[variant] = lines
    with open(os.path.join(a.dir, "assertions.txt"), "w", encoding="utf-8") as f:
        for variant, lines in sorted(asserts.items()):
            for l in lines:
                f.write(f"{variant}\t{l}\n")
    kinds = {}
    for lines in asserts.values():
        for l in lines:
            kinds[l] = kinds.get(l, 0) + 1
    for text, n in sorted(kinds.items(), key=lambda kv: -kv[1])[:15]:
        print(f"{n:5d} x {text[:200]}")
    print(f"{len(reps)} Varianten, {failed} mit Absturz oder Haenger, {len(asserts)} mit Assertion (bericht.txt, assertions.txt)")
    return 1 if failed else 0


def crc_offset(data):
    """Lage von Versionsnummer, exe-CRC und ini-CRC im Kopf."""
    if data[:6] != b"GENREP":
        raise ValueError("kein Replay (GENREP fehlt)")
    pos = 6 + 8 + 8 + 4 + 1 + 1 + MAX_SLOTS
    pos = skip_cstring(data, pos, 2)
    pos += 16
    pos = skip_cstring(data, pos, 2)
    pos = skip_cstring(data, pos, 2)
    return pos


def cmd_fremdversion(a):
    data = bytearray(open(a.rep, "rb").read())
    pos = crc_offset(data) + 8
    old = struct.unpack_from("<I", data, pos)[0]
    struct.pack_into("<I", data, pos, old ^ 0x1)
    open(a.ziel, "wb").write(data)
    print(f"{a.ziel}: ini-CRC {old:08X} -> {old ^ 0x1:08X}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("selftest")
    s.add_argument("reps", nargs="+")
    d = sub.add_parser("dump")
    d.add_argument("rep")
    sub.add_parser("types")
    m = sub.add_parser("mutate")
    m.add_argument("rep")
    m.add_argument("out")
    m.add_argument("--count", type=int, default=200)
    m.add_argument("--seed", type=int, default=1)
    m.add_argument("--from-frame", type=int, default=0)
    f = sub.add_parser("fremdversion")
    f.add_argument("rep")
    f.add_argument("ziel")
    p = sub.add_parser("play")
    p.add_argument("--exe", required=True)
    p.add_argument("dir")
    p.add_argument("--timeout", type=int, default=600)
    p.add_argument("--parallel", type=int, default=1, help="games at once")
    p.add_argument("--extra", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    return {"selftest": cmd_selftest, "dump": cmd_dump, "types": cmd_types, "mutate": cmd_mutate,
            "fremdversion": cmd_fremdversion, "play": cmd_play}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
