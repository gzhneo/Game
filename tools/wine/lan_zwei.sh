#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
#
# LAN game of two game instances under Wine on one Linux machine, driven by klicktest.sh.
#
#   tools/wine/lan_zwei.sh <data folder> <output folder>
#
# Each instance runs in its own network namespace (10.77.0.1 and 10.77.0.2, joined by a veth pair), with its
# own Wine prefix and its own copy of the data folder. Wine cannot tell several loopback addresses apart
# (-netzLokal), separate namespaces give real addresses and working broadcasts instead. Needs root and
# iproute2 (ip). Instance 1 hosts, instance 2 joins the first game in the list and accepts, instance 1 starts.
# Screenshots per step land in <output folder>/host and <output folder>/gast; at the end the two replays are
# compared (replay1.txt, replay2.txt). The waits assume the release
# build (both instances together run at a few frames per second).
set -u
DATA=$1; OUT=$2
HERE="$(cd "$(dirname "$0")" && pwd)"
command -v ip >/dev/null || { echo "ip fehlt (Paket iproute2)"; exit 2; }
[ "$(id -u)" = 0 ] || { echo "braucht root (ip netns)"; exit 2; }
BAU="${CNC_WINE_DIR:-${TMPDIR:-/tmp}/cnc-wine}"
mkdir -p "$OUT"

aufraeumen() {
	[ -n "${TP:-}" ] && kill "$TP" 2>/dev/null
	ip netns del gzh-lan1 2>/dev/null
	ip netns del gzh-lan2 2>/dev/null
}
aufraeumen
trap aufraeumen EXIT
ip netns add gzh-lan1 && ip netns add gzh-lan2 || exit 1
ip link add gzh-v1 type veth peer name gzh-v2 || exit 1
for i in 1 2; do
	ip link set gzh-v$i netns gzh-lan$i
	ip -n gzh-lan$i addr add 10.77.0.$i/24 dev gzh-v$i
	ip -n gzh-lan$i link set gzh-v$i up
	ip -n gzh-lan$i link set lo up
done

# own data copy (wine.log, exe link) and own prefix per instance. Player name in State.ini [Lan], mouse setup in
# Settings.ini [Controls]: the host orders with the left button (classic), the guest with the right (alternate).
# run_game.sh adds HardwareHint to an existing Settings.ini.
# LAN_HOST_DATEN: another data folder for the host (e.g. with a changed game INI: a different data checksum)
for i in 1 2; do
	rm -rf "$OUT/daten$i"
	if [ "$i" = 1 ] && [ -n "${LAN_HOST_DATEN:-}" ]; then cp -r "$LAN_HOST_DATEN" "$OUT/daten$i"; else cp -r "$DATA" "$OUT/daten$i"; fi
	rm -f "$OUT/daten$i/GZHNeoZH.exe" "$OUT/daten$i/Shaders" "$OUT/daten$i/ProjectData"
	U="$BAU/wineprefix-lan$i/drive_c/users/${WINEUSER_CNC:-$(id -un)}/Documents/GZH Neo/Zero Hour"
	mkdir -p "$U"
	python3 - "$U" "$i" <<'PYEOF'
import os, re, sys
d, i = sys.argv[1], int(sys.argv[2])

def setze(name, section, key, value):
    p = os.path.join(d, name)
    t = open(p, 'rb').read().decode('utf-8') if os.path.exists(p) else ''
    line = '%s = %s' % (key, value)
    m = re.search(r'(?m)^\[%s\][^\[]*' % section, t)
    if m and re.search(r'(?m)^%s[ \t]*=' % key, m.group(0)):
        block = re.sub(r'(?m)^%s[ \t]*=[^\r\n]*' % key, line, m.group(0))
        t = t[:m.start()] + block + t[m.end():]
    elif m:
        t = t[:m.start()] + m.group(0).replace('[%s]' % section, '[%s]\r\n%s' % (section, line), 1) + t[m.end():]
    else:
        t += '\r\n[%s]\r\n%s\r\n' % (section, line)
    open(p, 'wb').write(t.encode('utf-8'))

setze('State.ini', 'Lan', 'UserName', 'Spieler%d' % i)
setze('Settings.ini', 'Controls', 'UseAlternateMouse', 'yes' if i == 2 else 'no')
PYEOF
done

# Layout coordinates (800 x 600): main menu Multiplayer, LAN; lobby Host, first game, Join; setup Start/Accept;
# in the match the command center is selected: the host builds a dozer (button 1), the guest a collector (button 2).
# Then both select their rifleman (Q, select all; dozers and collectors are left out) and send it off: the host with
# a left click on the ground, the guest with a right click. Then each lets it force-attack its own command center
# (Ctrl held, the command center sits in the middle of the view). At the end both leave through the game menu (Esc,
# Exit, Yes): first the guest, then the host.
# LAN_HOST_PARTIE / LAN_GAST_PARTIE replace the steps in the match (host from its Start click, guest from Accept).
# LAN_KARTE=versus: the host picks the map Test Versus (second built-in map) before the guest accepts; the bases
# are in view of each other, and both send their rifleman against the other command center (host left click,
# guest right click). Start positions are picked on the map preview (host start 2 at the right, guest start 1 at
# the left), otherwise they are random and the other base is at the left or the right edge of the view.
MENUE="w60 400,300 w15 650,266 w20 650,214"
# LAN_MODUS=internet: the lobby in internet mode (main menu Multiplayer, Internet). A rendezvous server runs in the
# host's namespace (gzh-rendezvous, built with tools/rendezvous/build_linux.sh into the build folder); both games get
# -treffpunkt. The host lists its game publicly (checkbox in the setup), the guest finds it in the public list,
# which it asks the server for, so it looks a minute later.
HOST_LISTE=""; GAST_LOBBY=""; TREFFPUNKT=""
if [ "${LAN_MODUS:-}" = internet ]; then
	[ -x "$BAU/gzh-rendezvous" ] || "$HERE/../rendezvous/build_linux.sh" "$BAU/gzh-rendezvous" > "$OUT/rendezvous-bau.log" 2>&1 \
		|| { echo "Treffpunkt-Server nicht gebaut, siehe $OUT/rendezvous-bau.log"; exit 1; }
	ip netns exec gzh-lan1 "$BAU/gzh-rendezvous" --port 8090 --log-addresses --stats 30 > "$OUT/rendezvous.log" 2>&1 &
	TP=$!
	MENUE="w60 400,300 w15 650,266 w20 650,266"
	HOST_LISTE="m653,428 w3 653,428 w10 sliste"
	GAST_LOBBY="w60"
	TREFFPUNKT="-treffpunkt 10.77.0.1:8090"
