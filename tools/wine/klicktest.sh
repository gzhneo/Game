#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
#
# Drives the menus of the game under Wine with mouse clicks and keys and takes screenshots.
#
#   tools/wine/klicktest.sh <data folder> <output folder> <step>...
#
# Steps, in order:
#   X,Y        click at X,Y in the 800 x 600 layout (scaled to the game window)
#   rX,Y       right click at X,Y
#   mX,Y       only move the mouse there (a check box needs the mouse over it before the click)
#   w<sec>     wait
#   s<name>    screenshot of the whole screen to <output folder>/<name>.png
#   k<key>     key press (xdotool name, e.g. kEscape)
#   d<key>     hold a key down (e.g. dctrl for force attack), u<key> releases it
#
# The game starts with the main menu (run_game.sh --menue); the main menu appears after the first mouse
# move, so a first step like "w45 400,300 w3" is useful. Environment: CNC_WINE_DIR as for run_game.sh
# (e.g. the debug build). The exe link in the data folder points to the build that ran there first; use a
# fresh data folder when switching builds. Only the game process started here is stopped at the end.
# KLICK_NETNS=<name> runs the game in that network namespace (ip netns, see lan_zwei.sh); WINEPREFIX_CNC
# as for run_game.sh selects the Wine prefix; KLICK_SCHALTER adds game switches; KLICK_FAKTOR (integer)
# multiplies every wait (debug build); KLICK_GENERALS=1 starts Generals (GZHNeo.exe) instead of Zero Hour;
# KLICK_AUFLOESUNG=<W>x<H> sets the Xvfb screen (with KLICK_SCHALTER "-xres W -yres H" for the game).
set -u
DATA=$1; OUT=$2; shift 2
HERE="$(cd "$(dirname "$0")" && pwd)"
command -v xdotool >/dev/null || { echo "xdotool fehlt (Paket xdotool)"; exit 2; }
mkdir -p "$OUT"
${KLICK_NETNS:+ip netns exec $KLICK_NETNS} "$HERE/run_game.sh" ${KLICK_GENERALS:+--generals} ${KLICK_AUFLOESUNG:+--auflosung $KLICK_AUFLOESUNG} --menue --zeit 3600 "$DATA" ${KLICK_SCHALTER:-} > "$OUT/run.out" 2>&1 &
RP=$!
# run_game.sh names its display; several runs at once each find their own
disp=""
for _ in $(seq 1 100); do
	disp=$(sed -n 's/^Anzeige \(:[0-9]*\)$/\1/p' "$OUT/run.out" 2>/dev/null | head -1)
	[ -n "$disp" ] && [ -e "/tmp/.X11-unix/X${disp#:}" ] && break
	sleep 0.2
done
[ -n "$disp" ] || { echo "kein Xvfb gefunden"; exit 1; }
export DISPLAY="$disp"
WIN=""
for _ in $(seq 1 600); do
	WIN=$(xdotool search --name "Test Data Game" 2>/dev/null | head -1)
	[ -n "$WIN" ] && break
	kill -0 $RP 2>/dev/null || { echo "Spiel beendet vor dem Fenster, siehe $OUT/run.out"; exit 1; }
	sleep 0.5
done
[ -n "$WIN" ] || { echo "kein Spielfenster"; exit 1; }
eval "$(xdotool getwindowgeometry --shell "$WIN")"
echo "display $DISPLAY, window $WIN at $X,$Y ${WIDTH}x$HEIGHT"
# the game window can change its size after it appears (-xres/-yres), so read it again before each mouse step
lage() { eval "$(xdotool getwindowgeometry --shell "$WIN")"; }
HALTEN=0.3
[ "${KLICK_FAKTOR:-1}" -gt 1 ] && HALTEN=1
for st in "$@"; do
	case "$st" in
		w*) sleep "$(( ${st#w} * ${KLICK_FAKTOR:-1} ))" ;;
		s*) import -window root "$OUT/${st#s}.png" ;;
		# without a window manager the key goes to the focused window
		k*) xdotool windowfocus "$WIN" 2>/dev/null; xdotool key "${st#k}" ;;
		d*) xdotool windowfocus "$WIN" 2>/dev/null; xdotool keydown "${st#d}"; sleep 0.5 ;;
		u*) sleep 0.5; xdotool keyup "${st#u}" ;;
		m*,*)
			lage; p=${st#m}; xdotool mousemove $((X + ${p%,*} * WIDTH / 800)) $((Y + ${p#*,} * HEIGHT / 600)) ;;
		*,*)
			# the right button counts as a click only below Mouse DragToleranceMS (alternate mouse setup), so it is
			# released sooner
			b=1; p=$st; halten=$HALTEN
			[ "${p#r}" != "$p" ] && { b=3; p=${p#r}; halten=0.1; }
			lx=${p%,*}; ly=${p#*,}
			lage
			xdotool mousemove $((X + lx * WIDTH / 800)) $((Y + ly * HEIGHT / 600))
			sleep 0.4
			# held briefly: at a few frames per second a fast click can fall between two frames
			xdotool mousedown $b; sleep "$halten"; xdotool mouseup $b ;;
		*) echo "unbekannter Schritt: $st" ;;
	esac
done
GP=$(xdotool getwindowpid "$WIN" 2>/dev/null)
[ -n "$GP" ] && kill "$GP" 2>/dev/null
wait $RP 2>/dev/null
ls "$DATA/Crashes" 2>/dev/null | sed 's/^/Absturzbericht: /'
true
