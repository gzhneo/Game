#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# UnitTests unter Linux laufen lassen.
#
# Baut Code/Tests/UnitTests mit dem MinGW-Cross-Compiler gegen die echten
# Windows-Header und fuehrt die entstandene .exe unter Wine aus. Damit sind die Tests der
# angeglichenen Bibliotheken (WWMath, WWLib, Compression) auch ohne Windows-Rechner
# nachvollziehbar -- anders als tools/syntaxcheck wird hier wirklich gelinkt und ausgefuehrt.
#
# Aufruf:  tools/linuxtests/run.sh   (seit Phase 8 Schritt 6/7 ohne Baum-Argument: ein Test, ein Code/)
# Voraussetzungen: g++-mingw-w64-x86-64, libz-mingw-w64-dev, wine64, clang (fuer den Spiegel)
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${LINUXTESTS_DIR:-${TMPDIR:-/tmp}/cnc-linuxtests}"
CXX=x86_64-w64-mingw32-g++

command -v $CXX >/dev/null || { echo "$CXX fehlt (apt-get install g++-mingw-w64-x86-64)"; exit 2; }
command -v wine64 >/dev/null || [ -x /usr/lib/wine/wine64 ] || { echo "wine64 fehlt (apt-get install wine64)"; exit 2; }
[ -f /usr/x86_64-w64-mingw32/include/zlib.h ] || { echo "zlib fuer MinGW fehlt (apt-get install libz-mingw-w64-dev)"; exit 2; }
WINE=$(command -v wine64 || echo /usr/lib/wine/wine64)

mkdir -p "$OUT/obj"

# 1. Spiegel mit den Gross-/Kleinschreib-Aliasen (dasselbe Werkzeug wie die Syntaxpruefung).
# Seit Phase 8 Schritt 7 liegt der gesamte Code unter Code/; der Spiegel kommt nur noch von dort.
export SYNTAXCHECK_DIR="$OUT/mirror"
export SYNTAXCHECK_SHARED="$ROOT/Code"
"$ROOT/tools/syntaxcheck/setup.sh" >/dev/null || exit 1
T="$OUT/mirror/tree"

# 2. Systemheader, die der Quelltext anders schreibt als MinGW sie ablegt (EXCPT.H, TCHAR.H)
python3 "$HERE/sysalias.py" "$ROOT/Code" "$OUT/sysalias" || exit 1

INC="-I $HERE/shim -I $OUT/sysalias -I $OUT/mirror/shim -I $OUT/mirror/shim2/anchor/a/b/c
     -I $T/GameEngine -I $T/Core/EngineCore -I $T/Platform
     -I $T/Core -I $T/Renderer -I $T/Core/WWLib
     -I $T/Core/WWMath -I $T/Core/WWDebug
     -I $T/Core/WWSaveLoad -I $T/Core -I $T/Core/Compression"
# -include bits/stdc++.h: Lib/BaseType.h definiert min/max als Makro; die STL muss vorher da sein.
# -D_cdecl: MSVC kennt die Schreibweise ohne doppelten Unterstrich, MinGW nicht.
# WWDownload (EA-FTP-Patchdownload) ist seit 2026-09-26 entfernt (TODO 69 Runde A).
FLAGS="-std=c++20 -w -DWIN32 -D_WINDOWS -DNOMINMAX -D_WIN32_WINNT=0x0A00 -D_cdecl=__cdecl -include bits/stdc++.h"

# 3. Bibliotheken uebersetzen. Die Quelllisten kommen aus den CMakeLists, damit hier keine
#    zweite Wahrheit entsteht. Was MinGW nicht uebersetzt (alte MSVC-Konstrukte), bleibt
#    draussen und wird am Ende genannt; fehlt davon etwas beim Linken, faellt es dort auf.
python3 "$HERE/sources.py" "$T" > "$OUT/sources.txt" || exit 1
: > "$OUT/skipped.txt"
n_ok=0
for f in $(cat "$OUT/sources.txt"); do
	o="$OUT/obj/$(echo "$f" | md5sum | cut -c1-12)-$(basename "$f" | tr '[:upper:]' '[:lower:]').o"
	if $CXX $FLAGS $INC -c "$f" -o "$o" 2>/dev/null; then
		n_ok=$((n_ok + 1))
	else
		echo "$f" >> "$OUT/skipped.txt"
	fi