fi
# changing the map resets the acceptances, so the guest accepts later
KARTE=""; GAST_WARTE=""
[ "${LAN_KARTE:-}" = versus ] && {
	KARTE="652,320 w20 222,143 w10 250,188 w10 skarte 576,484 w20 673,177 w10 sstart"; GAST_WARTE="w90 633,177 w10 sstart"
	LAN_HOST_PARTIE=${LAN_HOST_PARTIE:-w150 spartie kq w10 40,215 w120 skampf1 w120 skampf2 w60 kEscape w15 400,386 w15 290,367 w60 sende}
	LAN_GAST_PARTIE=${LAN_GAST_PARTIE:-w240 spartie kq w10 r765,215 w120 skampf1 w120 skampf2 kEscape w15 400,386 w15 290,367 w60 sende}
}
# two software-rendered instances share the cores; without the D3D11 effects both keep up with each other
# LAN_SCHALTER adds game switches for both (e.g. -netzNurRelais with LAN_MODUS=internet: all traffic over the relay),
# LAN_HOST_SCHALTER only for the host (e.g. -mod Name: a mod in the Mods folder of the host's prefix)
export KLICK_SCHALTER="-d3d11Effects none $TREFFPUNKT ${LAN_SCHALTER:-}"
KLICK_SCHALTER="$KLICK_SCHALTER ${LAN_HOST_SCHALTER:-}" \
KLICK_NETNS=gzh-lan1 WINEPREFIX_CNC="$BAU/wineprefix-lan1" "$HERE/klicktest.sh" "$OUT/daten1" "$OUT/host" \
	$MENUE w60 slobby 674,532 w60 seinstellungen $HOST_LISTE $KARTE w200 sbereit 665,530 ${LAN_HOST_PARTIE:-w150 spartie 242,500 w120 sgebaut kq w10 600,300 w60 sbewegt dctrl 400,225 uctrl w60 sangriff w60 kEscape w15 400,386 w15 290,367 w60 sende} \
	> "$OUT/host.log" 2>&1 &
H=$!
KLICK_NETNS=gzh-lan2 WINEPREFIX_CNC="$BAU/wineprefix-lan2" "$HERE/klicktest.sh" "$OUT/daten2" "$OUT/gast" \
	$MENUE w120 $GAST_LOBBY slobby 150,168 w10 505,532 w60 seinstellungen $GAST_WARTE 665,530 w30 sangenommen ${LAN_GAST_PARTIE:-w200 spartie 305,500 w120 sgebaut kq w10 r250,330 w60 sbewegt dctrl r400,225 uctrl w60 sangriff kEscape w15 400,386 w15 290,367 w60 sende} \
	> "$OUT/gast.log" 2>&1 &
G=$!
wait $H $G
for r in host gast; do echo "== $r"; cat "$OUT/$r.log"; done

# Both instances record the same stream of network commands (types from 1000 on: commands of both players, logic
# CRCs). The shorter replay must be the start of the longer one; an instance that left earlier has fewer frames.
# Local messages (below 1000, e.g. clearing the game data on leaving) are recorded by one instance only.
for i in 1 2; do
	cp "$BAU/wineprefix-lan$i/drive_c/users/${WINEUSER_CNC:-$(id -un)}/Documents/GZH Neo/Zero Hour/Replays/00000000.rep" \
		"$OUT/replay$i.rep" 2>/dev/null
done
python3 - "$OUT" "$HERE/../fuzz/replaycmd/replaycmd.py" <<'PYEOF'
import os, subprocess, sys
out, tool = sys.argv[1:3]
dumps = []
for i in (1, 2):
    rep = os.path.join(out, 'replay%d.rep' % i)
    if not os.path.exists(rep):
        sys.exit('Replay %d fehlt' % i)
    text = subprocess.run([sys.executable, tool, 'dump', rep], capture_output=True, text=True).stdout
    open(os.path.join(out, 'replay%d.txt' % i), 'w').write(text)
    dumps.append([l for l in text.splitlines() if l.startswith(' ') and int(l.split()[2]) >= 1000])
short, long_ = sorted(dumps, key=len)
orders = [l for l in short if ' typ  1095 ' not in l]
if long_[:len(short)] == short:
    print('Replays gleich: %d Befehle, davon %d ausser Logik-CRC, bis Bild %s' % (len(short), len(orders),
          short[-1].split()[0] if short else '-'))
else:
    print('Replays weichen ab (replay1.txt, replay2.txt)')
    sys.exit(1)
PYEOF
