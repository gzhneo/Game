#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
#
# Dateisystem unter Wine: die Aufloesung des Spiels (Engine::Vfs::Index) gegen VfsDump, das die Vorrangregel
# nachbildet.
#
# Aufruf:  tools/wine/dateisystem_test.sh
#   Ablage des Baus: CNC_WINE_DIR (Standard ${TMPDIR:-/tmp}/cnc-wine), Bau mit tools/wine/build_game.sh
#
# Testdaten aus tools/wine/testdaten/generate.py (als Archiv), dazu die Archive aus tools/dateisystem/beispiel.py
# (jede Eigenheit der Vorrangregel: Sortierung, Ordner mit Punkt, ZH_Generals, beschaedigtes Archiv, Paketarchive
# mit abgewiesenem und manifestlosem Archiv, alter Stufenordner, Mod-Ordner mit losen Dateien und eigenem Paket, ZIP
# und beschaedigtem ZIP, Mod als einzelnes ZIP), das Texturpaket aus einer Stufe UHD der Testdaten
# (texturpaket_stufe.py, build_texture_package.py) und ein Paket freien Inhalts unter ProjectData\Inhalt.
#
# Je Fall ein Start mit -dateiIndexAusgeben; die Liste muss der von VfsDump gleichen (ohne freien Inhalt, den VfsDump
# nicht kennt):
#   sd              ohne Mod: kein Paket
#   hd_mod          -textureQuality hd, -mod <Mod-Ordner> (mit eigenem Paket)
#   sd_zip          -mod <einzel.zip>
#   sd_mod          SD mit Mod-Ordner: sein Paket bleibt zu
#   zwei            -mod <ZIP> -mod <Ordner>, zwei_umgekehrt dieselben andersherum: der Ordner oben
#   drei            UHD, -mod <Ordner> -mod <ZIP> -mod <Ordner 2>: Ordner 2 oben, sein Paket samt abgewiesenem Archiv
#   ohne_paket      -textureQuality hd ohne Paket des Spiels: nur SD, auch das Paket des Mods bleibt zu
#   liste           [Mods] Active = mod;fehlt;einzel.zip in Settings.ini: genau diese Reihenfolge, "fehlt" gemeldet
#   liste_umgekehrt dieselben andersherum (anders als bei -mod liegt dann das Archiv oben)
#   liste_befehlszeile  Liste in Settings.ini und -mod: nur -mod gilt
# Dazu uhd_paket (-textureQuality uhd mit Texturpaket, Mod-Ordner und freiem Inhalt): Paket und freier Inhalt mit
# Manifest geladen, die Dateien des freien Inhalts aus ProjectData\Inhalt, sein alter Stufenordner nicht gelesen.
# Rueckgabewert 0 = bestanden.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${CNC_WINE_DIR:-${TMPDIR:-/tmp}/cnc-wine}"
while [ $# -gt 0 ]; do
	case "$1" in
		-h|--help) sed -n 5,25p "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "unbekannte Option $1"; exit 2 ;;
	esac
	shift
