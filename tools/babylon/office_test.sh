#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Praxistest des neuen Babylon mit echten Tabellenprogrammen: BabylonTool exportiert Übersetzungstabellen
# (.xlsx, .ods, .csv), ein Office-Paket spielt den Übersetzer (office_uno.py: öffnen, Schutz und Formeln
# prüfen, Spalte G füllen, in jedem Format speichern), BabylonTool liest die Tabelle wieder ein und die
# Übersetzungen müssen zeichengenau in der Datenbank stehen; zum Schluss eine .csf, die der Leser des
# Spiels prüft (Teil der BabylonTests).
#
#   tools/babylon/office_test.sh [--openoffice <Ordner openoffice4/program>] [Arbeitsordner]
#
# LibreOffice (soffice, python3-uno) ist Pflicht. Mit --openoffice zusätzlich Apache OpenOffice 4.1, der
# Nachfolger der StarOffice-Linie (schreibt kein .xlsx, liest es aber). Gnumeric (ssconvert), falls
# installiert, wandelt die von LibreOffice gefüllten Tabellen in das jeweils andere Format. Microsoft Excel
# und SoftMaker PlanMaker gibt es unter Linux nicht frei; deren Abnahme steht unter Windows aus.
# Rückgabewert 0, wenn alle Durchgänge bestehen.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
AOO=""
if [ "${1:-}" = "--openoffice" ]; then
	AOO="$2"
	shift 2
fi
WORK="${1:-${TMPDIR:-/tmp}/cnc-babylon-office}"
rm -rf "$WORK"
mkdir -p "$WORK"
pass=0
fail=0
ok() { echo "  ok   $*"; pass=$((pass + 1)); }
bad() { echo "  FAIL $*"; fail=$((fail + 1)); }

echo "== BabylonTool bauen"
if ! "$HERE/linux_build.sh" "$WORK/build" > "$WORK/build.log" 2>&1; then
	cat "$WORK/build.log"
	exit 1
fi
TOOL="$WORK/build/BabylonTool"

# Projekt mit Generals.str (Latin-1, mehrzeiliger Text, Sprachaufnahme, printf-Umwandlung)
BASE="$WORK/base"
mkdir -p "$BASE/Data"
printf '%b' '// Testdaten\r\nGUI:Ok\r\n"OK"\r\nEND\r\n\r\nGUI:Built\r\n// COMMENT: %%d is a number\r\n"%%d units built"\r\nEND\r\n\r\nDIALOG:Hello\r\n"Hello, commander.\\nWelcome back!" = HELLO01e\r\nEND\r\n\r\nGUI:Greeting\r\n"Gr\xfc\xdfe & \\"Quotes\\" <tags>"\r\nEND\r\n\r\nLONG:Text\r\n"This text runs\r\n   over two lines"\r\nEND\r\n' > "$BASE/Data/Generals.str"
"$TOOL" --project "$BASE" update > "$WORK/update.log" 2>&1 || { cat "$WORK/update.log"; exit 1; }

# Übersetzungen, wie sie ein Übersetzer eintippt ("\n" = Zeilenumbruch in der Zelle)
cat > "$WORK/german.tsv" <<'TSV'
10000	OK
10001	%d Einheiten gebaut
10002	Hallo, Kommandant.\nWillkommen zurück!
10003	Grüße & „Anführungszeichen“ <Tags> – €
10004	   Dieser Text hat Leerzeichen vorne und mehrere   in der Mitte
TSV
# was danach in der Datenbank stehen muss (Zeilenumbruch als Escape der Stringdateien)
cat > "$WORK/expected.txt" <<'EXP'
    German r1 "OK"
    German r1 "%d Einheiten gebaut"
    German r1 "Hallo, Kommandant.\nWillkommen zurück!"
    German r1 "Grüße & „Anführungszeichen“ <Tags> – €"
    German r1 "   Dieser Text hat Leerzeichen vorne und mehrere   in der Mitte"
EXP

