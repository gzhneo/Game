#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Syntaxpruefung mehrerer Dateien, parallel. Gibt nur Fehler aus.
HERE="$(cd "$(dirname "$0")" && pwd)"
printf '%s\n' "$@" | xargs -P 8 -I{} bash -c 'out=$('"$HERE"'/check.sh "{}" 2>&1); if [ -n "$out" ]; then echo "FAIL {}"; echo "$out" | grep -E "error|fatal" | head -4; fi'
echo "--- fertig ($# Dateien) ---"
