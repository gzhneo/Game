#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Syntaxpruefung einer Quelldatei aus Code/ (Pfad relativ zu Code/).
# Gearbeitet wird im Spiegel, das Repository wird nicht angefasst.
HERE="$(cd "$(dirname "$0")" && pwd)"
: "${SYNTAXCHECK_DIR:=${TMPDIR:-/tmp}/cnc-syntaxcheck}"
T="$SYNTAXCHECK_DIR/tree"
cd "$T" || { echo "Spiegel fehlt -- erst setup.sh aufrufen"; exit 2; }
SP=$T/Core
f="$1"; shift
# Wie im Build: GameEngineDevice bindet per /FI ForceOrder.h vorweg ein (windows.h vor
# Lib/BaseType.h, sonst gewinnt winnt.h' BitTest), Main sieht seinen eigenen Ordner.
# Globale Definitionen wie in der Wurzel-CMakeLists.txt; x3daudio.h fehlt in MinGW und kommt aus
# dem Shim der UnitTests (-idirafter: echte MinGW-Header haben Vorrang); compat/mingw_compat.h
# ergaenzt, was MinGW gegenueber dem Windows-10-SDK fehlt.
extra=()
case "$f" in
  Platform/*) extra+=(-include "$T/Platform/ForceOrder.h" -I"$T/App") ;;
  App/*)             extra+=(-I"$T/App" '-DGAME_EDITION_NAME="Syntaxcheck"') ;;
esac
# SYNTAXCHECK_WARNINGS="-W..." ersetzt das pauschale -w (Fehlersuche mit ausgewaehlten Warnungen).
exec clang++ --target=x86_64-w64-mingw32 -fsyntax-only -std=c++20 ${SYNTAXCHECK_WARNINGS:--w} \
  -fms-extensions -fdelayed-template-parsing \
  -Wno-c++11-narrowing -Wno-non-pod-varargs -Wno-register -ferror-limit=200 \
  -DWIN32 -D_WINDOWS -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -D_USE_MATH_DEFINES -D_DEBUG -DTRUE_FALSE_DEFINED -DNOMINMAX -D_cdecl=__cdecl \
  -I"$SYNTAXCHECK_DIR/shim" -idirafter "$HERE/../linuxtests/shim" -I"$SYNTAXCHECK_DIR/shim2/anchor/a/b/c" \
  -include cstddef -include bits/stdc++.h -include fwd_friends.h -include "$HERE/compat/mingw_compat.h" \
  -I$T/Renderer/WW3D2 -I$T/Renderer/WW3D2/Animation -I$T/Renderer/WW3D2/Buffers -I$T/Renderer/WW3D2/Effects -I$T/Renderer/WW3D2/Material -I$T/Renderer/WW3D2/Mesh -I$T/Renderer/WW3D2/Render2D -I$T/Renderer/WW3D2/Scene -I$T/Renderer/WW3D2/System -I$SP/WWLib -I$SP/WWMath -I$SP/WWDebug -I$SP/WWSaveLoad -I$SP/Wwutil -I$SP -I$T/Renderer \
  -I$T/GameEngine -I$T/Core/EngineCore \
  -I$T/Platform -I$T/Core -I$T/Core/Compression -I$T \
  -isystem "$SYNTAXCHECK_DIR/vulkan/Vulkan-Headers/include" -I"$SYNTAXCHECK_DIR/vulkan/volk" -isystem "$SYNTAXCHECK_DIR/vulkan/VulkanMemoryAllocator/include" \
  "${extra[@]}" "$@" "$f"
