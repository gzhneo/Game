#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
#
# Leaving a LAN game under Wine: two instances, the guest leaves through the game menu while the host keeps giving
# orders (it queues dozers at its command center), both log the logic CRC of every frame (-crcLog). Afterwards the
# CRC histories are compared frame by frame: the frames both logged must match. Driven by test sequences
# (-testablauf), not by mouse clicks.
#
#   tools/wine/lan_austritt.sh [--emulator] <output folder> [rounds]
#
# Each instance runs in its own network namespace (10.77.0.1 host, 10.77.0.2 guest, as in lan_zwei.sh), with its own
# Wine prefix and its own copy of a freshly generated data set. Needs root and iproute2. CNC_WINE_DIR selects the
# build (tools/wine/build_game.sh). --emulator runs both instances with the game's network emulator (latency 80 ms,
# jitter 20 ms, 2 % loss, as zuschauen_test.py; AUSTRITT_EMULATOR replaces these switches), so that orders of the
# host can arrive late. rounds (default 1)
# repeats the game; the result per round is in
# <output folder>/rundeN/ergebnis.txt, the exit code is 0 if no round shows a difference.
set -u
EMU=()
[ "${1:-}" = --emulator ] && { read -r -a EMU <<< "${AUSTRITT_EMULATOR:--netzLatenz 80 -netzSchwankung 20 -netzVerlust 2}"; shift; }
OUT=$1; RUNDEN=${2:-1}
HERE="$(cd "$(dirname "$0")" && pwd)"
command -v ip >/dev/null || { echo "ip fehlt (Paket iproute2)"; exit 2; }
[ "$(id -u)" = 0 ] || { echo "braucht root (ip netns)"; exit 2; }
BAU="${CNC_WINE_DIR:-${TMPDIR:-/tmp}/cnc-wine}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"

aufraeumen() {
	ip netns del gzh-aus1 2>/dev/null
	ip netns del gzh-aus2 2>/dev/null
}
aufraeumen
trap aufraeumen EXIT
ip netns add gzh-aus1 && ip netns add gzh-aus2 || exit 1
ip link add gzh-a1 type veth peer name gzh-a2 || exit 1
for i in 1 2; do
	ip link set gzh-a$i netns gzh-aus$i
	ip -n gzh-aus$i addr add 10.77.0.$i/24 dev gzh-a$i
	ip -n gzh-aus$i link set gzh-a$i up
	ip -n gzh-aus$i link set lo up
done

DATA="$OUT/testdaten"
rm -rf "$DATA" && python3 "$HERE/testdaten/generate.py" "$DATA" > /dev/null || { echo "Generator fehlgeschlagen"; exit 1; }

# The host waits until the guest has joined and accepted (alone it would start at once; the guest accepts right after
# joining), then starts.
# In the game the host queues a dozer in every logic frame from 205 to 213 (the command center is selected at the start;
# a queued unit changes the game state at once). The guest leaves at frame 200 through the game menu; its leaving is
# executed some 45 frames later (run-ahead), the host's orders some 25 frames after the click, so they land in the
# frames right after the guest's leaving -- the frames a leaver runs alone before it clears the game.
host_ablauf() {
	printf 'hauptmenue\r\nklick MainMenu.wnd:ButtonMultiplayer 3000\r\nklick MainMenu.wnd:ButtonNetwork 3000\r\n'
	printf 'fenster LanLobbyMenu.wnd:ButtonHost 3000\r\nwarte 60\r\nklick LanLobbyMenu.wnd:ButtonHost\r\n'
	printf 'fenster LanGameOptionsMenu.wnd:ButtonStart 6000\r\nwarte 3200\r\nklick LanGameOptionsMenu.wnd:ButtonStart 9000\r\n'
	printf 'partie 2000000\r\nlog SPIEL BEGINNT\r\nlogik 205\r\n'
	for _ in $(seq 1 9); do
		printf 'klickFalls ControlBar.wnd:ButtonCommand01 30\r\nlogik 1\r\n'
	done
	printf 'logik 200\r\nspielmenue\r\nklick QuitNoSave.wnd:ButtonExit 3000\r\nklick QuitMessageBox.wnd:ButtonYes 3000\r\n'
	printf 'keinePartie 2000000\r\nbeenden\r\n'
}
gast_ablauf() {
	printf 'hauptmenue\r\nklick MainMenu.wnd:ButtonMultiplayer 3000\r\nklick MainMenu.wnd:ButtonNetwork 3000\r\n'
	printf 'fenster LanLobbyMenu.wnd:ListboxGames 3000\r\nwarte 600\r\nauswahl LanLobbyMenu.wnd:ListboxGames 0 6000\r\n'
	printf 'klick LanLobbyMenu.wnd:ButtonJoin 3000\r\nfenster LanGameOptionsMenu.wnd:TextEntryChat 6000\r\nwarte 100\r\n'
	printf 'klick LanGameOptionsMenu.wnd:ButtonStart 9000\r\npartie 2000000\r\nlog SPIEL BEGINNT\r\nlogik 200\r\n'
	printf 'spielmenue\r\nklick QuitNoSave.wnd:ButtonExit 3000\r\nklick QuitMessageBox.wnd:ButtonYes 3000\r\n'
	printf 'keinePartie 2000000\r\nbeenden\r\n'
}