done
DATA="$OUT/testdata-dateisystem"
BSP="$OUT/dateisystem-beispiel"
rm -rf "$DATA" "$BSP" && mkdir -p "$DATA" "$BSP"
python3 "$HERE/testdaten/generate.py" "$DATA" --big TestDataZH.big > /dev/null || { echo "Generator fehlgeschlagen"; exit 1; }
python3 "$ROOT/tools/dateisystem/beispiel.py" "$BSP" || exit 1
# Paket aus einer Stufe UHD der Testdaten vor den Beispielarchiven (die Werkzeuge lesen kein beschaedigtes Archiv); die
# Stufe selbst ist nur Eingabe des Werkzeugs, das Spiel liest sie nicht
python3 "$HERE/texturpaket_stufe.py" "$DATA" > /dev/null || exit 1
python3 "$ROOT/tools/assets/build_texture_package.py" --dir "$DATA" > /dev/null || { echo "Paket nicht gebaut"; exit 1; }
cp -r "$BSP/spiel/." "$DATA/"
# freier Inhalt: ProjectData als eigener Ordner mit Verknuepfungen auf den Bau und einem Testpaket unter Inhalt mit
# Manifest; eine Datei neu, eine ueber dem Paket (gzhtest_h.dds aus Texturstufen\Paket\gzh_a.big), eine im Manifest,
# dazu ein alter Stufenordner, der nicht gelesen wird
mkdir -p "$DATA/ProjectData"
for n in "$OUT/game/ProjectData/"*; do ln -s "$n" "$DATA/ProjectData/$(basename "$n")"; done
FREI="$DATA/ProjectData/Inhalt/TestFrei"
mkdir -p "$FREI/Art/Textures" "$FREI/Texturstufen/UHD/Art/Textures"
printf 'frei' > "$FREI/Art/Textures/gzhtest_frei.dds"
printf 'frei' > "$FREI/Art/Textures/gzhtest_h.dds"
printf 'frei' > "$FREI/Art/Textures/gzhtest_u.dds"
printf 'TEXTURPAKET 1 UHD\r\nArt\\Textures\\gzhtest_u.dds 4 4\r\n' > "$FREI/texturpaket.txt"
printf 'alt' > "$FREI/Texturstufen/UHD/Art/Textures/gzhtest_alt.dds"
printf 'Testpaket' > "$FREI/LIESMICH.md"
# zweiter Mod-Ordner: eine lose Datei, die auch die anderen Mods haben, und ein eigenes Paket mit einem Texturarchiv und
# einem Archiv mit fremdem Inhalt (abgewiesen)
MOD2="$BSP/mod2"
mkdir -p "$MOD2/GzhTest" "$MOD2/Texturstufen/Paket"
printf 'mod2' > "$MOD2/GzhTest/b.txt"
python3 - "$MOD2/Texturstufen/Paket" <<'PY'
import sys, zipfile
manifest = 'TEXTURPAKET 1 UHD\r\nArt\\Textures\\gzhtest_t.dds 4 4\r\n'
with zipfile.ZipFile(sys.argv[1] + '/m2.zip', 'w') as z:
    z.writestr('texturpaket.txt', manifest)
    z.writestr('Art/Textures/gzhtest_t.dds', 'mod2 paket')
with zipfile.ZipFile(sys.argv[1] + '/schlecht.zip', 'w') as z:
    z.writestr('texturpaket.txt', manifest)
    z.writestr('Art/Textures/gzhtest_h.dds', 'x')
    z.writestr('Data/INI/x.ini', 'x')
PY
WINMOD="Z:$(printf '%s' "$BSP/mod" | tr / '\\')"
WINMOD2="Z:$(printf '%s' "$MOD2" | tr / '\\')"
WINZIP="Z:$(printf '%s' "$BSP/einzel.zip" | tr / '\\')"

VFSDUMP="$OUT/VfsDump"
g++ -std=c++20 -O1 -I "$ROOT/Code/Core/EngineCore" -I "$ROOT/Code/GameEngine" -o "$VFSDUMP" \
	"$ROOT/Code/Tools/Diagnostics/VfsDump/main.cpp" "$ROOT/Code/Core/EngineCore/Engine/BigArchive.cpp" \
	"$ROOT/Code/Core/EngineCore/Engine/Vfs.cpp" "$ROOT/Code/Core/EngineCore/Engine/ZipArchive.cpp" -lz \
	|| { echo "VfsDump uebersetzt nicht"; exit 1; }

