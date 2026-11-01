#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Baut das Layout-Modell und den Kern des GUI-Editors (Code/Tools/Editors/GuiEditor/Model, Core), die Tests (Code/Tests/GuiEditorTests)
# und das Kommandozeilenwerkzeug WndTool unter Linux, und legt die Testlayouts von tools/wine/testdaten
# (gen_wnd.py) nach <Arbeitsordner>/wnd.
#
#   tools/guieditor/linux_build.sh [--sanitize | --mingw] [Arbeitsordner]
#
#   (ohne)      nativ mit g++ und strengen Warnungen (-Werror)
#   --sanitize  nativ mit clang, AddressSanitizer und UBSan
#   --mingw     Windows-Programme mit dem MinGW-Cross-Compiler, zum Ausfuehren unter Wine
#
# Danach:  <Arbeitsordner>/GuiEditorTests <Arbeitsordner>/wnd
# Rueckgabewert 0 bei Erfolg.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
MODE=native
case "${1:-}" in
	--sanitize) MODE=sanitize; shift ;;
	--mingw) MODE=mingw; shift ;;
esac
WORK="${1:-${TMPDIR:-/tmp}/cnc-guieditor-$MODE}"
mkdir -p "$WORK/obj"

EXE=""
LINK=()
case $MODE in
	native)   CXX=g++; OPT=(-O1 -g) ;;
	sanitize) CXX=clang++; OPT=(-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer) ;;
	mingw)    CXX=x86_64-w64-mingw32-g++; OPT=(-O1); EXE=.exe; LINK=(-static) ;;
esac
STRICT=(-std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -I "$ROOT/Code/Tools/Editors/GuiEditor"
	-I "$ROOT/Code/GameEngine" -I "$ROOT/Code/Core/EngineCore")

objs=()
pids=()
build() {	# Quelle, Objektname, Schalter...
	local src=$1 name=$2; shift 2
	"$CXX" "${OPT[@]}" "$@" -c "$src" -o "$WORK/obj/$name.o" 2> "$WORK/obj/$name.log" &
	pids+=($!)
	objs+=("$WORK/obj/$name.o")
}
for f in WndLayout WndCheck; do
	build "$ROOT/Code/Tools/Editors/GuiEditor/Model/$f.cpp" "model_$f" "${STRICT[@]}"
done
for f in WndEdit WndArrange WndScheme WndDiff Document; do
	build "$ROOT/Code/Tools/Editors/GuiEditor/Core/$f.cpp" "core_$f" "${STRICT[@]}"
done
# Die Leseregeln des Spiels, die ohne Engine auskommen.
build "$ROOT/Code/Core/EngineCore/Engine/WndFields.cpp" "engine_WndFields" -std=c++20 -w -I "$ROOT/Code/GameEngine" -I "$ROOT/Code/Core/EngineCore"
build "$ROOT/Code/Tests/GuiEditorTests/GuiEditorTests.cpp" "tests" "${STRICT[@]}"
build "$ROOT/Code/Tools/Editors/GuiEditor/Tool/WndTool.cpp" "tool" "${STRICT[@]}"
fail=0
for p in "${pids[@]}"; do wait "$p" || fail=1; done
if [ $fail != 0 ]; then
	cat "$WORK"/obj/*.log
	exit 1
fi
lib=("$WORK/obj/model_WndLayout.o" "$WORK/obj/model_WndCheck.o" "$WORK/obj/core_WndEdit.o" "$WORK/obj/core_WndScheme.o"
	"$WORK/obj/core_WndDiff.o" "$WORK/obj/core_WndArrange.o" "$WORK/obj/core_Document.o"
	"$WORK/obj/engine_WndFields.o")
"$CXX" "${OPT[@]}" -o "$WORK/GuiEditorTests$EXE" "$WORK/obj/tests.o" "${lib[@]}" "${LINK[@]}"
"$CXX" "${OPT[@]}" -o "$WORK/WndTool$EXE" "$WORK/obj/tool.o" "${lib[@]}" "${LINK[@]}"

# Testlayouts, wie sie das Spiel unter Wine liest.
rm -rf "$WORK/wnd" && mkdir -p "$WORK/wnd"
python3 - "$ROOT/tools/wine/testdaten" "$WORK/wnd" <<'PY'
import os, sys
sys.path.insert(0, sys.argv[1])
import gen_wnd
for rel, text in gen_wnd.window_files().items():
    path = os.path.join(sys.argv[2], rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(text.replace('\r\n', '\n').replace('\n', '\r\n').encode('latin-1'))
PY
echo "Fertig: $WORK/GuiEditorTests$EXE, $WORK/WndTool$EXE, Testlayouts in $WORK/wnd"
