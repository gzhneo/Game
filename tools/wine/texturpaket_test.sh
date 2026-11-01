#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Texturpaket unter Wine: das Spiel laedt dieselben Texturen aus einem Paket in der Groesse der gewaehlten Stufe.
#
# Aufruf:  tools/wine/texturpaket_test.sh [--frames N]
#   Ablage des Baus: CNC_WINE_DIR (Standard ${TMPDIR:-/tmp}/cnc-wine), Bau mit tools/wine/build_game.sh
#
# Ablauf: Testdaten als Archiv (tools/wine/testdaten/generate.py --big), daraus eine Stufe UHD mit vierfach
# vergroesserten Texturen und der Wassertextur als DXT1-DDS (texturpaket_stufe.py), daraus das Paket
# (tools/assets/build_texture_package.py). Dann sechs kurze Skirmish-Laeufe mit -logTextureStats: ohne Paket,
# mit Paket in -textureQuality sd, hd und uhd, uhd mit einem Mod, der die Wassertextur in Originalgroesse ersetzt, und
# die Texturen des Pakets samt Manifest als freier Inhalt (ProjectData\Inhalt) ohne Paket des Spiels, also in SD.
# Geprueft: Zeile TEXTURPAKET mit der verlangten Stufe in HD und UHD; SD laedt die Originale (Zeile TEXTURESTUFE SD,
# gleich viele Texel wie ohne Paket), HD mehr, UHD noch mehr, eine DXT-Textur aus dem Paket in HD; der Gelaendeatlas in SD wie ohne Paket, in HD und UHD mit
# Kacheln vom Faktor 2 bzw. 4 (Zeile TERRAINATLAS_HD); mit Mod die Zeile TEXTURPAKET ersetzt 1 und weniger Texel
# als UHD ohne Mod (die Datei des Mods wird nicht nach den Massen des Pakets behandelt); der freie Inhalt in SD mit
# seinem Manifest so viele Texel wie die Originale; jeder Lauf endet mit 0.
# Rueckgabewert 0 = bestanden.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${CNC_WINE_DIR:-${TMPDIR:-/tmp}/cnc-wine}"
FRAMES=120
while [ $# -gt 0 ]; do
	case "$1" in
		--frames) FRAMES="$2"; shift ;;
		-h|--help) sed -n 2,16p "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "unbekannte Option $1"; exit 2 ;;
	esac
	shift
done
DATA="$OUT/testdata-paket"
rm -rf "$DATA" && mkdir -p "$DATA"
# mit einer Teamfarben-Textur (ZHCA): sie kommt wie jede andere Textur ins Paket, die Engine faerbt sie im Shader ein
python3 "$HERE/testdaten/generate.py" "$DATA" --big TestDataZH.big --teamfarbe > /dev/null || { echo "Generator fehlgeschlagen"; exit 1; }
python3 "$HERE/texturpaket_stufe.py" "$DATA" --mod "$DATA/modtest" || exit 1
python3 "$ROOT/tools/assets/build_texture_package.py" --dir "$DATA" > "$DATA/paket_bau.txt" || { cat "$DATA/paket_bau.txt"; echo "Paket nicht gebaut"; exit 1; }
cat "$DATA/paket_bau.txt"
# die Teamfarben-Textur liegt im Paket (ZHCD bliebe draussen)
python3 - "$DATA/Texturstufen/Paket" <<'PY' || { echo "FEHLER: Teamfarben-Textur nicht im Paket"; exit 1; }
import glob, os, struct, sys
names = []
for big in glob.glob(os.path.join(sys.argv[1], '*.big')):
    data = open(big, 'rb').read()
    pos = 16
    for _ in range(struct.unpack('>I', data[8:12])[0]):
        end = data.index(b'\0', pos + 8)
        names.append(data[pos + 8:end].decode('ascii').lower())
        pos = end + 1
ok = any(n.endswith('zhca_testunit.tga') for n in names)
print('Teamfarbe im Paket:', 'ja' if ok else 'nein')
sys.exit(0 if ok else 1)
PY
# Der Stufenordner UHD bleibt liegen: er ist nur Eingabe des Werkzeugs, die Engine liest ihn nicht (TextureTiers.cpp).

