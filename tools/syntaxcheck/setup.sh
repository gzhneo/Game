#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Baut den Syntaxpruef-Spiegel neu. Nach Aenderungen im Repository erneut aufrufen.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
: "${SYNTAXCHECK_DIR:=${TMPDIR:-/tmp}/cnc-syntaxcheck}"
# Seit Phase 7 der Angleichung liegen die bytegleichen Bibliotheken (WWDebug, WWLib, WWMath,
# WWSaveLoad, WWDownload, Compression, Lib/) unter Code/ statt doppelt in beiden Baeumen.
# Der Spiegel nimmt beide Quellen auf, dadurch bleiben alle -I-Listen unveraendert: unter
# tree/Libraries/... steht wieder ein vollstaendiger Baum. Ueberschneidungsfrei geprueft.
# Seit Phase 8 Schritt 7 (2026-09-23) liegt der gesamte Code unter Code/, <Baum>/Code gibt es nicht
# mehr. SYNTAXCHECK_SOURCE ist deshalb nur noch ein optionaler zweiter Quellordner (Vorgabe: keiner).
: "${SYNTAXCHECK_SHARED:=$HERE/../../Code}"
mkdir -p "$SYNTAXCHECK_DIR"
rm -rf "$SYNTAXCHECK_DIR/tree"
python3 "$HERE/mktree.py"  "$SYNTAXCHECK_SHARED" "$SYNTAXCHECK_DIR/tree"
[ -n "${SYNTAXCHECK_SOURCE:-}" ] && python3 "$HERE/mktree.py" "$SYNTAXCHECK_SOURCE" "$SYNTAXCHECK_DIR/tree"
python3 "$HERE/mkalias.py" "$SYNTAXCHECK_DIR/tree"
python3 "$HERE/patches.py" "$SYNTAXCHECK_DIR/tree"
python3 "$HERE/gen_fwd.py" "$SYNTAXCHECK_DIR/tree" "$SYNTAXCHECK_DIR/shim"

# Shims fuer Includes mit Backslash-Pfaden (Windows-Schreibweise)
mkdir -p "$SYNTAXCHECK_DIR/shim2/gameengine/include/common" "$SYNTAXCHECK_DIR/shim2/anchor/a/b/c"
printf '#include "Common/Debug.h"\n' > "$SYNTAXCHECK_DIR/shim2/gameengine/include/common/debug.h"
printf '#include "argv.h"\n'         > "$SYNTAXCHECK_DIR/shim/..\\wwlib\\argv.h"
printf '#include "rect.h"\n'         > "$SYNTAXCHECK_DIR/shim/..\\wwmath\\rect.h"
printf '#include "Lib/BaseType.h"\n' > "$SYNTAXCHECK_DIR/shim/Lib\\BaseType.h"
# Zweiphasenlookup in Templates: im Spiegel "this->" ergaenzen, gefuehrt von clang.
# Eine Handvoll Dateien reicht, um alle betroffenen Header einmal anzufassen.
export SYNTAXCHECK_DIR SYNTAXCHECK_SOURCE
for f in Common/renderdevice assetmgr render2d vertmaterial pointgr mesh segline sphereobj visrasterizer streak; do
  python3 "$HERE/fixmirror.py" "Renderer/WW3D2/$f.cpp" >/dev/null 2>&1 || true
done

# Vulkan-Header und volk fuer die Vulkan-Dateien in WW3D2 (in vcpkg unter Windows)
"$HERE/../vulkan/header_holen.sh" "$SYNTAXCHECK_DIR/vulkan"

echo "Spiegel unter $SYNTAXCHECK_DIR bereit"
