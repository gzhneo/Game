#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Kartenskripte statisch gegen die Skriptvorlagen pruefen (Angleichung Phase 5, Scheibe B1, 2026-09-19).

Hintergrund: Generals bekommt die Zero-Hour-Fassung von
ScriptEngine/ScriptActions/ScriptConditions/Scripts. Eine Kartenskriptzeile bricht dann leise, wenn
  (a) ihre Aktions-/Bedingungsnummer in Zero Hour etwas anderes bedeutet, ihre Parameterzahl nach dem
      Heilen alter Dateien (Scripts.cpp) nicht zur ZH-Vorlage passt (-> NO_OP bzw. CONDITION_FALSE) oder
      ihre gespeicherten Parametertypen nicht zur ZH-Vorlage passen, oder
  (b) die Implementierung der Aktion/Bedingung sich zwischen den Baeumen unterscheidet (Rumpf im
      executeAction-/evaluateCondition-Zweig samt aufgerufener Funktionen aus ScriptActions.cpp,
      ScriptConditions.cpp und ScriptEngine.cpp, normalisiert: ohne Kommentare und Leerraum,
      NULL/nullptr, TRUE/true).

Gelesen werden nur Daten (nichts wird geschrieben):
  - alle Karten aus maps.big (RefPack-Entpacker aus tools/render/map_props.py),
  - Data/Scripts/SkirmishScripts.scb und MultiplayerScripts.scb der Generals-Daten,
  - Scripts.h, ScriptEngine.cpp, ScriptActions.cpp, ScriptConditions.cpp beider Baeume (Arbeitskopie oder
    per --gen-ref/--zh-ref aus einem git-Stand, z. B. --gen-ref 4a606ff25 fuer Generals vor B1).

Aufruf:
  python tools/determinism/check_map_scripts.py                      Bericht (a) und (b) fuer die Generals-Daten
  python tools/determinism/check_map_scripts.py --gen-ref 4a606ff25  Generals-Quellen aus git (Stand vor B1)
  python tools/determinism/check_map_scripts.py --usage              zusaetzlich jede benutzte Aktion/Bedingung
  python tools/determinism/check_map_scripts.py --map usa01 --dump   alle Skriptzeilen einer Karte ausgeben
  python tools/determinism/check_map_scripts.py --find TEAM_PARTIAL_USE_COMMANDBUTTON   Fundstellen mit Parametern
  python tools/determinism/check_map_scripts.py --zh-data            dieselbe Pruefung mit den ZH-Daten (MapsZH.big)

