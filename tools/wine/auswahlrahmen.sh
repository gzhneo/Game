#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
#
# Selection box under Wine: a skirmish with the local player as a participant (-skirmishAI H1,K2) at each given
# resolution; the test sequence drags a selection box with the left button over all own units on screen (command
# rahmen: their projected centres plus a margin) once at the start position and once zoomed out, and checks that
# every one of them is selected; after each box the command zeichenbereich checks that the ground under the edges of
# the whole 3D view lies in the region whose drawables are drawn each frame. Wide resolutions (UI layout pillarboxed)
# extend the 3D view under the control bar; there the command streifen moves an own unit beside the bar and behind it
# and selects all units on screen: the unit must be selected beside the bar and not behind it.
#
#   tools/wine/auswahlrahmen.sh <output folder> [WxH ...]
#
# Default resolutions 3440x1440 (21:9), 2560x1440 (16:9) and 1024x768 (4:3). Needs the build of
# tools/wine/build_game.sh (CNC_WINE_DIR as for run_game.sh). Exit code 0 if every check passed, 1 on a miss or a
# failed test sequence, 2 if the runs could not be made.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
[ $# -ge 1 ] || { sed -n 6,19p "$0" | sed 's/^# \{0,1\}//'; exit 2; }
OUT=$1
shift
AUFLOESUNGEN=("$@")
[ ${#AUFLOESUNGEN[@]} -gt 0 ] || AUFLOESUNGEN=(3440x1440 2560x1440 1024x768)
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
rm -rf "$OUT/daten" && python3 "$HERE/testdaten/generate.py" "$OUT/daten" > /dev/null || { echo "Generator fehlgeschlagen"; exit 2; }

fehler=0
for res in "${AUFLOESUNGEN[@]}"; do
	D="$OUT/$res"
	rm -rf "$D" && cp -r "$OUT/daten" "$D"
	printf 'partie 2000000\r\nlogik 30\r\nrahmen 8\r\nzeichenbereich\r\nrad -10\r\nlogik 10\r\nrahmen 4\r\nzeichenbereich\r\nrahmen 12 oben\r\nrahmen 12 unten\r\nrahmen 12 links\r\nrahmen 12 rechts\r\nstreifen\r\nstreifen mitte\r\nbeenden\r\n' > "$D/ablauf.txt"
	"$HERE/run_game.sh" --zeit 900 --auflosung "$res" "$D" -einstellungenNurLesen -d3d11Effects none \
		-xres "${res%x*}" -yres "${res#*x}" -skirmish TestSkirmish -skirmishAI H1,K2 -skirmishSeed 1 \
		-crcLog run.log -testablauf ablauf.txt > "$D/lauf.out" 2>&1
	rc=$?
	ok=$(grep -ac "TESTABLAUF OK line .* \(rahmen\|zeichenbereich\|streifen\)" "$D/run.log" 2>/dev/null)
	echo "$res: Exit $rc, $ok von 10 Pruefungen bestanden"
	grep -a "TESTABLAUF .*\(rahmen\|zeichenbereich\|streifen\)\|TESTABLAUF ERROR" "$D/run.log" 2>/dev/null | sed 's/^/  /'
	[ "$rc" -eq 0 ] && [ "$ok" = 10 ] || fehler=$((fehler + 1))
done
[ $fehler -eq 0 ] && { echo "Auswahlrahmen bestanden"; exit 0; }
echo "Auswahlrahmen: $fehler Aufloesung(en) mit Fehlern"
exit 1
