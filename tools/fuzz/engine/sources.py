#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Quellen fuer tools/fuzz/engine/run.sh aus den CMakeLists.

  sources.py <Spiegel> engine   EngineCore und GameEngine (Code/Core/EngineCore, Code/GameEngine/CMakeLists.txt)
  sources.py <Spiegel> device   WW3D2 und die W3D-Teile von GameEngineDevice (Zeichenmodule und was
                                sie brauchen; gerendert wird nichts)

Ausgelassen wird, was der Harness selbst ersetzt oder MinGW nicht uebersetzt (AUSGELASSEN)."""
import re, sys

AUSGELASSEN = {
    # Assertions, Log und Absturzdialog: der Harness zaehlt Assertions statt einen Dialog zu zeigen
    "GameEngine/Common/System/Debug.cpp",
    # MinGW-Header kennen SetThreadDescription und EfficiencyClass nicht; der Harness braucht beides nicht
    "Core/EngineCore/Engine/CpuTopology.cpp",
    "Core/EngineCore/Engine/ThreadRole.cpp",
    # braucht die externe Bibliothek DirectXTex
    "Renderer/WW3D2/Material/texutil.cpp",
}


def engine(tree):
    # EngineCore (Code/Core/EngineCore): Quellen relativ zu Code/Core/EngineCore
    text = open(tree + "/Core/EngineCore/CMakeLists.txt", errors="replace").read()
    for m in re.finditer(r"^\s*(Engine/[^\s)]+\.cpp)", text, re.M):
        yield "Core/EngineCore/" + m.group(1)
    text = open(tree + "/GameEngine/CMakeLists.txt", errors="replace").read()
    for m in re.finditer(r"\$\{SHARED_GAMEENGINE_DIR\}/([^\s)]+\.cpp)", text):
        yield "GameEngine/" + m.group(1)


def device(tree):
    text = open(tree + "/Platform/CMakeLists.txt", errors="replace").read()
    for m in re.finditer(r"\$\{SHARED_PLATFORM_DIR\}/(W3DDevice/[^\s)]+\.cpp)", text):
        yield "Platform/" + m.group(1)
    # W3DDisplay legt das Fenster auf den gewaehlten Bildschirm
    yield "Platform/Win32Device/Common/Win32Monitors.cpp"
    text = open(tree + "/Renderer/WW3D2/CMakeLists.txt", errors="replace").read()
    m = re.search(r"add_library\(WW3D2[^\n]*\n(.*?)\n\s*\)", text, re.S)
    for line in m.group(1).split("\n"):
        f = line.strip()
        if f.endswith(".cpp") and not f.startswith("#"):
            yield "Renderer/WW3D2/" + f


def main():
    tree, kind = sys.argv[1], sys.argv[2]
    seen = set()
    for rel in (engine(tree) if kind == "engine" else device(tree)):
        if rel in seen or rel in AUSGELASSEN:
            continue
        seen.add(rel)
        print(tree + "/" + rel)
    return 0


if __name__ == "__main__":
    sys.exit(main())
