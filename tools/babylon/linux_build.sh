#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Baut Modell und Kern des neuen Babylon (Code/Tools/Editors/Babylon/Model, Core), die Tests
# (Code/Tests/BabylonTests) und das Kommandozeilenwerkzeug BabylonTool unter Linux.
#
#   tools/babylon/linux_build.sh [--sanitize | --mingw] [Arbeitsordner]
#
#   (ohne)      nativ mit g++ und strengen Warnungen (-Werror)
#   --sanitize  nativ mit clang, AddressSanitizer und UBSan
#   --mingw     Windows-Programme mit dem MinGW-Cross-Compiler (-static), zum Ausfuehren unter Wine
#               (BabylonTests.exe, BabylonTool.exe)
#
# Aus der Engine kommen nur ZipArchive.cpp (ZIP lesen) und TextFiles.cpp (der .csf-/.str-Leser des
# Spiels, gegen den die Tests pruefen). Rueckgabewert 0 bei Erfolg.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
MODE=native
case "${1:-}" in
	--sanitize) MODE=sanitize; shift ;;
	--mingw) MODE=mingw; shift ;;
esac
WORK="${1:-${TMPDIR:-/tmp}/cnc-babylon-$MODE}"
mkdir -p "$WORK/obj"

EXE=""
LINK=(-lz)
case $MODE in
	native)   CXX=g++; OPT=(-O1 -g) ;;
	sanitize) CXX=clang++; OPT=(-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer) ;;
	mingw)    CXX=x86_64-w64-mingw32-g++-posix; command -v "$CXX" > /dev/null || CXX=x86_64-w64-mingw32-g++
	          OPT=(-O1); EXE=.exe; LINK=(-static -lz) ;;
esac
B="$ROOT/Code/Tools/Editors/Babylon"
STRICT=(-std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -I "$B" -I "$ROOT/Code/Core/EngineCore")

objs=()
pids=()
build() {	# Quelle, Objektname, Schalter...
	local src=$1 name=$2; shift 2
	"$CXX" "${OPT[@]}" "$@" -c "$src" -o "$WORK/obj/$name.o" 2> "$WORK/obj/$name.log" &
	pids+=($!)
	objs+=("$WORK/obj/$name.o")
}

for f in "$B"/Model/*.cpp; do
	build "$f" "model_$(basename "$f" .cpp)" "${STRICT[@]}"
done
for f in "$B"/Core/*.cpp; do
	build "$f" "core_$(basename "$f" .cpp)" "${STRICT[@]}"
done
for f in ZipArchive TextFiles; do
	build "$ROOT/Code/Core/EngineCore/Engine/$f.cpp" "engine_$f" "${STRICT[@]}"
done

failed=0
for pid in "${pids[@]}"; do
	wait "$pid" || failed=1
done
if [ $failed = 1 ]; then
	cat "$WORK"/obj/*.log
	exit 1
fi

"$CXX" "${OPT[@]}" "${STRICT[@]}" "$ROOT/Code/Tests/BabylonTests/BabylonTests.cpp" "${objs[@]}" "${LINK[@]}" -o "$WORK/BabylonTests$EXE"
"$CXX" "${OPT[@]}" "${STRICT[@]}" "$B/Cli/BabylonTool.cpp" "${objs[@]}" "${LINK[@]}" -o "$WORK/BabylonTool$EXE"
