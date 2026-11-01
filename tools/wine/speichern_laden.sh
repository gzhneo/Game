#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
#
# Save and load under Wine: the same skirmish (computer players only) runs twice, once straight through and once
# saved and loaded at the given frame. Both runs log the logic CRC of every frame (-crcLog); after loading, the game
# must compute exactly as without saving. On a difference both runs are repeated with full deep CRC dumps of the
# frame before and the first differing frame (-deepCRCFrames, -deepCRCFull), and tools/determinism/deepcrc_diff.py
# names the first differing objects and sections.
#
#   tools/wine/speichern_laden.sh [--generals] [--ki K1,K2] [--seed N] <output folder> [save frame] [frames after]
#
# Defaults: Zero Hour, computer players K1,K2, seed 2, save after 600 logic frames, 600 frames after loading.
# Needs the build of tools/wine/build_game.sh (CNC_WINE_DIR as for run_game.sh). The result is in
# <output folder>/ergebnis.txt; exit code 0 if every common frame has the same CRC, 1 on a difference or a failed
# test sequence, 2 if the runs could not be made.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
RUNOPT=()
KI=K1,K2
SEED=2
while [ $# -gt 0 ]; do
	case "$1" in
		--generals) RUNOPT+=(--generals) ;;
		--ki) KI="$2"; shift ;;
		--seed) SEED="$2"; shift ;;
		-h|--help) sed -n 6,17p "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		-*) echo "Unbekannter Schalter: $1"; exit 2 ;;
		*) break ;;
	esac
	shift
done
[ $# -ge 1 ] || { sed -n 6,17p "$0" | sed 's/^# \{0,1\}//'; exit 2; }
OUT=$1
SPEICHERN=${2:-600}
NACH=${3:-600}
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
rm -rf "$OUT/daten" && python3 "$HERE/testdaten/generate.py" "$OUT/daten" > /dev/null || { echo "Generator fehlgeschlagen"; exit 2; }

# lauf <name> [game switches...]: one run in a fresh copy of the data set
lauf() {
	local name=$1; shift
	local D="$OUT/$name"
	rm -rf "$D" && cp -r "$OUT/daten" "$D"
	if [ "$name" = ohne ] || [ "$name" = ohne-tief ]; then
		printf 'partie 2000000\r\nlogik %d\r\nbeenden\r\n' $((SPEICHERN + NACH + 20)) > "$D/ablauf.txt"
	else
		printf 'partie 2000000\r\nlogik %d\r\nspeichern pruefung.sav\r\nladen pruefung.sav\r\npartie 2000000\r\nlogik %d\r\nbeenden\r\n' \
			"$SPEICHERN" "$NACH" > "$D/ablauf.txt"
	fi
	"$HERE/run_game.sh" "${RUNOPT[@]}" --zeit $(( (SPEICHERN + NACH) / 4 + 600 )) "$D" -einstellungenNurLesen \
		-d3d11Effects none -skirmish TestSkirmish -skirmishAI "$KI" -skirmishSeed "$SEED" \
		-crcLog run.log -crcLogInterval 1 -testablauf ablauf.txt "$@" > "$D/lauf.out" 2>&1
}

# vergleich <run without saving> <run with saving>: prints the result, the last line is the first differing
# frame or "-" if there is none
vergleich() {
	python3 - "$OUT/$1/run.log" "$OUT/$2/run.log" <<'PYEOF'
import os, sys

def lesen(p):
    werte, fehler = {}, []
    if os.path.exists(p):
        for z in open(p, encoding='latin-1', errors='replace'):
            t = z.split()
            if len(t) == 2 and t[0].isdigit() and len(t[1]) == 8:
                werte[int(t[0])] = t[1]
            elif 'TESTABLAUF ERROR' in z:
                fehler.append(z.strip())
    return werte, fehler

(a, fa), (b, fb) = lesen(sys.argv[1]), lesen(sys.argv[2])
gemeinsam = sorted(set(a) & set(b))
falsch = [f for f in gemeinsam if a[f] != b[f]]
print('ohne Speichern: %d Frames (bis %s), mit Speichern: %d Frames (bis %s)'
      % (len(a), max(a) if a else '-', len(b), max(b) if b else '-'))
print('gemeinsame Frames: %d, abweichend: %d' % (len(gemeinsam), len(falsch)))
for z in fa + fb:
    print('Testablauf-Fehler: ' + z)
print(falsch[0] if falsch else ('-' if gemeinsam and not fa and not fb else 'kein Vergleich'))
PYEOF
}

lauf ohne
lauf mit
vergleich ohne mit > "$OUT/ergebnis.txt"
ERSTE=$(tail -1 "$OUT/ergebnis.txt")
if [ "$ERSTE" = "-" ]; then
	sed -i '$d' "$OUT/ergebnis.txt"
	echo "OK: nach dem Laden gleich (gespeichert bei Frame $((SPEICHERN + 8)), $NACH Frames danach)" >> "$OUT/ergebnis.txt"
	cat "$OUT/ergebnis.txt"
	exit 0
fi
if ! [[ "$ERSTE" =~ ^[0-9]+$ ]]; then
	sed -i '$d' "$OUT/ergebnis.txt"
	echo "FEHLER: kein Vergleich moeglich (Laeufe in $OUT/ohne und $OUT/mit)" >> "$OUT/ergebnis.txt"
	cat "$OUT/ergebnis.txt"
	exit 1
fi

# The CRC line of frame N is written at the start of frame N, so the dump of frame N-1 holds the last equal state
# and the dump of frame N the first differing one.
sed -i '$d' "$OUT/ergebnis.txt"
echo "FEHLER: erste Abweichung bei Frame $ERSTE" >> "$OUT/ergebnis.txt"
lauf ohne-tief -deepCRCFrames $((ERSTE - 1)) "$ERSTE" -deepCRCFull
lauf mit-tief -deepCRCFrames $((ERSTE - 1)) "$ERSTE" -deepCRCFull
for f in $((ERSTE - 1)) "$ERSTE"; do
	{
		echo
		echo "Tiefenabzug Frame $f (deepcrc_diff.py):"
		python3 "$ROOT/tools/determinism/deepcrc_diff.py" "$OUT/ohne-tief/run.log.f$f.deep" "$OUT/mit-tief/run.log.f$f.deep" \
			--max 10 --bytes 32 2>&1
	} >> "$OUT/ergebnis.txt"
done
cat "$OUT/ergebnis.txt"
exit 1
