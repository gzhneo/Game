#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Baut den Treffpunkt-Server gzh-rendezvous unter Linux (oder macOS) mit g++/clang++, ohne CMake.
# Aufruf: tools/rendezvous/build_linux.sh [Ausgabedatei]   (Standard: ./gzh-rendezvous)
# Danach: ./gzh-rendezvous --selftest ; Betrieb siehe Code/Tools/Server/Rendezvous/README.md
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${1:-./gzh-rendezvous}"
CXX="${CXX:-g++}"
E="$ROOT/Code/Core/EngineCore/Engine"
"$CXX" -std=c++20 -O2 -pthread -Wall -Wextra -I "$ROOT/Code/GameEngine" -I "$ROOT/Code/Core/EngineCore" -I "$ROOT/Code/Core" \
	"$ROOT/Code/Tools/Server/Rendezvous/main.cpp" "$E/Wire.cpp" "$E/Rendezvous.cpp" "$E/RendezvousServer.cpp" \
	"$E/InviteCode.cpp" "$E/NetAddress.cpp" "$E/RateLimiter.cpp" "$E/Monocypher.cpp" -o "$OUT"
echo "gebaut: $OUT"
