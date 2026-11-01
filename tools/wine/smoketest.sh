#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Rauchtest: Spiel ohne EA-Daten unter Wine starten (Testdatensatz erzeugen, Skirmish spielen, Ausgang pruefen).
#
# Aufruf:  tools/wine/smoketest.sh [--generals] [--menue] [--frames N] [--zeit s]
#   Ablage des Baus:  CNC_WINE_DIR (Standard ${TMPDIR:-/tmp}/cnc-wine; Debug-Bau: /tmp/cnc-wine-debug)
#   --frames N   Logik-Frames (Standard 300; unter llvmpipe rund 2,7 Frames/s, ein Skirmish mit Sieger braucht
#                rund 1600 Frames und -aiStats)
#   --menue      statt des Skirmish das Hauptmenue mit Shell-Karte starten (-quitAfter 60)
# Rueckgabewert 0, wenn das Spiel mit 0 endet und das Log "EXIT code 0" meldet.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${CNC_WINE_DIR:-${TMPDIR:-/tmp}/cnc-wine}"
FRAMES=300
ZEIT=""
RUNOPT=()
MENUE=0
KIND=zh
while [ $# -gt 0 ]; do
	case "$1" in
		--generals) RUNOPT+=(--generals); KIND=generals ;;
		--menue) RUNOPT+=(--menue); MENUE=1 ;;
		--frames) FRAMES="$2"; shift ;;
		--zeit) ZEIT="$2"; shift ;;
		-h|--help) sed -n 2,10p "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "unbekannte Option $1"; exit 2 ;;
	esac
	shift
done
[ -n "$ZEIT" ] || ZEIT=$(( FRAMES / 2 + 300 ))
DATA="$OUT/testdata-$KIND"
rm -rf "$DATA" && python3 "$HERE/testdaten/generate.py" "$DATA" > /dev/null || { echo "Generator fehlgeschlagen"; exit 1; }
if [ $MENUE = 1 ]; then
	GAMEARGS=(-quitAfter 60)
else
	GAMEARGS=(-skirmish TestSkirmish -skirmishAI 2 -skirmishSeed 4 -maxFrames "$FRAMES" -aiStats 100)
fi
"$HERE/run_game.sh" "${RUNOPT[@]}" --zeit "$ZEIT" "$DATA" "${GAMEARGS[@]}" -crcLog run.log
rc=$?
echo "Rueckgabewert $rc"
if [ $rc -eq 0 ] && grep -q "EXIT code 0" "$DATA/run.log" 2>/dev/null; then
	echo "Rauchtest bestanden ($KIND, ${FRAMES} Frames)"
	grep -a "KISIEG" "$DATA/run.log" | head -1
	exit 0
fi
echo "Rauchtest FEHLGESCHLAGEN -- $DATA/run.log, $DATA/wine.log"
exit 1
