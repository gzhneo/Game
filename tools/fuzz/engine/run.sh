#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Fuzzer mit geladener Spiellogik: baut die ganze GameEngine mit MinGW (Debug-Konfiguration,
# Speicherpool mit Grenzpruefung) und fuehrt einen Harness unter Wine aus.
#
# Aufruf:  tools/fuzz/engine/run.sh <ini|map|replay|save|lan|net|w3d|udp|lockstep> [Iterationen] [Saat]
#          tools/fuzz/engine/run.sh <ini|map|replay|save|lan|net|w3d|udp|lockstep> --run <Datei>   eine Datei mit Debug-Log laden
#   ENGINEFUZZ_DIR=<dir>  Ablage fuer Objektdateien und Wine-Praefix (Standard /tmp/cnc-enginefuzz)
#   HARNESS_VERBOSE=1     Debug-Log und jede Assertion ausgeben
#   HARNESS_SECONDS=<n>   Fuzz-Schleife nach n Sekunden beenden (Nachtlauf, tools/fuzz/nachtlauf.sh)
#   ENGINEFUZZ_RUN=<dir>  Arbeitsordner des Laufs (Standard <Ablage>/run); getrennte Ordner erlauben parallele Laeufe
#   ENGINEFUZZ_NOBUILD=1  nicht bauen, die vorhandene Exe nehmen (parallele Laeufe nach einem Bau)
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
LT="$ROOT/tools/linuxtests"
OUT="${ENGINEFUZZ_DIR:-${TMPDIR:-/tmp}/cnc-enginefuzz}"
KIND="${1:-}"
case "$KIND" in
	ini) EXE=IniFuzz ;;
	map) EXE=MapFuzz ;;
	replay) EXE=ReplayFuzz ;;
	save) EXE=SaveFuzz ;;
	lan) EXE=LanFuzz ;;
	net) EXE=NetFuzz ;;
	w3d) EXE=W3DFuzz ;;
	udp) EXE=UdpTest ;;
	lockstep) EXE=LockstepTest ;;
	*) echo "Aufruf: $0 <ini|map|replay|save|lan|net|w3d|udp|lockstep> [Iterationen] [Saat] | <ini|map|replay|save|lan|net|w3d|udp|lockstep> --run <Datei>"; exit 2 ;;
esac
ITER="${2:-20000}"
SEED="${3:-1}"
CXX=x86_64-w64-mingw32-g++
WINE=$(command -v wine64 || echo /usr/lib/wine/wine64)
command -v $CXX >/dev/null || { echo "$CXX fehlt"; exit 2; }
mkdir -p "$OUT"

T="$OUT/mirror/tree"
if [ "${ENGINEFUZZ_NOBUILD:-0}" != 1 ]; then
# Spiegel mit den Gross-/Kleinschreib-Aliasen und Systemheader-Aliase wie tools/linuxtests
# setup.sh baut den Spiegel neu; syncmirror.py uebernimmt nur Geaendertes, damit make nicht alles
# neu uebersetzt
export SYNTAXCHECK_DIR="$OUT/mirror-neu"
export SYNTAXCHECK_SHARED="$ROOT/Code"
"$ROOT/tools/syntaxcheck/setup.sh" >/dev/null || exit 1
mkdir -p "$OUT/mirror"
python3 "$HERE/syncmirror.py" "$OUT/mirror-neu" "$OUT/mirror" || exit 1
python3 "$LT/sysalias.py" "$ROOT/Code" "$OUT/sysalias" || exit 1

INC="-I $LT/shim -I $OUT/sysalias -I $OUT/mirror/shim -I $OUT/mirror/shim2/anchor/a/b/c
     -I $T/GameEngine -I $T/Core/EngineCore -I $T/Platform
     -I $T/Core -I $T/Renderer -I $T/Core/WWLib
     -I $T/Core/WWMath -I $T/Core/WWDebug
     -I $T/Core/WWSaveLoad -I $T/Core -I $T/Core/Compression"
