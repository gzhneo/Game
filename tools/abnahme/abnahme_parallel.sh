#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Abnahme im Parallelbetrieb (2026-09-24): Bildlaeufe mit --mehrfach (bis zu 3 Spiele gleichzeitig),
# Replays sechs Saetze parallel (-runner), Starttests mit -mehrfach und Protokoll je Instanz.
# Aufruf aus Git-Bash: bash tools/abnahme/abnahme_parallel.sh <Referenzpraefix> (etwa p8reg: vergleicht gegen
# render-out/p8reg-*). Zwischenergebnisse in render-out/abnahme/. Ende: Zeile ABNAHME_FERTIG.
# 2026-09-26 aus dem Sitzungs-Scratchpad uebernommen; Warteschlange tools/abnahme/slot.py.
cd "$(dirname "$0")/../.." || exit 1
# Windows-Pfad (pwd -W): Python bekommt den Pfad im -c-Text, dort wandelt Git-Bash kein /d/... um (2026-09-26).
A="$(pwd -W)/tools/abnahme"
S="$PWD/render-out/abnahme"; mkdir -p "$S"
C="${CMAKE:-/c/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe}"
CT="${C%cmake.exe}ctest.exe"
REF="$1"
[ -n "$REF" ] || { echo "Aufruf: $0 <Referenzpraefix>"; exit 2; }
T0=$(date +%s)
frei1() { python -c "import sys; sys.path.insert(0, r'$A'); from slot import wait_free; wait_free()"; }
frei3() { python -c "import sys; sys.path.insert(0, r'$A'); from slot import wait_free; wait_free(parallel=3)"; }
# Abbruch bei Baufehler (2026-09-25): sonst testete die Abnahme still mit den alten Exes weiter.
FEHLBAU=0
for cfg in Debug Release; do "$C" --build build-x64 --config $cfg 2>&1 | grep -E " error |error LNK" | head; rc=${PIPESTATUS[0]}; echo "BUILD_$cfg=$rc"; [ "$rc" = 0 ] || FEHLBAU=1; done
for cfg in Debug Release; do "$C" --build build-x64-tools --config $cfg 2>&1 | grep -E " error |error LNK" | head; rc=${PIPESTATUS[0]}; echo "TOOLS_$cfg=$rc"; [ "$rc" = 0 ] || FEHLBAU=1; done
if [ "$FEHLBAU" = 1 ]; then echo "ABBRUCH: Baufehler, keine Tests mit alten Exes"; echo ABNAHME_FERTIG; exit 1; fi
(cd build-x64 && for cfg in Debug Release; do echo "ctest $cfg: $("$CT" -C $cfg -LE assets 2>&1 | grep 'tests passed')"; done)
echo "Bau und ctest: $(( $(date +%s) - T0 )) s"
G=build-x64/Generals/Code/Main/Debug/GZHNeo.exe; Z=build-x64/GeneralsMD/Code/Main/Debug/GZHNeoZH.exe
GR=build-x64/Generals/Code/Main/Release/GZHNeo.exe; ZR=build-x64/GeneralsMD/Code/Main/Release/GZHNeoZH.exe
RR="python tools/render/run_render_test.py"
lauf() { out="$1"; shift; frei3; $RR run --mehrfach --out "$out" "$@" > "$out.txt" 2>&1; }
# Replays: vier Saetze parallel, laufen neben den Bildtests her
R="python tools/determinism/runner.py check"
k=0; for e in "$G tests/replays/x64" "$Z tests/replays/zh/x64" "$GR tests/replays/x64" "$ZR tests/replays/zh/x64" "$GR tests/replays/neueki/x64" "$ZR tests/replays/neueki/zh/x64"; do set -- $e; k=$((k+1))
  ( echo "## Replays $1"; $R --exe $1 --all $2 --timeout 5400 --extra -ignoreAsserts 2>&1 | grep -E "OK|FEHL|ABWEICH|Fehler"; echo "exit=${PIPESTATUS[0]}" ) > "$S/rep_par_$k.txt" 2>&1 &
