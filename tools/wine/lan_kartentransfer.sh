#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
#
# Map transfer in a LAN game under Wine, with the debug leak report: two instances of the debug build
# (tools/wine/build_game.sh --debug), the host picks a user map that only it has (<user data>\Maps\MeineKarte, with a
# map.str), the guest joins and receives the map at the start; both play 200 logic frames, leave through the game menu
# and quit. Afterwards the transfer must have arrived and both leak reports ("Leaked a block ...") must be empty.
#
#   tools/wine/lan_kartentransfer.sh <output folder>
#
# Each instance runs in its own network namespace (10.76.0.1 host, 10.76.0.2 guest) with its own Wine prefix and its
# own copy of a freshly generated data set. Needs root and iproute2. CNC_WINE_DIR selects the debug build (default
# ${TMPDIR:-/tmp}/cnc-wine-dbg). Exit code 0 if the transfer arrived and neither instance leaks, 1 otherwise.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
[ $# -ge 1 ] || { sed -n 6,15p "$0" | sed 's/^# \{0,1\}//'; exit 2; }
OUT=$1
BAU="${CNC_WINE_DIR:-${TMPDIR:-/tmp}/cnc-wine-dbg}"
export CNC_WINE_DIR="$BAU"
[ -f "$BAU/game/GZHNeoZH.exe" ] || { echo "kein Debug-Bau in $BAU (tools/wine/build_game.sh --debug)"; exit 2; }
command -v ip >/dev/null || { echo "ip fehlt (Paket iproute2)"; exit 2; }
[ "$(id -u)" = 0 ] || { echo "braucht root (ip netns)"; exit 2; }
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"

aufraeumen() {
	ip netns del gzh-kt1 2>/dev/null
	ip netns del gzh-kt2 2>/dev/null
}
aufraeumen
trap aufraeumen EXIT
ip netns add gzh-kt1 && ip netns add gzh-kt2 || exit 2
ip link add gzh-k1 type veth peer name gzh-k2 || exit 2
for i in 1 2; do
	ip link set gzh-k$i netns gzh-kt$i
	ip -n gzh-kt$i addr add 10.76.0.$i/24 dev gzh-k$i
	ip -n gzh-kt$i link set gzh-k$i up
	ip -n gzh-kt$i link set lo up
done

DATA="$OUT/testdaten"
rm -rf "$DATA" && python3 "$HERE/testdaten/generate.py" "$DATA" > /dev/null || { echo "Generator fehlgeschlagen"; exit 2; }

# The host opens the game with the user map (the only map besides the data set's, which the lobby takes as its default;
# checked below in the host's log); it waits for the guest, who accepts right after joining.
host_ablauf() {
	printf 'hauptmenue\r\nklick MainMenu.wnd:ButtonMultiplayer 3000\r\nklick MainMenu.wnd:ButtonNetwork 3000\r\n'
	printf 'fenster LanLobbyMenu.wnd:ButtonHost 3000\r\nwarte 60\r\nklick LanLobbyMenu.wnd:ButtonHost\r\n'
	printf 'fenster LanGameOptionsMenu.wnd:ButtonStart 6000\r\nwarte 3200\r\nklick LanGameOptionsMenu.wnd:ButtonStart 9000\r\n'
	printf 'partie 2000000\r\nlog SPIEL BEGINNT\r\nlogik 200\r\n'
	printf 'spielmenue\r\nklick QuitNoSave.wnd:ButtonExit 3000\r\nklick QuitMessageBox.wnd:ButtonYes 3000\r\n'
	printf 'keinePartie 2000000\r\nbeenden\r\n'
}
gast_ablauf() {
	printf 'hauptmenue\r\nklick MainMenu.wnd:ButtonMultiplayer 3000\r\nklick MainMenu.wnd:ButtonNetwork 3000\r\n'
	printf 'fenster LanLobbyMenu.wnd:ListboxGames 3000\r\nwarte 900\r\nauswahl LanLobbyMenu.wnd:ListboxGames 0 6000\r\n'
	printf 'klick LanLobbyMenu.wnd:ButtonJoin 3000\r\nfenster LanGameOptionsMenu.wnd:TextEntryChat 6000\r\nwarte 100\r\n'
	printf 'klick LanGameOptionsMenu.wnd:ButtonStart 9000\r\npartie 2000000\r\nlog SPIEL BEGINNT\r\nlogik 200\r\n'
	printf 'spielmenue\r\nklick QuitNoSave.wnd:ButtonExit 3000\r\nklick QuitMessageBox.wnd:ButtonYes 3000\r\n'
	printf 'keinePartie 2000000\r\nbeenden\r\n'
}

for i in 1 2; do
	D="$OUT/daten$i"
	rm -rf "$D" && cp -r "$DATA" "$D"
	rm -f "$D/GZHNeoZH.exe" "$D/Shaders" "$D/ProjectData"
	U="$BAU/wineprefix-kt$i/drive_c/users/${WINEUSER_CNC:-$(id -un)}/Documents/GZH Neo/Zero Hour"
	rm -rf "$U/Maps" && mkdir -p "$U"
	printf '[Lan]\r\nUserName = Spieler%d\r\n' "$i" > "$U/State.ini"
	if [ $i = 1 ]; then
		mkdir -p "$U/Maps/MeineKarte"
		cp "$D/Maps/TestVersus/TestVersus.map" "$U/Maps/MeineKarte/MeineKarte.map"
		printf 'MAP:TestVersus\r\n"Meine Karte"\r\nEND\r\n' > "$U/Maps/MeineKarte/map.str"
		[ -f "$D/Maps/TestVersus/TestVersus.tga" ] && cp "$D/Maps/TestVersus/TestVersus.tga" "$U/Maps/MeineKarte/MeineKarte.tga"
	fi
done
host_ablauf > "$OUT/daten1/ablauf.txt"
gast_ablauf > "$OUT/daten2/ablauf.txt"
for i in 1 2; do
	ip netns exec gzh-kt$i env WINEPREFIX_CNC="$BAU/wineprefix-kt$i" "$HERE/run_game.sh" --menue --zeit 3000 \
		"$OUT/daten$i" -einstellungenNurLesen -mehrfach -d3d11Effects none -crcLog crc.log -testablauf ablauf.txt \
		> "$OUT/lauf$i.out" 2>&1 &
	eval "P$i=\$!"
	[ $i = 1 ] && sleep 20
done
wait "$P1"; RC1=$?
wait "$P2"; RC2=$?

fehler=0
LH=$(ls "$OUT/daten1"/*DebugLogFile*.txt 2>/dev/null | head -1)
if [ -n "$LH" ] && ! grep -aiq "setMap() - Looking for .*maps.meinekarte" "$LH"; then
	echo "Host: die Benutzerkarte war nicht gewaehlt"
	fehler=$((fehler + 1))
fi
# the guest creates the folders in the case of the transferred names (lower case)
GM=$(find "$BAU/wineprefix-kt2/drive_c/users" -ipath "*/maps/meinekarte/meinekarte.map" 2>/dev/null | head -1)
if [ -n "$GM" ] && cmp -s "$GM" "$OUT/daten1/Maps/TestVersus/TestVersus.map"; then
	echo "Kartentransfer: angekommen ($(ls "$(dirname "$GM")" | tr '\n' ' '))"
else
	echo "Kartentransfer: FEHLT"
	fehler=$((fehler + 1))
fi
for i in 1 2; do
	rc=RC$i
	L=$(ls "$OUT/daten$i"/*DebugLogFile*.txt 2>/dev/null | head -1)
	name=$([ $i = 1 ] && echo Host || echo Gast)
	if [ -z "$L" ]; then
		echo "$name: kein Debug-Protokoll (Exit ${!rc})"
		fehler=$((fehler + 1))
		continue
	fi
	n=$(grep -ac "Leaked a block" "$L")
	e=$(grep -a "TESTABLAUF ERROR" "$OUT/daten$i/crc.log" 2>/dev/null | head -1)
	echo "$name: Exit ${!rc}, $n Lecks${e:+, $e}"
	grep -a "Leaked a block" "$L" | head -10 | sed 's/^/  /'
	[ "$n" -eq 0 ] && [ "${!rc}" -eq 0 ] && [ -z "$e" ] || fehler=$((fehler + 1))
done
[ $fehler -eq 0 ] && { echo "Kartentransfer ohne Lecks"; exit 0; }
echo "Kartentransfer: $fehler Fehler"
exit 1
