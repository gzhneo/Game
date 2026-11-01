#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
#
# Werkzeugkette fuer den Pruefgang ohne Windows bereitstellen (Debian/Ubuntu, als root: apt-get, /opt).
#
# Das Repository selbst hat keine Paketabhaengigkeiten (C++ mit CMake, gebaut wird unter Windows
# mit MSVC). Was hier installiert wird, braucht nur die Linux-seitige Pruefung:
#
#   tools/gate.sh          Engine- und Einheitentests, Kennzahl der Angleichung
#   tools/linuxtests/      UnitTests als MinGW-Cross-Build, ausgefuehrt unter Wine
#   tools/syntaxcheck/     clang mit MinGW-Headern, -fsyntax-only
#   tools/refactor/texte_im_code.py  Wortlisten (englisch, deutsch) fuer die Textpruefung
#   Sanitizer-Laeufe der EngineTests  clang-Laufzeit (libclang-rt-18-dev)
#   tools/wine/            Spiel unter Wine bauen und starten: Xvfb, Mesa-Software-OpenGL (llvmpipe fuer Wines
#                          Direct3D 11), X11-Bibliotheken; x11-apps/imagemagick fuer Bildschirmfotos;
#                          ninja-build fuer den Qt-Bau des Karteneditors (tools/wine/build_mapeditor.sh)
#   tools/assets/blender/  Blender unter /opt (Aufruf `blender`, Variable BLENDER); libegl fuer EEVEE
#   tools/wine/klicktest.sh  xdotool fuer Klicktests der Menues
#   tools/wine/lan_zwei.sh   iproute2 (ip netns) fuer die LAN-Partie zweier Instanzen
#   tools/vulkan/          Vulkan-Pruefung: Mesa lavapipe, Validierungsschicht, spirv-tools,
#                          Vulkan-Header und -Loader; DXC (dxc.exe aus dem NuGet-Paket, unter Wine) nach /opt
#   tools/mapeditor/gui_linux.sh  Karteneditor nativ mit Qt 6 des Systems und CMake (Selbsttest ohne Bildschirm)
#   tools/babylon/         Uebersetzungswerkzeug: Qt 6 des Systems fuer den Selbsttest der Oberflaeche ohne
#                          Bildschirm, LibreOffice Calc mit python3-uno und Gnumeric fuer den Praxistest der
#                          Tabellen (office_test.sh)
#
# Die Umgebungsvariablen (BLENDER, WINEPREFIX, WINEDEBUG) gehen nach $CLAUDE_ENV_FILE, wenn gesetzt, sonst nach
# stdout zum Uebernehmen in die Shell. Wiederholte Laeufe installieren nur, was fehlt.
set -euo pipefail

umgebung() {
	if [ -n "${CLAUDE_ENV_FILE:-}" ]; then echo "$1" >> "$CLAUDE_ENV_FILE"; else echo "$1"; fi
}

PKGS="clang mingw-w64-x86-64-dev g++-mingw-w64-x86-64 libz-mingw-w64-dev wine64 glslang-tools wamerican-large wngerman libclang-rt-18-dev xvfb libgl1-mesa-dri libglx-mesa0 libx11-6 libxext6 libfreetype6 x11-apps x11-utils imagemagick ninja-build libegl1 libegl-mesa0 xdotool iproute2 mesa-vulkan-drivers vulkan-tools vulkan-validationlayers spirv-tools libvulkan-dev qt6-base-dev qt6-svg-dev cmake libreoffice-calc python3-uno gnumeric"

# Idempotent: nur installieren, was fehlt (der Containerzustand wird nach dem Hook
# zwischengespeichert, ein zweiter Lauf ist dann ein No-op).
missing=""
for p in $PKGS; do
	dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q "ok installed" || missing="$missing $p"
done

if [ -n "$missing" ]; then
	export DEBIAN_FRONTEND=noninteractive
	apt-get update -qq || true
	# shellcheck disable=SC2086
	apt-get install -y --no-install-recommends $missing
fi

# Blender fuer die Skripte unter tools/assets/blender/; dieselbe Version wie dort unter Windows.
BLENDER=/opt/blender-5.2.2-linux-x64/blender
if [ ! -x "$BLENDER" ]; then
	curl -sSfL https://download.blender.org/release/Blender5.2/blender-5.2.2-linux-x64.tar.xz | tar -xJ -C /opt ||
		echo "Blender nicht installiert" >&2
fi
ln -sf "$BLENDER" /usr/local/bin/blender
umgebung "export BLENDER=$BLENDER"

# DXC fuer tools/vulkan/shader_spirv.py: dieselbe Version wie der vcpkg-Port directx-dxc der Baseline
# (vcpkg.json), als Windows-Programm aus dem NuGet-Paket; es laeuft unter Wine.
DXC_VERSION=1.9.2602.24
DXC_SHA256=4e4cef12283f7875a3602b9f5dc04f153c77cfa216559f58881305f59f8f7e2f
DXC_DIR=/opt/dxc-$DXC_VERSION
if [ ! -f "$DXC_DIR/dxc.exe" ]; then
	dxc_tmp=$(mktemp -d)
	if curl -sSfL -o "$dxc_tmp/dxc.nupkg" \
			"https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.dxc/$DXC_VERSION/microsoft.direct3d.dxc.$DXC_VERSION.nupkg" \
		&& echo "$DXC_SHA256  $dxc_tmp/dxc.nupkg" | sha256sum -c --quiet; then
		mkdir -p "$DXC_DIR"
		python3 - "$dxc_tmp/dxc.nupkg" "$DXC_DIR" <<'PY'
import os, sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
for name in z.namelist():
    base = os.path.basename(name)
    if (name.startswith("build/native/bin/x64/") and base in ("dxc.exe", "dxcompiler.dll", "dxil.dll")) \
            or (os.path.dirname(name) == "" and base.upper().startswith("LICEN")):
        with open(os.path.join(sys.argv[2], base), "wb") as f:
            f.write(z.read(name))
PY
	else
		echo "DXC nicht installiert" >&2
	fi
	rm -rf "$dxc_tmp"
fi

# Wine legt sein Praefix sonst bei jedem Lauf neu an und schreibt dabei nach stderr.
umgebung 'export WINEPREFIX="${WINEPREFIX:-$HOME/.wine-cnc}"'
umgebung 'export WINEDEBUG=-all'

echo "Pruefwerkzeuge bereit: $(x86_64-w64-mingw32-g++ --version | head -1)"
