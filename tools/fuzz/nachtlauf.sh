#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Nachtlauf aller Fuzzer unter Linux: jedes Ziel eine feste Zeit (Vorgabe eine Stunde), mehrere gleichzeitig.
#
#   tools/fuzz/nachtlauf.sh [Sekunden je Ziel, Vorgabe 3600] [gleichzeitig, Vorgabe Kerne/2]
#   NACHTLAUF_DIR=<Ordner>   Ablage (Vorgabe /tmp/cnc-nachtlauf); Korpus und Funde bleiben dort liegen
#   NACHTLAUF_SAAT=<n>       Saat der Engine-Fuzzer (Vorgabe: Datum, jede Nacht andere Folgen)
#   NACHTLAUF_TEIL=libfuzzer|engine   nur einen der beiden Teile
#
# Teil 1: die libFuzzer-Ziele (tools/fuzz/linux/run.sh, clang mit ASan und UBSan).
# Teil 2: die Fuzzer mit geladener Spiellogik (tools/fuzz/engine/run.sh, MinGW unter Wine), erst einmal je
# Art gebaut, dann mit Zeitgrenze (HARNESS_SECONDS) in getrennten Arbeitsordnern parallel.
# Am Ende steht die Fundliste in <Ablage>/ergebnis.txt; Rueckgabewert = Zahl der Ziele mit Fund.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SEK="${1:-3600}"
KERNE="$(nproc)"
PAR="${2:-$(( KERNE > 1 ? KERNE / 2 : 1 ))}"
WORK="${NACHTLAUF_DIR:-${TMPDIR:-/tmp}/cnc-nachtlauf}"
SAAT="${NACHTLAUF_SAAT:-$(date +%Y%m%d)}"
TEIL="${NACHTLAUF_TEIL:-alle}"
ENGINE_ARTEN="ini map replay save lan net w3d"
mkdir -p "$WORK"
ERG="$WORK/ergebnis.txt"
: > "$ERG"
fehler=0
echo "Nachtlauf $(date '+%Y-%m-%d %H:%M'), $SEK s je Ziel, $PAR gleichzeitig, Saat $SAAT" | tee -a "$ERG"

if [ "$TEIL" = alle ] || [ "$TEIL" = libfuzzer ]; then
	echo "== libFuzzer-Ziele" | tee -a "$ERG"
	FUZZ_DIR="$WORK/libfuzzer" FUZZ_PARALLEL="$PAR" "$HERE/linux/run.sh" "$SEK" > "$WORK/libfuzzer.txt" 2>&1
	n=$?
	grep -E '^   (OK|FEHLER)|ERROR|runtime error|SUMMARY|baut nicht' "$WORK/libfuzzer.txt" | tee -a "$ERG"
	fehler=$((fehler + n))
fi

if [ "$TEIL" = alle ] || [ "$TEIL" = engine ]; then
	echo "== Engine-Fuzzer (Wine)" | tee -a "$ERG"
	export ENGINEFUZZ_DIR="$WORK/engine"
	# bauen (der erste Aufruf baut die Engine, die weiteren linken nur ihren Harness); 0 Eingaben
	for art in $ENGINE_ARTEN; do
		if ! "$HERE/engine/run.sh" "$art" 0 1 > "$WORK/bau_$art.txt" 2>&1; then
			echo "   FEHLER $art: Bau oder Start -- $WORK/bau_$art.txt" | tee -a "$ERG"
			tail -5 "$WORK/bau_$art.txt" | tee -a "$ERG"
			fehler=$((fehler + 1))
		fi
	done
	engine_lauf() {
		local art=$1
		local lauf="$WORK/engine/lauf_$art"
		# eigenes Wine-Praefix je Art: MapFuzz legt seine Karte im Nutzerordner ab, die Kartenliste der
		# anderen Fuzzer (etwa LAN) laese sie sonst mit
		if WINEPREFIX="$WORK/engine/prefix_$art" ENGINEFUZZ_NOBUILD=1 ENGINEFUZZ_RUN="$lauf" HARNESS_SECONDS="$SEK" \
				"$HERE/engine/run.sh" "$art" 1000000000000 "$SAAT" > "$WORK/engine_$art.txt" 2>&1; then
			echo "   OK   $art: $(grep -m1 -E '^[0-9]+ inputs' "$WORK/engine_$art.txt")" > "$WORK/engine_$art.ergebnis"
		else
			{ echo "   FEHLER $art: Fund oder Abbruch -- $WORK/engine_$art.txt, Eingaben unter $lauf (crash-*, hang-*)"
			  grep -v '^$' "$WORK/engine_$art.txt" | tail -12; } > "$WORK/engine_$art.ergebnis"
		fi
	}
	for art in $ENGINE_ARTEN; do
		exe="${art^}Fuzz"
		[ "$art" = w3d ] && exe=W3DFuzz
		[ -f "$WORK/engine/$exe.exe" ] || continue
		engine_lauf "$art" &
		while [ "$(jobs -rp | wc -l)" -ge "$PAR" ]; do
			wait -n
		done
	done
	wait
	for art in $ENGINE_ARTEN; do
		[ -f "$WORK/engine_$art.ergebnis" ] || continue
		cat "$WORK/engine_$art.ergebnis" | tee -a "$ERG"
		grep -q '^   OK' "$WORK/engine_$art.ergebnis" || fehler=$((fehler + 1))
		rm -f "$WORK/engine_$art.ergebnis"
	done
fi

echo "Ende $(date '+%Y-%m-%d %H:%M'): $fehler Ziele mit Fund oder Fehler" | tee -a "$ERG"
exit $fehler
