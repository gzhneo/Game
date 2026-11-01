#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Baut den Karteneditor ohne Windows: MapEditor.exe (Qt 6) und MapEditorEngine.dll mit dem MinGW-Cross-Compiler,
# auf den Engine-Bibliotheken von tools/wine/build_game.sh. Qt (qtbase, qtsvg) wird beim ersten Lauf aus den
# Quellen gebaut und in der Ablage behalten. Das Ergebnis laeuft unter Wine (tools/wine/run_mapeditor.sh).
#
# Aufruf:  tools/wine/build_mapeditor.sh [--nur-qt] [Ablage]
#   Ablage      dieselbe wie bei build_game.sh: Standard ${TMPDIR:-/tmp}/cnc-wine (auch CNC_WINE_DIR)
#   --nur-qt    nur Qt holen und bauen
#   JOBS=<n>    parallele Uebersetzungen (Standard: Kerne)
# Ergebnis:  <Ablage>/mapeditor/MapEditor.exe mit MapEditorEngine.dll, Qt-DLLs, Plugins, Shaders\ und dxcompiler.dll.
#            Daneben der GUI-Editor GuiEditor.exe (dieselbe DLL, dasselbe Qt; tools/wine/run_guieditor.sh).
#
# Erster Lauf: Engine wie build_game.sh (15 bis 25 Minuten), Qt rund 20 bis 30 Minuten auf vier Kernen (3 GB).
# Danach uebersetzt make nur, was sich geaendert hat (-MMD, auch geaenderte Header).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
ME="$ROOT/Code/Tools/Editors/MapEditor"

NURQT=0
OUT="${CNC_WINE_DIR:-${TMPDIR:-/tmp}/cnc-wine}"
while [ $# -gt 0 ]; do
	case "$1" in
		--nur-qt) NURQT=1 ;;
		-h|--help) sed -n 2,14p "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		-*) echo "Unbekannter Schalter: $1"; exit 2 ;;
		*) OUT="$1" ;;
	esac
	shift
done
JOBS="${JOBS:-$(nproc)}"
mkdir -p "$OUT" || exit 1
OUT="$(cd "$OUT" && pwd)"

CXX=x86_64-w64-mingw32-g++-posix
command -v $CXX >/dev/null || { echo "Werkzeug fehlt: $CXX (siehe tools/linux_einrichten.sh)"; exit 2; }
CC=${CXX/g++/gcc}
for t in cmake ninja git g++ make python3; do
	command -v "$t" >/dev/null || { echo "Werkzeug fehlt: $t (siehe tools/linux_einrichten.sh)"; exit 2; }
done

# --- Qt: qtbase und qtsvg, einmal fuer Linux (moc, rcc, ... fuer den Cross-Bau) und fuer Windows -----------
# Fertige Qt-Pakete fuer MinGW kommen ueber den Installer von download.qt.io, der hinter dem Proxy der
# Cloud-Umgebung nicht erreichbar ist; die Quellen kommen von github.com/qt. Nur Widgets, Gui, Core und Svg
# werden gebraucht, ohne OpenGL, Netzwerk, SQL und Druck.
QT_TAG=v6.8.3
Q="$OUT/qt"
QTFLAGS="-DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=Release
         -DINPUT_opengl=no -DFEATURE_opengl=OFF -DFEATURE_dbus=OFF -DFEATURE_sql=OFF -DFEATURE_network=OFF
         -DFEATURE_printsupport=OFF -DFEATURE_testlib=OFF -DFEATURE_icu=OFF -DFEATURE_vulkan=OFF
         -DFEATURE_system_zlib=OFF -DFEATURE_system_pcre2=OFF -DFEATURE_system_png=OFF -DFEATURE_system_jpeg=OFF
         -DFEATURE_system_freetype=OFF -DFEATURE_system_harfbuzz=OFF"
