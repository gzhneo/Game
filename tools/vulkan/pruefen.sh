#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Vulkan-Pruefung ohne Windows: alle Shader-Varianten mit DXC nach SPIR-V,
# spirv-val und Bindungsbereiche (shader_spirv.py), danach jedes Modul in einem Vulkan-Geraet mit der
# Validierungsschicht laden (modulpruefung.cpp; in der Cloud-Umgebung Mesa lavapipe).
#
# Aufruf:  tools/vulkan/pruefen.sh [Ablage]     (Standard ${TMPDIR:-/tmp}/cnc-vulkan)
# Rueckgabewert 0: alles in Ordnung. Voraussetzungen: tools/linux_einrichten.sh (DXC unter /opt,
# Wine, spirv-tools, libvulkan-dev, mesa-vulkan-drivers, vulkan-validationlayers).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-${TMPDIR:-/tmp}/cnc-vulkan}"
mkdir -p "$OUT"
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine-cnc}" WINEDEBUG="${WINEDEBUG:--all}"

rm -rf "$OUT/spirv"
python3 "$HERE/shader_spirv.py" --out "$OUT/spirv" || exit 1
g++ -std=c++20 -O1 -Wall -Wextra -Werror -o "$OUT/modulpruefung" "$HERE/modulpruefung.cpp" -lvulkan || exit 2
"$OUT/modulpruefung" "$OUT/spirv"
