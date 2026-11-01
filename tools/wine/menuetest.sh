#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
#
# Menu test under Wine: a test sequence must get through the main menu without any real mouse move (test
# sequences and -echteEingabeAus ignore the real mouse). Same sequences as tools/abnahme/menue_test.py:
#   LAN       hauptmenue, klick ButtonMultiplayer, klick ButtonNetwork, fenster LanLobbyMenu.wnd:ButtonHost
#   LAN_ohne  the same with -echteEingabeAus
#   Skirmish  hauptmenue, klickFalls ButtonSinglePlayer, klick ButtonSkirmish, fenster SkirmishGameOptionsMenu.wnd:ButtonStart
#             (the test data has no campaign: single player is disabled and Skirmish sits in the main panel)
#
#   tools/wine/menuetest.sh [--generals]
# Needs the build of tools/wine/build_game.sh (CNC_WINE_DIR as for run_game.sh). Exit code 0 if every run ends
# with 0, logs one OK line per command and no TESTABLAUF ERROR.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${CNC_WINE_DIR:-${TMPDIR:-/tmp}/cnc-wine}"
RUNOPT=()
KIND=zh
[ "${1:-}" = --generals ] && RUNOPT+=(--generals) && KIND=generals
DATA="$OUT/menuetest-$KIND"
rm -rf "$DATA" && python3 "$HERE/testdaten/generate.py" "$DATA" > /dev/null || { echo "Generator fehlgeschlagen"; exit 1; }

lan='hauptmenue\r\nklick MainMenu.wnd:ButtonMultiplayer 900\r\nklick MainMenu.wnd:ButtonNetwork 900\r\nfenster LanLobbyMenu.wnd:ButtonHost 900\r\nbeenden\r\n'
skirmish='hauptmenue\r\nklickFalls MainMenu.wnd:ButtonSinglePlayer 60\r\nklick MainMenu.wnd:ButtonSkirmish 900\r\nfenster SkirmishGameOptionsMenu.wnd:ButtonStart 900\r\nbeenden\r\n'
fails=0
lauf() {
	local name=$1 ablauf=$2; shift 2
	printf "$ablauf" > "$DATA/$name.txt"
	"$HERE/run_game.sh" "${RUNOPT[@]}" --menue --zeit 600 "$DATA" -einstellungenNurLesen \
		-crcLog "$name.log" -testablauf "$name.txt" "$@" > "$DATA/$name.out" 2>&1
	local rc=$?
	local okzahl fehler
	okzahl=$(grep -ac "TESTABLAUF OK line" "$DATA/$name.log" 2>/dev/null)
	fehler=$(grep -a -m1 "TESTABLAUF ERROR" "$DATA/$name.log" 2>/dev/null)
	if [ $rc -eq 0 ] && [ "${okzahl:-0}" -ge 5 ] && [ -z "$fehler" ]; then
		echo "OK      $name: Exit-Code 0, $okzahl von 5 Befehlen"
	else
		echo "FEHLER  $name: Exit-Code $rc, ${okzahl:-0} von 5 Befehlen ${fehler#*TESTABLAUF } -- $DATA/$name.log"
		fails=$((fails + 1))
	fi
}
lauf LAN "$lan"
lauf LAN_ohne "$lan" -echteEingabeAus
lauf Skirmish "$skirmish"
if [ $fails -eq 0 ]; then
	echo "Menuetest bestanden ($KIND)"
	exit 0
fi
echo "Menuetest FEHLGESCHLAGEN ($KIND): $fails Lauf/Laeufe"
exit 1