qt_bauen() {
	local stamp="$Q/.stand-$QT_TAG"
	[ -f "$stamp" ] && [ -f "$Q/win/bin/Qt6Widgets.dll" ] && [ -x "$Q/host/libexec/rcc" ] && return 0
	echo "Baue Qt $QT_TAG (einmalig, rund 20 bis 30 Minuten) ..."
	local n
	for n in qtbase qtsvg; do
		[ -d "$Q/src/$n/.git" ] || {
			mkdir -p "$Q/src"
			git -c advice.detachedHead=false clone -q --depth 1 --branch "$QT_TAG" "https://github.com/qt/$n.git" "$Q/src/$n" || {
				echo "Klonen von $n ($QT_TAG) fehlgeschlagen"; return 1; }
		}
	done
	cat > "$Q/mingw.cmake" <<E
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_C_COMPILER $CC)
set(CMAKE_CXX_COMPILER $CXX)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32 $Q/win)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
E
	{
		# Linux-Werkzeuge: ohne Fenstersystem, nur was moc/rcc/uic brauchen
		cmake -G Ninja -S "$Q/src/qtbase" -B "$Q/build-host" -DCMAKE_INSTALL_PREFIX="$Q/host" $QTFLAGS \
			-DFEATURE_xcb=OFF -DFEATURE_xlib=OFF -DFEATURE_wayland=OFF -DFEATURE_glib=OFF -DFEATURE_fontconfig=OFF \
			-DFEATURE_libudev=OFF -DFEATURE_evdev=OFF -DFEATURE_linuxfb=OFF -DFEATURE_xkbcommon=OFF \
			-DFEATURE_eglfs=OFF -DFEATURE_egl=OFF &&
		cmake --build "$Q/build-host" -j"$JOBS" && cmake --install "$Q/build-host" &&
		cmake -G Ninja -S "$Q/src/qtbase" -B "$Q/build-win" -DCMAKE_INSTALL_PREFIX="$Q/win" \
			-DCMAKE_TOOLCHAIN_FILE="$Q/mingw.cmake" -DQT_HOST_PATH="$Q/host" $QTFLAGS &&
		cmake --build "$Q/build-win" -j"$JOBS" && cmake --install "$Q/build-win" &&
		cmake -G Ninja -S "$Q/src/qtsvg" -B "$Q/build-svg" -DCMAKE_INSTALL_PREFIX="$Q/win" -DCMAKE_PREFIX_PATH="$Q/win" \
			-DCMAKE_TOOLCHAIN_FILE="$Q/mingw.cmake" -DQT_HOST_PATH="$Q/host" -DCMAKE_BUILD_TYPE=Release \
			-DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF &&
		cmake --build "$Q/build-svg" -j"$JOBS" && cmake --install "$Q/build-svg"
	} > "$Q/build.log" 2>&1 || { echo "Qt bauen fehlgeschlagen, siehe $Q/build.log"; return 1; }
	touch "$stamp"
}
mkdir -p "$Q"
qt_bauen || exit 1
echo "Qt bereit: $Q"
[ "$NURQT" = 1 ] && exit 0

# --- Engine-Bibliotheken: build_game.sh baut sie (ein zweiter Lauf ohne Aenderungen kostet Sekunden) -------
"$HERE/build_game.sh" "$OUT" || { echo "Engine bauen fehlgeschlagen (build_game.sh)"; exit 1; }
[ -f "$OUT/schalter.sh" ] || { echo "$OUT/schalter.sh fehlt: build_game.sh zu alt"; exit 1; }
# CXX, FLAGS, INC, DEVFLAGS, LIBS, T und DEPS genau wie beim Spiel
. "$OUT/schalter.sh"

W="$OUT/mapeditor-obj"
Z="$OUT/mapeditor"
mkdir -p "$W" "$Z"
# Die Ressourcen (Texte, Symbole) als C++-Quelle; rcc schreibt nur, wenn sich etwas geaendert hat.
"$Q/host/libexec/rcc" -name MapEditor "$ME/App/Resources/MapEditor.qrc" -o "$W/qrc_neu.cpp" || exit 1
cmp -s "$W/qrc_neu.cpp" "$W/qrc_MapEditor.cpp" || mv "$W/qrc_neu.cpp" "$W/qrc_MapEditor.cpp"
rm -f "$W/qrc_neu.cpp"
"$Q/host/libexec/rcc" -name GuiEditor "$ROOT/Code/Tools/Editors/GuiEditor/App/Resources/GuiEditor.qrc" -o "$W/qrc_neu.cpp" || exit 1
cmp -s "$W/qrc_neu.cpp" "$W/qrc_GuiEditor.cpp" || mv "$W/qrc_neu.cpp" "$W/qrc_GuiEditor.cpp"
rm -f "$W/qrc_neu.cpp"
# moc fuer die Klassen mit Q_OBJECT (Signale, Modelle); nur neu schreiben, was sich geaendert hat.
MOCS=""
for h in $(grep -l "Q_OBJECT" "$ME"/App/*.h "$ME"/App/*/*.h 2>/dev/null); do
	n="moc_$(basename "$h" .h).cpp"
	MOCS="$MOCS $n"
	"$Q/host/libexec/moc" "$h" -o "$W/$n.neu" || exit 1
	cmp -s "$W/$n.neu" "$W/$n" || mv "$W/$n.neu" "$W/$n"
	rm -f "$W/$n.neu"
done
for f in "$W"/moc_*.cpp; do
	[ -e "$f" ] || continue
	case " $MOCS " in *" $(basename "$f") "*) ;; *) rm -f "$f" ;; esac
done