# Definitionen wie die Debug-Konfiguration der Wurzel-CMakeLists (_DEBUG: Grenzpruefung im Speicherpool).
# -fsanitize=bounds im Trap-Modus: Indexfehler auf Arrays fester Groesse brechen ab.
# -fno-delete-null-pointer-checks: MemoryPoolObject::deleteInstance prueft this auf null;
# MSVC laesst die Pruefung stehen, GCC entfernt sie sonst.
# gzh_mingw_compat.h: was die MinGW-Header gegenueber dem Windows-SDK nicht kennen (wie im Wine-Bau).
FLAGS="-std=c++20 -w -O1 -g1 -fpermissive -fms-extensions -finput-charset=cp1252
       -DWIN32 -D_WINDOWS -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00
       -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE -DDISABLE_MEMORYPOOL_STACKTRACE -D_DEBUG
       -D_cdecl=__cdecl -include bits/stdc++.h -include $ROOT/tools/wine/compat/gzh_mingw_compat.h -fsanitize=bounds -fsanitize-undefined-trap-on-error -fno-delete-null-pointer-checks"

# Die Engine meldet ihre Abdeckung und ihre Vergleiche mit Konstanten an den Harness (Harness.cpp,
# __sanitizer_cov_trace_pc und _cmp); der Harness selbst bleibt ohne.
ENGINE_FLAGS="$FLAGS -fsanitize-coverage=trace-pc,trace-cmp"
# W3D-Teil (Zeichenmodule und was sie brauchen): zusaetzlich WW3D2, Code/App und die Windows-Header,
# die der MSVC-Build ueber die vorkompilierten Header bekommt
DEVFLAGS="-I $T/Renderer/WW3D2 -I $T/Renderer/WW3D2/Animation -I $T/Renderer/WW3D2/Buffers -I $T/Renderer/WW3D2/Effects -I $T/Renderer/WW3D2/Material -I $T/Renderer/WW3D2/Mesh -I $T/Renderer/WW3D2/Render2D -I $T/Renderer/WW3D2/Scene -I $T/Renderer/WW3D2/System -I $T/App -include windows.h -include mmsystem.h -D_USE_MATH_DEFINES"
# Gelaendekacheln als DDS (TerrainTileImage.cpp) lesen mit DirectXTex; fuer MinGW einmal gebaut wie in
# tools/linuxtests/run.sh (Wine-Ablage CNC_WINE_DIR, sonst unter $OUT)
WINE_DIR="${CNC_WINE_DIR:-$OUT/wine}"
"$ROOT/tools/wine/build_game.sh" --nur-deps "$WINE_DIR" > "$OUT/deps.txt" 2>&1 || {
	cat "$OUT/deps.txt"; echo "DirectXTex fuer MinGW fehlt"; exit 1; }
DXTEXFLAGS="-I $ROOT/tools/wine/compat -I $WINE_DIR/compat -I $WINE_DIR/deps/include"
# Vulkan-Header, volk und VMA fuer die Vulkan-Dateien in WW3D2 (holt build_game.sh --nur-deps mit)
DEVFLAGS="$DEVFLAGS -isystem $WINE_DIR/src/Vulkan-Headers/include -I $WINE_DIR/src/volk -isystem $WINE_DIR/src/VulkanMemoryAllocator/include"

# Quellen: Bibliotheken (wie tools/linuxtests) und die GameEngine (aus ihrer CMakeLists)
python3 "$LT/sources.py" "$T" > "$OUT/lib_sources.txt" || exit 1
python3 "$HERE/sources.py" "$T" engine > "$OUT/engine_sources.txt" || exit 1
python3 "$HERE/sources.py" "$T" device > "$OUT/device_sources.txt" || exit 1

# Makefile mit Abhaengigkeiten (-MMD): nach dem ersten Lauf wird nur neu uebersetzt, was sich
# geaendert hat. Pfade mit Leerzeichen gibt es in den Listen nicht.
MK="$OUT/Makefile"
# andere Schalter: alles neu uebersetzen
if [ "$(cat "$OUT/flags.txt" 2>/dev/null)" != "$(echo $ENGINE_FLAGS)" ]; then
	rm -rf "$OUT/obj"
	echo $ENGINE_FLAGS > "$OUT/flags.txt"