done
T1=$(date +%s)
# Bildvergleiche (5) und Fahrten (5), bis zu drei Spiele gleichzeitig
n=0
job() { lauf "$@" & n=$((n+1)); sleep 3; if [ $((n % 3)) = 0 ]; then wait -n; fi; }
# Nur noch Direct3D 11 (TODO 67, 2026-09-26; Teil 2: D3D9 entfernt, run_render_test.py ohne --backend).
for b in d3d11; do
  job render-out/v64-g-shell-$b --exe $G --scene shellmap --frames 300,900 --reference render-out/$REF-g-shell-$b --extra -ignoreAsserts
  job render-out/v64-g-water-$b --exe $G --scene skirmish --map "Tournament Lake" --ai 2 --seed 1 --frames 300,900 --reference render-out/$REF-g-water-$b --extra -ignoreAsserts
done
job render-out/v64-zh-shell-d3d11 --exe $Z --scene shellmap --frames 300,900 --reference render-out/$REF-zh-shell-d3d11 --extra -ignoreAsserts
RA=tests/replays/x64/e0-alpine-assault-2ai-seed1.rep; RZ=tests/replays/zh/x64/e0zh-green-pastures-2ai-seed1.rep
for f in "quer $G $RA quer" "rundum $G $RA rundum" "zoom $G $RA zoom" "rand $G $RA rand" "zh-rundum $Z $RZ rundum"; do set -- $f
  job render-out/v64-$1 --exe $2 --scene replay --replay $3 --path tests/render/paths/$4.txt --reference render-out/$REF-$1 --extra -ignoreAsserts
done
wait_bilder() { for j in $(jobs -p); do :; done; }
# Starttests mit -mehrfach
for e in $G $Z; do frei3; n=$(basename $e); d=$(dirname $e)
  ( cd $d && ./$n -win -xres 800 -yres 600 -nologo -noaudio -fixedTimeStep -quitAfter 60 -ignoreAsserts -mehrfach; echo "rc=$?" ) > "$S/start_$n.txt" 2>&1 &
done
wait
echo "Spieltests und Replays: $(( $(date +%s) - T1 )) s"
for b in d3d11; do for s in shell water; do f=render-out/v64-g-$s-$b.txt; echo "g-$s-$b: $(grep -cE '^OK' $f) OK, $(grep -cE '^ABWEICHT|^FEHLER' $f) abweichend, $(grep -c '^HELL' $f) Helligkeit"; done; done
f=render-out/v64-zh-shell-d3d11.txt; echo "zh-shell-d3d11: $(grep -cE '^OK' $f) OK, $(grep -cE '^ABWEICHT|^FEHLER' $f) abweichend, $(grep -c '^HELL' $f) Helligkeit"
for n in quer rundum zoom rand zh-rundum; do echo "Fahrt $n: $(ls render-out/v64-$n/shot_*.bmp 2>/dev/null | wc -l) Bilder, $(grep -c '"pixels_over_threshold": 0' render-out/v64-$n/compare.json 2>/dev/null) pixelgleich"; done
# Bildpruefung der Fahrten (zoom meldet bekannt Bild 840 und 900, tools/render/alle_fahrten.py ERWARTET)
for n in quer rundum zoom rand zh-rundum; do echo "Bildpruefung $n: $(python tools/render/inspect_shots.py render-out/v64-$n 2>&1 | tail -1)"; done
cat "$S"/rep_par_[1-6].txt
for e in $G $Z; do n=$(basename $e); d=$(dirname $e); f=$(ls -t $d/m*_DebugLogFileD.txt 2>/dev/null | head -1)
  echo "START $n $(cat "$S/start_$n.txt" | tail -1) Protokoll $(basename "$f") Lecks=$(grep -c '^Leaked' "$f" 2>/dev/null)"; done
echo "Gesamt: $(( $(date +%s) - T0 )) s"
echo ABNAHME_FERTIG
