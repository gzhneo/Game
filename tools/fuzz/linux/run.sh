#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Fuzzing unter Linux: baut die Ziele aus tools/fuzz nativ mit clang, libFuzzer, AddressSanitizer und
# UBSan und laesst jedes eine Weile laufen. Ein Fund (Absturz, Speicherfehler, undefiniertes
# Verhalten, Zeitlimit) laesst das Skript fehlschlagen; die Eingabe liegt dann unter <Arbeitsordner>/funde.
#
# Aufruf: tools/fuzz/linux/run.sh [Sekunden je Ziel, Vorgabe 60]
#   FUZZ_PARALLEL=<n>   n Ziele gleichzeitig (Vorgabe 1)
#   FUZZ_ZIELE="a b"    nur diese Ziele laufen lassen (Namen wie fuzz_compression; gebaut werden alle)
# Voraussetzungen: clang, libclang-rt-18-dev (Sanitizer-Laufzeit), zlib; der Syntaxpruef-Spiegel
# (tools/syntaxcheck/setup.sh) liefert die Schreibweisen-Aliase der Includes.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SEK="${1:-60}"
WORK="${FUZZ_DIR:-${TMPDIR:-/tmp}/cnc-fuzz-gate}"
mkdir -p "$WORK"
export SYNTAXCHECK_DIR="$WORK/sc"
export SYNTAXCHECK_SHARED="$ROOT/Code"
"$ROOT/tools/syntaxcheck/setup.sh" > /dev/null 2>&1 || { echo "Spiegel nicht gebaut"; exit 2; }
T="$SYNTAXCHECK_DIR/tree"
FLAGS=(-std=c++20 -g -O1 -DNDEBUG -DFUZZ_LIBFUZZER_MAIN -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined
       -w -include "$HERE/native_compat.h" -I "$HERE/shim" -I "$SYNTAXCHECK_DIR/shim")

