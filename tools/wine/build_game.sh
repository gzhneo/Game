#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Baut das Spiel ohne Windows: GZHNeoZH.exe (Zero Hour), auf Wunsch auch GZHNeo.exe (Generals), mit dem
# MinGW-Cross-Compiler aus dem Quelltext. Ohne FFmpeg und XAudio2 (stumme Klassen, GZH_NULL_MEDIA), mit
# DirectXTex fuer MinGW. Die Ergebnisse laufen unter Wine (tools/wine/run_game.sh).
#
# Aufruf:  tools/wine/build_game.sh [--debug] [--generals] [--nur-deps] [--nur-vulkan|--nur-d3d11] [Ablage]
#   Ablage      Standard ${TMPDIR:-/tmp}/cnc-wine (auch CNC_WINE_DIR)
#   --debug     Konfiguration wie Debug: -D_DEBUG (Assertions, DebugLogFile, Grenzpruefung im Speicherpool),
#               -O1 -g1. Ohne: -O2 -DNDEBUG -DIG_DEBUG_STACKTRACE wie die Release-Konfiguration.
#   --generals  zusaetzlich GZHNeo.exe (Generals) nach <Ablage>/game-generals
#   --nur-deps  nur DirectXMath, DirectX-Headers und DirectXTex holen und bauen
#   --nur-vulkan, --nur-d3d11  nur eine Geraeteschicht des Renderers (wie GZH_RENDER_D3D11=OFF bzw.
#               GZH_RENDER_VULKAN=OFF): ohne den Ordner WW3D2/D3D11 bzw. WW3D2/Vulkan und ohne deren
#               Bibliotheken; eigene Ablage verwenden, die Schalter gelten fuer alle Dateien
#   JOBS=<n>    parallele Uebersetzungen (Standard: Kerne)
# Ergebnis:  <Ablage>/game/GZHNeoZH.exe, Shaders\ und ProjectData\ daneben.
#
# Zweiter Lauf ohne Aenderungen kostet Sekunden: make mit -MMD-Abhaengigkeiten, Spiegel ueber syncmirror.py.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
LT="$ROOT/tools/linuxtests"

DEBUG=0
GENERALS=0
NURDEPS=0
SCHICHT=beide
OUT="${CNC_WINE_DIR:-${TMPDIR:-/tmp}/cnc-wine}"
while [ $# -gt 0 ]; do
	case "$1" in
		--debug) DEBUG=1 ;;
		--generals) GENERALS=1 ;;
		--nur-deps) NURDEPS=1 ;;
		--nur-vulkan) SCHICHT=vulkan ;;
		--nur-d3d11) SCHICHT=d3d11 ;;
		-h|--help) sed -n 2,18p "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		-*) echo "Unbekannter Schalter: $1"; exit 2 ;;
		*) OUT="$1" ;;
	esac
	shift
done
JOBS="${JOBS:-$(nproc)}"
mkdir -p "$OUT" || exit 1
OUT="$(cd "$OUT" && pwd)"
# DirectXTex schreibt <wrl\client.h>; ein Dateiname mit Backslash gehoert nicht ins Repo (unter Windows ungueltig)
mkdir -p "$OUT/compat" && printf '#include <wrl/client.h>\n' > "$OUT/compat/wrl\\client.h"

CXX=x86_64-w64-mingw32-g++-posix
command -v $CXX >/dev/null || CXX=x86_64-w64-mingw32-g++
CC=${CXX/g++/gcc}
AR=x86_64-w64-mingw32-ar
WINDRES=x86_64-w64-mingw32-windres
for t in $CXX $CC $AR $WINDRES cmake git python3 make; do
	command -v "$t" >/dev/null || { echo "Werkzeug fehlt: $t (siehe tools/linux_einrichten.sh)"; exit 2; }
done

