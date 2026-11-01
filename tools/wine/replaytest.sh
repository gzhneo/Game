#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Determinismus-Test mit dem Testdatensatz unter Wine (ohne EA-Daten).
#
# Aufruf:  tools/wine/replaytest.sh [--frames N] [--generals] [--ki Liste] [--seed N]
#   --ki Liste   Wert fuer -skirmishAI (Standard 2 = zwei klassische KIs; "N1,K2" = neue KI gegen klassische,
#                "E1,M2" = neue KI Leicht gegen Mittel)
#   --seed N     Zufallssaat der Partie (Standard 4; Seed 1 gibt beiden Plaetzen dieselbe Seite und eine lange Partie)
#   --karte Name Karte (Standard TestSkirmish; TestWeit hat acht Startplaetze, etwa --ki "N1,K2,N3,K4,E1,K2m,M3,K4e")
#   1. Skirmish mit -runner aufnehmen (Release-Bau): Replay (.rep) und CRC-Verlauf (.crc)
#   2. dasselbe Replay im Release-Bau zweimal abspielen: CRC-Verlauf muss der Aufnahme entsprechen
#   3. dasselbe Replay als zugeschaute Partie (-zuschauerTest): Nachholen und Warten auf Daten aendern nichts
#   4. dasselbe Replay im Debug-Bau abspielen (falls gebaut): Release und Debug muessen gleich rechnen
# Ablagen: CNC_WINE_DIR (Release, Standard ${TMPDIR:-/tmp}/cnc-wine), CNC_WINE_DEBUG_DIR (Standard /tmp/cnc-wine-debug).
# Die Referenzreplays unter tests/replays gehoeren zu EA-Karten und laufen nur unter Windows mit den Spieldaten.
# Rueckgabewert 0, wenn alle Vergleiche gleich sind.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
REL="${CNC_WINE_DIR:-${TMPDIR:-/tmp}/cnc-wine}"
DBG="${CNC_WINE_DEBUG_DIR:-/tmp/cnc-wine-debug}"
FRAMES=3000
SKI=2
SEED=4
MAP=TestSkirmish
RUNOPT=()
KIND=zh
EXE=GZHNeoZH.exe
GDIR=game
while [ $# -gt 0 ]; do
	case "$1" in
		--frames) FRAMES="$2"; shift ;;
		--ki) SKI="$2"; shift ;;
		--seed) SEED="$2"; shift ;;
		--karte) MAP="$2"; shift ;;
		--generals) RUNOPT+=(--generals); KIND=generals; EXE=GZHNeo.exe; GDIR=game-generals ;;
		-h|--help) sed -n 2,15p "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "unbekannte Option $1"; exit 2 ;;
	esac
	shift
done
fail=0
prep() {	# Ablage -> Datenordner (frisch erzeugt)
	local d="$1/replaytest-$KIND"
	rm -rf "$d" && python3 "$HERE/testdaten/generate.py" "$d" > /dev/null || { echo "Generator fehlgeschlagen" >&2; exit 1; }
	echo "$d"
}
play() {	# Ablage Datenordner Schalter...
	local out="$1" d="$2"; shift 2
	CNC_WINE_DIR="$out" "$HERE/run_game.sh" "${RUNOPT[@]}" --zeit 900 "$d" -runner "$@" > "$d/play.out" 2>&1
	local rc=$?
	[ $rc -eq 0 ] || echo "  Rueckgabewert $rc ($d/play.out)"
	return $rc
}
winpath() { printf 'Z:%s' "${1//\//\\}"; }
crcs() { tr -d '\r' < "$1" | grep -E '^[0-9]+ [0-9A-F]{8}$'; }
# Vergleich: gemeinsamer Abschnitt gleich, und der Lauf deckt die Aufnahme ab
same() {	# Referenz Lauf Beschriftung
	crcs "$1" > "$1.werte"; crcs "$2" > "$2.werte"
	local n=$(wc -l < "$1.werte") m=$(wc -l < "$2.werte")
	if [ "$n" -gt 0 ] && [ "$m" -ge "$n" ] && head -n "$n" "$2.werte" | cmp -s - "$1.werte"; then
		echo "  OK   $3: $n CRC-Werte gleich (letzter Frame $(tail -1 "$1.werte" | cut -d' ' -f1))"
	else
		echo "  FEHLER $3: Aufnahme $n Werte, Lauf $m Werte"
		diff <(head -n "$n" "$2.werte") "$1.werte" | head -4
		fail=1
	fi
}

echo "== Aufnahme (Release, $FRAMES Frames)"
RD=$(prep "$REL")
play "$REL" "$RD" -skirmish "$MAP" -skirmishAI "$SKI" -skirmishSeed "$SEED" -maxFrames "$FRAMES" -recordReplay rec.rep -crcLog rec.crc -crcLogInterval 30 || fail=1
[ -s "$RD/rec.rep" ] || { echo "  FEHLER kein Replay"; exit 1; }
echo "  Replay $(stat -c %s "$RD/rec.rep") Byte, Ende: $(grep -a 'END\|KISIEG' "$RD/rec.crc" | tr -d '\r' | head -2 | tr '\n' ' ')"

echo "== Wiedergabe 1 und 2 (Release)"
for i in 1 2; do
	play "$REL" "$RD" -replay "$(winpath "$RD/rec.rep")" -crcLog play$i.crc -crcLogInterval 30 || fail=1
	same "$RD/rec.crc" "$RD/play$i.crc" "Wiedergabe $i gegen Aufnahme"
done

echo "== Wiedergabe als zugeschaute Partie (Release, -zuschauerTest: Daten kommen schubweise, Nachholen und Warten)"
play "$REL" "$RD" -replay "$(winpath "$RD/rec.rep")" -zuschauerTest 300 -crcLog live.crc -crcLogInterval 30 || fail=1
same "$RD/rec.crc" "$RD/live.crc" "Zuschauer-Wiedergabe gegen Aufnahme"
grep -a "LIVE complete" "$RD/live.crc" | tr -d '\r' | sed 's/^/  /'
grep -aq "LIVE complete" "$RD/live.crc" || { echo "  FEHLER die Zuschauer-Wiedergabe erreichte das Ende des Stroms nicht"; fail=1; }

if [ -f "$DBG/$GDIR/$EXE" ]; then
	echo "== Wiedergabe im Debug-Bau"
	DD=$(prep "$DBG")
	cp "$RD/rec.rep" "$DD/rec.rep"
	play "$DBG" "$DD" -replay "$(winpath "$DD/rec.rep")" -crcLog play.crc -crcLogInterval 30 || fail=1
	same "$RD/rec.crc" "$DD/play.crc" "Debug-Wiedergabe gegen Release-Aufnahme"
else
	echo "== Debug-Bau fehlt ($DBG/$GDIR/$EXE): Vergleich Release/Debug uebersprungen"
fi
[ $fail -eq 0 ] && echo "Determinismus-Test bestanden" || echo "Determinismus-Test FEHLGESCHLAGEN"
exit $fail