Rueckgabe 0; 1 nur, wenn es Faelle (a) gibt.
"""
import argparse
import collections
import fnmatch
import os
import re
import struct
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "render"))
import map_props  # noqa: E402  (RefPack, BIG-Archiv)

sys.path.insert(0, os.path.join(ROOT, "tools"))
import pfade  # noqa: E402
# Muster: ScriptEngine.cpp und ScriptActions.cpp sind zerlegt (TODO 72 a), ihre Teile heissen ScriptEngine*.cpp usw.
SRC = {
    "scripts_h": "GameEngine/GameLogic/Scripts.h",
    "engine": "GameEngine/GameLogic/ScriptEngine/ScriptEngine*.cpp",
    "actions": "GameEngine/GameLogic/ScriptEngine/ScriptActions*.cpp",
    "conditions": "GameEngine/GameLogic/ScriptEngine/ScriptConditions*.cpp",
}
# Kampagnenmissionen wie in tools/determinism/missions.py (Generals 25, Zero Hour 24)
MISSIONS = (["training01"] + ["usa%02d" % i for i in range(1, 9)] + ["gla01", "gla02"]
            + ["gla%02d" % i for i in range(4, 9)] + ["chi%02d" % i for i in range(1, 9)] + ["chifinal"]
            + ["md_%s%02d" % (s, i) for s in ("usa", "gla", "chi") for i in range(1, 6)]
            + ["gc_airgeneral", "gc_chemgeneral", "gc_chinaboss", "gc_demolitiongeneral", "gc_lasergeneral",
               "gc_nukegeneral", "gc_stealth", "gc_superweaponsgeneral", "gc_tankgeneral"])


# --- Quellen -------------------------------------------------------------------------------------

def read_source(tree, rel, ref):
    """Datei(en) als Latin-1-Text (CP1252 bytegenau), aus der Arbeitskopie oder aus git. Ein Muster mit '*'
    liest alle passenden Dateien des Ordners, sortiert und hintereinander."""
    if "*" in rel:
        ordner, muster = rel.rsplit("/", 1)
        path = f"{tree}/Code/{ordner}"
        if not ref and not os.path.isdir(os.path.join(ROOT, path)):
            path = f"Code/{ordner}"
        if ref:
            namen = subprocess.run(["git", "-C", ROOT, "ls-tree", "--name-only", f"{ref}:{path}"],
                                   capture_output=True, check=True, text=True).stdout.split()
        else:
            namen = os.listdir(os.path.join(ROOT, path))
        treffer = sorted(n for n in namen if fnmatch.fnmatch(n, muster))
        return "\n".join(read_source(tree, f"{ordner}/{n}", ref) for n in treffer)
    path = f"{tree}/Code/{rel}"
    if not ref and not os.path.exists(os.path.join(ROOT, path)):
        path = f"Code/{rel}"                               # merged tree: one Code/ for both games
    if ref:
        data = subprocess.run(["git", "-C", ROOT, "show", f"{ref}:{path}"], capture_output=True, check=True).stdout
    else:
        with open(os.path.join(ROOT, path), "rb") as fh:
            data = fh.read()
    return data.decode("latin-1").replace("\r\n", "\n")


def strip_comments(text):
    """Kommentare entfernen, Zeichenketten unangetastet lassen."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1]); i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            out.append(" ")
            i = n if j < 0 else j + 2
        else:
            out.append(c); i += 1
    return "".join(out)


def parse_enum(text, name):
    m = re.search(r"enum\s+" + name + r"\s*\{(.*?)\}", strip_comments(text), re.S)
    names = []
    for part in m.group(1).split(","):
        part = part.strip()
        if not part:
            continue
        ident = re.match(r"(\w+)\s*(?:=\s*(-?\d+))?", part)
        if ident.group(2) is not None:
            assert int(ident.group(2)) == len(names), (name, part)
        names.append(ident.group(1))
    return names


def parse_templates(text):
    """Vorlagen aus ScriptEngine::init: {('A'|'C', NAME): (Parameterzahl, [Typen])}. Spaetere Zuweisungen
    ueberschreiben fruehere wie zur Laufzeit (CAMERA_FADE_MULTIPLY steht in Zero Hour zweimal)."""
    t = strip_comments(text)
    tmpl, cur = {}, None
    ui = {}
    for m in re.finditer(r"&m_actionTemplates\[\s*ScriptAction::(\w+)\s*\][^;]*;\s*"
                         r"(?:curTemplate->m_internalName\s*=\s*\"[^\"]*\"\s*;\s*)?"
                         r"curTemplate->m_(?:uiName|name)\s*=\s*\"([^\"]*)\"", t):
        ui[m.group(1)] = m.group(2)
    parse_templates.ui = ui
    for m in re.finditer(r"&m_(action|condition)Templates\[\s*(?:ScriptAction|Condition)::(\w+)\s*\]"
                         r"|curTemplate->m_numParameters\s*=\s*(\d+)"
                         r"|curTemplate->m_parameters\[(\d+)\]\s*=\s*Parameter::(\w+)", t):
        if m.group(1):
            cur = ("A" if m.group(1) == "action" else "C", m.group(2))
            tmpl.setdefault(cur, [0, {}])
        elif m.group(3):
            tmpl[cur][0] = int(m.group(3))
        else:
            tmpl[cur][1][int(m.group(4))] = m.group(5)
    return {k: (n, [p.get(i, "?") for i in range(n)]) for k, (n, p) in tmpl.items()}


