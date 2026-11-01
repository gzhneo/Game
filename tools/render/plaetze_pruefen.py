#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Prueft die Platztabelle der D3D11-Shader (Pruefgang, tools/gate.sh).

  - Code/Renderer/WW3D2/Common/renderslots.h (RENDER_SLOT_<Name> = <Zahl>) und
    Data/Shaders/sm4/slots.hlsli (#define SLOT_<Name> <b|t|s><Zahl>) nennen dieselben Plaetze mit denselben
    Nummern; CB_ gehoert zu b, T_ zu t, S_ zu s.
  - Kein Shader nennt einen Konstantenpuffer-Platz oder einen Textur-/Sampler-Platz ab 8 als Zahl.
  - Der Renderkern (rendercore*.cpp, auch in Unterordnern) bindet Konstantenpuffer nur ueber die Namen und Texturen/Sampler ab
    Platz 8 ebenso.
  - Jedes Layout eines Zeichenteils in Code/Renderer/WW3D2/Common/renderpieces.h (struct Render<Name> aus Vector4-Feldern)
    hat in den Shadern einen gleichnamigen cbuffer auf einem Teil-Platz (SLOT_CB_PIECE_VS/_PS) mit denselben Feldern (float4, gleiche Namen,
    Anzahl und Reihenfolge), und umgekehrt.
Rueckgabewert 0, wenn alles passt; sonst 1 mit den Fundstellen.
"""
import glob
import os
import re
import sys

WURZEL = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CPP = os.path.join(WURZEL, "Code", "Renderer", "WW3D2", "Common", "renderslots.h")
HLSL = os.path.join(WURZEL, "Data", "Shaders", "sm4", "slots.hlsli")
SHADER = os.path.join(WURZEL, "Data", "Shaders", "sm4")
BACKEND = os.path.join(WURZEL, "Code", "Renderer", "WW3D2")
TEILE = os.path.join(WURZEL, "Code", "Renderer", "WW3D2", "Common", "renderpieces.h")


def lesen(pfad):
    with open(pfad, encoding="latin-1") as f:
        return f.read()


def felder(rumpf, typ):
    """Feldliste (Name, Anzahl) eines struct- bzw. cbuffer-Rumpfs ohne Kommentare."""
    rumpf = re.sub(r"//[^\n]*", "", rumpf)
    liste = []
    for m in re.finditer(r"\b(\w+)\s+(\w+)\s*(?:\[\s*(\d+)\s*\])?\s*;", rumpf):
        if m.group(1) != typ:
            return None
        liste.append((m.group(2), int(m.group(3) or 1)))
    return liste


def layouts_pruefen(fehler):
    """renderpieces.h gegen die cbuffer gleichen Namens in den Shadern; Rueckgabe: Zahl der Layouts."""
    cpp = {}
    for m in re.finditer(r"struct\s+(Render\w+)\s*\{(.*?)\};", lesen(TEILE), re.S):
        f = felder(m.group(2), "Vector4")
        if f is None:
            fehler.append("renderpieces.h: %s hat ein Feld, das kein Vector4 ist" % m.group(1))
        else:
            cpp[m.group(1)] = f
    shader = {}
    for pfad in sorted(glob.glob(os.path.join(SHADER, "*.hlsl*"))):
        for m in re.finditer(r"cbuffer\s+(Render\w+)\s*:\s*register\(\s*(\w+)\s*\)\s*\{(.*?)\};", lesen(pfad), re.S):
            name, platz = m.group(1), m.group(2)
            ort = os.path.relpath(pfad, WURZEL)
            if platz not in ("SLOT_CB_PIECE_VS", "SLOT_CB_PIECE_PS"):
                fehler.append("%s: cbuffer %s liegt auf %s statt auf einem Teil-Platz" % (ort, name, platz))
            f = felder(m.group(3), "float4")
            if f is None:
                fehler.append("%s: cbuffer %s hat ein Feld, das kein float4 ist" % (ort, name))
            elif name not in cpp:
                fehler.append("%s: cbuffer %s fehlt in renderpieces.h" % (ort, name))
            elif f != cpp[name]:
                fehler.append("%s: cbuffer %s weicht von renderpieces.h ab: %s gegen %s" % (ort, name, f, cpp[name]))
            shader[name] = ort
    for name in sorted(set(cpp) - set(shader)):
        fehler.append("renderpieces.h: %s hat keinen cbuffer in den Shadern" % name)
    return len(cpp)


def main():
    fehler = []
    cpp = {m.group(1): int(m.group(2)) for m in re.finditer(r"RENDER_SLOT_(\w+)\s*=\s*(\d+)", lesen(CPP))}
    hlsl = {m.group(1): (m.group(2), int(m.group(3)))
            for m in re.finditer(r"#define\s+SLOT_(\w+)\s+([bts])(\d+)", lesen(HLSL))}
    for name in sorted(set(cpp) | set(hlsl)):
        if name not in cpp:
            fehler.append("slots.hlsli: SLOT_%s fehlt in renderslots.h" % name)
            continue
        if name not in hlsl:
            fehler.append("renderslots.h: RENDER_SLOT_%s fehlt in slots.hlsli" % name)
            continue
        art = {"CB": "b", "T": "t", "S": "s"}.get(name.split("_")[0])
        if art != hlsl[name][0]:
            fehler.append("SLOT_%s: Art %s passt nicht zum Namen" % (name, hlsl[name][0]))
        if cpp[name] != hlsl[name][1]:
            fehler.append("SLOT_%s: renderslots.h %d, slots.hlsli %d" % (name, cpp[name], hlsl[name][1]))

    nackt = re.compile(r"register\((b\d+|[ts](?:[89]|1\d))\)")
    for pfad in sorted(glob.glob(os.path.join(SHADER, "*.hlsl*"))):
        for nr, zeile in enumerate(lesen(pfad).splitlines(), 1):
            if nackt.search(zeile.split("//")[0]):
                fehler.append("%s:%d: Platz als Zahl: %s" % (os.path.relpath(pfad, WURZEL), nr, zeile.strip()))

    bindung = re.compile(r"(?:VS|PS|HS|DS|GS|CS)Set(?:ConstantBuffers1?\(\s*\d|(?:ShaderResources|Samplers)\(\s*(?:[89]|1\d)\b)")
    for pfad in sorted(glob.glob(os.path.join(BACKEND, "**", "rendercore*.cpp"), recursive=True)):
        for nr, zeile in enumerate(lesen(pfad).splitlines(), 1):
            if bindung.search(zeile.split("//")[0]):
                fehler.append("%s:%d: Platz als Zahl: %s" % (os.path.relpath(pfad, WURZEL), nr, zeile.strip()))

    teile = layouts_pruefen(fehler)

    for f in fehler:
        print(f)
    print("%d Plaetze in beiden Tabellen gleich, %d Teil-Layouts, %d Fundstellen" % (len(set(cpp) & set(hlsl)), teile, len(fehler)))
    return 1 if fehler else 0


if __name__ == "__main__":
    sys.exit(main())
