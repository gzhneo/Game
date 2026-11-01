#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
#
# Leak check with a user map under Wine: the debug build (tools/wine/build_game.sh --debug) starts to the main menu
# and quits: without a user map, with a map in <user data>\Maps that is not in the map list of the data set, and with
# such a map plus the map.str that names it (the main menu then also loads the map's string file). The
# main menu reads that map from its file (MapCache::addMap, loadMap) -- the path a map received by map transfer also
# takes. Afterwards the debug leak report ("Leaked a block ...") of every run must be empty.
#
#   tools/wine/lecks_benutzerkarte.sh <output folder>
#
# CNC_WINE_DIR selects the debug build (default ${TMPDIR:-/tmp}/cnc-wine-dbg). Exit code 0 if neither run leaks, 1 on
# a leak or a missing leak report, 2 if the runs could not be made.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
[ $# -ge 1 ] || { sed -n 6,15p "$0" | sed 's/^# \{0,1\}//'; exit 2; }
OUT=$1
export CNC_WINE_DIR="${CNC_WINE_DIR:-${TMPDIR:-/tmp}/cnc-wine-dbg}"
[ -f "$CNC_WINE_DIR/game/GZHNeoZH.exe" ] || { echo "kein Debug-Bau in $CNC_WINE_DIR (tools/wine/build_game.sh --debug)"; exit 2; }
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
rm -rf "$OUT/daten" && python3 "$HERE/testdaten/generate.py" "$OUT/daten" > /dev/null || { echo "Generator fehlgeschlagen"; exit 2; }
export WINEPREFIX_CNC="$CNC_WINE_DIR/wineprefix-lecks"
U="$WINEPREFIX_CNC/drive_c/users/${WINEUSER_CNC:-$(id -un)}/Documents/GZH Neo/Zero Hour"

fehler=0
for lauf in ohne mit mit-str; do
	D="$OUT/$lauf"
	rm -rf "$D" && cp -r "$OUT/daten" "$D"
	rm -rf "$U/Maps" && mkdir -p "$U"
	if [ "$lauf" != ohne ]; then
		mkdir -p "$U/Maps/MeineKarte"
		cp "$D/Maps/TestVersus/TestVersus.map" "$U/Maps/MeineKarte/MeineKarte.map"
	fi
	if [ "$lauf" = mit-str ]; then
		# the map's name tag (WorldInfo mapName) is MAP:TestVersus
		printf 'MAP:TestVersus\r\n"Meine Karte"\r\nEND\r\n' > "$U/Maps/MeineKarte/map.str"
	fi
	printf 'hauptmenue\r\nwarte 120\r\nbeenden\r\n' > "$D/ablauf.txt"
	"$HERE/run_game.sh" --menue --zeit 900 "$D" -einstellungenNurLesen -mehrfach -crcLog run.log -testablauf ablauf.txt \
		> "$D/lauf.out" 2>&1
	rc=$?
	L=$(ls "$D"/*DebugLogFile*.txt 2>/dev/null | head -1)
	if [ -z "$L" ]; then
		echo "$lauf: kein Debug-Protokoll (Exit $rc)"
		fehler=$((fehler + 1))
		continue
	fi
	if [ "$lauf" != ohne ] && ! grep -aq "MapCache::addMap(): caching" "$L"; then
		echo "$lauf: die Benutzerkarte wurde nicht eingelesen"
		fehler=$((fehler + 1))
	fi
	n=$(grep -ac "Leaked a block" "$L")
	echo "$lauf Benutzerkarte: Exit $rc, $n Lecks"
	grep -a "Leaked a block" "$L" | head -10 | sed 's/^/  /'
	[ "$n" -eq 0 ] && [ "$rc" -eq 0 ] || fehler=$((fehler + 1))
done
[ $fehler -eq 0 ] && { echo "Lecksuche Benutzerkarte bestanden"; exit 0; }
echo "Lecksuche Benutzerkarte: $fehler Fehler"
exit 1