def split_functions(text):
    """Funktionsdefinitionen auf oberster Ebene: {kurzer Name: [normalisierter Rumpf, ...]}."""
    t = strip_comments(text)
    t = "\n".join(l for l in t.split("\n") if not l.lstrip().startswith("#"))
    funcs = collections.defaultdict(list)
    depth, start, sig_start, i, n = 0, 0, 0, 0, len(t)
    while i < n:
        c = t[i]
        if c in "\"'":
            j = i + 1
            while j < n and t[j] != c:
                j += 2 if t[j] == "\\" else 1
            i = j + 1
            continue
        if c == "{":
            if depth == 0:
                start = i
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                sig = t[sig_start:start]
                m = re.search(r"(\w+)\s*\([^;{}]*\)[^;{}()]*$", sig)
                if m and "=" not in sig.split("(")[0]:
                    funcs[m.group(1)].append((sig.strip(), t[start:i + 1]))
                sig_start = i + 1
        elif c == ";" and depth == 0:
            sig_start = i + 1
        i += 1
    return funcs


def norm(code):
    code = re.sub(r"\bNULL\b", "nullptr", code)
    code = re.sub(r"\bTRUE\b", "true", code)
    code = re.sub(r"\bFALSE\b", "false", code)
    return re.sub(r"\s+", "", code)


def switch_cases(funcs, fnames, prefix):
    """Zweige der switch-Anweisungen in den genannten Funktionen: {Name: Code}. Aufeinanderfolgende Marken teilen
    den Code. Aktionen: ScriptEngine::executeActions (Flags, Zaehler, Zeitgeber ...) vor
    ScriptActions::executeAction; Bedingungen: ScriptEngine::evaluateCondition vor ScriptConditions::evaluateCondition."""
    cases = {}
    for fname in fnames:
        for _, body in funcs[fname]:
            parts = re.split(r"case\s+" + prefix + r"::(\w+)\s*:", body)
            pending = []
            for k in range(1, len(parts), 2):
                pending.append(parts[k])
                code = parts[k + 1]
                # die schliessenden Klammern von switch und Funktion hinter dem letzten Zweig nicht mitvergleichen
                while code.rstrip().endswith("}"):
                    code = code.rstrip()[:-1]
                code = code.rstrip() + "\n"
                if code.strip():
                    for nm in pending:
                        cases.setdefault(nm, code)
                    pending = []
    return cases


CALL = re.compile(r"(?:(TheScriptEngine)\s*->\s*|(?<![\w.>:])(?:\w+::)?)(\w+)\s*\(")


# Nicht verfolgt: Sprachwoerter und die Verteiler selbst (CALL_SUBROUTINE ruft ueber executeScript wieder jede
# Bedingung und Aktion auf -- deren Unterschiede stehen bei der jeweiligen Aktion/Bedingung).
STOP = {"if", "for", "while", "switch", "return", "sizeof", "evaluateCondition", "evaluateConditions",
        "executeAction", "executeActions", "executeScript", "executeScripts", "update"}


def closure(code, funcs):
    """Transitiv aufgerufene Funktionen dieser drei Dateien (unqualifiziert, Klasse:: oder TheScriptEngine->)."""
    seen, todo = set(), [code]
    while todo:
        c = todo.pop()
        for m in CALL.finditer(c):
            nm = m.group(2)
            if nm in funcs and nm not in seen and nm not in STOP:
                seen.add(nm)
                todo.extend(b for _, b in funcs[nm])
    return seen


class Tree:
    def __init__(self, name, ref):
        self.name = name
        txt = {k: read_source(name, v, ref) for k, v in SRC.items()}
        self.actions = parse_enum(txt["scripts_h"], "ScriptActionType")
        self.conditions = parse_enum(txt["scripts_h"], "ConditionType")
        self.ptypes = parse_enum(txt["scripts_h"], "ParameterType")
        self.templates = parse_templates(txt["engine"])
        self.ui_names = parse_templates.ui
        self.funcs = collections.defaultdict(list)
        for k in ("engine", "actions", "conditions"):
            for nm, lst in split_functions(txt[k]).items():
                self.funcs[nm].extend(lst)
        self.action_cases = switch_cases(self.funcs, ("executeActions", "executeAction"), "ScriptAction")
        self.condition_cases = switch_cases(self.funcs, ("evaluateCondition",), "Condition")

    def impl(self, kind, name):
        """(Zweig, {Funktion: Rumpf}) normalisiert; None, wenn es keinen Zweig gibt."""
        cases = self.action_cases if kind == "A" else self.condition_cases
        if name not in cases:
            return None
        code = cases[name]
        return norm(code), {f: norm("".join(s + b for s, b in self.funcs[f])) for f in closure(code, self.funcs)}

    def raw(self, kind, name):
        """Zweig und aufgerufene Funktionen als lesbarer Text (fuer --diff)."""
        cases = self.action_cases if kind == "A" else self.condition_cases
        code = cases.get(name, "")
        out = [f"// Zweig {name}", code.strip()]
        for f in sorted(closure(code, self.funcs)):
            for sig, body in self.funcs[f]:
                out += ["", sig, body]
        return "\n".join(l.rstrip() for l in "\n".join(out).split("\n") if l.strip())


