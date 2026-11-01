#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Laufende Abnahme je Scheibe, nur 64 Bit (x86 entfernt 2026-09-26).
# Aufruf: make_x64_refs.sh [--release]   legt die Referenzen render-out/x64ref-* an (siehe README.md)
cd /d/prj/CnC_Generals_Zero_Hour || exit 1
C="/c/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
CT="/c/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe"
rm -rf render-out/x64ref-*

echo "##### build-x64 Debug #####"
"$C" --build build-x64 --config Debug 2>&1 | grep -E " error |error LNK" | head -20; echo "BUILD_EXIT=${PIPESTATUS[0]}"
if [ "$1" == "--release" ]; then
  echo "##### build-x64 Release #####"
  "$C" --build build-x64 --config Release 2>&1 | grep -E " error |error LNK" | head -20; echo "RELEASE_EXIT=${PIPESTATUS[0]}"
fi
echo "##### ctest build-x64 #####"; (cd build-x64 && "$CT" -C Debug -LE assets 2>&1 | tail -2)

G=build-x64/Generals/Code/Main/Debug/GZHNeo.exe; Z=build-x64/GeneralsMD/Code/Main/Debug/GZHNeoZH.exe
RR="python tools/render/run_render_test.py"
cmp_out() { grep -E "^OK|^ABWEICHT|^FEHLER|Exit-Code" | sed 's/, max.*//'; }
# TODO 67 Teil 2 (2026-09-26): nur noch Direct3D 11 (D3D9 und --mit-d3d9 entfernt); die Ordner behalten die Endung -d3d11.
for b in d3d11; do
  echo "##### Bild Generals Shell-Map $b #####"
  $RR run --exe $G --scene shellmap --frames 300,900 --out render-out/x64ref-g-shell-$b  --extra -ignoreAsserts -$b 2>&1 | cmp_out
  echo "##### Bild Generals Tournament Lake $b #####"
  $RR run --exe $G --scene skirmish --map "Tournament Lake" --ai 2 --seed 1 --frames 300,900 --out render-out/x64ref-g-water-$b  --extra -ignoreAsserts -$b 2>&1 | cmp_out
  echo "##### Bild ZH Shell-Map $b #####"
  $RR run --exe $Z --scene shellmap --frames 300,900 --out render-out/x64ref-zh-shell-$b  --extra -ignoreAsserts -$b 2>&1 | cmp_out
done
# R-131: Zwischensequenz ZH USA 1 (Bilder 100 bis 450 in der Sequenz, 600 danach) -- vor dem Anlegen ansehen:
# Gelaende unter Baeumen und Einheiten, nicht nur Himmel
echo "##### Bild ZH Zwischensequenz USA 1 d3d11 #####"
$RR run --exe $Z --scene mission --map 'Maps\md_usa01.map' --frames 100,300,450,600 --out render-out/x64ref-zh-usa01-d3d11  --extra -ignoreAsserts -d3d11 2>&1 | cmp_out

R="python tools/determinism/runner.py check"
echo "##### Replay Generals Alpine #####"; $R --exe $G --replay tests/replays/x64/e0-alpine-assault-2ai-seed1.rep --extra -ignoreAsserts; echo "exit=$?"
echo "##### Replay ZH Green Pastures #####"; $R --exe $Z --replay tests/replays/zh/x64/e0zh-green-pastures-2ai-seed1.rep --extra -ignoreAsserts; echo "exit=$?"
if [ "$1" == "--release" ]; then
  echo "##### Replay RELEASE Generals Alpine #####"; $R --exe build-x64/Generals/Code/Main/Release/GZHNeo.exe --replay tests/replays/x64/e0-alpine-assault-2ai-seed1.rep --extra -ignoreAsserts; echo "exit=$?"
  echo "##### Replay RELEASE ZH Green Pastures #####"; $R --exe build-x64/GeneralsMD/Code/Main/Release/GZHNeoZH.exe --replay tests/replays/zh/x64/e0zh-green-pastures-2ai-seed1.rep --extra -ignoreAsserts; echo "exit=$?"
fi

for e in $G $Z; do
  n=$(basename $e)
  (cd $(dirname $e) && ./$n -win -xres 800 -yres 600 -nologo -noaudio -fixedTimeStep -quitAfter 60 -ignoreAsserts; echo "START $e EXIT=$? Lecks=$(grep -c '^Leaked' DebugLogFileD.txt)")
done

f() { n=$1; e=$2; r=$3; p=$4
  echo "##### Fahrt $n #####"
  $RR run --exe $e --scene replay --replay $r --path tests/render/paths/$p.txt --out render-out/x64ref-$n  --extra -ignoreAsserts 2>&1 | grep -E "^ABWEICHT|^FEHLER" | sed 's/, max.*//'
  echo "Fahrt $n: $(ls render-out/x64ref-$n/shot_*.bmp 2>/dev/null | wc -l) Bilder, $(grep -c '"pixels_over_threshold": 0' render-out/x64ref-$n/compare.json 2>/dev/null) pixelgleich"
  python tools/render/inspect_shots.py --quiet render-out/x64ref-$n | tail -1; }
RA=tests/replays/x64/e0-alpine-assault-2ai-seed1.rep; RZ=tests/replays/zh/x64/e0zh-green-pastures-2ai-seed1.rep
f quer $G $RA quer
f rundum $G $RA rundum
f zoom $G $RA zoom
f rand $G $RA rand
f zh-rundum $Z $RZ rundum
echo VERIFY_X64_FERTIG
