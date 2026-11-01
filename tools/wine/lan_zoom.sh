#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
#
# Host's zoom-out limit in a LAN game under Wine: two instances, both with the local setting -maxZoom 1.25.
# The host picks a limit in the lobby (combo box ComboBoxMaxZoom, entry <index>: 0 = 100 %, 1 = 105 %, 2 = 110 %, ...),
# the guest joins and accepts. In the game both zoom out all the way (mouse wheel) and log the camera height; then the
# host's replay is played back with -maxZoom 1.25 and zoomed out the same way. Expected: every height is the normal
# maximum (310) times the host's limit (within 1 %: the replay's camera may stand over other terrain), the guest and the replay log "ZOOMLIMIT local 1.25 host <limit>", the replay header
# carries "ZM=<percent>;" (none for 100 %), and both CRC histories of the game match.
#
#   tools/wine/lan_zoom.sh <output folder> [index]
#
# Each instance runs in its own network namespace (10.79.0.1 host, 10.79.0.2 guest), with its own Wine prefix and its
# own copy of a freshly generated data set. Needs root and iproute2. CNC_WINE_DIR selects the build
# (tools/wine/build_game.sh). The result is in <output folder>/ergebnis.txt, exit code 0 if it is as expected.
set -u
OUT=$1; INDEX=${2:-2}
HERE="$(cd "$(dirname "$0")" && pwd)"
command -v ip >/dev/null || { echo "ip fehlt (Paket iproute2)"; exit 2; }
[ "$(id -u)" = 0 ] || { echo "braucht root (ip netns)"; exit 2; }
BAU="${CNC_WINE_DIR:-${TMPDIR:-/tmp}/cnc-wine}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"

aufraeumen() {
	ip netns del gzh-zoom1 2>/dev/null
	ip netns del gzh-zoom2 2>/dev/null
}
aufraeumen
trap aufraeumen EXIT
ip netns add gzh-zoom1 && ip netns add gzh-zoom2 || exit 1
ip link add gzh-z1 type veth peer name gzh-z2 || exit 1
for i in 1 2; do
	ip link set gzh-z$i netns gzh-zoom$i
	ip -n gzh-zoom$i addr add 10.79.0.$i/24 dev gzh-z$i
	ip -n gzh-zoom$i link set gzh-z$i up
	ip -n gzh-zoom$i link set lo up
done

DATA="$OUT/testdaten"
rm -rf "$DATA" && python3 "$HERE/testdaten/generate.py" "$DATA" > /dev/null || { echo "Generator fehlgeschlagen"; exit 1; }

# The host sets the limit before the guest joins (a change resets the guest's acceptance), waits for the guest and
# starts (the guest's choice is disabled, so the test sequence cannot list it there). Both zoom out by 40 wheel notches in the game (more than enough for the highest limit) and quit.
host_ablauf() {
	printf 'hauptmenue\r\nklick MainMenu.wnd:ButtonMultiplayer 3000\r\nklick MainMenu.wnd:ButtonNetwork 3000\r\n'
	printf 'fenster LanLobbyMenu.wnd:ButtonHost 3000\r\nwarte 60\r\nklick LanLobbyMenu.wnd:ButtonHost\r\n'
	printf 'fenster LanGameOptionsMenu.wnd:ButtonStart 6000\r\nliste LanGameOptionsMenu.wnd:ComboBoxMaxZoom\r\n'
	printf 'auswahl LanGameOptionsMenu.wnd:ComboBoxMaxZoom %d\r\nwarte 3200\r\n' "$INDEX"
	printf 'klick LanGameOptionsMenu.wnd:ButtonStart 9000\r\npartie 2000000\r\nlog SPIEL BEGINNT\r\nlogik 60\r\n'
	printf 'rad -40\r\nlogik 30\r\nspielmenue\r\nklick QuitNoSave.wnd:ButtonExit 3000\r\nklick QuitMessageBox.wnd:ButtonYes 3000\r\n'
	printf 'keinePartie 2000000\r\nbeenden\r\n'
}
gast_ablauf() {
	printf 'hauptmenue\r\nklick MainMenu.wnd:ButtonMultiplayer 3000\r\nklick MainMenu.wnd:ButtonNetwork 3000\r\n'
	printf 'fenster LanLobbyMenu.wnd:ListboxGames 3000\r\nwarte 600\r\nauswahl LanLobbyMenu.wnd:ListboxGames 0 6000\r\n'
	printf 'klick LanLobbyMenu.wnd:ButtonJoin 3000\r\nfenster LanGameOptionsMenu.wnd:TextEntryChat 6000\r\nwarte 100\r\n'
	printf 'klick LanGameOptionsMenu.wnd:ButtonStart 9000\r\npartie 2000000\r\nlog SPIEL BEGINNT\r\nlogik 60\r\n'
	printf 'rad -40\r\nlogik 30\r\nspielmenue\r\nklick QuitNoSave.wnd:ButtonExit 3000\r\nklick QuitMessageBox.wnd:ButtonYes 3000\r\n'
	printf 'keinePartie 2000000\r\nbeenden\r\n'
}
replay_ablauf() {
	printf 'partie 2000000\r\nlog REPLAY BEGINNT\r\nlogik 60\r\nrad -40\r\nlogik 10\r\nbeenden\r\n'
}