fi
# die Objekte der aktuellen Quelllisten: umbenannte oder entfernte Quellen hinterlassen alte Objekte
rm -f "$OUT/objects.txt"
{
	echo "CXX := $CXX"
	echo "FLAGS := $(echo $ENGINE_FLAGS)"
	echo "INC := $(echo $INC)"
	echo "DEVFLAGS := $(echo $DEVFLAGS)"
	echo "DXTEXFLAGS := $(echo $DXTEXFLAGS)"
	for kind in lib engine device; do
		extra=""
		[ "$kind" = device ] && extra=' $(DEVFLAGS)'
		objs=""
		while read -r f; do
			fextra="$extra"
			case "$f" in */TerrainTileImage.cpp) fextra="$extra \$(DXTEXFLAGS)" ;; esac
			o="$OUT/obj/$kind/$(echo "$f" | md5sum | cut -c1-12).o"
			objs="$objs $o"
			echo "$o" >> "$OUT/objects.txt"
			printf '%s: %s\n\t@mkdir -p $(@D)\n\t@$(CXX) $(FLAGS) $(INC)%s -MMD -MP -c "%s" -o $@ 2> $@.err || { echo "FEHLT %s"; touch $@.skip; rm -f $@; }\n' "$o" "$f" "$fextra" "$f" "$f"
		done < "$OUT/${kind}_sources.txt"
		echo "${kind}_OBJS :=$objs"
	done
	echo 'all: $(lib_OBJS) $(engine_OBJS) $(device_OBJS)'
	echo '-include $(lib_OBJS:.o=.d) $(engine_OBJS:.o=.d) $(device_OBJS:.o=.d)'
} > "$MK"
make -s -k -f "$MK" -j"$(nproc)" all > "$OUT/build.log" 2>&1
grep '^FEHLT' "$OUT/build.log" | sed 's|.*/tree/||' > "$OUT/skipped.txt"
echo "Uebersetzt: $(while read -r o; do [ -f "$o" ] && echo "$o"; done < "$OUT/objects.txt" | wc -l) Dateien, nicht uebersetzbar: $(wc -l < "$OUT/skipped.txt")"
rm -f "$OUT/libgameengine.a"
while read -r o; do [ -f "$o" ] && echo "$o"; done < "$OUT/objects.txt" | xargs x86_64-w64-mingw32-ar rcs "$OUT/libgameengine.a" || exit 1

# Harness mit Platzhaltern; die Dateisysteme der Geraeteschicht dazu, das Spiel ist Zero Hour
D="$T/Platform/Win32Device/Common"
$CXX $FLAGS $INC $DEVFLAGS -o "$OUT/$EXE.exe" "$HERE/$EXE.cpp" "$HERE/Harness.cpp" "$HERE/stubs.cpp" \
	"$D/Win32LocalFileSystem.cpp" "$D/Win32LocalFile.cpp" "$D/Win32BIGFileSystem.cpp" \
	"$ROOT/Code/Games/ZeroHour/GameEdition.cpp" \
	-Wl,--start-group "$OUT/libgameengine.a" -Wl,--end-group "$WINE_DIR/deps/lib/libDirectXTex.a" \
	-lz -lwinmm -lws2_32 -lole32 -luuid -ldbghelp -lversion -lshlwapi -limm32 -lgdi32 \
	-ld3d11 -ldxgi -ld3dcompiler -ldxguid \
	-static -static-libgcc -static-libstdc++ 2> "$OUT/link.log" || {
	echo "Linken fehlgeschlagen, fehlende Symbole: $(grep -c 'undefined reference' "$OUT/link.log")"; exit 1; }
else
	[ -f "$OUT/$EXE.exe" ] || { echo "$OUT/$EXE.exe fehlt (ENGINEFUZZ_NOBUILD)"; exit 1; }
fi

