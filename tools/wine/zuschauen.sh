#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
#
# Watching a running LAN game under Wine, like tools/netz/zuschauen_test.py (variant "direkt") on Windows: three
# instances, driven by the test sequences tools/netz/szenen/zuschauen_*.txt. Each runs in its own network namespace
# (gzh-zu1 to -zu3, 10.78.0.1 to .3 on one bridge); loopback instances (-netzLokal) do not work under Wine, which binds
# a broadcast socket to every address, so the second instance cannot bind the lobby port.
# Instance 1 hosts, instance 2 joins, both play; instance 3 starts once the game runs, joins as spectator, watches and
# quits, then starts a second time and watches again. Every instance logs the logic CRC of every frame.
#
#   tools/wine/zuschauen.sh <output folder> [rounds] [logic frames]
#
# Needs root and iproute2. Each instance has its own Wine prefix (<build>/wineprefix-zu1 to -zu3) and its own copy of a
# freshly generated data set. The result per round is in <output folder>/rundeN/ergebnis.txt: the players compute the same, and each
# spectator became one, saw at least a twelfth of the frames and has the CRCs of the host. Exit code 0 if every round
# passes. CNC_WINE_DIR selects the build (tools/wine/build_game.sh); ZUSCHAUEN_SCHALTER appends game switches to
# all instances (e.g. the network emulator). ZUSCHAUEN_KI=1: the host plays against a computer player instead of instance
# 2 (two instances, less load; scene zuschauen_host_ki.txt). ZUSCHAUEN_FRAMES sets how many logic frames each spectator
# watches (default a quarter of the host's); the host's game must outlast both spectators, otherwise the round reports
# that the game had already ended when a spectator started (test setup, not a game fault).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
[ $# -ge 1 ] || { sed -n 6,22p "$0" | sed 's/^# \{0,1\}//'; exit 2; }
OUT=$1
RUNDEN=${2:-1}
LOGIK=${3:-1800}
VORLAUF=${ZUSCHAUEN_VORLAUF:-20}
KI=${ZUSCHAUEN_KI:-0}
ZFRAMES=${ZUSCHAUEN_FRAMES:-$((LOGIK / 4))}
read -r -a EXTRA <<< "${ZUSCHAUEN_SCHALTER:-}"
BAU="${CNC_WINE_DIR:-${TMPDIR:-/tmp}/cnc-wine}"
command -v ip >/dev/null || { echo "ip fehlt (Paket iproute2)"; exit 2; }
[ "$(id -u)" = 0 ] || { echo "braucht root (ip netns)"; exit 2; }
# one run at a time: a second run would delete the namespaces of the first
exec 9> "${TMPDIR:-/tmp}/gzh-zuschauen.lock"
flock -n 9 || { echo "laeuft schon (${TMPDIR:-/tmp}/gzh-zuschauen.lock)"; exit 2; }

aufraeumen() {
	for i in 1 2 3; do
		ip netns del gzh-zu$i 2>/dev/null
		ip link del gzh-zb$i 2>/dev/null
	done
	ip link del gzh-zbr 2>/dev/null
}
aufraeumen
trap aufraeumen EXIT
ip link add gzh-zbr type bridge && ip link set gzh-zbr up || exit 2
for i in 1 2 3; do
	ip netns add gzh-zu$i || exit 2
	ip link add gzh-zb$i type veth peer name gzh-z$i || exit 2
	ip link set gzh-z$i netns gzh-zu$i
	ip link set gzh-zb$i master gzh-zbr
	ip link set gzh-zb$i up
	ip -n gzh-zu$i addr add 10.78.0.$i/24 dev gzh-z$i
	ip -n gzh-zu$i link set gzh-z$i up
	ip -n gzh-zu$i link set lo up
done
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
DATA="$OUT/testdaten"
rm -rf "$DATA" && python3 "$HERE/testdaten/generate.py" "$DATA" > /dev/null || { echo "Generator fehlgeschlagen"; exit 2; }

# szene <name> <LOGIK>: the scene with its placeholders filled in, CRLF as the game expects
szene() {
	sed -e "s/{LOGIK}/$2/g" -e "s/{WARTE}/2400/g" "$ROOT/tools/netz/szenen/$1.txt" | sed 's/$/\r/'
}

# instanz <number> <folder> <scene file>: starts one instance in the background, sets PID
instanz() {
	local i=$1 D=$2 S=$3
	local U="$BAU/wineprefix-zu$i/drive_c/users/${WINEUSER_CNC:-$(id -un)}/Documents/GZH Neo/Zero Hour"
	mkdir -p "$U"
	printf '[Lan]\r\nUserName = Spieler%d\r\n' "$i" > "$U/State.ini"
	ip netns exec gzh-zu$i env WINEPREFIX_CNC="$BAU/wineprefix-zu$i" "$HERE/run_game.sh" --menue --zeit 3600 "$D" \
		-einstellungenNurLesen -d3d11Effects none -crcLog crc.log -crcLogInterval 1 -testablauf "$S" "${EXTRA[@]}" \
		> "$D.out" 2>&1 &
	PID=$!
}

maxframe() {
	awk '{ sub(/\r$/, "") } NF == 2 && $1 ~ /^[0-9]+$/ && length($2) == 8 { if ($1 > m) m = $1 } END { print m + 0 }' "$1" 2>/dev/null || echo 0
}

fehler=0
for runde in $(seq 1 "$RUNDEN"); do
	R="$OUT/runde$runde"
	rm -rf "$R" && mkdir -p "$R"
	for n in host spieler zuschauer1 zuschauer2; do
		cp -r "$DATA" "$R/$n"
		rm -f "$R/$n/GZHNeoZH.exe" "$R/$n/Shaders" "$R/$n/ProjectData"
	done
	if [ "$KI" = 1 ]; then szene zuschauen_host_ki "$LOGIK"; else szene zuschauen_host "$LOGIK"; fi > "$R/host/ablauf.txt"
	szene zuschauen_spieler "$LOGIK" > "$R/spieler/ablauf.txt"
	szene zuschauen_zuschauer "$ZFRAMES" > "$R/zuschauer1/ablauf.txt"
	cp "$R/zuschauer1/ablauf.txt" "$R/zuschauer2/ablauf.txt"

	instanz 1 "$R/host" ablauf.txt; P1=$PID
	sleep 20
	P2=
	[ "$KI" = 1 ] || { instanz 2 "$R/spieler" ablauf.txt; P2=$PID; }
	for _ in $(seq 1 600); do
		grep -aq "LOG SPIEL BEGINNT" "$R/host/crc.log" 2>/dev/null && break
		kill -0 "$P1" 2>/dev/null || break
		sleep 2
	done
	sleep "$VORLAUF"
	echo "Zuschauer 1 startet bei Frame $(maxframe "$R/host/crc.log")" > "$R/ablauf.txt"
	instanz 3 "$R/zuschauer1" ablauf.txt; wait "$PID"
	sleep 10
	echo "Zuschauer 2 startet bei Frame $(maxframe "$R/host/crc.log")" >> "$R/ablauf.txt"
	instanz 3 "$R/zuschauer2" ablauf.txt; wait "$PID"
	wait "$P1" $P2

	python3 - "$R" "$LOGIK" "$KI" "$ZFRAMES" <<'PYEOF' || fehler=$((fehler + 1))
import os, re, sys
r, logik, ki, zframes = sys.argv[1], int(sys.argv[2]), sys.argv[3] == '1', int(sys.argv[4])
start = {}
for z in open(os.path.join(r, 'ablauf.txt')):
    m = re.match(r'Zuschauer (\d) startet bei Frame (\d+)', z)
    if m:
        start['zuschauer' + m.group(1)] = int(m.group(2))

def lesen(n):
    werte, zeilen = {}, []
    p = os.path.join(r, n, 'crc.log')
    if os.path.exists(p):
        for z in open(p, encoding='latin-1', errors='replace'):
            t = z.split()
            if len(t) == 2 and t[0].isdigit() and len(t[1]) == 8:
                werte[int(t[0])] = t[1]
            elif z.startswith('TESTABLAUF'):
                zeilen.append(z.rstrip())
    return werte, zeilen

host, hz = lesen('host')
spieler, sz = lesen('spieler')
text, ok = [], True
if ki:
    gut = len(host) >= logik // 2
    ok &= gut
    text.append('Gastgeber gegen KI: %s (%d Frames)' % ('OK' if gut else 'FEHLER', len(host)))
else:
    gem = sorted(set(host) & set(spieler))
    falsch = [f for f in gem if host[f] != spieler[f]]
    gut = len(gem) >= logik // 2 and not falsch
    ok &= gut
    text.append('Spieler gleich: %s (%d gemeinsame Frames, %d abweichend)' % ('OK' if gut else 'FEHLER', len(gem), len(falsch)))
for n in ('zuschauer1', 'zuschauer2'):
    w, zz = lesen(n)
    sah = any('LOG ZUSCHAUER SIEHT ZU' in z for z in zz)
    g = sorted(set(host) & set(w))
    f = [x for x in g if host[x] != w[x]]
    gut = sah and len(g) >= zframes // 3 and not f
    ok &= gut
    letzte = zz[-1] if zz else 'keine Testablauf-Zeile'
    text.append('%s: %s (%s, %d gemeinsame Frames, %d abweichend; letzte Zeile: %s)'
                % (n, 'OK' if gut else 'FEHLER', 'wurde Zuschauer' if sah else 'kein Zuschauen', len(g), len(f), letzte))
    if not sah and start.get(n, 0) >= logik - 30:
        text.append('  %s startete bei Frame %d, die Partie des Gastgebers endet bei %d: Testaufbau, Partie zu kurz'
                    % (n, start[n], logik))
fehler = [z for z in hz + sz if 'TESTABLAUF ERROR' in z]
if fehler:
    ok = False
    text.append('Testablauf-Fehler: ' + fehler[0])
text.append('OK' if ok else 'FEHLER')
open(os.path.join(r, 'ergebnis.txt'), 'w').write('\n'.join(text) + '\n')
print('\n'.join(text))
sys.exit(0 if ok else 1)
PYEOF
done
[ $fehler -eq 0 ] && { echo "Zuschauen bestanden ($RUNDEN Runden)"; exit 0; }
echo "Zuschauen mit Fehlern: $fehler von $RUNDEN Runden"
exit 1