for i in 1 2; do
	D="$OUT/daten$i"
	rm -rf "$D" && cp -r "$DATA" "$D"
	rm -f "$D/GZHNeoZH.exe" "$D/Shaders" "$D/ProjectData"
	U="$BAU/wineprefix-zoom$i/drive_c/users/${WINEUSER_CNC:-$(id -un)}/Documents/GZH Neo/Zero Hour"
	mkdir -p "$U"
	rm -f "$U/Replays/00000000.rep"
	printf '[Lan]\r\nUserName = Spieler%d\r\n' "$i" > "$U/State.ini"
done
host_ablauf > "$OUT/daten1/ablauf.txt"
gast_ablauf > "$OUT/daten2/ablauf.txt"
for i in 1 2; do
	ip netns exec gzh-zoom$i env WINEPREFIX_CNC="$BAU/wineprefix-zoom$i" "$HERE/run_game.sh" --menue --zeit 3000 \
		"$OUT/daten$i" -einstellungenNurLesen -d3d11Effects none -maxZoom 1.25 -crcLog crc.log -crcLogInterval 1 \
		-testablauf ablauf.txt > "$OUT/lauf$i.out" 2>&1 &
	eval "P$i=\$!"
	[ $i = 1 ] && sleep 20
done
wait "$P1" "$P2"

REP="$BAU/wineprefix-zoom1/drive_c/users/${WINEUSER_CNC:-$(id -un)}/Documents/GZH Neo/Zero Hour/Replays/00000000.rep"
cp "$REP" "$OUT/partie.rep" 2>/dev/null
if [ -s "$OUT/partie.rep" ]; then
	replay_ablauf > "$OUT/daten1/replay_ablauf.txt"
	ip netns exec gzh-zoom1 env WINEPREFIX_CNC="$BAU/wineprefix-zoom1" "$HERE/run_game.sh" --zeit 1500 \
		"$OUT/daten1" -einstellungenNurLesen -d3d11Effects none -maxZoom 1.25 -replay 00000000.rep \
		-crcLog replay.log -testablauf replay_ablauf.txt > "$OUT/replay.out" 2>&1
fi

python3 - "$OUT" "$INDEX" <<'PYEOF'
import os, re, sys
out, index = sys.argv[1], int(sys.argv[2])
percent = [100, 105, 110, 115, 120, 125][index]

def lesen(p):
    return open(p, encoding='latin-1', errors='replace').read().splitlines() if os.path.exists(p) else []

def crc(z):
    werte = {}
    for l in z:
        t = l.split()
        if len(t) == 2 and t[0].isdigit() and len(t[1]) == 8:
            werte[int(t[0])] = t[1]
    return werte

def hoehen(z):
    return [float(m.group(1)) for l in z for m in [re.search(r'rad -40 zoom \S+ height \S+ current (\S+)', l)] if m]

host, gast, rep = lesen(os.path.join(out, 'daten1', 'crc.log')), lesen(os.path.join(out, 'daten2', 'crc.log')), \
    lesen(os.path.join(out, 'daten1', 'replay.log'))
h, g = crc(host), crc(gast)
gemeinsam = sorted(set(h) & set(g))
falsch = [f for f in gemeinsam if h[f] != g[f]]
fehler = [l for l in host + gast + rep if 'TESTABLAUF ERROR' in l]
kopf = open(os.path.join(out, 'partie.rep'), 'rb').read(4096).decode('latin-1') if os.path.exists(os.path.join(out, 'partie.rep')) else ''
zm = re.search(r'ZM=(\d+);', kopf)
text = ['Gastgeber-Wert: Eintrag %d = %d %%' % (index, percent),
        'Hoehe nach dem Herauszoomen: Gastgeber %s, Gast %s, Replay %s' % (hoehen(host), hoehen(gast), hoehen(rep)),
        'ZOOMLIMIT: Gastgeber %s | Gast %s | Replay %s' % tuple(
            ([l for l in z if l.startswith('ZOOMLIMIT')] or ['-'])[0] for z in (host, gast, rep)),
        'Replay-Kopf: %s' % (zm.group(0) if zm else ('kein ZM' if kopf else 'kein Replay')),
        'CRC: gemeinsame Frames %d, abweichend %d' % (len(gemeinsam), len(falsch)),
        'Testablauf-Fehler: %d%s' % (len(fehler), (' (%s)' % fehler[0]) if fehler else '')]
alle = hoehen(host) + hoehen(gast) + hoehen(rep)
soll = 310.0 * percent / 100.0
ok = (len(alle) == 3 and all(abs(x - soll) <= 0.01 * soll for x in alle) and bool(gemeinsam) and not falsch and not fehler and bool(kopf)
      and ((zm is None) if percent == 100 else (zm is not None and int(zm.group(1)) == percent)))
if percent < 125:
    ok = ok and all(any(l.startswith('ZOOMLIMIT') for l in z) for z in (host, gast, rep))
text.append('OK' if ok else 'FEHLER')
open(os.path.join(out, 'ergebnis.txt'), 'w').write('\n'.join(text) + '\n')
print('\n'.join(text))
sys.exit(0 if ok else 1)
PYEOF
