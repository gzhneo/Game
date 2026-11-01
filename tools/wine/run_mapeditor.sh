#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Selbsttest des Karteneditors unter Wine und Xvfb: MapEditor.exe --selftest mit den Testdaten des Editors
# (tools/mapeditor/testdaten.py), mit Engine und 3D-Ansicht. Vorher tools/wine/build_mapeditor.sh.
#
# Aufruf:  tools/wine/run_mapeditor.sh [--zeit <s>] [--bilder <Ordner>] [--vulkan]
#   --vulkan          die Engine des Editors rendert mit Vulkan (-vulkan an der Kommandozeile)
#   --zeit <s>        Zeitlimit (Standard 900 s)
#   --bilder <Ordner> wohin Bilder, selftest.log und engine.log kommen (Standard <Ablage>/mapeditor-lauf)
#   CNC_WINE_DIR      Ablage des Baus (Standard ${TMPDIR:-/tmp}/cnc-wine)
# Rueckgabewert 0 nur, wenn der Selbsttest bis zu seiner Schlusszeile laeuft, mit 0 endet, keine Zeile FAIL meldet
# und Wine keinen Absturz meldet.
# Die Einstellungen des Nutzers bleiben unberuehrt: der Selbsttest schreibt nur in den Bilderordner.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${CNC_WINE_DIR:-${TMPDIR:-/tmp}/cnc-wine}"
SCHALTER=""
ZEIT=900
BILDER=""
while [ $# -gt 0 ]; do
	case "$1" in
		--zeit) ZEIT="$2"; shift ;;
		--vulkan) SCHALTER="-vulkan" ;;
		--bilder) BILDER="$2"; shift ;;
		-h|--help) sed -n 2,11p "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "Unbekannter Schalter: $1"; exit 2 ;;
	esac
	shift
done
Z="$OUT/mapeditor"
[ -f "$Z/MapEditor.exe" ] || { echo "$Z/MapEditor.exe fehlt: erst tools/wine/build_mapeditor.sh ausfuehren"; exit 2; }
[ -n "$BILDER" ] || BILDER="$OUT/mapeditor-lauf"
WINE=$(command -v wine64 || echo /usr/lib/wine/wine64)
WINESERVER=$(command -v wineserver || echo /usr/lib/wine/wineserver64)
command -v Xvfb >/dev/null || { echo "Xvfb fehlt (Paket xvfb)"; exit 2; }

# Frische Testdaten und ein leerer Bilderordner: kein Rest eines frueheren Laufs zaehlt mit.
DATEN="$OUT/mapeditor-daten"
rm -rf "$DATEN" "$BILDER"
python3 "$ROOT/tools/mapeditor/testdaten.py" "$DATEN" > /dev/null || { echo "Testdaten erzeugen fehlgeschlagen"; exit 1; }
mkdir -p "$BILDER"
BILDER="$(cd "$BILDER" && pwd)"
win() { echo "Z:$(echo "$1" | tr / '\\')"; }

export WINEPREFIX="${WINEPREFIX_CNC:-$OUT/wineprefix}"
export WINEDEBUG="${WINEDEBUG:--all}"
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:-mscoree,mshtml=d}"
export LIBGL_ALWAYS_SOFTWARE=1
export GALLIUM_DRIVER=llvmpipe
export MESA_GL_VERSION_OVERRIDE=4.5
export MESA_GLSL_VERSION_OVERRIDE=450
mkdir -p "$WINEPREFIX"
# Ein wineserver aus einem vorigen Lauf mit derselben Ablage endet womoeglich noch; startet Wine waehrenddessen,
# kam das Programm einmal nicht bis zur ersten Ausgabe. Darum erst abwarten (hoechstens 30 s).
T_WS=$SECONDS
timeout 30 "$WINESERVER" -w >/dev/null 2>&1
WS_WARTEN=$((SECONDS - T_WS))

for _ in $(seq 1 50); do
	N=$((100 + RANDOM % 800))
	[ -e "/tmp/.X11-unix/X$N" ] || [ -e "/tmp/.X$N-lock" ] || break
done
Xvfb ":$N" -screen 0 1600x1000x24 > "$BILDER/xvfb.log" 2>&1 &
XPID=$!
for _ in $(seq 1 50); do [ -e "/tmp/.X11-unix/X$N" ] && break; sleep 0.1; done
export DISPLAY=":$N"
LOG="$BILDER/wine.log"
echo "wineserver vor dem Start abgewartet: $WS_WARTEN s" > "$BILDER/start.txt"
cd "$Z" || exit 2
"$WINE" MapEditor.exe --selftest "$(win "$BILDER")" --data "$(win "$DATEN")" $SCHALTER > "$LOG" 2>&1 &
WPID=$!
T0=$SECONDS
while kill -0 "$WPID" 2>/dev/null && [ $((SECONDS - T0)) -lt "$ZEIT" ]; do sleep 1; done
if kill -0 "$WPID" 2>/dev/null; then
	echo "Zeitlimit ($ZEIT s) erreicht" | tee -a "$LOG"
	if command -v import >/dev/null; then import -display ":$N" -window root "$BILDER/zeitlimit.png" 2>/dev/null; fi
	"$WINESERVER" -k >/dev/null 2>&1
	kill "$WPID" 2>/dev/null; sleep 1; kill -9 "$WPID" 2>/dev/null
	RC=124
else
	wait "$WPID"; RC=$?
fi
"$WINESERVER" -k >/dev/null 2>&1
kill "$XPID" 2>/dev/null

# Der Selbsttest schliesst mit "<n> images, <m> failed"; fehlt die Zeile, lief er nicht zu Ende.
FERTIG=$(grep -c -E '^[0-9]+ images, [0-9]+ failed' "$LOG")
[ "$FERTIG" -gt 0 ] || echo "Selbsttest nicht zu Ende gelaufen (Schlusszeile fehlt)"
OK=$(grep -c '^ok' "$LOG")
FAIL=$(grep -c '^FAIL' "$LOG")
ABSTURZ=$(grep -c -E 'Unhandled (page fault|exception)|starting debugger' "$LOG")
grep -E '^FAIL|Unhandled|starting debugger' "$LOG" | sed "s|Z:$BILDER/||g" | head -40
echo "Selbsttest: $OK ok, $FAIL FAIL, Abstuerze $ABSTURZ, Rueckgabewert $RC (Log $LOG, Bilder $BILDER)"
[ "$RC" = 0 ] && [ "$FAIL" = 0 ] && [ "$ABSTURZ" = 0 ] && [ "$OK" -gt 0 ] && [ "$FERTIG" -gt 0 ]
