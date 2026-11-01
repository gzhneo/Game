#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Baut die Oberfläche von Babylon (Code/Tools/Editors/Babylon/App, Qt 6 Widgets) nativ unter Linux mit dem Qt
# des Systems (Pakete qt6-base-dev, qt6-svg-dev) und führt ihren Selbsttest ohne Bildschirm aus
# (QT_QPA_PLATFORM=offscreen). Die Bilder des Selbsttests (Fenster deutsch und englisch, jeder Dialog)
# liegen danach in <Arbeitsordner>/selftest.
#
#   tools/babylon/gui_linux.sh [Arbeitsordner]
#
# Rückgabewert 0, wenn Bau und Selbsttest bestehen; 2, wenn kein Qt 6 installiert ist.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
WORK="${1:-${TMPDIR:-/tmp}/cnc-babylon-gui}"
mkdir -p "$WORK/obj"

if ! pkg-config --exists Qt6Widgets Qt6Svg 2> /dev/null; then
	echo "Qt 6 (Widgets, Svg) nicht installiert: apt-get install qt6-base-dev qt6-svg-dev"
	exit 2
fi
RCC=$(pkg-config --variable=libexecdir Qt6Core 2> /dev/null)/rcc
[ -x "$RCC" ] || RCC=/usr/lib/qt6/libexec/rcc

B="$ROOT/Code/Tools/Editors/Babylon"
QT=($(pkg-config --cflags Qt6Widgets Qt6Svg))
STRICT=(-std=c++20 -fPIC -Wall -Wextra -Wpedantic -Wshadow -Werror -I "$B" -I "$ROOT/Code/Tools/Editors" -I "$ROOT/Code/Core/EngineCore")

"$RCC" "$B/App/Resources/Babylon.qrc" -name Babylon -o "$WORK/obj/qrc_Babylon.cpp"

objs=()
pids=()
build() {	# Quelle, Objektname, Schalter...
	local src=$1 name=$2; shift 2
	g++ -O1 -g "$@" -c "$src" -o "$WORK/obj/$name.o" 2> "$WORK/obj/$name.log" &
	pids+=($!)
	objs+=("$WORK/obj/$name.o")
}
for f in "$B"/Model/*.cpp "$B"/Core/*.cpp; do
	build "$f" "$(basename "$(dirname "$f")")_$(basename "$f" .cpp)" "${STRICT[@]}" -Wconversion
done
for f in ZipArchive TextFiles; do
	build "$ROOT/Code/Core/EngineCore/Engine/$f.cpp" "engine_$f" "${STRICT[@]}"
done
for f in "$B"/App/*.cpp; do
	build "$f" "app_$(basename "$f" .cpp)" "${STRICT[@]}" "${QT[@]}"
done
# Gemeinsame Teile der Editoren; fremde Warnstufe, wie im Windows-Bau.
for f in "$ROOT"/Code/Tools/Editors/EditorCommon/*.cpp; do
	build "$f" "common_$(basename "$f" .cpp)" -std=c++20 -fPIC -I "$ROOT/Code/Tools/Editors" "${QT[@]}"
done
build "$WORK/obj/qrc_Babylon.cpp" qrc_Babylon -std=c++20 -fPIC "${QT[@]}"

failed=0
for pid in "${pids[@]}"; do
	wait "$pid" || failed=1
done
if [ $failed = 1 ]; then
	grep -h -B2 -A6 -E "error|warning" "$WORK"/obj/*.log | head -80
	exit 1
fi
g++ "${objs[@]}" $(pkg-config --libs Qt6Widgets Qt6Svg) -lz -o "$WORK/Babylon"

QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME="$WORK/config" "$WORK/Babylon" --selftest "$WORK/selftest"