lauf() {
	local name=$1; shift
	rm -f "$DATA/run_$name.log"
	"$HERE/run_game.sh" --zeit 900 "$DATA" -skirmish TestSkirmish -skirmishAI 2 -skirmishSeed 4 -maxFrames "$FRAMES" \
		-logTextureStats 30 -crcLog "run_$name.log" "$@" > "$DATA/wine_$name.txt" 2>&1
	local rc=$?
	echo "$name: Rueckgabewert $rc"
	return $rc
}
# groesster Wert eines Feldes ueber alle TEXSTATS-Zeilen (in der Partie, nicht beim Abbau am Ende)
feld() { grep -a "^TEXSTATS" "$DATA/run_$1.log" | sed -E "s/.* $2 ([0-9.]+).*/\1/" | sort -g | tail -1; }
# Kachelfaktor des Gelaendeatlas (1 ohne Zeile TERRAINATLAS_HD)
atlas() { local f; f=$(grep -a -m1 "^TERRAINATLAS_HD scale" "$DATA/run_$1.log" | awk '{print $3}'); echo "${f:-1}"; }

fehler=0
mv "$DATA/Texturstufen/Paket" "$DATA/Paket.weg"
lauf ohne || fehler=$((fehler + 1))
mv "$DATA/Paket.weg" "$DATA/Texturstufen/Paket"
for stufe in sd hd uhd; do
	lauf "$stufe" -textureQuality "$stufe" || fehler=$((fehler + 1))
done
lauf mod -textureQuality uhd -mod "Z:$(printf '%s' "$DATA/modtest" | tr / '\\')" || fehler=$((fehler + 1))
# freier Inhalt: die Dateien des Pakets lose unter ProjectData\Inhalt\TestFrei (ein eigener Ordner ProjectData mit
# Verknuepfungen auf den Bau statt der Verknuepfung, die run_game.sh anlegt), das Paket des Spiels beiseite
[ -L "$DATA/ProjectData" ] && rm -f "$DATA/ProjectData"
mkdir -p "$DATA/ProjectData"
for n in "$OUT/game/ProjectData/"*; do [ -e "$DATA/ProjectData/$(basename "$n")" ] || ln -s "$n" "$DATA/ProjectData/$(basename "$n")"; done
python3 - "$DATA/Texturstufen/Paket" "$DATA/ProjectData/Inhalt/TestFrei" <<'PY' || fehler=$((fehler + 1))
import glob, os, struct, sys
quelle, ziel = sys.argv[1:3]
n = 0
for big in sorted(glob.glob(os.path.join(quelle, '*.big'))):
    data = open(big, 'rb').read()
    count = struct.unpack('>I', data[8:12])[0]
    pos = 16
    for _ in range(count):
        off, size = struct.unpack('>II', data[pos:pos + 8])
        end = data.index(b'\0', pos + 8)
        name = data[pos + 8:end].decode('ascii').replace('\\', '/')
        pos = end + 1
        pfad = os.path.join(ziel, name)
        os.makedirs(os.path.dirname(pfad) or ziel, exist_ok=True)
        open(pfad, 'wb').write(data[off:off + size])
        n += 1
print(f'freier Inhalt: {n} Dateien nach {ziel}')
PY
mv "$DATA/Texturstufen/Paket" "$DATA/Paket.weg"
lauf frei || fehler=$((fehler + 1))
mv "$DATA/Paket.weg" "$DATA/Texturstufen/Paket"
rm -rf "$DATA/ProjectData/Inhalt"
grep -aq "^FREIER INHALT .*TestFrei.* stufe SD manifest [1-9]" "$DATA/run_frei.log" \
	|| { echo "FEHLER frei: freier Inhalt ohne Manifest geladen"; fehler=$((fehler + 1)); }