# Liest eine gefüllte Tabelle in eine Kopie des Projekts ein und vergleicht die deutschen Übersetzungen.
check_import() {	# Tabelle, Bezeichnung
	local sheet=$1 what=$2 proj="$WORK/p-$RANDOM$RANDOM"
	cp -r "$BASE" "$proj"
	if ! "$TOOL" --project "$proj" import "$sheet" > "$proj.log" 2>&1; then
		bad "$what: Import gescheitert ($(tail -2 "$proj.log" | tr '\n' ' '))"
		return
	fi
	"$TOOL" --project "$proj" dump German | grep '^    German' > "$proj.dump"
	if diff -q "$WORK/expected.txt" "$proj.dump" > /dev/null; then
		if "$TOOL" --project "$proj" generate Generals --out "$proj" German > "$proj.gen" 2>&1 && [ -s "$proj/generals_ge.csf" ]; then
			ok "$what"
		else
			bad "$what: generate"
		fi
	else
		bad "$what: Übersetzungen weichen ab"
		diff "$WORK/expected.txt" "$proj.dump" | head -6
	fi
}

office_run() {	# Name, soffice, Python, Optionen-Präfix (- oder --), Eingabeformate, Ausgabeformate
	local name=$1 soffice=$2 python=$3 dash=$4 ins=$5 outs=$6 port=$((20000 + RANDOM % 20000))
	local profile="$WORK/profile-$name"
	echo "== $name"
	setsid "$soffice" ${dash}headless ${dash}invisible ${dash}norestore ${dash}nologo ${dash}nofirststartwizard \
		"-env:UserInstallation=file://$profile" "${dash}accept=socket,host=127.0.0.1,port=$port;urp;" > "$WORK/$name-office.log" 2>&1 &
	local pid=$!
	for in in $ins; do
		local exported="$WORK/$name-export.$in"
		"$TOOL" --project "$BASE" export German "$exported" --filter all > /dev/null 2>&1 || { bad "$name: Export $in"; continue; }
		for out in $outs; do
			local filled="$WORK/$name-$in-filled.$out"
			if "$python" "$HERE/office_uno.py" "$port" "$exported" "$filled" "$WORK/german.tsv" > "$filled.log" 2>&1; then
				check_import "$filled" "$name: $in -> Übersetzer -> $out"
			else
				bad "$name: $in -> $out: $(grep -m3 FAIL "$filled.log" | tr '\n' ' ')$(tail -1 "$filled.log")"
			fi
		done
	done
	kill -TERM -- "-$pid" 2> /dev/null
	wait "$pid" 2> /dev/null
}

if command -v soffice > /dev/null && python3 -c "import uno" 2> /dev/null; then
	office_run LibreOffice soffice python3 -- "xlsx ods csv" "xlsx ods csv"
else
	bad "LibreOffice mit python3-uno nicht installiert"
fi

if [ -n "$AOO" ]; then
	if [ -x "$AOO/soffice" ] && [ -x "$AOO/python" ]; then
		office_run OpenOffice "$AOO/soffice" "$AOO/python" - "xlsx ods csv" "ods csv"
	else
		bad "Apache OpenOffice nicht unter $AOO"
	fi
fi

if command -v ssconvert > /dev/null; then
	echo "== Gnumeric"
	for pair in "xlsx ods" "ods xlsx" "csv xlsx"; do
		set -- $pair
		src="$WORK/LibreOffice-$1-filled.$1"
		[ -f "$src" ] || { bad "Gnumeric: $src fehlt"; continue; }
		dst="$WORK/gnumeric-from-$1.$2"
		if ssconvert "$src" "$dst" > "$dst.log" 2>&1; then
			check_import "$dst" "Gnumeric: LibreOffice-$1 -> $2"
		else
			bad "Gnumeric: ssconvert $1 -> $2"
		fi
	done
	# ungefüllte Exporte: lesbar, nichts übersetzt
	for f in xlsx ods; do
		"$TOOL" --project "$BASE" export German "$WORK/raw.$f" --filter all > /dev/null 2>&1
		ssconvert "$WORK/raw.$f" "$WORK/raw-gnumeric.csv" > /dev/null 2>&1
		if grep -q "German Translation" "$WORK/raw-gnumeric.csv" && grep -q "HELLO01G.WAV" "$WORK/raw-gnumeric.csv"; then
			ok "Gnumeric liest den Export .$f"
		else
			bad "Gnumeric liest den Export .$f nicht"
		fi
	done
fi

echo
echo "$pass bestanden, $fail gescheitert"
[ $fail = 0 ]
