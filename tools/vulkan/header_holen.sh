#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Vulkan-Header (Khronos), volk (Header-Fassung) und der Vulkan Memory Allocator (ein Header) fuer die
# Linux-Bauten ohne vcpkg: MinGW-Bau des Spiels (tools/wine/build_game.sh) und Syntaxpruefung (tools/syntaxcheck).
# Derselbe Stand wie die vcpkg-Ports (vulkan-headers und volk 1.4.357.0, vulkan-memory-allocator 3.4.0); alle
# oeffentlich (Apache-2.0/MIT, MIT, MIT).
#
# Aufruf:  tools/vulkan/header_holen.sh <Ablage>
# Ergebnis: <Ablage>/Vulkan-Headers/include, <Ablage>/volk (volk.h, volk.c) und
# <Ablage>/VulkanMemoryAllocator/include (vk_mem_alloc.h). Ein zweiter Aufruf ist ein No-op.
set -u
VK_TAG=vulkan-sdk-1.4.357.0
ZIEL="${1:?Aufruf: header_holen.sh <Ablage>}"
mkdir -p "$ZIEL" || exit 1
VMA_TAG=v3.4.0
holen() { # <Ordner> <Repo> <Tag>
	[ -f "$ZIEL/$1/.stand-$3" ] && return 0
	rm -rf "$ZIEL/$1"
	git -c advice.detachedHead=false clone -q --depth 1 --branch "$3" "https://github.com/$2.git" "$ZIEL/$1" \
		&& touch "$ZIEL/$1/.stand-$3" || { echo "Klonen von $2 ($3) fehlgeschlagen"; return 1; }
}
holen Vulkan-Headers KhronosGroup/Vulkan-Headers "$VK_TAG" || exit 1
holen volk zeux/volk "$VK_TAG" || exit 1
holen VulkanMemoryAllocator GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator "$VMA_TAG" || exit 1
