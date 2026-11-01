#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Prueft alle Sub-Objekt-Verweise der Objekt-INIs gegen die Namen in den W3D-Modellen.

Hintergrund A-01: `GLARocketBuggyFullDebris` nennt `ShowSubObject = Turret`, das
Truemmermodell hat aber keinen Turm. Dieses Skript findet alle solchen Faelle auf einmal:
fuer jeden `Draw = W3D*Draw`-Block jeder Objektdefinition werden pro Zustand das Modell und
die `ShowSubObject`/`HideSubObject`-Namen gesammelt und gegen die Mesh-/HLOD-Namen der
W3D-Datei geprueft (Vergleich wie RenderObjClass::Get_Sub_Object_By_Name: voller Name
"CONTAINER.MESH" oder nur der Teil nach dem Punkt, ohne Gross-/Kleinschreibung).

Aufruf:
    python tools/assets/check_subobjects.py [--assets <Asset-Ordner>]   (Vorgabe GZH_ASSETS_GENERALS)

Liest nur aus den .big-Archiven des Asset-Verzeichnisses (INI.big, W3D.big, ...).
"""
import argparse
import os
import re
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import pfade  # noqa: E402
DEFAULT_ASSETS = pfade.assets("generals", pflicht=False)


def big_entries(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] not in (b"BIGF", b"BIG4"):
        raise ValueError(f"{path}: kein BIG-Archiv")
    _total, num, _body = struct.unpack(">III", data[4:16])
    pos = 16
    entries = {}
    for _ in range(num):
        off, size = struct.unpack(">II", data[pos:pos + 8])
        pos += 8
        end = data.index(b"\0", pos)
        entries[data[pos:end].decode("latin-1").lower()] = (off, size)
        pos = end + 1
    return data, entries


class Archives:
    """Alle .big-Archive eines Verzeichnisses; spaetere (alphabetisch) ueberdecken fruehere."""

    def __init__(self, assets_dir):
        self.files = {}  # entry name -> (data, off, size)
        for name in sorted(os.listdir(assets_dir)):
            if name.lower().endswith(".big"):
                data, entries = big_entries(os.path.join(assets_dir, name))
                for k, (off, size) in entries.items():
                    self.files[k] = (data, off, size)

    def read(self, name):
        data, off, size = self.files[name.lower()]
        return data[off:off + size]

    def names(self, prefix):
        prefix = prefix.lower()
        return [k for k in self.files if k.startswith(prefix)]


# --- W3D: Chunk-Walker fuer Mesh- und HLOD-Sub-Objekt-Namen ---------------------------------------
W3D_CHUNK_MESH = 0x00000000
W3D_CHUNK_MESH_HEADER3 = 0x0000001F
W3D_CHUNK_HLOD = 0x00000700
W3D_CHUNK_HLOD_LOD_ARRAY = 0x00000702
W3D_CHUNK_HLOD_SUB_OBJECT = 0x00000704
W3D_CHUNK_HLOD_AGGREGATE_ARRAY = 0x00000705
W3D_CHUNK_HLOD_PROXY_ARRAY = 0x00000706
RECURSE = {W3D_CHUNK_MESH, W3D_CHUNK_HLOD, W3D_CHUNK_HLOD_LOD_ARRAY, W3D_CHUNK_HLOD_AGGREGATE_ARRAY, W3D_CHUNK_HLOD_PROXY_ARRAY}


def cstr(b):
    return b.split(b"\0", 1)[0].decode("latin-1")


def w3d_subobject_names(blob):
    names = set()

    def walk(start, end):
        pos = start
        while pos + 8 <= end:
            cid, size = struct.unpack("<II", blob[pos:pos + 8])
            size &= 0x7FFFFFFF
            body = pos + 8
            if cid == W3D_CHUNK_MESH_HEADER3:
                mesh = cstr(blob[body + 8:body + 24])
                container = cstr(blob[body + 24:body + 40])
                names.add(f"{container}.{mesh}" if container else mesh)
            elif cid == W3D_CHUNK_HLOD_SUB_OBJECT:
                names.add(cstr(blob[body + 4:body + 36]))
            elif cid in RECURSE:
                walk(body, body + size)
            pos = body + size

    walk(0, len(blob))
    return names


def name_matches(wanted, names):
    w = wanted.lower()
    for n in names:
        nl = n.lower()
        if nl == w or nl.rsplit(".", 1)[-1] == w:
            return True
    return False


# --- INI: Objekt -> Draw-Module -> Zustaende -----------------------------------------------------
def parse_objects(text):
    """Liefert [(objektname, [(zustandsname, modell, [subobjekt-namen])...])...]."""
    result = []
    obj = None
    stack = []           # Blocknamen (fuer End-Zuordnung)
    draw_states = None   # Zustaende des aktuellen Draw-Blocks
    default_model = None
    state = None
    for raw in text.splitlines():
        line = raw.split(";", 1)[0].strip()
        if not line:
            continue
        m = re.match(r"^(\w+)(?:\s*=\s*(.*)|\s+(\S.*))?$", line)
        if not m:
            continue
        key, val = m.group(1), (m.group(2) or m.group(3) or "").strip()
        kl = key.lower()
        if kl in ("object", "objectreskin") and not stack:
            obj = (val.split()[0], [])
            result.append(obj)
            stack.append("Object")
        elif kl == "end":
            top = stack.pop() if stack else None
            if top == "State":
                if state is not None:
                    name, model, subs = state
                    if model is None:
                        model = default_model
                    if name == "DefaultConditionState":
                        default_model = model
                    obj[1].append((name, model, subs))
                state = None
            elif top == "Draw":
                draw_states = None
                default_model = None
        elif obj is None:
            continue
        elif kl == "draw":
            stack.append("Draw")
            draw_states = [] if re.match(r"W3D\w*Draw", val) else None
        elif kl in ("behavior", "body", "clientupdate", "clientbehavior"):
            stack.append("Other")          # Modulbloecke mit "= Klasse Tag"
        elif kl in ("defaultconditionstate", "conditionstate", "transitionstate") and draw_states is not None:
            stack.append("State")
            state = ("DefaultConditionState" if kl == "defaultconditionstate" else f"{key} {val}".strip(), None, [])
        elif stack and stack[-1] == "State" and state is not None:
            if kl == "model":
                state = (state[0], val, state[2])
            elif kl in ("showsubobject", "hidesubobject"):
                state[2].extend(n for n in val.split() if n.lower() != "none")  # "None" ist im Spiel ein Leerwert
        elif val == "":
            stack.append("Other")          # Bloecke ohne '=' (ArmorSet, WeaponSet, Prerequisites, ...)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--assets", default=DEFAULT_ASSETS, required=DEFAULT_ASSETS is None)
    args = ap.parse_args()
    arch = Archives(args.assets)

    w3d_index = {}
    for name in arch.names("art\\w3d\\"):
        w3d_index[os.path.basename(name).rsplit(".", 1)[0].lower()] = name

    model_cache = {}

    def model_names(model):
        key = model.lower()
        if key not in model_cache:
            entry = w3d_index.get(key)
            model_cache[key] = w3d_subobject_names(arch.read(entry)) if entry else None
        return model_cache[key]

    problems = 0
    missing_models = set()
    checked = 0
    for ini in sorted(arch.names("data\\ini\\object\\")):
        if not ini.endswith(".ini"):
            continue
        text = arch.read(ini).decode("latin-1")
        for obj_name, states in parse_objects(text):
            for state_name, model, subs in states:
                if not subs or not model or model.upper() == "NONE":
                    continue
                names = model_names(model)
                if names is None:
                    missing_models.add(model)
                    continue
                for s in subs:
                    checked += 1
                    if not name_matches(s, names):
                        problems += 1
                        print(f"{os.path.basename(ini)}: {obj_name} [{state_name}] Modell {model}: Sub-Objekt '{s}' fehlt "
                              f"(vorhanden: {', '.join(sorted(names))})")
    print(f"\n{checked} Sub-Objekt-Verweise geprueft, {problems} ohne Treffer im Modell.")
    if missing_models:
        print(f"{len(missing_models)} referenzierte Modelle nicht in Art\\W3D gefunden (nicht geprueft): "
              + ", ".join(sorted(missing_models)[:20]) + (" ..." if len(missing_models) > 20 else ""))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
