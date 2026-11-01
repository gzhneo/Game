#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Netzpaket-Tests: Rundlauf aller Befehlstypen gegen golden.txt und Fuzzing von NetPacket
# (MinGW-Build, laeuft unter Wine; Spiegel und Bibliotheken von tools/linuxtests/run.sh).
#
# Aufruf:  tools/fuzz/netpacket/run.sh [Iterationen] [--write-golden]
#   Iterationen   Fuzz-Eingaben (Standard 200000; 0 = nur Rundlauf)
#   NETPACKET_DIFF=<n>  Folgen im Differenztest gegen die EA-Referenz (Standard 2000, 0 = aus)
#   --write-golden  golden.txt aus dem aktuellen Stand neu schreiben (nur nach einer gewollten
#                   Formataenderung, die Abweichung vorher ansehen)
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
LT="$ROOT/tools/linuxtests"
OUT="${LINUXTESTS_DIR:-${TMPDIR:-/tmp}/cnc-linuxtests}"
ITER="${1:-200000}"
EXTRA="${2:-}"
DIFF="${NETPACKET_DIFF:-2000}"	# Folgen im Differenztest (0 = aus)
CXX=x86_64-w64-mingw32-g++
WINE=$(command -v wine64 || echo /usr/lib/wine/wine64)

# Spiegel und libengine.a wie der UnitTest-Lauf; fehlen sie, zuerst dort bauen
if [ ! -f "$OUT/libengine.a" ] || [ ! -d "$OUT/mirror/tree" ]; then
	"$LT/run.sh" >/dev/null 2>&1 || { echo "tools/linuxtests/run.sh fehlgeschlagen"; exit 1; }
fi
# Spiegel auf den aktuellen Stand bringen (er ist eine Kopie von Code/)
SYNTAXCHECK_DIR="$OUT/mirror" SYNTAXCHECK_SHARED="$ROOT/Code" "$ROOT/tools/syntaxcheck/setup.sh" >/dev/null || exit 1
T="$OUT/mirror/tree"
G="$T/GameEngine"
INC="-I $LT/shim -I $OUT/sysalias -I $OUT/mirror/shim -I $OUT/mirror/shim2/anchor/a/b/c
     -I $T/GameEngine -I $T/Core/EngineCore -I $T/Platform
     -I $T/Core -I $T/Renderer -I $T/Core/WWLib
     -I $T/Core/WWMath -I $T/Core/WWDebug
     -I $T/Core/WWSaveLoad -I $T/Core -I $T/Core/Compression"
# -fsanitize=bounds im Trap-Modus: Indexfehler auf Arrays fester Groesse brechen ab (ohne Laufzeitbibliothek)
FLAGS="-std=c++20 -w -O1 -g -DWIN32 -D_WINDOWS -DNOMINMAX -D_WIN32_WINNT=0x0A00 -D_cdecl=__cdecl -include bits/stdc++.h
       -fsanitize=bounds -fsanitize-undefined-trap-on-error"

# Referenz fuer den Differenztest: Serialisierung von NetPacket vor der Neufassung (EA-Code samt den
# Korrekturen R-306, R-308, R-309), als Kopie in referenz/ (Stand von Commit bc8042d7, damit der Test
# ohne Historie laeuft). Textuell in RefNetPacket umbenannt (ganze Woerter; NetPacketRouter* bleibt),
# eigener Kopfschutz.
REF="$OUT/netref"
mkdir -p "$REF"
RENAME_SED='s/\bNetPacket(List|ListIter)?\b/RefNetPacket\1/g'
cat "$HERE/referenz/NetPacket.h" \
	| sed -E -e 's/__NETPACKET_H/__REF_NETPACKET_H/g' -e '/#pragma once/d' -e 's#"NetworkDefs.h"#"GameNetwork/NetworkDefs.h"#' \
	  -e "$RENAME_SED" > "$REF/NetPacket_ref.h" || exit 1
cat "$HERE/referenz/NetPacket.cpp" \
	| sed -E -e 's#"GameNetwork/NetPacket.h"#"NetPacket_ref.h"#' -e 's#"GameNetwork/NetworkUtil.h"#"GameNetwork/networkutil.h"#' \
	  -e "$RENAME_SED" > "$REF/NetPacket_ref.cpp" || exit 1

$CXX $FLAGS $INC -I "$REF" -c "$REF/NetPacket_ref.cpp" -o "$REF/NetPacket_ref.o" || { echo "Referenz nicht uebersetzbar"; exit 1; }
SRC="$HERE/NetPacketTests.cpp $HERE/NetPacketDiff.cpp $REF/NetPacket_ref.o $HERE/net_stubs.cpp
     $G/GameNetwork/NetPacket.cpp $G/GameNetwork/NetCommandMsg.cpp $G/GameNetwork/NetCommandRef.cpp
     $G/GameNetwork/NetCommandList.cpp $G/GameNetwork/GameMessageParser.cpp $G/GameNetwork/NetworkUtil.cpp
     $G/Common/Replay/MessageStream.cpp $G/Common/System/NameKeyGenerator.cpp
     $G/Common/System/AsciiString.cpp $G/Common/System/UnicodeString.cpp $LT/poolstubs.cpp"
$CXX $FLAGS $INC -I "$REF" -o "$OUT/NetPacketTests.exe" $SRC "$OUT/libengine.a" -lz -lwinmm -lws2_32 \
	-static -static-libgcc -static-libstdc++ || { echo "Bau von NetPacketTests fehlgeschlagen"; exit 1; }

export WINEPREFIX="${WINEPREFIX:-$OUT/wineprefix}"
export WINEDEBUG=-all
ARGS="$HERE/golden.txt"
[ "$EXTRA" = "--write-golden" ] && ARGS="$ARGS --write-golden"
[ "$ITER" != "0" ] && ARGS="$ARGS --fuzz $ITER"
[ "$DIFF" != "0" ] && ARGS="$ARGS --diff $DIFF"
# Wine legt ein Unix-Arbeitsverzeichnis auf Z:, absolute Unix-Pfade gehen daher direkt
"$WINE" "$OUT/NetPacketTests.exe" $ARGS