# --- Abhaengigkeiten: DirectXMath, DirectX-Headers, DirectXTex (feste Staende) --------------------------
# MinGW bringt weder DirectXMath in der neuen Fassung noch die D3D12-Header mit, die DirectXTex im
# MinGW-Zweig verlangt; alle drei kommen von Microsoft (MIT-Lizenz) und werden mit CMake gebaut.
DXMATH_TAG=apr2025
DXHEADERS_TAG=v1.619.5
DXTEX_TAG=oct2025
DEPS="$OUT/deps"
holen() { # <Name> <Repo> <Tag>
	[ -d "$OUT/src/$1/.git" ] || {
		mkdir -p "$OUT/src"
		git -c advice.detachedHead=false clone -q --depth 1 --branch "$3" "https://github.com/microsoft/$2.git" "$OUT/src/$1" || {
			echo "Klonen von $2 ($3) fehlgeschlagen"; return 1; }
	}
}
deps_bauen() {
	local stamp="$DEPS/.stand-$DXMATH_TAG-$DXHEADERS_TAG-$DXTEX_TAG"
	[ -f "$stamp" ] && [ -f "$DEPS/lib/libDirectXTex.a" ] && return 0
	holen DirectXMath DirectXMath "$DXMATH_TAG" || return 1
	holen DirectX-Headers DirectX-Headers "$DXHEADERS_TAG" || return 1
	holen DirectXTex DirectXTex "$DXTEX_TAG" || return 1
	cat > "$OUT/mingw-toolchain.cmake" <<E
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER $CC)
set(CMAKE_CXX_COMPILER $CXX)
set(CMAKE_RC_COMPILER $WINDRES)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
E
	local tc="-DCMAKE_TOOLCHAIN_FILE=$OUT/mingw-toolchain.cmake"
	{
		cmake -S "$OUT/src/DirectXMath" -B "$OUT/build/dxmath" $tc -DCMAKE_INSTALL_PREFIX="$DEPS" -DBUILD_TESTING=OFF &&
		cmake --install "$OUT/build/dxmath" &&
		cmake -S "$OUT/src/DirectX-Headers" -B "$OUT/build/dxheaders" $tc -DCMAKE_INSTALL_PREFIX="$DEPS" \
			-DDXHEADERS_BUILD_TEST=OFF -DDXHEADERS_BUILD_GOOGLE_TEST=OFF -DBUILD_SHARED_LIBS=OFF &&
		cmake --build "$OUT/build/dxheaders" -j"$JOBS" && cmake --install "$OUT/build/dxheaders" &&
		# nur die Bibliothek: ohne Werkzeuge, Beispiel, Direct3D-11/12-Laufzeitteil und OpenMP
		cmake -S "$OUT/src/DirectXTex" -B "$OUT/build/dxtex" $tc -DCMAKE_PREFIX_PATH="$DEPS" -DCMAKE_INSTALL_PREFIX="$DEPS" \
			-DCMAKE_BUILD_TYPE=Release -DBUILD_TOOLS=OFF -DBUILD_SAMPLE=OFF -DBUILD_DX11=OFF -DBUILD_DX12=OFF \
			-DBC_USE_OPENMP=OFF -DCMAKE_CXX_FLAGS="-I$HERE/compat -I$OUT/compat" &&
		cmake --build "$OUT/build/dxtex" -j"$JOBS" && cmake --install "$OUT/build/dxtex"
	} > "$OUT/deps.log" 2>&1 || { echo "Abhaengigkeiten bauen fehlgeschlagen, siehe $OUT/deps.log"; return 1; }
	touch "$stamp"
}
deps_bauen || exit 1
# Vulkan-Header und volk: nur Header, volk laedt vulkan-1.dll zur Laufzeit (unter Wine winevulkan)
"$ROOT/tools/vulkan/header_holen.sh" "$OUT/src" || exit 1
echo "Abhaengigkeiten bereit: $DEPS"
[ "$NURDEPS" = 1 ] && exit 0