done
rm -f "$OUT/libengine.a"
x86_64-w64-mingw32-ar rcs "$OUT/libengine.a" "$OUT"/obj/*.o || exit 1
echo "Bibliotheken: $n_ok uebersetzt, $(wc -l < "$OUT/skipped.txt") ausgelassen (MinGW-fremde MSVC-Konstrukte)"

# 4. Testziel selbst -- Quellen wie in Code/Tests/UnitTests/CMakeLists.txt
# Die Testquellen liegen nur einmal im Repo (ein Test, zwei Baeume); seit Phase 7 Teil 5 der
# Angleichung unter Code/Tests/UnitTests, von beiden Baeumen mit derselben Liste eingebunden.
TESTSRC="$ROOT/Code/Tests/UnitTests/UnitTests.cpp
         $ROOT/Code/Tests/UnitTests/UnitTestStubs.cpp
         $T/GameEngine/Common/Bezier/BezierSegment.cpp
         $T/GameEngine/Common/Bezier/BezFwdIterator.cpp
         $T/GameEngine/Common/System/AsciiString.cpp
         $T/GameEngine/Common/Replay/crc.cpp
         $T/Platform/XAudio2Device/WavParser.cpp
         $HERE/poolstubs.cpp"
# -static: sonst sucht die .exe libstdc++-6.dll/libgcc_s_seh-1.dll/libwinpthread-1.dll und
# Wine bricht mit 53 ab, bevor ein Test laeuft.
$CXX $FLAGS $INC -o "$OUT/UnitTests.exe" $TESTSRC "$OUT/libengine.a" -lz -lwinmm \
	-static -static-libgcc -static-libstdc++ || {
	echo "Linken fehlgeschlagen -- siehe $OUT/skipped.txt"; exit 1; }

# 4b. RenderUnitTests: Format-, Zustands- und FVF-Umsetzung des Render-Backends (kein Geraet, kein WW3D2),
#     Geländekacheln aus TGA und DDS. DDS und BC1/BC7 brauchen DirectXTex fuer MinGW; das baut
#     tools/wine/build_game.sh --nur-deps einmal in die Wine-Ablage (CNC_WINE_DIR, sonst unter $OUT).
WINE_DIR="${CNC_WINE_DIR:-$OUT/wine}"
"$ROOT/tools/wine/build_game.sh" --nur-deps "$WINE_DIR" > "$OUT/deps.txt" 2>&1 || {
	cat "$OUT/deps.txt"; echo "RenderUnitTests: DirectXTex fuer MinGW fehlt"; exit 1; }
$CXX $FLAGS $INC -I $T/Renderer/WW3D2 -I $T/Renderer/WW3D2/Animation -I $T/Renderer/WW3D2/Buffers -I $T/Renderer/WW3D2/Effects -I $T/Renderer/WW3D2/Material -I $T/Renderer/WW3D2/Mesh -I $T/Renderer/WW3D2/Render2D -I $T/Renderer/WW3D2/Scene -I $T/Renderer/WW3D2/System -I "$ROOT/tools/wine/compat" -I "$WINE_DIR/compat" \
	-I "$WINE_DIR/deps/include" -o "$OUT/RenderUnitTests.exe" \
	"$ROOT/Code/Tests/RenderUnitTests/RenderUnitTests.cpp" "$ROOT/Code/Tests/RenderUnitTests/RenderUnitTestStubs.cpp" \
	$T/Renderer/WW3D2/Common/renderformat.cpp $T/Renderer/WW3D2/Common/renderstates.cpp \
	$T/Renderer/WW3D2/Common/renderfvf.cpp $T/Renderer/WW3D2/Vulkan/vulkanrequirements.cpp $T/Renderer/WW3D2/Vulkan/vulkanspirv.cpp \
	$T/Platform/W3DDevice/GameClient/Terrain/TerrainTileImage.cpp \
	"$OUT/libengine.a" "$WINE_DIR/deps/lib/libDirectXTex.a" -lz -lwinmm -lole32 -luuid \
	-static -static-libgcc -static-libstdc++ || {
	echo "RenderUnitTests: Linken fehlgeschlagen"; exit 1; }

# 5. Ausfuehren
export WINEPREFIX="${WINEPREFIX:-$OUT/wineprefix}"
export WINEDEBUG=-all
rc=0
"$WINE" "$OUT/UnitTests.exe" || rc=1
"$WINE" "$OUT/RenderUnitTests.exe" || rc=1
exit $rc