# --- Kartendaten ----------------------------------------------------------------------------------

class Reader:
    def __init__(self, data, names):
        self.d, self.p, self.names = data, 0, names

    def i32(self):
        v, = struct.unpack_from("<i", self.d, self.p); self.p += 4; return v

    def f32(self):
        v, = struct.unpack_from("<f", self.d, self.p); self.p += 4; return v

    def u8(self):
        v = self.d[self.p]; self.p += 1; return v

    def astr(self):
        ln, = struct.unpack_from("<H", self.d, self.p); self.p += 2
        s = self.d[self.p:self.p + ln].decode("latin-1"); self.p += ln; return s

    def namekey(self):
        return self.names.get(self.i32() >> 8, "?")

    def dict(self):
        """DataChunkInput::readDict: Anzahl, dann je Paar Schluessel/Typ und Wert."""
        n, = struct.unpack_from("<H", self.d, self.p); self.p += 2
        for _ in range(n):
            typ = self.i32() & 0xFF
            if typ == 0:
                self.u8()
            elif typ in (1, 2):
                self.i32()
            elif typ == 3:
                self.astr()
            elif typ == 4:
                ln, = struct.unpack_from("<H", self.d, self.p); self.p += 2 + 2 * ln
            else:
                raise ValueError(f"unbekannter Woerterbuchtyp {typ}")

    def chunks(self, end):
        while self.p + 10 <= end:
            cid, ver, ln = struct.unpack_from("<IHI", self.d, self.p)
            cend = self.p + 10 + ln
            self.p += 10
            yield self.names.get(cid, str(cid)), ver, cend
            self.p = cend


ScriptLine = collections.namedtuple("ScriptLine", "source script kind ftype ver fnamekey params")


def parse_script_file(data, source):
    """Alle Aktionen/Bedingungen einer Karte oder .scb: Liste von ScriptLine (Typnummer wie gespeichert)."""
    if data[:4] != b"CkMp":
        raise ValueError(f"{source}: kein CkMp-Kopf")
    p = 4
    cnt, = struct.unpack_from("<I", data, p); p += 4
    names = {}
    for _ in range(cnt):
        ln = data[p]; p += 1
        nm = data[p:p + ln].decode("latin-1"); p += ln
        cid, = struct.unpack_from("<I", data, p); p += 4
        names[cid] = nm
    r = Reader(data, names)
    r.p = p
    out = []

    def params(end):
        n = r.i32()
        ps = []
        for _ in range(n):
            t = r.i32()
            if t == COORD3D:
                ps.append((t, (r.f32(), r.f32(), r.f32())))
            else:
                ps.append((t, (r.i32(), r.f32(), r.astr())))
        return ps

    def script(ver, end):
        name = r.astr(); r.astr(); r.astr(); r.astr()
        for _ in range(6):
            r.u8()
        if ver >= 2:
            r.i32()
        for nm, cver, cend in r.chunks(end):
            if nm == "OrCondition":
                for nm2, v2, e2 in r.chunks(cend):
                    if nm2 == "Condition":
                        t = r.i32()
                        key = r.namekey() if v2 >= 4 else None
                        out.append(ScriptLine(source, name, "C", t, v2, key, params(e2)))
                        if r.p != e2:
                            raise ValueError(f"{source}/{name}: Bedingung nicht bis zum Ende gelesen")
            elif nm in ("ScriptAction", "ScriptActionFalse"):
                t = r.i32()
                key = r.namekey() if cver >= 2 else None
                out.append(ScriptLine(source, name, "A" if nm == "ScriptAction" else "F", t, cver, key,
                                      params(cend)))
                if r.p != cend:
                    raise ValueError(f"{source}/{name}: Aktion nicht bis zum Ende gelesen")

    def container(end):
        for nm, ver, cend in r.chunks(end):
            if nm == "Script":
                script(ver, cend)
            elif nm == "ScriptGroup":
                r.astr(); r.u8()
                if ver >= 2:
                    r.u8()
                container(cend)
            elif nm in ("ScriptList", "PlayerScriptsList"):
                container(cend)

    # .scb: PlayerScriptsList oben. Karten: im Chunk SidesList hinter den Seiten (Woerterbuch und Bauliste je
    # Seite) und den Teams (SidesList::ParseSidesDataChunk).
    for nm, ver, end in r.chunks(len(data)):
        if nm == "PlayerScriptsList":
            container(end)
        elif nm == "SidesList":
            for _ in range(r.i32()):
                r.dict()
                for _ in range(r.i32()):
                    r.astr(); r.astr()
                    r.f32(); r.f32(); r.f32(); r.f32()
                    r.u8(); r.i32()
                    if ver >= 3:
                        r.astr(); r.i32(); r.u8(); r.u8(); r.u8()
            if ver >= 2:
                for _ in range(r.i32()):
                    r.dict()
            container(end)
            if r.p != end:
                raise ValueError(f"{source}: SidesList nicht bis zum Ende gelesen")
    return out


