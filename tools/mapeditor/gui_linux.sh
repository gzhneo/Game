#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Baut das Fensterprogramm des Karteneditors (Code/Tools/Editors/MapEditor/App, Qt 6) nativ unter Linux mit dem Qt
# des Systems (qt6-base-dev, qt6-svg-dev) und führt seinen Selbsttest ohne Bildschirm aus
# (QT_QPA_PLATFORM=offscreen): Start-, Karten- und Einstellungsseite in Hell/Dunkel und Deutsch/Englisch.
# Ohne Engine: unter Linux gibt es keine MapEditorEngine.dll, die 3D-Ansicht bleibt aus; dafür ist kein
# Wine und kein Windows-Qt nötig. Modell und Kern kommen aus tools/mapeditor/linux_build.sh.
#
#   tools/mapeditor/gui_linux.sh [Arbeitsordner]
#
# Bilder unter <Arbeitsordner>/selftest. Rückgabewert 0 bei Erfolg, 2 ohne Qt 6 oder CMake.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
WORK="${1:-${TMPDIR:-/tmp}/cnc-mapeditor-gui}"
mkdir -p "$WORK"

if ! pkg-config --exists Qt6Widgets Qt6Svg 2> /dev/null || ! command -v cmake > /dev/null; then
	echo "Qt 6 (Widgets, Svg) oder CMake nicht installiert: apt-get install qt6-base-dev qt6-svg-dev cmake"
	exit 2
fi

"$HERE/linux_build.sh" "$WORK/kern" > "$WORK/kern.log" 2>&1 || { cat "$WORK/kern.log"; exit 1; }

mkdir -p "$WORK/projekt"
cat > "$WORK/projekt/CMakeLists.txt" <<CMAKE
cmake_minimum_required(VERSION 3.25)
project(MapEditorLinux CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)
set(ME "$ROOT/Code/Tools/Editors/MapEditor")
file(GLOB APP \${ME}/App/*.cpp \${ME}/App/*.h \${ME}/App/Windows/*.cpp \${ME}/App/Windows/*.h \${ME}/App/SelfTests/*.cpp)
file(GLOB COMMON "$ROOT/Code/Tools/Editors/EditorCommon/*.cpp")
file(GLOB KERN "$WORK/kern/obj/*.o")
add_executable(MapEditor \${APP} \${COMMON} \${ME}/App/Resources/MapEditor.qrc \${KERN})
target_include_directories(MapEditor PRIVATE \${ME} "$ROOT/Code/Tools/Editors")
target_link_libraries(MapEditor PRIVATE Qt6::Widgets Qt6::Svg z)
CMAKE
cmake -S "$WORK/projekt" -B "$WORK/bau" > "$WORK/cmake.log" 2>&1 || { cat "$WORK/cmake.log"; exit 1; }
cmake --build "$WORK/bau" -j"$(nproc)" > "$WORK/bau.log" 2>&1 || { grep -m20 -E "error|undefined" "$WORK/bau.log"; exit 1; }

rm -rf "$WORK/selftest"
rc=0
QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME="$WORK/config" "$WORK/bau/MapEditor" --selftest "$WORK/selftest" > "$WORK/selftest.log" 2>&1 || rc=$?
grep -E "FAIL|images," "$WORK/selftest.log" | tail -5
exit $rc