# --- Spiegel mit den Gross-/Kleinschreib-Aliasen (wie tools/fuzz/engine/run.sh) ------------------------
export SYNTAXCHECK_DIR="$OUT/mirror-neu"
export SYNTAXCHECK_SHARED="$ROOT/Code"
"$ROOT/tools/syntaxcheck/setup.sh" >/dev/null || { echo "Spiegel bauen fehlgeschlagen"; exit 1; }
mkdir -p "$OUT/mirror"
python3 "$ROOT/tools/fuzz/engine/syncmirror.py" "$OUT/mirror-neu" "$OUT/mirror" || exit 1
python3 "$LT/sysalias.py" "$ROOT/Code" "$OUT/sysalias" || exit 1
T="$OUT/mirror/tree"

INC="-I $HERE/compat -I $OUT/compat -I $LT/shim -I $OUT/sysalias -I $OUT/mirror/shim -I $OUT/mirror/shim2/anchor/a/b/c
     -I $T/GameEngine -I $T/Core/EngineCore -I $T/Platform
     -I $T/Core -I $T/Renderer -I $T/Core/WWLib
     -I $T/Core/WWMath -I $T/Core/WWDebug
     -I $T/Core/WWSaveLoad -I $T/Core -I $T/Core/Compression"
# Definitionen wie die Wurzel-CMakeLists. -fno-delete-null-pointer-checks: MemoryPoolObject::deleteInstance prueft
# this auf null, GCC wuerde die Pruefung sonst entfernen. -fno-strict-aliasing und -fwrapv: das Alt-Code
# verlaesst sich auf MSVC-Verhalten (Zeiger-Umdeutung, Ueberlauf). -ffp-contract=off: keine FMA-Kontraktion,
# wie /fp:precise. Kein -mavx2: Wine-Hosts sind nicht zwingend AVX2-faehig.
BASIS="-std=c++20 -w -fpermissive -fms-extensions -finput-charset=cp1252 -fno-delete-null-pointer-checks
       -fno-strict-aliasing -fwrapv -ffp-contract=off
       -DWIN32 -D_WINDOWS -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00
       -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE -DDISABLE_MEMORYPOOL_STACKTRACE
       -D_cdecl=__cdecl -include bits/stdc++.h -DGZH_NULL_MEDIA"
case "$SCHICHT" in
	vulkan) BASIS="$BASIS -DGZH_RENDER_D3D11=0 -DGZH_RENDER_VULKAN=1" ;;
	d3d11) BASIS="$BASIS -DGZH_RENDER_D3D11=1 -DGZH_RENDER_VULKAN=0" ;;
esac
if [ "$DEBUG" = 1 ]; then
	FLAGS="$BASIS -O1 -g1 -D_DEBUG"
	KONF=debug
else
	FLAGS="$BASIS -O2 -g1 -DNDEBUG -DIG_DEBUG_STACKTRACE"
	KONF=release
fi
# WW3D2, GameEngineDevice und Main: Windows-Header vorab (wie ForceOrder.h im MSVC-Build), DirectXTex, Vulkan-Header,
# volk und VMA (eine Zeile: der Wert landet so im Makefile)
DEVFLAGS="-I $T/Renderer/WW3D2 -I $T/Renderer/WW3D2/Animation -I $T/Renderer/WW3D2/Buffers -I $T/Renderer/WW3D2/Effects -I $T/Renderer/WW3D2/Material -I $T/Renderer/WW3D2/Mesh -I $T/Renderer/WW3D2/Render2D -I $T/Renderer/WW3D2/Scene -I $T/Renderer/WW3D2/System -I $T/App -include windows.h -include mmsystem.h -D_USE_MATH_DEFINES -I $DEPS/include -isystem $OUT/src/Vulkan-Headers/include -I $OUT/src/volk -isystem $OUT/src/VulkanMemoryAllocator/include"