COORD3D = None  # wird aus Scripts.h gesetzt

# Hilfsfunktionen, deren Unterschied mit Generals-Daten nicht wirkt (geprueft 2026-09-19):
# ZH loest "ThePlayer"/"teamThePlayer" nur in einer Generalsherausforderung (Kampagne mit m_isChallengeCampaign)
# auf den lokalen Spieler auf; die Generals-Kampagnen sind keine. Ohne --all nicht als Unterschied gemeldet.
NEUTRAL = {"getTeamNamed", "getPlayerFromAsciiString", "getSkirmishEnemyPlayer"}


def unpack(raw):
    """RefPack ('EAR' + NUL) wie map_props, dazu zlib ('ZL1'..'ZL9', Woodcrest Circle)."""
    if raw[:2] == b"ZL" and raw[3:4] == b"\0":
        import zlib
        return zlib.decompress(raw[8:])
    return map_props.unpack_map(raw)


def load_sources(data_dir, zh):
    """(Quelle, Bytes) fuer alle Karten und .scb eines Datensatzes."""
    big = os.path.join(data_dir, "MapsZH.big" if zh else "maps.big")
    f, ents = map_props.big_entries(big)
    for nm, off, size in ents:
        if nm.endswith(".map"):
            f.seek(off)
            yield nm.split("/")[-2], unpack(f.read(size))
    for scb in ("SkirmishScripts.scb", "MultiplayerScripts.scb"):
        path = os.path.join(data_dir, "Data", "Scripts", scb)
        if os.path.exists(path):
            with open(path, "rb") as fh:
                yield scb, unpack(fh.read())


# --- Heilen alter Dateien (Scripts.cpp, Zero-Hour-Fassung) ---------------------------------------

VER2_CONDITIONS = {"TEAM_INSIDE_AREA_PARTIALLY", "TEAM_INSIDE_AREA_ENTIRELY", "TEAM_OUTSIDE_AREA_ENTIRELY",
                   "TEAM_ENTERED_AREA_PARTIALLY", "TEAM_ENTERED_AREA_ENTIRELY", "TEAM_EXITED_AREA_ENTIRELY",
                   "TEAM_EXITED_AREA_PARTIALLY"}