export WINEPREFIX="${WINEPREFIX:-$OUT/wineprefix}"
export WINEDEBUG=-all
# Eine vom Karten-Fuzzer zurueckgelassene (absichtlich kaputte) Nutzerkarte wuerde jeder andere Harness in der
# Kartenliste finden und als Assertion zaehlen (MapCache::addMap); vor jedem Lauf entfernen.
rm -rf "$WINEPREFIX"/drive_c/users/*/Documents/"Command and Conquer Generals"*Data/Maps/fuzzmap
# Der Harness arbeitet mit relativen Windows-Pfaden: Seeds nach $OUT/run, dort starten
RUN="${ENGINEFUZZ_RUN:-$OUT/run}"
rm -rf "$RUN" && mkdir -p "$RUN" && cp -r "$HERE/seeds" "$RUN/seeds" || exit 1
# eine Datei zum Nachstellen liegt ausserhalb von $RUN: vorher hineinkopieren
if [ "$ITER" = "--run" ]; then
	cp "$SEED" "$RUN/repro$(printf '%s' "$SEED" | sed 's/.*\(\.[A-Za-z0-9]*\)$/\1/')" || exit 1
	SEED="repro$(printf '%s' "$SEED" | sed 's/.*\(\.[A-Za-z0-9]*\)$/\1/')"
fi
# ein Objekt je Modul mit allen Feldern (der Harness behaelt die Felder, die sauber laden)
python3 "$HERE/seedgen.py" "$ROOT/Code" "$RUN/seeds/gen" || exit 1
# Einige Speicher laden ihre Datei in init() selbst; leer genuegt, die Testdaten kommen aus seeds/base
mkdir -p "$RUN/Data/INI" && touch "$RUN/Data/INI/ParticleSystem.ini" "$RUN/Data/INI/Animation2D.ini"
# Replays: die Referenz-Replays aus tests/replays, gekuerzt auf Kopf und erste Befehle
mkdir -p "$RUN/seeds/replay"
for r in "$ROOT/tests/replays/x64/e0-alpine-assault-2ai-seed1.rep" "$ROOT/tests/replays/zh/x64/e0zh-green-pastures-2ai-seed1.rep"; do
	[ -f "$r" ] && head -c 16384 "$r" > "$RUN/seeds/replay/$(basename "$r")"
done
# W3D-Modelle und DDS-Texturen aus eigener Vorschrift
python3 "$HERE/w3dseeds.py" "$RUN/seeds/w3d" || exit 1
# kleine Stringtabelle fuer TheGameText (Kartenliste); die Sprache ist unter Wine English
mkdir -p "$RUN/Data/English" && cp "$ROOT/tools/fuzz/seeds/text/small.csf" "$RUN/Data/English/Generals.csf"
# Projekttexte wie im Spiel neben der Exe
mkdir -p "$RUN/ProjectData" && cp -r "$ROOT/Data/Text" "$RUN/ProjectData/Text"
cd "$RUN" || exit 1
# Mit Zeitgrenze zusaetzlich eine harte Grenze von aussen: ein Wine-Prozess, der beim Beenden haengen bleibt,
# haelt sonst den Nachtlauf auf (Rueckgabewert 124)
GRENZE=()
[ -n "${HARNESS_SECONDS:-}" ] && GRENZE=(timeout --kill-after=30 $((HARNESS_SECONDS + 600)))
if [ "$ITER" = "--run" ]; then
	"${GRENZE[@]}" "$WINE" "$OUT/$EXE.exe" --run seeds "$SEED" 2> "$RUN/stderr.txt"
else
	"${GRENZE[@]}" "$WINE" "$OUT/$EXE.exe" seeds "$ITER" "$SEED" 2> "$RUN/stderr.txt"
fi
RC=$?
[ $RC -eq 124 ] && echo "Abbruch von aussen nach $((HARNESS_SECONDS + 600)) s (Prozess haengt)" >&2
grep -v '^Harness: stack' "$RUN/stderr.txt" >&2
# Aufrufkette eines Absturzes in Funktionen und Zeilen aufloesen
STACK=$(grep -m1 '^Harness: stack' "$RUN/stderr.txt" | sed 's/^Harness: stack//')
if [ -n "$STACK" ]; then
	echo "Aufrufkette:" >&2
	x86_64-w64-mingw32-addr2line -e "$OUT/$EXE.exe" -f -C -i -p $STACK | sed "s|$T/||" | head -60 >&2
fi
exit $RC
