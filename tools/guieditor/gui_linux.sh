#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Baut das Fensterprogramm des GUI-Editors (Code/Tools/Editors/GuiEditor/App, Qt 6) nativ unter Linux mit dem Qt
# des Systems (qt6-base-dev, qt6-svg-dev) und führt seinen Selbsttest ohne Engine und ohne Bildschirm aus
# (--no-engine, QT_QPA_PLATFORM=offscreen): die Testlayouts in Modell und Baum, Eigenschaften, Sprache und
# Thema, Bearbeiten, Zwischenablage, Speichern, Vergleichen, Schemata. Die Zeichnung durch das Fenstersystem
# des Spiels braucht die MapEditorEngine.dll und bleibt Windows (bzw. Wine) vorbehalten. Modell, Kern und
# Testlayouts kommen aus tools/guieditor/linux_build.sh.
#
#   tools/guieditor/gui_linux.sh [Arbeitsordner]
#
# Bilder und selftest.log unter <Arbeitsordner>/selftest. Rückgabewert 0 bei Erfolg, 2 ohne Qt 6 oder CMake.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
WORK="${1:-${TMPDIR:-/tmp}/cnc-guieditor-gui}"
mkdir -p "$WORK"

if ! pkg-config --exists Qt6Widgets Qt6Svg 2> /dev/null || ! command -v cmake > /dev/null; then
	echo "Qt 6 (Widgets, Svg) oder CMake nicht installiert: apt-get install qt6-base-dev qt6-svg-dev cmake"
	exit 2
fi

"$HERE/linux_build.sh" "$WORK/kern" > "$WORK/kern.log" 2>&1 || { cat "$WORK/kern.log"; exit 1; }

mkdir -p "$WORK/projekt"
cat > "$WORK/projekt/CMakeLists.txt" <<CMAKE
cmake_minimum_required(VERSION 3.25)
project(GuiEditorLinux CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)
set(GE "$ROOT/Code/Tools/Editors/GuiEditor")
file(GLOB APP \${GE}/App/*.cpp \${GE}/App/*.h)
file(GLOB COMMON "$ROOT/Code/Tools/Editors/EditorCommon/*.cpp")
# Modell, Kern und die Leseregeln des Spiels; Tests und WndTool haben ein eigenes main.
file(GLOB KERN "$WORK/kern/obj/model_*.o" "$WORK/kern/obj/core_*.o" "$WORK/kern/obj/engine_*.o")
add_executable(GuiEditor \${APP} \${COMMON} \${GE}/App/Resources/GuiEditor.qrc \${KERN})
target_include_directories(GuiEditor PRIVATE \${GE} "$ROOT/Code/Tools/Editors/MapEditor" "$ROOT/Code/Tools/Editors")
target_link_libraries(GuiEditor PRIVATE Qt6::Widgets Qt6::Svg z)
target_compile_options(GuiEditor PRIVATE -Wall -Wextra -Wshadow -Werror)
CMAKE
cmake -S "$WORK/projekt" -B "$WORK/bau" > "$WORK/cmake.log" 2>&1 || { cat "$WORK/cmake.log"; exit 1; }
cmake --build "$WORK/bau" -j"$(nproc)" > "$WORK/bau.log" 2>&1 || { grep -m20 -E "error|undefined" "$WORK/bau.log"; exit 1; }

layouts=()
while IFS= read -r f; do layouts+=(--layout "$f"); done < <(find "$WORK/kern/wnd" -iname '*.wnd' | sort)
rm -rf "$WORK/selftest"
rc=0
QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME="$WORK/config" "$WORK/bau/GuiEditor" --selftest "$WORK/selftest" --no-engine \
	"${layouts[@]}" > "$WORK/selftest.log" 2>&1 || rc=$?
grep -E "FAIL|images," "$WORK/selftest.log" | tail -8
exit $rc