# Ziel, Quellen, Include-Pfade, Saatordner
bau() {
	local name=$1; shift
	clang++ "${FLAGS[@]}" "$@" -o "$WORK/$name" 2> "$WORK/$name.bau.log" \
		|| { echo "FEHLER $name baut nicht -- $WORK/$name.bau.log"; return 1; }
}
bau fuzz_compression "$ROOT/tools/fuzz/FuzzCompression.cpp" "$T/Core/Compression/CompressionManager.cpp" \
	"$T"/Core/Compression/EAC/*.cpp -I "$T/Core/Compression" -I "$T/Core" \
	-I "$T/Core/WWLib" -lz || exit 1
bau fuzz_w3d "$ROOT/tools/fuzz/FuzzW3DChunks.cpp" "$T/Core/WWLib/chunkio.cpp" \
	-I "$T/Core/WWLib" -I "$T/Core" -I "$T/Renderer" -I "$T/Core" || exit 1
bau fuzz_map "$ROOT/tools/fuzz/FuzzMapReader.cpp" "$ROOT"/Code/Tools/Editors/MapEditor/Model/{ByteIO,Chunk,MapIO}.cpp \
	-I "$ROOT/Code/Tools/Editors/MapEditor" || exit 1
bau fuzz_wav "$ROOT/tools/fuzz/FuzzWavParser.cpp" "$T/Platform/XAudio2Device/WavParser.cpp" \
	-I "$T/Platform" -I "$T/Core" -I "$T/GameEngine" -I "$T/Core/EngineCore" || exit 1
B="$ROOT/Code/Tools/Editors/Babylon"
bau fuzz_babylon "$ROOT/tools/fuzz/FuzzBabylon.cpp" "$B"/Model/*.cpp "$B"/Core/*.cpp "$T/Core/EngineCore/Engine/ZipArchive.cpp" \
	"$T/Core/EngineCore/Engine/TextFiles.cpp" -I "$B" -I "$T/Core/EngineCore" -lz || exit 1
bau fuzz_big "$ROOT/tools/fuzz/FuzzBigArchive.cpp" "$T/Core/EngineCore/Engine/BigArchive.cpp" \
	-I "$T/GameEngine" -I "$T/Core/EngineCore" || exit 1
bau fuzz_text "$ROOT/tools/fuzz/FuzzTextFiles.cpp" "$T/Core/EngineCore/Engine/TextFiles.cpp" \
	-I "$T/GameEngine" -I "$T/Core/EngineCore" || exit 1
bau fuzz_zip "$ROOT/tools/fuzz/FuzzZipArchive.cpp" "$T/Core/EngineCore/Engine/ZipArchive.cpp" \
	-I "$T/GameEngine" -I "$T/Core/EngineCore" -lz || exit 1
E="$T/Core/EngineCore/Engine"
bau fuzz_session "$ROOT/tools/fuzz/FuzzSession.cpp" "$E/Wire.cpp" "$E/SessionMessage.cpp" "$E/Rendezvous.cpp" \
	"$E/RendezvousServer.cpp" "$E/InviteCode.cpp" "$E/NetAddress.cpp" "$E/RateLimiter.cpp" "$E/Monocypher.cpp" \
	-I "$T/GameEngine" -I "$T/Core/EngineCore" -I "$T/Core" || exit 1
bau fuzz_tga "$ROOT/tools/fuzz/FuzzTarga.cpp" "$T/Core/WWLib/TARGA.CPP" \
	-include "$HERE/shim/targa_native.h" -I "$T/Core/WWLib" -I "$T/Core" -I "$T/Renderer" \
	-I "$T/Core" || exit 1

PARALLEL="${FUZZ_PARALLEL:-1}"
ZIELE="${FUZZ_ZIELE:-}"
lauf() {
	local name=$1 saat=$2
	local korpus="$WORK/korpus_$name" funde="$WORK/funde/$name/"
	mkdir -p "$korpus" "$funde"
	cp "$saat"/* "$korpus"/ 2>/dev/null
	if "$WORK/$name" -max_total_time="$SEK" -timeout=10 -rss_limit_mb=2048 -artifact_prefix="$funde" \
			"$korpus" > "$WORK/$name.log" 2>&1; then
		echo "   OK   $name: $(grep -o 'Done [0-9]* runs' "$WORK/$name.log") in $SEK s" > "$WORK/$name.ergebnis"
	else
		{ echo "   FEHLER $name: Fund in $funde -- $WORK/$name.log"
		  grep -m2 -E "ERROR|runtime error|SUMMARY" "$WORK/$name.log"; } > "$WORK/$name.ergebnis"
	fi
}
namen=()
starte() {
	local name=$1
	if [ -n "$ZIELE" ] && ! printf ' %s ' $ZIELE | grep -q " $name "; then
		return
	fi
	namen+=("$name")
	rm -f "$WORK/$name.ergebnis"
	lauf "$@" &
	while [ "$(jobs -rp | wc -l)" -ge "$PARALLEL" ]; do
		wait -n
	done
}
starte fuzz_compression "$ROOT/tools/fuzz/seeds/compression"
starte fuzz_w3d "$ROOT/tools/fuzz/seeds/w3d"
starte fuzz_wav "$ROOT/tools/fuzz/seeds/wav"
starte fuzz_map "$ROOT/tools/fuzz/seeds/map"
starte fuzz_babylon "$ROOT/tools/fuzz/seeds/babylon"
starte fuzz_big "$ROOT/tools/fuzz/seeds/big"
starte fuzz_zip "$ROOT/tools/fuzz/seeds/zip"
starte fuzz_text "$ROOT/tools/fuzz/seeds/text"
starte fuzz_tga "$ROOT/tools/fuzz/seeds/tga"
starte fuzz_session "$ROOT/tools/fuzz/seeds/session"
wait
fehler=0
for name in "${namen[@]}"; do
	cat "$WORK/$name.ergebnis" 2>/dev/null || echo "   FEHLER $name: kein Ergebnis"
	grep -q '^   OK' "$WORK/$name.ergebnis" 2>/dev/null || fehler=$((fehler + 1))
done
exit $fehler