fehler=0
start() {	# start <Name> <Spielschalter ...>
	local name=$1; shift
	rm -f "$DATA/$name.idx" "$DATA/run_$name.log"
	"$HERE/run_game.sh" --zeit 600 "$DATA" -quitAfter 5 -dateiIndexAusgeben "$name.idx" -crcLog "run_$name.log" "$@" \
		> "$DATA/wine_$name.txt" 2>&1
	local rc=$?
	[ $rc -eq 0 ] || { echo "FEHLER $name: Rueckgabewert $rc ($DATA/wine_$name.txt)"; fehler=$((fehler + 1)); }
	[ -s "$DATA/$name.idx" ] || { echo "FEHLER $name: Indexabzug fehlt"; fehler=$((fehler + 1)); return 1; }
}
gegen_vfsdump() {	# gegen_vfsdump <Name> <Stufe> <VfsDump-Schalter ...> -- <Spielschalter ...>
	local name=$1 stufe=$2; shift 2
	local vfs=()
	while [ $# -gt 0 ] && [ "$1" != "--" ]; do vfs+=("$1"); shift; done
	shift
	start "$name" -textureQuality "$stufe" -freeContent ea "$@" || return
	"$VFSDUMP" "$DATA" --zh --tier "$stufe" "${vfs[@]}" > "$DATA/$name.vfs" 2> "$DATA/$name.vfs.err"
	local anders
	anders=$(python3 "$ROOT/tools/dateisystem/vergleichen.py" "$DATA/$name.idx" "$DATA/$name.vfs" 2>&1)
	if [ $? -ne 0 ]; then
		echo "FEHLER $name: Spiel und VfsDump verschieden"; echo "$anders" | head -20; fehler=$((fehler + 1))
	else
		echo "$name: $(wc -l < "$DATA/$name.idx") Pfade wie VfsDump"
	fi
}

gegen_vfsdump sd sd -- 
gegen_vfsdump hd_mod hd --mod "$BSP/mod=$WINMOD" -- -mod "$WINMOD"
# kaputtes ZIP im Mod-Ordner: als Problem des Mods gemeldet (Hinweis im Hauptmenue)
if [ -s "$DATA/hd_mod.idx" ]; then
	grep -aq "^MOD problem mod unreadable 0 Kaputt.zip: not a ZIP archive" "$DATA/run_hd_mod.log" || { echo "FEHLER hd_mod: Zeile MOD problem fuer Kaputt.zip fehlt"; fehler=$((fehler + 1)); }
fi
gegen_vfsdump sd_zip sd --mod "$BSP/einzel.zip=$WINZIP" -- -mod "$WINZIP"
# das Paket eines Mods erst ab HD: in SD nicht
gegen_vfsdump sd_mod sd --mod "$BSP/mod=$WINMOD" -- -mod "$WINMOD"
# zwei Mods, Ordner und Archiv: der Ordner oben, gleich in welcher Reihenfolge
gegen_vfsdump zwei hd --mod "$BSP/einzel.zip=$WINZIP" --mod "$BSP/mod=$WINMOD" -- -mod "$WINZIP" -mod "$WINMOD"
gegen_vfsdump zwei_umgekehrt hd --mod "$BSP/mod=$WINMOD" --mod "$BSP/einzel.zip=$WINZIP" -- -mod "$WINMOD" -mod "$WINZIP"
# drei Mods: der zuletzt genannte oben, samt seinem Paket
gegen_vfsdump drei uhd --mod "$BSP/mod=$WINMOD" --mod "$BSP/einzel.zip=$WINZIP" --mod "$MOD2=$WINMOD2" \
	-- -mod "$WINMOD" -mod "$WINZIP" -mod "$WINMOD2"
if [ -s "$DATA/drei.idx" ]; then
	grep -q "^gzhtest/b.txt	Z:.*mod2.GzhTest.b.txt" "$DATA/drei.idx" || { echo "FEHLER drei: b.txt nicht aus mod2"; fehler=$((fehler + 1)); }
	grep -q "^art/textures/gzhtest_t.dds	Z:.*mod2.Texturstufen.Paket.m2.zip" "$DATA/drei.idx" || { echo "FEHLER drei: gzhtest_t.dds nicht aus dem Paket des Mods"; fehler=$((fehler + 1)); }
	grep -aq "^MOD paket UHD .*mod2.* archive 1 abgewiesen 1" "$DATA/run_drei.log" || { echo "FEHLER drei: Zeile MOD paket fehlt"; fehler=$((fehler + 1)); }
fi
# ohne Paket des Spiels gibt es nur SD
mv "$DATA/Texturstufen/Paket" "$DATA/Paket.weg"
gegen_vfsdump ohne_paket hd --mod "$BSP/mod=$WINMOD" -- -mod "$WINMOD"
if [ -s "$DATA/ohne_paket.idx" ]; then
	grep -aq "^TEXTURESTUFE SD wunsch HD .*paket 0 " "$DATA/run_ohne_paket.log" || { echo "FEHLER ohne_paket: Zeile TEXTURESTUFE SD fehlt"; fehler=$((fehler + 1)); }
	! grep -aq "^MOD paket" "$DATA/run_ohne_paket.log" || { echo "FEHLER ohne_paket: Paket des Mods geladen"; fehler=$((fehler + 1)); }
fi
mv "$DATA/Paket.weg" "$DATA/Texturstufen/Paket"
if start uhd_paket -textureQuality uhd -mod "$WINMOD"; then
	grep -aq "^TEXTURPAKET UHD " "$DATA/run_uhd_paket.log" || { echo "FEHLER uhd_paket: Paket nicht geladen"; fehler=$((fehler + 1)); }
	grep -aq "^FREIER INHALT .*TestFrei.* dateien 3 stufe UHD manifest 1" "$DATA/run_uhd_paket.log" \
		|| { echo "FEHLER uhd_paket: freier Inhalt nicht geladen"; fehler=$((fehler + 1)); }
	for p in gzhtest_frei.dds gzhtest_h.dds gzhtest_u.dds; do
		grep -q "^art/textures/$p	ProjectData" "$DATA/uhd_paket.idx" || { echo "FEHLER uhd_paket: $p nicht aus dem freien Inhalt"; fehler=$((fehler + 1)); }
	done
	! grep -q "gzhtest_alt" "$DATA/uhd_paket.idx" || { echo "FEHLER uhd_paket: alter Stufenordner gelesen"; fehler=$((fehler + 1)); }
	grep -q "^art/textures/testwater.dds	Texturstufen\\\\Paket" "$DATA/uhd_paket.idx" || { echo "FEHLER uhd_paket: testwater.dds nicht aus dem Paket"; fehler=$((fehler + 1)); }
	echo "uhd_paket: $(wc -l < "$DATA/uhd_paket.idx") Pfade, Paket und freier Inhalt an ihrem Rang"
fi

# aktive Mods aus Settings.ini ([Mods] Active, Namen unter <Benutzerdaten>\Mods\, hoechster zuerst): genau in dieser
# Reihenfolge (VfsDump --exact), ein fehlender Name wird gemeldet und uebergangen; -mod ersetzt die Liste
USERDIR="${WINEPREFIX_CNC:-$OUT/wineprefix}/drive_c/users/${WINEUSER_CNC:-$(id -un)}/Documents/GZH Neo/Zero Hour"
WINUSERMODS="C:\\users\\${WINEUSER_CNC:-$(id -un)}\\Documents\\GZH Neo\\Zero Hour\\Mods"
rm -rf "$USERDIR/Mods" && mkdir -p "$USERDIR/Mods"
cp -r "$BSP/mod" "$USERDIR/Mods/mod" && cp "$BSP/einzel.zip" "$USERDIR/Mods/einzel.zip"
aktive_mods() {	# aktive_mods <Liste>: [Mods] Active in Settings.ini setzen (leer: Eintrag entfernen)
	python3 - "$USERDIR/Settings.ini" "$1" <<'PY'
import os, re, sys
p, liste = sys.argv[1], sys.argv[2]
b = open(p, 'rb').read() if os.path.exists(p) else b''
b = re.sub(rb'(?ims)^\[Mods\][^\[]*', b'', b)
if liste:
    b = b.rstrip(b'\r\n') + b'\r\n\r\n[Mods]\r\nActive = ' + liste.encode() + b'\r\n'
open(p, 'wb').write(b)
PY
}
aktive_mods "mod;fehlt;einzel.zip"
gegen_vfsdump liste hd --exact --mod "$BSP/einzel.zip=$WINUSERMODS\\einzel.zip" --mod "$BSP/mod=$WINUSERMODS\\mod" --
if [ -s "$DATA/liste.idx" ]; then
	for z in "MOD active einzel.zip ready" "MOD active fehlt missing" "MOD active mod ready" "MOD problem fehlt missing"; do
		grep -aq "^$z" "$DATA/run_liste.log" || { echo "FEHLER liste: Zeile $z fehlt"; fehler=$((fehler + 1)); }
	done
fi
aktive_mods "einzel.zip;mod"
gegen_vfsdump liste_umgekehrt hd --exact --mod "$BSP/mod=$WINUSERMODS\\mod" --mod "$BSP/einzel.zip=$WINUSERMODS\\einzel.zip" --
gegen_vfsdump liste_befehlszeile hd --mod "$BSP/mod=$WINMOD" -- -mod "$WINMOD"
if [ -s "$DATA/liste_befehlszeile.idx" ]; then
	! grep -aq "^MOD active" "$DATA/run_liste_befehlszeile.log" || { echo "FEHLER liste_befehlszeile: Liste trotz -mod gelesen"; fehler=$((fehler + 1)); }
fi
aktive_mods ""
rm -rf "$USERDIR/Mods"

[ $fehler -eq 0 ] && echo "Dateisystem-Test bestanden" || echo "Dateisystem-Test FEHLGESCHLAGEN ($fehler) -- $DATA"
exit $fehler
