#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Baut das Kartenmodell und den Kern des neuen Karteneditors (Code/Tools/Editors/MapEditor/Model, Core), die Tests
# (Code/Tests/MapEditorTests) und das Kommandozeilenwerkzeug MapTool unter Linux.
#
#   tools/mapeditor/linux_build.sh [--sanitize | --mingw] [Arbeitsordner]
#
#   (ohne)      nativ mit g++ und strengen Warnungen (-Werror)
#   --sanitize  nativ mit clang, AddressSanitizer und UBSan
#   --mingw     Windows-Programme mit dem MinGW-Cross-Compiler (Windows-Header, -static), zum
#               Ausfuehren unter Wine (MapEditorTests.exe, MapTool.exe)
#
# Die Kompressionsbibliothek des Spiels kommt aus dem Spiegel von tools/syntaxcheck
# (Schreibweisen-Aliase); nativ mit den MSVC-Ersatzdefinitionen des Fuzzing-Laufs.
# Rueckgabewert 0 bei Erfolg.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
MODE=native
case "${1:-}" in
	--sanitize) MODE=sanitize; shift ;;
	--mingw) MODE=mingw; shift ;;
esac
WORK="${1:-${TMPDIR:-/tmp}/cnc-mapeditor-$MODE}"
mkdir -p "$WORK/obj"

export SYNTAXCHECK_DIR="$WORK/sc"
export SYNTAXCHECK_SHARED="$ROOT/Code"
"$ROOT/tools/syntaxcheck/setup.sh" > "$WORK/setup.log" 2>&1
T="$SYNTAXCHECK_DIR/tree"

EXE=""
LINK=()
case $MODE in
	native)   CXX=g++; OPT=(-O1 -g) ;;
	sanitize) CXX=clang++; OPT=(-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer) ;;
	mingw)    CXX=x86_64-w64-mingw32-g++; OPT=(-O1); EXE=.exe; LINK=(-static) ;;
esac
STRICT=(-std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -I "$ROOT/Code/Tools/Editors/MapEditor")
# Kopfdateien des Spiels als Systempfade, damit ihre Warnungen die strenge Stufe nicht stoeren.
GAME_INC=(-isystem "$SYNTAXCHECK_DIR/shim" -isystem "$T/Core/Compression" -isystem "$T/Core"
	-isystem "$T/Core/WWLib")
if [ $MODE != mingw ]; then
	GAME_INC=(-include "$ROOT/tools/fuzz/linux/native_compat.h" -isystem "$ROOT/tools/fuzz/linux/shim" "${GAME_INC[@]}")
fi

objs=()
pids=()
build() {	# Quelle, Objektname, Schalter...
	local src=$1 name=$2; shift 2
	"$CXX" "${OPT[@]}" "$@" -c "$src" -o "$WORK/obj/$name.o" 2> "$WORK/obj/$name.log" &
	pids+=($!)
	objs+=("$WORK/obj/$name.o")
}

# Die Bibliothek des Spiels: fremder Code, ohne eigene Warnstufe.
for f in "$T/Core/Compression/CompressionManager.cpp" "$T"/Core/Compression/EAC/*.cpp; do
	build "$f" "lib_$(basename "$f" .cpp)" -std=c++20 -w "${GAME_INC[@]}"
done
for f in ByteIO Chunk MapIO MapDump MapFile; do
	build "$ROOT/Code/Tools/Editors/MapEditor/Model/$f.cpp" "model_$f" "${STRICT[@]}"
done
for f in BuildListCommands CliffTexture Contours Fairness GameLaunch GraphicsLevels History HeightImage HeightShapes LayerCommands MapCheck MapCompare MapPreview MapSettingsCommands MapSize ObjectCommands ParticleIni WaypointCommands AreaCommands PlacementCommands RoadCommands ScriptCommands ScriptExchange SelectionTools SidesCommands References Slope Symmetry TerrainCommands TextureBlend TextureCommands WaterLook; do
	build "$ROOT/Code/Tools/Editors/MapEditor/Core/$f.cpp" "core_$f" "${STRICT[@]}"
done
# Einzige Datei mit Compression.h (Lib/BaseType.h).
build "$ROOT/Code/Tools/Editors/MapEditor/Model/CompressionCodec.cpp" model_CompressionCodec "${STRICT[@]}" "${GAME_INC[@]}"

failed=0
for pid in "${pids[@]}"; do
	wait "$pid" || failed=1
done
if [ $failed = 1 ]; then
	cat "$WORK"/obj/*.log
	exit 1
fi

"$CXX" "${OPT[@]}" "${STRICT[@]}" "$ROOT/Code/Tests/MapEditorTests/MapEditorTests.cpp" "${objs[@]}" "${LINK[@]}" -lz -o "$WORK/MapEditorTests$EXE"
"$CXX" "${OPT[@]}" "${STRICT[@]}" "$ROOT/Code/Tools/Editors/MapEditor/Cli/MapTool.cpp" "${objs[@]}" "${LINK[@]}" -lz -o "$WORK/MapTool$EXE"
