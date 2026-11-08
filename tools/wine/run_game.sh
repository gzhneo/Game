#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Startet das mit tools/wine/build_game.sh gebaute Spiel unter Wine und Xvfb (Software-Rendering).
#
# Aufruf:  tools/wine/run_game.sh [Optionen] <Datenordner> [Spielschalter ...]
#   <Datenordner>   Spieldaten (Data\INI, .big-Archive, Art, ...); das Spiel liest sie aus seinem
#                   Arbeitsverzeichnis. Der Ordner wird als Arbeitsverzeichnis benutzt, die Exe,
#                   Shaders\, ProjectData\ und dxcompiler.dll (Vulkan) werden als Verknuepfungen hineingelegt.
#   --generals      GZHNeo.exe (Generals) statt GZHNeoZH.exe
#   --zeit <s>      Zeitlimit in Sekunden (Standard 120), danach wird das Spiel beendet (Rueckgabe 124)
#   --auflosung <B>x<H>   Bildschirm von Xvfb (Standard 1280x720)
#   --menue         ohne -noshellmap: Hauptmenue mit Shell-Karte (Standard: ohne Shell-Karte)
#   --log <Datei>   Logdatei fuer Wines Ausgabe (Standard <Datenordner>/wine.log)
#   --fotos <Ordner> <s>  alle <s> Sekunden ein Bildschirmfoto des Xvfb-Displays (auch im Menue, unabhaengig vom Spiel)
#   CNC_WINE_DIR    Ablage des Baus (Standard ${TMPDIR:-/tmp}/cnc-wine)
#   WINEDEBUG       Wine-Meldungen (Standard err+all)
# Standardschalter des Spiels: -win -noaudio -novideo -nologo -noshellmap -ignoreAsserts
# Options.ini im Praefix bekommt HardwareHint = no (sonst blockiert der CPU-Hinweisdialog); andere Werte bleiben.
# (-win: nie Vollbild; weitere Schalter werden angehaengt und gelten zuletzt).
# Rueckgabewert: der des Spiels bzw. 124 bei Zeitueberschreitung.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${CNC_WINE_DIR:-${TMPDIR:-/tmp}/cnc-wine}"
EXE=GZHNeoZH.exe
GAMEDIR=game
ZEIT=120
RES=1280x720
LOG=""
FOTOS=""
FOTO_TAKT=10
SHELLMAP=-noshellmap
while [ $# -gt 0 ]; do
	case "$1" in
		--generals) EXE=GZHNeo.exe; GAMEDIR=game-generals ;;
		--zeit) ZEIT="$2"; shift ;;
		--auflosung) RES="$2"; shift ;;
		--menue) SHELLMAP= ;;
		--log) LOG="$2"; shift ;;
		--fotos) FOTOS="$2"; FOTO_TAKT="$3"; shift 2 ;;
		-h|--help) sed -n 2,19p "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) break ;;
	esac
	shift
done
[ $# -ge 1 ] || { echo "Aufruf: $0 [Optionen] <Datenordner> [Spielschalter ...]"; exit 2; }
[ -d "$1" ] || { echo "Datenordner fehlt: $1 (Erzeugen: python3 tools/wine/testdaten/generate.py <Ordner>)"; exit 2; }
DATA="$(cd "$1" && pwd)"
[ -d "$DATA/Data/INI" ] || { echo "Warnung: $DATA/Data/INI fehlt -- kein Spieldatensatz? (tools/wine/testdaten/generate.py <Ordner>)" >&2; }
shift
G="$OUT/$GAMEDIR"
[ -f "$G/$EXE" ] || { echo "$G/$EXE fehlt: erst tools/wine/build_game.sh (bei GZHNeo.exe mit --generals) ausfuehren"; exit 2; }
WINE=$(command -v wine64 || echo /usr/lib/wine/wine64)
WINESERVER=$(command -v wineserver || echo /usr/lib/wine/wineserver64)
command -v Xvfb >/dev/null || { echo "Xvfb fehlt (Paket xvfb)"; exit 2; }
[ -n "$LOG" ] || LOG="$DATA/wine.log"

# Exe und Projektdaten neben die Spieldaten (Verknuepfungen; vorhandene Dateien bleiben unangetastet). Eine
# Verknuepfung auf einen anderen Bau (anderes CNC_WINE_DIR, kopierter Datenordner) wird neu gesetzt, sonst liefe
# still die alte Exe. Ein echter Ordner ProjectData im Datenordner (etwa generate.py --winter) bleibt; was ihm fehlt,
# wird Datei fuer Datei aus dem Bau verlinkt.
for n in "$EXE" Shaders ProjectData dxcompiler.dll; do
	[ -e "$G/$n" ] || continue
	if [ -L "$DATA/$n" ]; then
		[ "$(readlink "$DATA/$n")" = "$G/$n" ] || ln -sfn "$G/$n" "$DATA/$n"
	elif [ ! -e "$DATA/$n" ]; then
		ln -s "$G/$n" "$DATA/$n"
	elif [ "$n" = ProjectData ] && [ -d "$DATA/$n" ]; then
		(cd "$G/$n" && find . -type f) | while read -r f; do
			if [ -L "$DATA/$n/$f" ]; then
				[ "$(readlink "$DATA/$n/$f")" = "$G/$n/$f" ] || ln -sfn "$G/$n/$f" "$DATA/$n/$f"
			elif [ ! -e "$DATA/$n/$f" ]; then
				mkdir -p "$(dirname "$DATA/$n/$f")" && ln -s "$G/$n/$f" "$DATA/$n/$f"
			fi
		done
	fi
done

export WINEPREFIX="${WINEPREFIX_CNC:-$OUT/wineprefix}"
export WINEDEBUG="${WINEDEBUG:-err+all}"
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:-mscoree,mshtml=d}"
# Mesa-Software-Rendering: Wines Direct3D 11 laeuft ueber OpenGL (llvmpipe) unter Xvfb
export LIBGL_ALWAYS_SOFTWARE=1
export GALLIUM_DRIVER=llvmpipe
export MESA_GL_VERSION_OVERRIDE=4.5
export MESA_GLSL_VERSION_OVERRIDE=450
mkdir -p "$WINEPREFIX"

