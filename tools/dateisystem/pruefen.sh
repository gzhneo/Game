#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
#
# Dateisystem: VfsDump nativ bauen und auf dem Beispielordner (beispiel.py) gegen
# die von Hand hergeleiteten Soll-Listen der alten Vorrangregel prüfen.
#
#   tools/dateisystem/pruefen.sh [Arbeitsordner]
#
# Für den Vergleich mit dem Spiel (Wine oder Windows) dieselben Aufrufe wie unten mit
# -dateiIndexAusgeben im Spiel wiederholen; die Listen müssen gleich sein.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
WORK="${1:-${TMPDIR:-/tmp}/cnc-dateisystem}"
rm -rf "$WORK" && mkdir -p "$WORK"

g++ -std=c++20 -O1 -Wall -Wextra -I "$ROOT/Code/GameEngine" -I "$ROOT/Code/Core/EngineCore" -o "$WORK/VfsDump" \
	"$ROOT/Code/Tools/Diagnostics/VfsDump/main.cpp" "$ROOT/Code/Core/EngineCore/Engine/BigArchive.cpp" \
	"$ROOT/Code/Core/EngineCore/Engine/Vfs.cpp" "$ROOT/Code/Core/EngineCore/Engine/ZipArchive.cpp" -lz \
	|| { echo "VfsDump übersetzt nicht"; exit 1; }
python3 "$HERE/beispiel.py" "$WORK" || exit 1

fehler=0
fall() {	# fall <Name> <Soll> <VfsDump-Schalter ...>
	local name="$1" soll="$2"
	shift 2
	"$WORK/VfsDump" "$WORK/spiel" "$@" > "$WORK/$name.ist" 2> "$WORK/$name.err"
	printf "%b" "$soll" > "$WORK/$name.soll"
	if ! diff -u "$WORK/$name.soll" "$WORK/$name.ist" > "$WORK/$name.diff"; then
		echo "Abweichung im Fall $name:"; cat "$WORK/$name.diff"; fehler=1
	fi
}

T='\t'
# Zero Hour, HD, Mod-Ordner: Mod über dem Paket, darin sein eigenes Paket oben, dann das spätere Archiv
# (auch .zip), lose Dateien des Mods mit ihrem Pfad über allen seinen Archiven; Data\INI\INIZH.big gelöscht
fall zh_hd_mod "art/textures/gzhtest_h.dds${T}Texturstufen\\\\Paket\\\\gzh_a.big\nart/textures/gzhtest_t.dds${T}M:\\\\mod\\\\Texturstufen\\\\Paket\\\\mt.big\ngzhtest/a.txt${T}INIZH.big\ngzhtest/b.txt${T}M:\\\\mod\\\\m.big\ngzhtest/g.txt${T}ZH_Generals\\\\INI.big\ngzhtest/l.txt${T}M:\\\\mod\\\\GzhTest\\\\l.txt\ngzhtest/n1.txt${T}A1.big\ngzhtest/n2.txt${T}a\\\\b.big\ngzhtest/nur.txt${T}M:\\\\mod\\\\GzhTest\\\\nur.txt\ngzhtest/z.txt${T}M:\\\\mod\\\\z.zip\ntexturpaket.txt${T}M:\\\\mod\\\\Texturstufen\\\\Paket\\\\mt.big\n" \
	--zh --tier hd --mod "$WORK/mod=M:\\mod"
# Zero Hour, SD, Mod als einzelnes ZIP: über den Grundarchiven
fall zh_sd_zip "gzhtest/a.txt${T}INIZH.big\ngzhtest/b.txt${T}M:\\\\einzel.zip\ngzhtest/e.txt${T}M:\\\\einzel.zip\ngzhtest/g.txt${T}ZH_Generals\\\\INI.big\ngzhtest/n1.txt${T}A1.big\ngzhtest/n2.txt${T}a\\\\b.big\n" \
	--zh --tier sd --mod "$WORK/einzel.zip=M:\\einzel.zip"
# Generals, UHD, ohne Mod: im Paket das spätere Archiv oben, Data\INI\INIZH.big vor INIZH.big
fall gen_uhd "art/textures/gzhtest_h.dds${T}Texturstufen\\\\Paket\\\\gzh_a.big\nart/textures/gzhtest_t.dds${T}Texturstufen\\\\Paket\\\\gzh_b.big\ngzhtest/a.txt${T}Data\\\\INI\\\\INIZH.big\ngzhtest/b.txt${T}INIZH.big\ngzhtest/g.txt${T}ZH_Generals\\\\INI.big\ngzhtest/n1.txt${T}A1.big\ngzhtest/n2.txt${T}a\\\\b.big\ntexturpaket.txt${T}Texturstufen\\\\Paket\\\\gzh_b.big\n" \
	--tier uhd
# SD: kein Paket
fall sd "gzhtest/a.txt${T}Data\\\\INI\\\\INIZH.big\ngzhtest/b.txt${T}INIZH.big\ngzhtest/g.txt${T}ZH_Generals\\\\INI.big\ngzhtest/n1.txt${T}A1.big\ngzhtest/n2.txt${T}a\\\\b.big\n" \
	--tier sd

# HD ohne Paket des Spiels: nur SD, auch das Paket des Mods bleibt zu
mv "$WORK/spiel/Texturstufen/Paket" "$WORK/Paket.weg"
fall zh_hd_ohne_paket "art/textures/gzhtest_t.dds${T}M:\\\\mod\\\\m.big\ngzhtest/a.txt${T}INIZH.big\ngzhtest/b.txt${T}M:\\\\mod\\\\m.big\ngzhtest/g.txt${T}ZH_Generals\\\\INI.big\ngzhtest/l.txt${T}M:\\\\mod\\\\GzhTest\\\\l.txt\ngzhtest/n1.txt${T}A1.big\ngzhtest/n2.txt${T}a\\\\b.big\ngzhtest/nur.txt${T}M:\\\\mod\\\\GzhTest\\\\nur.txt\ngzhtest/z.txt${T}M:\\\\mod\\\\z.zip\n" \
	--zh --tier hd --mod "$WORK/mod=M:\\mod"
mv "$WORK/Paket.weg" "$WORK/spiel/Texturstufen/Paket"

grep -q "schlecht.big rejected" "$WORK/gen_uhd.err" || { echo "abgewiesenes Paketarchiv nicht gemeldet"; fehler=1; }
grep -q "ohne.big left out (no manifest)" "$WORK/gen_uhd.err" || { echo "Paketarchiv ohne Manifest nicht gemeldet"; fehler=1; }
grep -q "no texture package: tier SD" "$WORK/zh_hd_ohne_paket.err" || { echo "fehlendes Paket nicht gemeldet"; fehler=1; }
grep -q "Kaputt.big not used" "$WORK/sd.err" || { echo "beschädigtes Archiv nicht gemeldet"; fehler=1; }
grep -q "Kaputt.zip not used" "$WORK/zh_hd_mod.err" || { echo "beschädigtes ZIP nicht gemeldet"; fehler=1; }
[ $fehler = 0 ] && echo "5 Fälle wie die Vorrangregel, die Mod-Ränge und das Texturpaket"
exit $fehler