for name in ohne sd hd uhd mod frei; do
	printf '%-5s %s | %s\n' "$name" "$(grep -a -m1 '^TEXTURPAKET\|^TEXTURESTUFE' "$DATA/run_$name.log" 2>/dev/null)" \
		"$(grep -a '^TEXSTATS' "$DATA/run_$name.log" 2>/dev/null | tail -1)"
done
for stufe in hd uhd; do
	want=$(echo "$stufe" | tr a-z A-Z)
	grep -aq "^TEXTURPAKET $want " "$DATA/run_$stufe.log" || { echo "FEHLER $stufe: keine Zeile TEXTURPAKET $want"; fehler=$((fehler + 1)); }
done
# SD zeigt die Originale, das Paket bleibt ungeladen
grep -aq "^TEXTURESTUFE SD " "$DATA/run_sd.log" || { echo "FEHLER sd: keine Zeile TEXTURESTUFE SD"; fehler=$((fehler + 1)); }
grep -aq "^TEXTURPAKET ersetzt 0 " "$DATA/run_uhd.log" || { echo "FEHLER uhd: keine Zeile TEXTURPAKET ersetzt 0"; fehler=$((fehler + 1)); }
grep -aq "^TEXTURPAKET ersetzt 1 " "$DATA/run_mod.log" || { echo "FEHLER mod: keine Zeile TEXTURPAKET ersetzt 1"; fehler=$((fehler + 1)); }
tmod=$(feld mod mtexel)
echo "Texel mit Mod (UHD): $tmod"
python3 -c "import sys; sys.exit(0 if float('${tmod:-0}') > 0 and float('${tmod:-0}') < float('$(feld uhd mtexel)') else 1)" \
	|| { echo "FEHLER mod: nicht weniger Texel als UHD ohne Mod"; fehler=$((fehler + 1)); }
t0=$(feld ohne mtexel); tsd=$(feld sd mtexel); thd=$(feld hd mtexel); tuhd=$(feld uhd mtexel); tfrei=$(feld frei mtexel)
m0=$(feld ohne mb); msd=$(feld sd mb); dhd=$(feld hd dxt)
a0=$(atlas ohne); asd=$(atlas sd); ahd=$(atlas hd); auhd=$(atlas uhd); afrei=$(atlas frei)
echo "Texel (Millionen): ohne Paket $t0, SD $tsd, HD $thd, UHD $tuhd, freier Inhalt SD $tfrei; MB ohne $m0, SD $msd; DXT-Texturen in HD: $dhd"
echo "Kachelfaktor Gelaende: ohne $a0, SD $asd, HD $ahd, UHD $auhd, freier Inhalt $afrei"
python3 - "$t0" "$tsd" "$thd" "$tuhd" "$m0" "$msd" "$dhd" "$a0" "$asd" "$ahd" "$auhd" "$tfrei" "$afrei" <<'EOF' || fehler=$((fehler + 1))
import sys
try:
    t0, tsd, thd, tuhd, m0, msd, dhd, a0, asd, ahd, auhd, tfrei, afrei = (float(x) for x in sys.argv[1:14])
except ValueError:
    print("FEHLER: TEXSTATS fehlt"); sys.exit(1)
ok = abs(tsd - t0) < 0.051 and thd > tsd and tuhd > thd and dhd >= 1
# SD mit den Originalen: so viel Speicher wie ohne Paket
ok = ok and abs(msd - m0) < 0.051
ok = ok and asd == a0 == 1 and ahd == 2 and auhd == 4
# freier Inhalt in UHD-Groesse mit Manifest: in SD auf Originalgroesse
ok = ok and abs(tfrei - t0) < 0.051 and afrei == 1
print("Ergebnis: " + ("passt" if ok else "FEHLER (SD wie ohne Paket, HD und UHD groesser, DDS in HD geladen, Gelaende 1/2/4, freier Inhalt in SD wie ohne Paket)"))
sys.exit(0 if ok else 1)
EOF
[ $fehler -eq 0 ] && echo "Texturpaket-Test bestanden" || echo "Texturpaket-Test FEHLGESCHLAGEN ($fehler) -- $DATA"
exit $fehler