# HardwareHint = no in Options.ini (und Settings.ini) des Praefixes: der CPU-Hinweisdialog wartet sonst auf einen Klick.
# Nur diese Zeile wird gesetzt bzw. ergaenzt; vorhandene Werte und Zeilenenden bleiben.
# CNC_HARDWARE_HINT=1 laesst die Dateien unberuehrt (zum Pruefen des Hinweises).
if [ "$EXE" = GZHNeo.exe ]; then OPTDIR_NAME="GZH Neo/Generals"; else OPTDIR_NAME="GZH Neo/Zero Hour"; fi
OPTDIR="$WINEPREFIX/drive_c/users/${WINEUSER_CNC:-$(id -un)}/Documents/$OPTDIR_NAME"
[ -n "${CNC_HARDWARE_HINT:-}" ] || python3 - "$OPTDIR/Options.ini" <<'PYEOF'
import os, re, sys
p = sys.argv[1]
os.makedirs(os.path.dirname(p), exist_ok=True)
b = open(p, 'rb').read() if os.path.exists(p) else b''
eol = b'\r\n' if b'\r\n' in b or not b else b'\n'
if re.search(rb'(?im)^[ \t]*HardwareHint[ \t]*=', b):
    n = re.sub(rb'(?im)^([ \t]*HardwareHint[ \t]*=)[^\r\n]*', rb'\1 no', b)
else:
    n = b + (b'' if not b or b.endswith(b'\n') else eol) + b'HardwareHint = no' + eol
if n != b:
    open(p, 'wb').write(n)
# Seit es Settings.ini gibt, liest das Spiel nur noch dort (Options.ini nur beim ersten Start): dort
# [General] HardwareHint = no setzen.
s = os.path.join(os.path.dirname(p), 'Settings.ini')
if os.path.exists(s):
    b = open(s, 'rb').read()
    eol = b'\r\n' if b'\r\n' in b else b'\n'
    m = re.search(rb'(?im)^[ \t]*\[[ \t]*General[ \t]*\][ \t]*(?=\r?$)', b)
    if re.search(rb'(?im)^[ \t]*HardwareHint[ \t]*=', b):
        n = re.sub(rb'(?im)^([ \t]*HardwareHint[ \t]*=)[^\r\n]*', rb'\1 no', b)
    elif m:
        n = b[:m.end()] + eol + b'HardwareHint = no' + b[m.end():]
    else:
        n = b + (b'' if not b or b.endswith(b'\n') else eol) + eol + b'[General]' + eol + b'HardwareHint = no' + eol
    if n != b:
        open(s, 'wb').write(n)
PYEOF

cd "$DATA" || exit 2
# Eigener Xvfb (statt xvfb-run), damit bei einem Zeitlimit vor dem Beenden ein Bildschirmfoto und die
# Fensterliste entstehen: ein Fehlerdialog des Spiels ("Technical Difficulties") wartet sonst ewig auf OK.
for _ in $(seq 1 50); do
	N=$((100 + RANDOM % 800))
	[ -e "/tmp/.X11-unix/X$N" ] || [ -e "/tmp/.X$N-lock" ] || break
done
Xvfb ":$N" -screen 0 "${RES}x24" > "$LOG.xvfb" 2>&1 &
XPID=$!
for _ in $(seq 1 50); do [ -e "/tmp/.X11-unix/X$N" ] && break; sleep 0.1; done
export DISPLAY=":$N"
# fuer Werkzeuge, die das Fenster bedienen (klicktest.sh): welche Anzeige zu diesem Lauf gehoert
echo "Anzeige :$N"
"$WINE" "$EXE" -win -noaudio -novideo -nologo $SHELLMAP -ignoreAsserts "$@" > "$LOG" 2>&1 &
WPID=$!
T0=$SECONDS
FPID=""
if [ -n "$FOTOS" ] && command -v import >/dev/null; then
	mkdir -p "$FOTOS"
	( i=0; while kill -0 "$WPID" 2>/dev/null; do sleep "$FOTO_TAKT"; i=$((i + 1)); import -window root "$FOTOS/foto_$(printf %04d $i).png" 2>/dev/null; done ) &
	FPID=$!
fi
while kill -0 "$WPID" 2>/dev/null && [ $((SECONDS - T0)) -lt "$ZEIT" ]; do sleep 0.5; done
if kill -0 "$WPID" 2>/dev/null; then
	echo "Zeitlimit ($ZEIT s) erreicht" >&2
	{ echo "--- Fenster bei Zeitlimit ---"; xwininfo -root -tree 2>/dev/null | grep -E '"' | grep -v -E 'explorer|Default IME'; } >> "$LOG"
	if command -v import >/dev/null; then import -window root "${LOG%.log}.png" 2>/dev/null && echo "Bildschirmfoto: ${LOG%.log}.png" >&2; fi
	"$WINESERVER" -k >/dev/null 2>&1
	kill "$WPID" 2>/dev/null
	sleep 1; kill -9 "$WPID" 2>/dev/null
	RC=124
else
	wait "$WPID"; RC=$?
fi
[ -n "$FPID" ] && kill "$FPID" 2>/dev/null
kill "$XPID" 2>/dev/null
echo "Rueckgabewert $RC, Log: $LOG" >&2
exit $RC