python3 "$HERE/quellen.py" "$T" > "$OUT/quellen.txt" || exit 1
# Eine Geraeteschicht weglassen, wie die Liste in Code/Renderer/WW3D2/CMakeLists.txt
case "$SCHICHT" in
	vulkan) sed -i '\|/Renderer/WW3D2/D3D11/|d' "$OUT/quellen.txt" ;;
	d3d11) sed -i '\|/Renderer/WW3D2/Vulkan/|d' "$OUT/quellen.txt" ;;
esac
# GameEdition.cpp legt das Spiel fest; die Engine ist fuer beide Spiele gleich
echo "edition-zh $ROOT/Code/Games/ZeroHour/GameEdition.cpp" >> "$OUT/quellen.txt"
echo "edition-gen $ROOT/Code/Games/Generals/GameEdition.cpp" >> "$OUT/quellen.txt"

# Makefile mit Abhaengigkeiten (-MMD): nach dem ersten Lauf wird nur neu uebersetzt, was sich aendert.
# -MP legt fuer jeden Header ein leeres Ziel an: ohne das blieb nach dem Entfernen eines Headers (etwa beim
# Wechsel des Stands) das alte Objekt stehen, weil make das fehlende Ziel nicht bauen konnte (-k ueberspringt).
# Andere Schalter: alles neu.
if [ "$(cat "$OUT/flags.txt" 2>/dev/null)" != "$KONF $(echo $FLAGS)" ]; then
	rm -rf "$OUT/obj"
	echo "$KONF $(echo $FLAGS)" > "$OUT/flags.txt"
fi
gruppenflags() {
	case "$1" in
		wwlib) echo "-DREGEX_MALLOC -DSTDC_HEADERS" ;;
		wwmath) echo "-DG_CODE_BASE" ;;
		ww3d2|device|main) echo "$DEVFLAGS" ;;
	esac
}
MK="$OUT/Makefile"
{
	echo "CXX := $CXX"
	echo "FLAGS := $(echo $FLAGS) -include $HERE/compat/gzh_mingw_compat.h"
	echo "INC := $(echo $INC)"
	objs=""
	while read -r gruppe f; do
		o="$OUT/obj/$gruppe/$(echo "$f" | md5sum | cut -c1-12).o"
		objs="$objs $o"
		# Pfade mit Leerzeichen (heute keine mehr unter Code/) gehen nicht als Voraussetzung; die
		# -MMD-Datei liefert die Abhaengigkeit nach der ersten Uebersetzung
		dep="$f"; case "$f" in *" "*) dep="" ;; esac
		printf '%s: %s\n\t@mkdir -p $(@D)\n\t@$(CXX) $(FLAGS) $(INC) %s -MMD -MP -c "%s" -o $@ 2> $@.err || { echo "FEHLT %s"; rm -f $@; }\n' \
			"$o" "$dep" "$(gruppenflags "$gruppe")" "$f" "$f"
	done < "$OUT/quellen.txt"
	echo "OBJS :=$objs"
	echo 'all: $(OBJS)'
	echo '-include $(OBJS:.o=.d)'
} > "$MK"
# Objekte, deren -MMD-Datei eine nicht mehr vorhandene Datei nennt (umbenannter Header), neu uebersetzen:
# aeltere .d-Dateien ohne -MP liessen make dort mit "No rule to make target" stehen, gelinkt wurde das alte Objekt.
python3 - "$OUT/obj" <<'PYEOF'
import os, sys
for wurzel, _, dateien in os.walk(sys.argv[1]):
	for name in dateien:
		if not name.endswith('.d'):
			continue
		d = os.path.join(wurzel, name)
		with open(d, encoding='latin-1') as f:
			text = f.read().replace('\\\n', ' ')
		ziel, _, rest = text.partition(': ')
		# Leerzeichen im Pfad stehen als "\ " in der Datei
		pfade = [x.replace('\0', ' ') for x in rest.split('\n', 1)[0].replace('\\ ', '\0').split()]
		if any(not os.path.exists(x) for x in pfade):
			for alt in (d, d[:-2] + '.o'):
				if os.path.exists(alt):
					os.remove(alt)