fehler=0
for runde in $(seq 1 "$RUNDEN"); do
	R="$OUT/runde$runde"
	rm -rf "$R" && mkdir -p "$R"
	for i in 1 2; do
		D="$R/daten$i"
		cp -r "$DATA" "$D"
		rm -f "$D/GZHNeoZH.exe" "$D/Shaders" "$D/ProjectData"
		U="$BAU/wineprefix-aus$i/drive_c/users/${WINEUSER_CNC:-$(id -un)}/Documents/GZH Neo/Zero Hour"
		mkdir -p "$U"
		printf '[Lan]\r\nUserName = Spieler%d\r\n' "$i" > "$U/State.ini"
	done
	host_ablauf > "$R/daten1/ablauf.txt"
	gast_ablauf > "$R/daten2/ablauf.txt"
	# two software-rendered instances share the cores; without the D3D11 effects both keep up with each other
	for i in 1 2; do
		ip netns exec gzh-aus$i env WINEPREFIX_CNC="$BAU/wineprefix-aus$i" "$HERE/run_game.sh" --menue --zeit 3000 \
			"$R/daten$i" -einstellungenNurLesen -d3d11Effects none -crcLog crc.log -crcLogInterval 1 \
			-testablauf ablauf.txt "${EMU[@]}" > "$R/lauf$i.out" 2>&1 &
		eval "P$i=\$!"
		[ $i = 1 ] && sleep 20
	done
	wait "$P1" "$P2"
	python3 - "$R" <<'PYEOF' || fehler=$((fehler + 1))
import os, sys
r = sys.argv[1]

def verlauf(i):
    werte, ablauf = {}, []
    p = os.path.join(r, 'daten%d' % i, 'crc.log')
    if os.path.exists(p):
        for z in open(p, encoding='latin-1', errors='replace'):
            t = z.split()
            if len(t) == 2 and t[0].isdigit() and len(t[1]) == 8:
                werte[int(t[0])] = t[1]
            elif z.startswith('TESTABLAUF'):
                ablauf.append(z.rstrip())
    return werte, ablauf

(h, ah), (g, ag) = verlauf(1), verlauf(2)
gemeinsam = sorted(set(h) & set(g))
falsch = [f for f in gemeinsam if h[f] != g[f]]
fehlerzeilen = [z for z in ah + ag if 'TESTABLAUF ERROR' in z]
text = ['Host: %d Frames (bis %s), Gast: %d Frames (bis %s)' % (len(h), max(h) if h else '-', len(g), max(g) if g else '-'),
        'gemeinsame Frames: %d, abweichend: %d%s' % (len(gemeinsam), len(falsch), (' (%s)' % ', '.join(map(str, falsch[:10]))) if falsch else ''),
        'Testablauf-Fehler: %d%s' % (len(fehlerzeilen), (' (%s)' % fehlerzeilen[0]) if fehlerzeilen else '')]
ok = bool(gemeinsam) and not falsch and not fehlerzeilen
text.append('OK' if ok else 'FEHLER')
open(os.path.join(r, 'ergebnis.txt'), 'w').write('\n'.join(text) + '\n')
print('\n'.join(text))
sys.exit(0 if ok else 1)
PYEOF
done
[ $fehler -eq 0 ] && { echo "Austritt sauber ($RUNDEN Runden)"; exit 0; }
echo "Austritt mit Abweichungen: $fehler von $RUNDEN Runden"
exit 1
