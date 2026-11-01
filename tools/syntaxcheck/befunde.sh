#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Fehlersuche mit clang: alle Dateien unter Code/ mit ausgewaehlten Warnungen, die fast immer echte
# Fehler anzeigen (vertauschte memset-Argumente, sizeof auf Zeiger, delete auf void*, delete/delete[]
# vertauscht, fehlendes return, Selbstzuweisung, falsche Klammerung, ...). Liste der Warnungen in
# befunde_warnungen.txt. Ausgabe: je Befund "Datei|Meldung" ohne Zeilennummer, sortiert. Die Editor-Modelle
# (MapEditorTests, GuiEditorTests) prueft der Pruefgang streng mit g++ und clang, RenderUnitTests braucht
# DirectXTex; die drei fehlen hier.
#
# Aufruf: tools/syntaxcheck/befunde.sh <Ausgabedatei>   (Spiegel vorher mit setup.sh bauen)
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
: "${SYNTAXCHECK_DIR:=${TMPDIR:-/tmp}/cnc-syntaxcheck}"
export SYNTAXCHECK_DIR
SYNTAXCHECK_WARNINGS="$(cat "$HERE/befunde_warnungen.txt")"
export SYNTAXCHECK_WARNINGS
(cd "$ROOT/Code" && find Core Renderer GameEngine Platform App Tests \( -name '*.cpp' -o -name '*.CPP' \) \
		-not -path 'Tests/MapEditorTests/*' -not -path 'Tests/GuiEditorTests/*' -not -path 'Tests/RenderUnitTests/*' | LC_ALL=C sort) \
	| tr '\n' '\0' \
	| xargs -0 -P "$(nproc)" -I{} bash -c '"$0/check.sh" "$1" 2>&1 | grep -E "warning:"' "$HERE" {} \
	| sed -E "s#^$SYNTAXCHECK_DIR/tree/##; s#^([^:]+):[0-9]+:[0-9]+: warning: #\1|#" \
	| awk -F'|' '{ print tolower($1) "|" $2 }' \
	| LC_ALL=C sort -u > "$1"