PYEOF
echo "Uebersetze ($KONF, $JOBS Prozesse) ..."
make -s -k -f "$MK" -j"$JOBS" all > "$OUT/build.log" 2>&1
if grep -q 'No rule to make target' "$OUT/build.log"; then
	grep 'No rule to make target' "$OUT/build.log"
	exit 1
fi
grep '^FEHLT' "$OUT/build.log" | sed 's|.*/tree/||' > "$OUT/fehlt.txt"
if [ -s "$OUT/fehlt.txt" ]; then
	echo "Nicht uebersetzbar: $(wc -l < "$OUT/fehlt.txt") Datei(en), Meldungen in obj/**/*.o.err:"
	cat "$OUT/fehlt.txt"
	exit 1
fi

# --- Bibliotheken je Gruppe; Engine-Bibliotheken als eine Gruppe beim Linken ------------------------------
# Die Reihenfolge, in der der Linker Objekte aufnimmt, bestimmt die Reihenfolge der statischen
# Initialisierung (Kommentar in Code/Games/GameTargets.cmake): die Objekte kommen deshalb in der Reihenfolge der
# Quelllisten in eine Archivdatei und werden als --start-group gelinkt, wie bei CMake.
objliste() { # <Gruppe...>
	while read -r gruppe f; do
		for g in "$@"; do
			[ "$gruppe" = "$g" ] && echo "$OUT/obj/$gruppe/$(echo "$f" | md5sum | cut -c1-12).o"
		done
	done < "$OUT/quellen.txt"
}
archiv() { # <Name> <Gruppe...>
	local name="$1"; shift
	rm -f "$OUT/lib$name.a"
	objliste "$@" | xargs -n 200 $AR qc "$OUT/lib$name.a" && x86_64-w64-mingw32-ranlib "$OUT/lib$name.a"
}
archiv engine engine wwdebug wwlib wwmath wwsaveload compression || exit 1
archiv device device ww3d2 || exit 1

# --- Programm: WinMain, CpuCheck, Ressourcen je Spiel ------------------------------------------------------
LIBS="-lz -lwinmm -lws2_32 -lwsock32 -limm32 -lole32 -loleaut32 -luuid -lshell32 -lcomdlg32 -ladvapi32 -lgdi32
      -luser32 -lkernel32 -ldbghelp -lversion -lshlwapi -ldxguid -ldsound"
