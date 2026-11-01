#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Quellliste des Spiels fuer tools/wine/build_game.sh, gelesen aus den CMakeLists.

  quellen.py <Spiegel>          je Zeile "<Gruppe> <Datei>"

Gruppen: wwdebug wwlib wwmath wwsaveload compression (Bibliotheken), ww3d2, engine (Engine und GameEngine),
device (GameEngineDevice), main (Code/App). Die Gruppe bestimmt die zusaetzlichen Schalter
(build_game.sh). Ausgelassen wird, was der Wine-Build ersetzt (AUSGELASSEN)."""
import os, re, sys

WWV = "Core/"
BIBLIOTHEKEN = (
    ("wwdebug", WWV + "WWDebug"),
    ("wwlib", WWV + "WWLib"),
    ("wwmath", WWV + "WWMath"),
    ("wwsaveload", WWV + "WWSaveLoad"),
    ("compression", "Core/Compression"),
)

# FFmpeg und XAudio2 gibt es unter MinGW nicht; GZH_NULL_MEDIA setzt stumme Klassen ein
# (Platform/NullMedia).
AUSGELASSEN = {
    "Platform/VideoDevice/FFmpeg/FFmpegFile.cpp",
    "Platform/VideoDevice/FFmpeg/FFmpegVideoPlayer.cpp",
    "Platform/XAudio2Device/Mp3Parser.cpp",
    "Platform/XAudio2Device/XAudio2AudioManager.cpp",
    # je Spiel mit eigenem Titel uebersetzt (build_game.sh)
    "App/CpuCheck.cpp",
}


def ohne_kommentare(text):
    return "\n".join(z for z in text.split("\n") if not z.strip().startswith("#"))


def lesen(tree, rel):
    with open(os.path.join(tree, rel), errors="replace") as fh:
        return ohne_kommentare(fh.read())


def liste(text):
    """Dateien aus add_library/add_executable und target_sources (PRIVATE)."""
    out = []
    for m in re.finditer(r"(?:add_library|add_executable)\([^\n]*\n(.*?)\n\s*\)", text, re.S):
        out += m.group(1).split("\n")
    for m in re.finditer(r"target_sources\([^)]*?PRIVATE(.*?)\)", text, re.S):
        out += m.group(1).split("\n")
    return [f.strip() for f in out if f.strip()]


def main():
    tree = sys.argv[1]
    gesehen = set()

    def ausgeben(gruppe, rel):
        if rel in gesehen or rel in AUSGELASSEN or not rel.lower().endswith(".cpp"):
            return
        gesehen.add(rel)
        print(gruppe, os.path.join(tree, rel))

    for gruppe, sub in BIBLIOTHEKEN:
        for f in liste(lesen(tree, sub + "/CMakeLists.txt")):
            ausgeben(gruppe, sub + "/" + f)
    for f in liste(lesen(tree, "Renderer/WW3D2/CMakeLists.txt")):
        ausgeben("ww3d2", "Renderer/WW3D2/" + f)
    # EngineCore (Code/Core/EngineCore): Bausteine ohne Engine-Kopf, Pfade relativ zu Code/Core/EngineCore
    for f in liste(lesen(tree, "Core/EngineCore/CMakeLists.txt")):
        ausgeben("engine", "Core/EngineCore/" + f)
    # GameEngine: mehrere Bibliotheken in einer Datei, Pfade mit ${SHARED_GAMEENGINE_DIR}
    text = lesen(tree, "GameEngine/CMakeLists.txt")
    for m in re.finditer(r"\$\{SHARED_GAMEENGINE_DIR\}/([^\"\n)]+?\.cpp)", text):
        ausgeben("engine", "GameEngine/" + m.group(1))
    text = lesen(tree, "Platform/CMakeLists.txt")
    for m in re.finditer(r"\$\{SHARED_PLATFORM_DIR\}/([^\"\n)]+?\.cpp)", text):
        ausgeben("device", "Platform/" + m.group(1))
    for f in liste(lesen(tree, "App/CMakeLists.txt")):
        ausgeben("main", "App/" + f)
    return 0


if __name__ == "__main__":
    sys.exit(main())
