#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Holt Apache OpenOffice 4.1.15 (Linux x86-64, deutsch) für tools/babylon/office_test.sh --openoffice und
# entpackt die Pakete ohne Installation in einen Ordner. OpenOffice ist der Nachfolger der StarOffice-Linie;
# es liest .xlsx, schreibt aber nur .ods und .csv. Die Prüfsumme kommt von archive.apache.org.
#
#   tools/babylon/openoffice_holen.sh [Zielordner]     (Vorgabe: /tmp/cnc-gate/openoffice)
#
# Danach: tools/babylon/office_test.sh --openoffice <Zielordner>/opt/openoffice4/program
set -eu
DEST="${1:-${GATE_DIR:-${TMPDIR:-/tmp}/cnc-gate}/openoffice}"
URL=https://archive.apache.org/dist/openoffice/4.1.15/binaries/de/Apache_OpenOffice_4.1.15_Linux_x86-64_install-deb_de.tar.gz
DL="$(mktemp -d)"
trap 'rm -rf "$DL"' EXIT
curl -sS --fail --max-time 900 -o "$DL/aoo.tar.gz" "$URL"
expected=$(curl -sS --fail "$URL.sha256" | cut -d' ' -f1)
actual=$(sha256sum "$DL/aoo.tar.gz" | cut -d' ' -f1)
if [ "$expected" != "$actual" ]; then
	echo "Prüfsumme stimmt nicht: $actual statt $expected"
	exit 1
fi
mkdir -p "$DL/x" "$DEST"
tar -xzf "$DL/aoo.tar.gz" -C "$DL/x"
for f in "$DL"/x/*/DEBS/*.deb; do
	dpkg -x "$f" "$DEST"
done
echo "Apache OpenOffice in $DEST/opt/openoffice4/program"