# Wie der MSVC-Build: der Editor-Code streng (Warnungen sind Fehler), die Engine-DLL mit den Schaltern der Engine.
STRENG="-std=c++20 -O1 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -I $ME -I $ROOT/Code/Tools/Editors"
QT_INC="-isystem $Q/win/include -isystem $Q/win/include/QtCore -isystem $Q/win/include/QtGui
        -isystem $Q/win/include/QtWidgets -isystem $Q/win/include/QtSvg"
KOMPR="-isystem $T/Core/Compression -isystem $T/Core -isystem $T/Core/WWLib"
MK="$W/Makefile"
regel() { # <Objekt> <Quelle> <Schalter...>
	local o="$1" f="$2"; shift 2
	printf '%s: %s\n\t@$(CXX) %s -MMD -MP -c "%s" -o $@ 2> $@.err || { echo "FEHLT %s"; rm -f $@; }\n' "$o" "$f" "$*" "$f" "$f"
}
DLLOBJ=""
APPOBJ=""
GEOBJ=""
GE="$ROOT/Code/Tools/Editors/GuiEditor"
{
	echo "CXX := $CXX"
	for f in "$ME"/Engine/*.cpp; do
		o="$W/engine/$(basename "$f" .cpp).o"; DLLOBJ="$DLLOBJ $o"
		regel "$o" "$f" "$(echo $FLAGS $INC $DEVFLAGS) -include $T/Platform/ForceOrder.h -I $ME -DMAPEDITOR_ENGINE_BUILD"
	done
	# Das Kartenmodell packt Karten mit der Compression-Bibliothek des Spiels; sie kommt als eigene Objekte
	# in die Exe, nicht aus libengine.a (die Engine ersetzt den globalen operator new, deshalb die DLL).
	for f in "$T"/Core/Compression/CompressionManager.cpp "$T"/Core/Compression/EAC/*.cpp; do
		o="$W/kompr/$(basename "$f" .cpp).o"; APPOBJ="$APPOBJ $o"
		regel "$o" "$f" "-std=c++20 -O1 -w $(echo $KOMPR)"
	done
	for f in "$ME"/Model/*.cpp "$ME"/Core/*.cpp; do
		o="$W/kern/$(basename "$(dirname "$f")")_$(basename "$f" .cpp).o"; APPOBJ="$APPOBJ $o"
		regel "$o" "$f" "$STRENG $(echo $KOMPR)"
	done
	for f in "$ME"/App/*.cpp "$ME"/App/SelfTests/*.cpp "$ME"/App/Windows/*.cpp; do
		o="$W/app/$(basename "$f" .cpp).o"; APPOBJ="$APPOBJ $o"
		regel "$o" "$f" "$STRENG $(echo $QT_INC) -DUNICODE -D_UNICODE"
	done
	for f in "$W"/moc_*.cpp; do
		[ -e "$f" ] || continue
		o="$W/app/$(basename "$f" .cpp).o"; APPOBJ="$APPOBJ $o"
		regel "$o" "$f" "-std=c++20 -O1 -I $ME -I $ROOT/Code/Tools/Editors $(echo $QT_INC) -DUNICODE -D_UNICODE"
	done
	# Gemeinsame Teile beider Editoren (Code/Tools/Editors/EditorCommon): in beide Exe.
	for f in "$ROOT"/Code/Tools/Editors/EditorCommon/*.cpp; do
		o="$W/common/$(basename "$f" .cpp).o"; APPOBJ="$APPOBJ $o"; GEOBJ="$GEOBJ $o"
		regel "$o" "$f" "$STRENG $(echo $QT_INC) -DUNICODE -D_UNICODE"
	done
	# Der GUI-Editor nutzt dieselbe Engine-DLL und dasselbe Qt; seine Quellen vor denen des Karteneditors suchen.
	for f in "$GE"/App/*.cpp; do
		o="$W/guieditor/$(basename "$f" .cpp).o"; GEOBJ="$GEOBJ $o"
		regel "$o" "$f" "-I $GE $STRENG $(echo $QT_INC) -DUNICODE -D_UNICODE"
	done
	# Layout-Modell und Kern des GUI-Editors (ohne Engine und Qt), mit den Feldregeln des Spiels.
	for f in "$GE"/Model/*.cpp "$GE"/Core/*.cpp "$ROOT"/Code/Core/EngineCore/Engine/WndFields.cpp; do
		o="$W/guieditor/model_$(basename "$f" .cpp).o"; GEOBJ="$GEOBJ $o"
		regel "$o" "$f" "-I $GE $STRENG -I $ROOT/Code/GameEngine -I $ROOT/Code/Core/EngineCore"
	done
	o="$W/guieditor/qrc_GuiEditor.o"; GEOBJ="$GEOBJ $o"
	regel "$o" "$W/qrc_GuiEditor.cpp" "-std=c++20 -O1 $(echo $QT_INC)"
	o="$W/app/qrc_MapEditor.o"; APPOBJ="$APPOBJ $o"
	regel "$o" "$W/qrc_MapEditor.cpp" "-std=c++20 -O1 $(echo $QT_INC)"
	echo "OBJS :=$(printf '%s\n' $DLLOBJ $APPOBJ $GEOBJ | sort -u | tr '\n' ' ')"
	echo 'all: $(OBJS)'
	echo '-include $(OBJS:.o=.d)'
} > "$MK"
mkdir -p "$W/engine" "$W/kompr" "$W/kern" "$W/app" "$W/guieditor" "$W/common"
echo "Uebersetze den Editor ($JOBS Prozesse) ..."
make -s -k -f "$MK" -j"$JOBS" all > "$W/build.log" 2>&1
if grep -q '^FEHLT' "$W/build.log"; then
	echo "Nicht uebersetzbar:"
	grep '^FEHLT' "$W/build.log" | sed "s|$ROOT/||"
	for e in $(grep '^FEHLT' "$W/build.log" | cut -d' ' -f2-); do
		find "$W" -name "$(basename "$e" .cpp).o.err" -exec head -20 {} \;
	done
	exit 1
fi

# --- Linken: die DLL wie das Spiel statisch gegen die Engine, die Exe gegen Qt ---------------------------------
$CXX -shared -o "$Z/MapEditorEngine.dll" $DLLOBJ \
	-Wl,--start-group "$OUT/libdevice.a" "$OUT/libengine.a" "$DEPS/lib/libDirectXTex.a" -Wl,--end-group \
	$LIBS -static -static-libgcc -static-libstdc++ 2> "$W/link-dll.log" || {
	echo "Linken der DLL fehlgeschlagen, fehlende Symbole: $(grep -c 'undefined reference' "$W/link-dll.log") (siehe $W/link-dll.log)"
	exit 1; }
$CXX -mwindows -o "$Z/MapEditor.exe" $APPOBJ -L"$Q/win/lib" -lQt6Widgets -lQt6Svg -lQt6Gui -lQt6Core -lz -ldwmapi \
	2> "$W/link-exe.log" || { echo "Linken der Exe fehlgeschlagen (siehe $W/link-exe.log)"; exit 1; }
$CXX -mwindows -o "$Z/GuiEditor.exe" $GEOBJ -L"$Q/win/lib" -lQt6Widgets -lQt6Svg -lQt6Gui -lQt6Core \
	2> "$W/link-guieditor.log" || { echo "Linken des GUI-Editors fehlgeschlagen (siehe $W/link-guieditor.log)"; exit 1; }

# --- Laufzeit neben die Exe: Qt, Plugins, MinGW-Laufzeit, Shader ------------------------------------------------
for d in Qt6Core Qt6Gui Qt6Widgets Qt6Svg; do cp -u "$Q/win/bin/$d.dll" "$Z/"; done
for p in platforms/qwindows iconengines/qsvgicon imageformats/qsvg styles/qmodernwindowsstyle; do
	mkdir -p "$Z/$(dirname "$p")"
	cp -u "$Q/win/plugins/$p.dll" "$Z/$(dirname "$p")/"
done
for d in libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll; do
	f="$($CXX -print-file-name="$d")"
	[ -f "$f" ] || f="$(find /usr/lib/gcc/x86_64-w64-mingw32 /usr/x86_64-w64-mingw32 -name "$d" -path '*posix*' 2>/dev/null | head -1)"
	[ -f "$f" ] || f="$(find /usr/x86_64-w64-mingw32 -name "$d" 2>/dev/null | head -1)"
	[ -f "$f" ] || { echo "MinGW-Laufzeit fehlt: $d"; exit 1; }
	cp -u "$f" "$Z/"
done
rm -rf "$Z/Shaders" && cp -r "$ROOT/Data/Shaders" "$Z/Shaders"
# Shader-Compiler des Vulkan-Renderers (-vulkan), wie neben dem Spiel (build_game.sh)
DXC_DLL="$(ls -d /opt/dxc-*/dxcompiler.dll 2>/dev/null | sort | tail -1)"
if [ -n "$DXC_DLL" ]; then cp -u "$DXC_DLL" "$Z/"; else echo "Hinweis: dxcompiler.dll nicht gefunden (/opt/dxc-*), -vulkan faellt auf Direct3D 11 zurueck"; fi
# Texturen der Detailstufen (Boden-Detail, Fels, Wasser), wie beim Spiel neben der Exe
rm -rf "$Z/ProjectData" && mkdir -p "$Z/ProjectData" && cp -r "$ROOT/Data/Textures" "$Z/ProjectData/Textures"
echo "Fertig: $Z/MapEditor.exe"
