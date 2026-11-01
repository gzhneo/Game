#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Bildserie unter Wine fuer pixelgleiche Vorher-/Nachher-Vergleiche: dasselbe Gefecht
# mit den freien Testdaten, feste Zeitschritte, alle N Logik-Frames ein Bild des Backbuffers.
#
# Aufruf:  tools/vulkan/bildserie_wine.sh <Datenordner> <Bildordner> [Spielschalter ...]
#   <Datenordner>  erzeugt mit tools/wine/testdaten/generate.py
#   <Bildordner>   wird geleert; danach shot_NNNNNN.bmp
#   CNC_WINE_DIR   Ablage des Baus (tools/wine/build_game.sh), Standard ${TMPDIR:-/tmp}/cnc-wine
#   BILD_FRAMES    letzter Logik-Frame (Standard 240), BILD_ABSTAND Frames zwischen zwei Bildern (Standard 60)
# Weitere Schalter werden angehaengt (etwa -effects none, -vulkan). Vergleich zweier Ordner:
#   python3 tools/render/compare_shots.py <Referenz> <Neu> --threshold 0 --max-diff-ratio 0 --min-psnr 0
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
DATEN="${1:?Aufruf: bildserie_wine.sh <Datenordner> <Bildordner> [Schalter ...]}"
BILDER="${2:?Bildordner fehlt}"
shift 2
rm -rf "$BILDER" && mkdir -p "$BILDER" || exit 2
BILDER="$(cd "$BILDER" && pwd)"
WINPFAD="Z:$(printf '%s' "$BILDER" | sed 's|/|\\|g')"
"$ROOT/tools/wine/run_game.sh" --zeit "${BILD_ZEIT:-1800}" "$DATEN" -skirmish TestSkirmish -skirmishAI 2 -skirmishSeed 1 \
	-fixedTimeStep -maxFrames "${BILD_FRAMES:-240}" -shotEvery "${BILD_ABSTAND:-60}" -shotDir "$WINPFAD" "$@"
rc=$?
n=$(ls "$BILDER"/*.bmp 2>/dev/null | wc -l)
echo "Bildserie: $n Bilder in $BILDER (Rueckgabewert $rc)"
[ "$rc" = 0 ] && [ "$n" -gt 0 ]