def heal(line, name, pt):
    """Parametertypen nach dem Heilen, wie ScriptAction::ParseAction/Condition::ParseConditionDataChunk (ZH).
    Liefert (Name, [Typname])."""
    types = [pt[t] if 0 <= t < len(pt) else f"?{t}" for t, _ in line.params]
    n = len(types)
    if line.kind == "C":
        if line.ver < 2 and name in VER2_CONDITIONS:
            types = types[:n] + ["SURFACES_ALLOWED"]
            types = types[:3]
        if name == "SKIRMISH_SPECIAL_POWER_READY" and n == 1:
            types = ["SIDE"] + types
        return name, types
    if name == "SKIRMISH_FIRE_SPECIAL_POWER_AT_MOST_COST" and n == 1:
        types = ["SIDE"] + types
    elif name == "TEAM_FOLLOW_WAYPOINTS" and n == 2:
        types = types + ["BOOLEAN"]
    elif name == "SKIRMISH_BUILD_BASE_DEFENSE_FRONT" and n == 1:
        name, types = "SKIRMISH_BUILD_BASE_DEFENSE_FRONT/FLANK", []
    elif name in ("NAMED_SET_ATTITUDE", "TEAM_SET_ATTITUDE") and n >= 2 and types[1] == "INT":
        types[1] = "AI_MOOD"
    elif name in ("MAP_REVEAL_AT_WAYPOINT", "MAP_SHROUD_AT_WAYPOINT") and n == 2:
        types = types + ["SIDE"]
    elif name in ("MAP_REVEAL_ALL", "MAP_REVEAL_ALL_PERM", "MAP_REVEAL_ALL_UNDO_PERM", "MAP_SHROUD_ALL") and n == 0:
        types = ["SIDE"]
    elif name == "SPEECH_PLAY" and n == 1:
        types = types + ["BOOLEAN"]
    elif name in ("CAMERA_MOD_SET_FINAL_ZOOM", "CAMERA_MOD_SET_FINAL_PITCH") and n == 1:
        types = types + ["PERCENT", "PERCENT"]
    elif name in ("MOVE_CAMERA_TO", "MOVE_CAMERA_ALONG_WAYPOINT_PATH", "CAMERA_LOOK_TOWARD_OBJECT") and n == 3:
        types = types + ["REAL", "REAL"]
    elif name in ("RESET_CAMERA", "ZOOM_CAMERA", "PITCH_CAMERA", "ROTATE_CAMERA") and n == 2:
        types = types + ["REAL", "REAL"]
    elif name == "CAMERA_LOOK_TOWARD_WAYPOINT" and n == 2:
        types = types + ["REAL", "REAL", "BOOLEAN"]
    elif name == "CAMERA_LOOK_TOWARD_WAYPOINT" and n == 4:
        types = types + ["BOOLEAN"]
    return name, types


def check(gen, zh, lines):
    """Faelle (a): [(Art, Quelle, Skript, Name, Text)]."""
    global COORD3D
    problems = []
    for ln in lines:
        kind = "C" if ln.kind == "C" else "A"
        genlist = gen.conditions if kind == "C" else gen.actions
        zhlist = zh.conditions if kind == "C" else zh.actions
        gname = genlist[ln.ftype] if 0 <= ln.ftype < len(genlist) else None
        zname = zhlist[ln.ftype] if 0 <= ln.ftype < len(zhlist) else None
        if ln.fnamekey is not None and ln.fnamekey != zname:
            # ZH ordnet ueber den Namen neu zu; Generals-Karten schreiben keinen Namen (Version 1/3)
            zname = ln.fnamekey if ln.fnamekey in zhlist else None
        if gname != zname:
            problems.append(("Nummer", ln, gname, f"Generals {gname}, Zero Hour {zname}"))
            continue
        if zname is None:
            problems.append(("Nummer", ln, gname, "unbekannte Nummer"))
            continue
        hname, htypes = heal(ln, zname, zh.ptypes)
        if hname.endswith("/FLANK"):
            hname = zname
            if not htypes:
                continue
        tn = zh.templates.get((kind, hname))
        if tn is None:
            problems.append(("Vorlage", ln, zname, "keine ZH-Vorlage"))
            continue
        if kind == "A" and zh.ui_names.get(hname, "") in ("", "(placeholder)"):
            # ScriptAction::ParseAction meldet im Debug-Build "Invalid Script Action found"
            problems.append(("Vorlage ohne Namen", ln, zname, "Debug-Meldung beim Laden"))
        if len(htypes) != tn[0]:
            problems.append(("Parameterzahl", ln, zname,
                             f"{len(htypes)} statt {tn[0]} -> {'CONDITION_FALSE' if kind == 'C' else 'NO_OP'}"))
            continue
        # "?": die Vorlage nennt den Typ nicht (CAMERA_BW_MODE_END in beiden Baeumen), dann zaehlt nur die Anzahl
        bad = [(i, a, b) for i, (a, b) in enumerate(zip(htypes, tn[1])) if a != b and b != "?"]
        if bad:
            gt = gen.templates.get((kind, hname), (0, []))[1]
            new = [(i, a, b) for i, a, b in bad if i >= len(gt) or gt[i] != b]
            problems.append(("Parametertyp" if new else "Parametertyp (schon Generals)", ln, zname,
                             ", ".join(f"#{i} {a} statt {b}" for i, a, b in bad)))
    return problems


