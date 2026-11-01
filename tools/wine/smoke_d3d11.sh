#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Rauchtest fuer Direct3D 11 unter Wine ohne Spieldaten: Geraet, Flip-Swapchain, Loeschen, Rueckleseprobe und
# D3DCompile aller Shader aus Data/Shaders/sm4 (d3d11_smoke.cpp). Baut das Testprogramm mit MinGW.
#
# Aufruf:  tools/wine/smoke_d3d11.sh [Ablage]     (Standard ${TMPDIR:-/tmp}/cnc-wine)
# Rueckgabewert 0: Geraet und Rueckleseprobe in Ordnung. Ausgabe auch in <Ablage>/smoke_d3d11.log.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${1:-${CNC_WINE_DIR:-${TMPDIR:-/tmp}/cnc-wine}}"
mkdir -p "$OUT"
CXX=x86_64-w64-mingw32-g++-posix
command -v $CXX >/dev/null || CXX=x86_64-w64-mingw32-g++
$CXX -std=c++20 -O1 -o "$OUT/d3d11_smoke.exe" "$HERE/d3d11_smoke.cpp" -ld3d11 -ldxgi -ld3dcompiler -ldxguid -luuid \
	-static -static-libgcc -static-libstdc++ -mconsole || exit 2
export WINEPREFIX="${WINEPREFIX_CNC:-$OUT/wineprefix}" WINEDEBUG="${WINEDEBUG:--all,err+all}"
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
WINE=$(command -v wine64 || echo /usr/lib/wine/wine64)
N=$((100 + RANDOM % 800))
Xvfb ":$N" -screen 0 1280x720x24 >/dev/null 2>&1 &
XPID=$!
sleep 1
DISPLAY=":$N" timeout 120 "$WINE" "$OUT/d3d11_smoke.exe" "$(printf '%s' "$ROOT/Data/Shaders/sm4" | sed 's|^/|Z:\\|; s|/|\\|g')" 2>&1 | tee "$OUT/smoke_d3d11.log"
RC=${PIPESTATUS[0]}
kill $XPID 2>/dev/null
exit $RC
