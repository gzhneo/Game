#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Pixelgleich-Nachweis unter Wine: mehrere Bildserien (tools/vulkan/bildserie_wine.sh) mit
# Profilen, die zusammen jeden Render-Pfad des Backends beruehren, gegen eine Referenz vergleichen.
#
# Aufruf:  tools/vulkan/bildvergleich.sh referenz <Datenordner> <Ablage>     Referenzbilder erzeugen (Stand vorher)
#          tools/vulkan/bildvergleich.sh pruefen  <Datenordner> <Ablage>     neue Serien, Vergleich mit Toleranz 0
#          tools/vulkan/bildvergleich.sh vulkan   <Datenordner> <Ablage>     Serien mit -vulkan gegen die (D3D11-)Referenz,
#                                                                           Standardtoleranz von compare_shots.py
#          weitere Schalter nach der Ablage gelten fuer alle Profile
# Ablage: <Ablage>/referenz/<Profil>, <Ablage>/neu/<Profil>, <Ablage>/vulkan/<Profil>. CNC_WINE_DIR wie bei
# tools/wine/run_game.sh.
# Rueckgabewert 0: alle Bilder aller Profile pixelgleich bzw. innerhalb der Toleranz (bzw. Referenz vollstaendig erzeugt).
# Zwei Renderer rastern Kanten nicht bitgleich; die Vulkan-Toleranz (PSNR >= 40 dB, hoechstens 0,1 % Pixel mit einer
# Kanalabweichung ueber 8) faengt Einzelpixel auf, keine fehlenden oder falsch gefaerbten Flaechen.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
ART="${1:?Aufruf: bildvergleich.sh referenz|pruefen <Datenordner> <Ablage> [Schalter ...]}"
DATEN="${2:?Datenordner fehlt}"
ABLAGE="${3:?Ablage fehlt}"
shift 3
TOLERANZ=(--threshold 0 --max-diff-ratio 0 --min-psnr 0)
case "$ART" in
	referenz) ZIEL="$ABLAGE/referenz" ;;
	pruefen) ZIEL="$ABLAGE/neu" ;;
	vulkan) ZIEL="$ABLAGE/vulkan"; TOLERANZ=(); set -- -vulkan "$@" ;;
	*) echo "referenz, pruefen oder vulkan"; exit 2 ;;
esac
mkdir -p "$ZIEL" || exit 2

# Profil: Name und Schalter. Zusammen: ohne Effekte; alle Effekte samt Shadow Map, Bloom, Hitze, Dunst, Lichtern und den
# Gelaende-Ebenen samt Tessellation; Multisampling; interne Aufloesung mit Hochskalieren; die CPU-Wege fuer Skinning,
# Partikel und Meshes ohne Instancing.
PROFILE=(
	"ohne|-effects none"
	"alle|-effects all,shadowmap,shadowterrain,bloom,heat,haze,lights,particlelight,particleground,terraindetail,terrainrock,terrainnormal,terraintess,rocktess"
	"msaa|-effects fxaa,sharpen,softparticles,ssao,colorgrade,shadowmap,bloom,msaa=4"
	"skaliert|-effects all,shadowmap -renderScale 0.75 -renderScaleFilter bicubic"
	"cpu|-effects all -cpuSkinning -cpuParticles -noMeshInstancing"
)
fails=0
for p in "${PROFILE[@]}"; do
	name="${p%%|*}"
	# shellcheck disable=SC2206
	schalter=(${p#*|})
	if ! "$HERE/bildserie_wine.sh" "$DATEN" "$ZIEL/$name" "${schalter[@]}" "$@" > "$ZIEL.$name.log" 2>&1; then
		echo "FEHLER $name: Lauf gescheitert ($ZIEL.$name.log)"; fails=$((fails + 1)); continue
	fi
	if [ "$ART" != referenz ]; then
		if python3 "$ROOT/tools/render/compare_shots.py" "$ABLAGE/referenz/$name" "$ZIEL/$name" "${TOLERANZ[@]}" \
			> "$ZIEL.$name.vergleich" 2>&1; then
			if [ "$ART" = pruefen ]; then echo "OK     $name: $(ls "$ZIEL/$name"/*.bmp | wc -l) Bilder pixelgleich"
			else echo "OK     $name: $(ls "$ZIEL/$name"/*.bmp | wc -l) Bilder in der Toleranz"; grep "PSNR" "$ZIEL.$name.vergleich" | head -4; fi
		else
			echo "FEHLER $name: Bilder weichen ab ($ZIEL.$name.vergleich)"; grep -v "^OK" "$ZIEL.$name.vergleich" | head -4
			fails=$((fails + 1))
		fi
	else
		echo "OK     $name: $(ls "$ZIEL/$name"/*.bmp | wc -l) Referenzbilder"
	fi
done
[ "$fails" = 0 ]