# --- Bericht --------------------------------------------------------------------------------------

def main():
    global COORD3D
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gen-ref", help="Generals-Quellen aus diesem git-Stand statt der Arbeitskopie")
    ap.add_argument("--zh-ref", help="Zero-Hour-Quellen aus diesem git-Stand statt der Arbeitskopie")
    ap.add_argument("--zh-data", action="store_true", help="ZH-Daten (MapsZH.big, ZH-.scb) statt Generals")
    ap.add_argument("--usage", action="store_true", help="jede benutzte Aktion/Bedingung mit Anzahl")
    ap.add_argument("--map", help="nur diese Quelle (Kartenname oder .scb)")
    ap.add_argument("--dump", action="store_true", help="alle Skriptzeilen ausgeben")
    ap.add_argument("--find", help="Fundstellen einer Aktion/Bedingung (Name) mit Parametern")
    ap.add_argument("--diff", help="Unterschied der Implementierung einer Aktion/Bedingung (Name) als unified diff")
    ap.add_argument("--all", action="store_true", help="auch die als neutral bekannten Hilfsfunktionen (NEUTRAL)")
    a = ap.parse_args()

    gen, zh = Tree("Generals", a.gen_ref), Tree("GeneralsMD", a.zh_ref)
    COORD3D = zh.ptypes.index("COORD3D")
    assert gen.ptypes.index("COORD3D") == COORD3D

    # Nummern der Aufzaehlungen: ZH darf nur anhaengen
    if a.diff:
        import difflib
        kind = "A" if a.diff in gen.actions or a.diff in zh.actions else "C"
        g, z = gen.raw(kind, a.diff).split("\n"), zh.raw(kind, a.diff).split("\n")
        sys.stdout.writelines(l + "\n" for l in difflib.unified_diff(g, z, "Generals", "Zero Hour", lineterm="", n=2))
        return 0

    print("== Aufzaehlungen")
    for label, g, z in (("ScriptActionType", gen.actions, zh.actions), ("ConditionType", gen.conditions, zh.conditions),
                        ("ParameterType", gen.ptypes, zh.ptypes)):
        common = min(len(g), len(z)) - 1  # ohne NUM_ITEMS
        diff = [(i, g[i], z[i]) for i in range(common) if g[i] != z[i]]
        print(f"  {label}: Generals {len(g) - 1}, Zero Hour {len(z) - 1}, abweichende Nummern {len(diff)}"
              + ("" if not diff else ": " + ", ".join(f"{i} {x}/{y}" for i, x, y in diff[:10])))
        print(f"    nur ZH: {', '.join(z[len(g) - 1:-1]) or '-'}")


    data_dir = pfade.assets("zh" if a.zh_data else "generals")
    lines = []
    for src, data in load_sources(data_dir, a.zh_data):
        if a.map and src.lower() != a.map.lower():
            continue
        lines.extend(parse_script_file(data, src))
    ref = zh if a.zh_data else gen
    sources = sorted({ln.source for ln in lines})
    nA = sum(1 for ln in lines if ln.kind == "A")
    nF = sum(1 for ln in lines if ln.kind == "F")
    nC = sum(1 for ln in lines if ln.kind == "C")
    print(f"\n== Daten: {len(sources)} Quellen, {nA} Aktionen, {nF} Sonst-Aktionen, {nC} Bedingungen")

    def lname(ln):
        lst = ref.conditions if ln.kind == "C" else ref.actions
        return lst[ln.ftype] if 0 <= ln.ftype < len(lst) else f"#{ln.ftype}"

    def fmt_params(ln):
        out = []
        for t, v in ln.params:
            tn = ref.ptypes[t] if 0 <= t < len(ref.ptypes) else f"?{t}"
            if tn == "COORD3D":
                out.append(f"{tn}({v[0]:.0f},{v[1]:.0f},{v[2]:.0f})")
            else:
                out.append(f"{tn}({v[2]!r})" if v[2] else f"{tn}({v[0]}|{v[1]:g})")
        return ", ".join(out)

    if a.dump or a.find:
        # --find zeigt jedes Skript, das die Aktion/Bedingung enthaelt, vollstaendig (C Bedingung, A Dann, F Sonst)
        hits = {(ln.source, ln.script) for ln in lines if a.find and lname(ln) == a.find}
        last = None
        for ln in lines:
            if a.find and (ln.source, ln.script) not in hits:
                continue
            if a.find and (ln.source, ln.script) != last:
                print(f"\n  {ln.source} [{ln.script}]")
                last = (ln.source, ln.script)
            mark = "*" if a.find and lname(ln) == a.find else " "
            print(f"  {mark}{ln.source:22} {ln.kind} {lname(ln):45} {fmt_params(ln)}")
        return 0

    # (a)
    problems = check(gen, zh, lines) if not a.zh_data else check(zh, zh, lines)
    print(f"\n== (a) Nummer, Parameterzahl oder -typ passt nicht zur ZH-Vorlage: {len(problems)}")
    agg = collections.Counter((p[0], p[2], p[3]) for p in problems)
    for (art, nm, txt), cnt in sorted(agg.items()):
        where = sorted({p[1].source for p in problems if (p[0], p[2], p[3]) == (art, nm, txt)})
        print(f"  {art:28} {nm:40} {cnt:4}x  {txt}  [{', '.join(where)}]")

    # (b)
    usage = collections.defaultdict(collections.Counter)
    for ln in lines:
        usage[("C" if ln.kind == "C" else "A", lname(ln))][ln.source] += 1
    print("\n== (b) Implementierung unterscheidet sich (Generals gegen Zero Hour), in den Daten benutzt")
    unused = []
    for (kind, nm) in sorted(set((k, n) for k, n in [(("A", x)) for x in gen.actions[:-1]]
                                 + [("C", x) for x in gen.conditions[:-1]])):
        gi, zi = gen.impl(kind, nm), zh.impl(kind, nm)
        if gi == zi:
            continue
        what = []
        if gi is None or zi is None:
            what.append("Zweig fehlt in " + ("Generals" if gi is None else "Zero Hour"))
        else:
            if gi[0] != zi[0]:
                what.append("Zweig")
            for f in sorted(set(gi[1]) | set(zi[1])):
                if gi[1].get(f) != zi[1].get(f) and (a.all or f not in NEUTRAL):
                    what.append(f)
        if not what:
            continue
        use = usage.get((kind, nm))
        if not use:
            unused.append(f"{kind} {nm}")
            continue
        total = sum(use.values())
        missions = sum(v for s, v in use.items() if s in MISSIONS)
        print(f"  {'Aktion' if kind == 'A' else 'Bedingung':9} {nm:45} {total:4}x (Missionen {missions:3})  "
              f"anders: {', '.join(what)}")
        print("      " + ", ".join(f"{s} {v}" for s, v in sorted(use.items())))
    print(f"\n  unbenutzt, aber verschieden: {len(unused)}")
    for u in unused:
        print("    " + u)

    if a.usage:
        print("\n== Benutzung je Aktion/Bedingung")
        for (kind, nm), use in sorted(usage.items(), key=lambda kv: -sum(kv[1].values())):
            print(f"  {kind} {nm:45} {sum(use.values()):5}  in {len(use)} Quellen")
    # Rueckgabe 1 nur fuer neue Faelle (a); "schon Generals" heisst: die Generals-Vorlage passt ebenso wenig
    return 1 if any("schon Generals" not in p[0] for p in problems) else 0


if __name__ == "__main__":
    sys.exit(main())