# Direct3D 11 nur mit seiner Schicht; ohne sie zeigt das Linken, dass nichts anderes sie braucht
[ "$SCHICHT" = vulkan ] || LIBS="$LIBS -ld3d11 -ldxgi -ld3dcompiler"
# Die Schalter fuer weitere Ziele auf denselben Bibliotheken (tools/wine/build_mapeditor.sh)
{
	printf 'CXX=%q\n' "$CXX"
	printf 'FLAGS=%q\n' "$(echo $FLAGS) -include $HERE/compat/gzh_mingw_compat.h"
	printf 'INC=%q\n' "$(echo $INC)"
	printf 'DEVFLAGS=%q\n' "$(echo $DEVFLAGS)"
	printf 'LIBS=%q\n' "$(echo $LIBS)"
	printf 'T=%q\nDEPS=%q\n' "$T" "$DEPS"
} > "$OUT/schalter.sh"
spiel_bauen() { # <exe> <Titel> <Spielordner> <edition-Gruppe> <Zielordner>
	local exe="$1" titel="$2" spiel="$3" ed="$4" ziel="$5"
	local w="$OUT/main-$exe"
	mkdir -p "$w" "$ziel"
	# CpuCheck ohne AVX2 (kein -mavx2 im ganzen Build); der Titel steht im Fehlerfenster
	$CXX $FLAGS -mxsave -include "$HERE/compat/gzh_mingw_compat.h" $INC $DEVFLAGS -DGAME_EDITION_NAME="\"$titel\"" -c "$T/App/CpuCheck.cpp" -o "$w/CpuCheck.o" 2> "$w/CpuCheck.err" || {
		echo "CpuCheck.cpp fehlgeschlagen, siehe $w/CpuCheck.err"; return 1; }
	local res=""
	# RTS.RC: Symbol, Manifest, Versionsinfo; der Build laeuft auch ohne
	if $WINDRES --input-format=rc -O coff -I "$T/App" -I "$spiel" -I "$T/GameEngine" \
		-DGAME_FILE_DESCRIPTION="\\\"GZH Neo - $titel\\\"" -DGAME_EXE_FILE="\\\"$exe\\\"" \
		"$T/App/RTS.RC" "$w/RTS.res.o" 2> "$w/RTS.rc.err"; then res="$w/RTS.res.o"
	else echo "Hinweis: RTS.RC nicht uebersetzt ($w/RTS.rc.err), Exe ohne Ressourcen"; fi
	local mainobj edobj
	mainobj="$(grep '^main ' "$OUT/quellen.txt" | grep WinMain | cut -d' ' -f2)"
	mainobj="$OUT/obj/main/$(echo "$mainobj" | md5sum | cut -c1-12).o"
	edobj="$OUT/obj/$ed/$(echo "$spiel/GameEdition.cpp" | md5sum | cut -c1-12).o"
	# Reihenfolge wie bei CMake: Main, dann die Engine-Bibliotheken als Gruppe, GameEdition danach
	$CXX -o "$ziel/$exe" "$mainobj" "$w/CpuCheck.o" $res "$edobj" \
		-Wl,--start-group "$OUT/libdevice.a" "$OUT/libengine.a" "$DEPS/lib/libDirectXTex.a" -Wl,--end-group \
		$LIBS -static -static-libgcc -static-libstdc++ -mwindows 2> "$w/link.log" || {
		echo "Linken von $exe fehlgeschlagen, fehlende Symbole: $(grep -c 'undefined reference' "$w/link.log") (siehe $w/link.log)"
		return 1; }
	# Shader und Projektdaten wie die Post-Build-Schritte in Code/App/CMakeLists.txt
	rm -rf "$ziel/Shaders" "$ziel/ProjectData"
	mkdir -p "$ziel/ProjectData"
	cp -r "$ROOT/Data/Shaders" "$ziel/Shaders"
	cp -r "$ROOT/Data/INI" "$ziel/ProjectData/INI"
	cp -r "$ROOT/Data/Textures" "$ziel/ProjectData/Textures"
	cp -r "$ROOT/Data/Text" "$ziel/ProjectData/Text"
	cp -r "$ROOT/Data/Network" "$ziel/ProjectData/Network"
	# Shader-Compiler des Vulkan-Renderers (wie der Post-Build-Schritt; unter Linux aus dem NuGet-Paket des Hooks)
	local dxc
	dxc="$(ls -d /opt/dxc-*/dxcompiler.dll 2>/dev/null | sort | tail -1)"
	rm -f "$ziel/dxcompiler.dll"
	if [ "$SCHICHT" = d3d11 ]; then :
	elif [ -n "$dxc" ]; then cp "$dxc" "$ziel/"
	else echo "Hinweis: dxcompiler.dll nicht gefunden (/opt/dxc-*), -vulkan faellt auf Direct3D 11 zurueck"; fi
	echo "Fertig: $ziel/$exe ($(du -h "$ziel/$exe" | cut -f1))"
}
spiel_bauen GZHNeoZH.exe "Zero Hour" "$ROOT/Code/Games/ZeroHour" edition-zh "$OUT/game" || exit 1
if [ "$GENERALS" = 1 ]; then
	spiel_bauen GZHNeo.exe "Generals" "$ROOT/Code/Games/Generals" edition-gen "$OUT/game-generals" || exit 1
fi
