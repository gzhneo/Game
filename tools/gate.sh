#!/bin/bash
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Prüfgang vor einem Commit, der ohne Windows-Rechner läuft.
#
# Gedacht für die Angleichung und jede andere Arbeit am Bestand: Was hier
# durchfällt, fällt auch unter MSVC durch. Umgekehrt gilt das **nicht** -- ein grüner Lauf ersetzt
# den Windows-Build, ctest, die Replay-Determinismusprüfung und den Bildvergleich nicht.
#
#   tools/gate.sh           schnell (rund 5 Minuten): Engine- und Einheitentests, Kennzahl
#   tools/gate.sh --full    zusätzlich Syntaxprüfung, clang-Fehlersuche und Shader (rund 20 Minuten)
#   tools/gate.sh --editor  zusätzlich die Selbsttests des Karteneditors und des GUI-Editors unter Wine
#                           (tools/wine/build_mapeditor.sh, run_mapeditor.sh, run_guieditor.sh; der erste Bau mit Engine und Qt dauert rund 45 Minuten). Mit --full
#                           allein läuft er, sobald Qt und Engine in der Ablage gebaut sind.
#
# Rückgabewert 0, wenn alles durchläuft. Voraussetzungen: siehe tools/linux_einrichten.sh
# beziehungsweise tools/linuxtests/README.md.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
FULL=0
EDITOR=0
for a in "$@"; do
	case "$a" in
		--full) FULL=1 ;;
		--editor) EDITOR=1 ;;
		*) echo "Unbekannter Schalter: $a"; exit 2 ;;
	esac
done

WORK="${GATE_DIR:-${TMPDIR:-/tmp}/cnc-gate}"
mkdir -p "$WORK"
fails=0
step() { printf '\n=== %s\n' "$1"; }
ok()   { printf '   OK   %s\n' "$1"; }
bad()  { printf '   FEHLER %s\n' "$1"; fails=$((fails + 1)); }

# ------------------------------------------------------------------------------------------------
step "EngineTests (nativ, g++ für Linux)"
# Das Ziel hängt an keinem Engine-Header und braucht weder Wine noch Assets. Die Engine-Bausteine
# liegen seit Phase 7 Teil 3 für beide Bäume unter Code/GameEngine, der Test selbst seit Teil 5 unter
# Code/Tests/EngineTests (wie in der CMake-Fassung). Seit Phase 8 Schritt 2 gibt es keine Header mehr im
# Baum, der fruehere Suchpfad GeneralsMD/Code/GameEngine/Include ist entfallen.
if g++ -std=c++20 -O1 -pthread \
      -I "$ROOT/Code/GameEngine" -I "$ROOT/Code/Core/EngineCore" -I "$ROOT/Code/Core" -I "$ROOT/Code/Core/WWLib" \
      -o "$WORK/enginetests" \
      "$ROOT/Code/Tests/EngineTests/EngineTests.cpp" \
      "$ROOT"/Code/Core/EngineCore/Engine/*.cpp -lz 2> "$WORK/enginetests.log"; then
	if "$WORK/enginetests" > "$WORK/enginetests.out" 2>&1; then
		ok "$(tail -1 "$WORK/enginetests.out")"
	else
		bad "EngineTests fehlgeschlagen -- $WORK/enginetests.out"
	fi
else
	bad "EngineTests übersetzen nicht -- $WORK/enginetests.log"
fi

# ------------------------------------------------------------------------------------------------
step "EngineTests unter AddressSanitizer/UBSan und ThreadSanitizer (clang)"
# Speicherfehler, undefiniertes Verhalten und Datenrennen im Job-System, Log und Watchdog. clang statt
# g++: seine UBSan fand den Ueberlauf in SimRandom::valueInRange, den g++ nicht zeigte, und die TSan von
# g++ 13 faengt pthread_mutex_clocklock (std::timed_mutex::try_lock_for) nicht ab und meldet dann
# falsche Rennen.
san_src=("$ROOT/Code/Tests/EngineTests/EngineTests.cpp" "$ROOT"/Code/Core/EngineCore/Engine/*.cpp)
for san in address,undefined thread; do
	if clang++ -std=c++20 -O1 -g -pthread -fsanitize=$san -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
	      -I "$ROOT/Code/GameEngine" -I "$ROOT/Code/Core/EngineCore" -I "$ROOT/Code/Core" -I "$ROOT/Code/Core/WWLib" -o "$WORK/enginetests_${san%%,*}" "${san_src[@]}" -lz \
	      2> "$WORK/enginetests_${san%%,*}.log" \
	   && (cd "$WORK" && "./enginetests_${san%%,*}" > "enginetests_${san%%,*}.out" 2>&1); then
		ok "$san: $(tail -1 "$WORK/enginetests_${san%%,*}.out")"
	else
		bad "$san -- $WORK/enginetests_${san%%,*}.out (Build: .log)"
		grep -m3 -E "ERROR: AddressSanitizer|runtime error|LeakSanitizer|WARNING: ThreadSanitizer|FAILED" "$WORK/enginetests_${san%%,*}.out"
	fi
done

# ------------------------------------------------------------------------------------------------
step "Dateisystem unter Last (ThreadSanitizer)"
# Acht Leser suchen und lesen Dateien aus echten .big-Dateien (positionsgenau, ohne Sperre je Archiv),
# waehrend ein Einhaenger Ebenen einhaengt und abhaengt; jeder Lesevorgang prueft die Bytes gegen die
# Ebene, die der Index nannte. Nutzt das TSan-Programm des Schritts davor.
if [ -x "$WORK/enginetests_thread" ] \
   && (cd "$WORK" && timeout 300 ./enginetests_thread --vfs-last 100000 > vfslast.out 2>&1) \
   && ! grep -q "WARNING: ThreadSanitizer" "$WORK/vfslast.out"; then
	ok "$(grep -m1 accesses "$WORK/vfslast.out" | sed 's/^ *//')"
else
	bad "Lasttest -- $WORK/vfslast.out"
	grep -m3 -E "WARNING: ThreadSanitizer|FAILED" "$WORK/vfslast.out"
fi

# ------------------------------------------------------------------------------------------------
step "MapEditorTests: Kartenmodell des neuen Editors (g++ streng, clang mit ASan/UBSan)"
# Code/Tools/Editors/MapEditor/Model mit der Kompressionsbibliothek des Spiels; ohne Spieldaten und Grafik.
# Der Windows-Build (MinGW unter Wine) laeuft mit --full.
for me_mode in native sanitize; do
	me_flag=""
	[ $me_mode = sanitize ] && me_flag=--sanitize
	if "$HERE/mapeditor/linux_build.sh" $me_flag "$WORK/mapeditor-$me_mode" > "$WORK/mapeditor-$me_mode.log" 2>&1 \
	   && (cd "$WORK/mapeditor-$me_mode" && ./MapEditorTests > run.txt 2>&1); then
		ok "$me_mode: $(tail -1 "$WORK/mapeditor-$me_mode/run.txt")"
	else
		bad "$me_mode -- $WORK/mapeditor-$me_mode.log, $WORK/mapeditor-$me_mode/run.txt"
		grep -m5 -E "error|FAILED|ERROR|runtime error" "$WORK/mapeditor-$me_mode.log" "$WORK/mapeditor-$me_mode/run.txt" 2>/dev/null
	fi
done

# ------------------------------------------------------------------------------------------------
step "GuiEditorTests: Layout-Modell des GUI-Editors (g++ streng, clang mit ASan/UBSan, Testlayouts)"
# Code/Tools/Editors/GuiEditor/Model ohne Engine; die Testlayouts von tools/wine/testdaten muessen bytegenau
# zurueckgeschrieben werden und ohne Fehler durch die Pruefung kommen (auch mit WndTool).
for ge_mode in native sanitize; do
	ge_flag=""
	[ $ge_mode = sanitize ] && ge_flag=--sanitize
	ge_dir="$WORK/guieditor-$ge_mode"
	if "$HERE/guieditor/linux_build.sh" $ge_flag "$ge_dir" > "$ge_dir.log" 2>&1 \
	   && (cd "$ge_dir" && ./GuiEditorTests wnd > run.txt 2>&1 && ./WndTool --roundtrip wnd >> run.txt 2>&1 \
	       && ./WndTool --check wnd >> run.txt 2>&1); then
		ok "$ge_mode: $(grep -E 'checks,' "$ge_dir/run.txt"); WndTool: $(tail -1 "$ge_dir/run.txt")"
	else
		bad "$ge_mode -- $ge_dir.log, $ge_dir/run.txt"
		grep -m5 -E "error|FAILED|ERROR|runtime error|differently" "$ge_dir.log" "$ge_dir/run.txt" 2>/dev/null
	fi
done

# ------------------------------------------------------------------------------------------------
step "BabylonTests: Übersetzungswerkzeug (g++ streng, clang mit ASan/UBSan)"
# Code/Tools/Editors/Babylon/Model und Core: main.db bytegenau wie das Original, Generals.str, .csf gegen den
# Leser des Spiels, Tabellen .xlsx/.ods/.csv, Rundlauf der Projekttexte Data/Text; dazu der Rundlauf
# .csf -> Generals.str -> main.db -> .csf mit BabylonTool (tools/babylon/rundlauf.py). Der Windows-Build (MinGW
# unter Wine) laeuft mit --full.
for bb_mode in native sanitize; do
	bb_flag=""
	[ $bb_mode = sanitize ] && bb_flag=--sanitize
	bb_dir="$WORK/babylon-$bb_mode"
	if "$HERE/babylon/linux_build.sh" $bb_flag "$bb_dir" > "$bb_dir.log" 2>&1 \
	   && (cd "$bb_dir" && ./BabylonTests "$ROOT" > run.txt 2>&1) \
	   && python3 "$HERE/babylon/rundlauf.py" "$bb_dir/BabylonTool" "$bb_dir/rundlauf" >> "$bb_dir/run.txt" 2>&1; then
		ok "$bb_mode: $(tail -2 "$bb_dir/run.txt" | tr '\n' ' ')"
	else
		bad "$bb_mode -- $bb_dir.log, $bb_dir/run.txt"
		grep -m5 -E "error|FAILED|ERROR|runtime error" "$bb_dir.log" "$bb_dir/run.txt" 2>/dev/null
	fi
done

# ------------------------------------------------------------------------------------------------
step "Babylon-Oberfläche: Selbsttest ohne Bildschirm (Qt 6 des Systems)"
# Bau mit dem Qt der Distribution, Selbsttest mit QT_QPA_PLATFORM=offscreen; Bilder unter $WORK/babylon-gui/selftest.
"$HERE/babylon/gui_linux.sh" "$WORK/babylon-gui" > "$WORK/babylon-gui.log" 2>&1
bb_rc=$?
if [ $bb_rc = 0 ]; then
	ok "$(tail -1 "$WORK/babylon-gui.log")"
elif [ $bb_rc = 2 ]; then
	echo "   HINWEIS nicht gelaufen: $(tail -1 "$WORK/babylon-gui.log")"
else
	bad "Bau oder Selbsttest -- $WORK/babylon-gui.log"
	grep -m8 -E "FAIL|error" "$WORK/babylon-gui.log"
fi

# ------------------------------------------------------------------------------------------------
step "Karteneditor-Oberfläche: Selbsttest ohne Bildschirm (Qt 6 des Systems, ohne Engine)"
# Start-, Karten- und Einstellungsseite in Hell/Dunkel und Deutsch/Englisch; Bilder unter $WORK/mapeditor-gui/selftest.
"$HERE/mapeditor/gui_linux.sh" "$WORK/mapeditor-gui" > "$WORK/mapeditor-gui.log" 2>&1
me_rc=$?
if [ $me_rc = 0 ]; then
	ok "$(tail -1 "$WORK/mapeditor-gui.log")"
elif [ $me_rc = 2 ]; then
	echo "   HINWEIS nicht gelaufen: $(tail -1 "$WORK/mapeditor-gui.log")"
else
	bad "Bau oder Selbsttest -- $WORK/mapeditor-gui.log"
	tail -8 "$WORK/mapeditor-gui.log"
fi

# ------------------------------------------------------------------------------------------------
step "GUI-Editor-Oberfläche: Selbsttest ohne Bildschirm (Qt 6 des Systems, ohne Engine)"
# Testlayouts in Modell und Baum, Eigenschaften, Sprache und Thema, Bearbeiten, Speichern, Vergleichen;
# Bilder unter $WORK/guieditor-gui/selftest. Die Zeichnung durch das Spiel prüft --editor unter Wine.
"$HERE/guieditor/gui_linux.sh" "$WORK/guieditor-gui" > "$WORK/guieditor-gui.log" 2>&1
gg_rc=$?
if [ $gg_rc = 0 ]; then
	ok "$(tail -1 "$WORK/guieditor-gui.log")"
elif [ $gg_rc = 2 ]; then
	echo "   HINWEIS nicht gelaufen: $(tail -1 "$WORK/guieditor-gui.log")"
else
	bad "Bau oder Selbsttest -- $WORK/guieditor-gui.log"
	tail -8 "$WORK/guieditor-gui.log"
fi

# ------------------------------------------------------------------------------------------------
# Seit Phase 8 Schritt 6 gibt es die UnitTests einmal (nicht mehr je Baum).
step "UnitTests (MinGW-Cross-Build, Wine)"
if LINUXTESTS_DIR="$WORK/linuxtests" "$HERE/linuxtests/run.sh" > "$WORK/unittests.out" 2>&1; then
	ok "$(grep -E '^All .* tests passed' "$WORK/unittests.out" || echo 'durchgelaufen')"
else
	bad "UnitTests -- $WORK/unittests.out"
	grep -E '^\[FAIL\]|FAILED|error:|undefined reference' "$WORK/unittests.out" | head -5
fi

# ------------------------------------------------------------------------------------------------
step "Netzpakete: Rundlauf, Differenztest gegen Referenz, Fuzzing (Wine)"
# Rundlauf gegen tools/fuzz/netpacket/golden.txt (Leitungsformat unveraendert), Zufallsfolgen
# schreiben und lesen gegen den Referenzstand der Serialisierung (REF_COMMIT in run.sh) und Fuzzing
# von NetPacket; jede Eingabe endet vor einer gesperrten Seite.
# Schnell 50 000 Eingaben und 2 000 Folgen, --full 500 000 und 20 000.
NET_ITER=50000
NET_DIFF=2000
[ "$FULL" = 1 ] && NET_ITER=500000 && NET_DIFF=20000
if NETPACKET_DIFF="$NET_DIFF" LINUXTESTS_DIR="$WORK/linuxtests" "$HERE/fuzz/netpacket/run.sh" "$NET_ITER" > "$WORK/netpacket.out" 2>&1 \
	&& grep -q "^fuzz: .* no fault" "$WORK/netpacket.out" && grep -q "^diff: .* 0 differences" "$WORK/netpacket.out" \
	&& grep -q "^leaks: .* 0 leaking" "$WORK/netpacket.out" && grep -q "^fuzz leaks: 0 " "$WORK/netpacket.out"; then
	ok "$(grep '^round trip' "$WORK/netpacket.out"); $(grep '^leaks' "$WORK/netpacket.out"); $(grep '^diff' "$WORK/netpacket.out" | sed -E 's/ \(seed [0-9]+\)//'); $(grep '^fuzz' "$WORK/netpacket.out" | paste -sd ';')"
else
	bad "Netzpakete -- $WORK/netpacket.out"
	grep -E '^\[FAIL\]|page fault|fehlgeschlagen|nicht uebersetzbar|Unhandled' "$WORK/netpacket.out" | head -5
fi

# ------------------------------------------------------------------------------------------------
step "Texte im Code englisch, ohne Marker"
# Zeichenketten mit deutschen Woertern oder Kennungen (R-nn, E-n.n, TODO nn) fallen durch; feste
# Schluessel, die Werkzeuge auswerten, stehen in tools/refactor/texte_schluessel.txt.
if python3 "$HERE/refactor/texte_im_code.py" > "$WORK/texte.txt" 2>&1; then
	ok "$(tail -1 "$WORK/texte.txt")"
else
	bad "neue deutsche Texte oder Marker in Zeichenketten -- $WORK/texte.txt"
	head -10 "$WORK/texte.txt"
fi

# ------------------------------------------------------------------------------------------------
step "Debug-Ausgabe nach Bedingung in Klammern (MSVC C4390 im Release-Bau)"
# Im Release-Bau sind WWDEBUG_SAY/DEBUG_LOG leer: "if (x) WWDEBUG_SAY((...));" wird zur leeren Anweisung, MSVC
# meldet C4390 und bricht mit /WX ab; clang meldet das bei Makros nicht. StateMachine.cpp: die Stellen stehen in
# einem Block, den nur Debug-Baue uebersetzen.
leer=$(grep -rnE '^\s*(\}\s*)?(if|else if|for|while)\s*\(.*\)\s*(WWDEBUG_SAY|WWDEBUG_WARNING|DEBUG_LOG)\(\(|^\s*(\}\s*)?else\s+(WWDEBUG_SAY|WWDEBUG_WARNING|DEBUG_LOG)\(\(' \
	"$ROOT/Code" --include=*.cpp --include=*.h | grep -v '/StateMachine\.cpp:' || true)
if [ -z "$leer" ]; then
	ok "keine Debug-Ausgabe ohne Klammern nach if/else/for/while"
else
	bad "Debug-Ausgabe ohne Klammern nach einer Bedingung (C4390 im Release-Bau)"
	echo "$leer" | head -5
fi

# ------------------------------------------------------------------------------------------------
step "Replay-Befehlswerkzeug liest die Referenzreplays"
# tools/fuzz/replaycmd: Format lesen und unverändert schreiben; die Befehlsnummern aus MessageStream.h.
# grep ohne -q: es liest die Liste ganz, sonst bricht replaycmd.py unter pipefail je nach Zeitverhalten mit
# BrokenPipeError ab (Wackler bei Last).
if python3 "$HERE/fuzz/replaycmd/replaycmd.py" selftest $(find "$ROOT/tests/replays" -name '*.rep' | sort) > "$WORK/replaycmd.txt" 2>&1 \
	&& python3 "$HERE/fuzz/replaycmd/replaycmd.py" types | grep '^1092 MSG_SET_REPLAY_CAMERA$' > /dev/null; then
	ok "$(grep -c '^OK' "$WORK/replaycmd.txt") Replays gelesen und gleich geschrieben"
else
	bad "Replay-Format oder Befehlsnummern -- $WORK/replaycmd.txt"
	grep -v '^OK' "$WORK/replaycmd.txt" | head -5
fi

step "INI-Tabellen mit Endeintrag"
# FieldParse-Tabellen und Namenslisten der Listen-Parser enden mit nullptr; sonst liest ein
# unbekanntes Feld oder ein unbekannter Name einer INI-Datei hinter dem Array weiter.
if python3 "$HERE/refactor/tabellenende.py" > "$WORK/tabellenende.txt" 2>&1; then
	ok "$(tail -1 "$WORK/tabellenende.txt")"
else
	bad "Tabelle ohne Endeintrag -- $WORK/tabellenende.txt"
	head -10 "$WORK/tabellenende.txt"
fi

# ------------------------------------------------------------------------------------------------
step "Platztabelle der Shader: renderslots.h gleich slots.hlsli, keine Plätze als Zahl"
if python3 "$HERE/render/plaetze_pruefen.py" > "$WORK/plaetze.txt" 2>&1; then
	ok "$(tail -1 "$WORK/plaetze.txt")"
else
	bad "Platztabelle -- $WORK/plaetze.txt"
	head -10 "$WORK/plaetze.txt"
fi

# ------------------------------------------------------------------------------------------------
step "Schichten des Renderers: Common, D3D11 und Vulkan binden sich nicht gegenseitig ein, der Rest von Code/ keine der Schichten"
if python3 "$HERE/refactor/renderer_schichten.py" > "$WORK/schichten.txt" 2>&1; then
	ok "$(tail -1 "$WORK/schichten.txt")"
else
	bad "Schichten des Renderers -- $WORK/schichten.txt"
	grep VERSTOSS "$WORK/schichten.txt" | head -10
fi

# ------------------------------------------------------------------------------------------------
step "Projekttexte des Spiels (Data/Text): Sprachen vollständig, Kennungen benutzt"
if python3 "$HERE/refactor/projekttexte.py" > "$WORK/projekttexte.txt" 2>&1; then
	ok "$(tail -1 "$WORK/projekttexte.txt")"
else
	bad "Projekttexte -- $WORK/projekttexte.txt"
	head -10 "$WORK/projekttexte.txt"
fi

# ------------------------------------------------------------------------------------------------
step "Schnittstellennamen: Inventar entspricht dem Code"
# Namen, die der Code fest vom Spielinhalt erwartet; ein neuer fest verdrahteter Name soll im Inventar auffallen.
if python3 "$HERE/inhalt/schnittstellen.py" --pruefen > "$WORK/schnittstellen.txt" 2>&1; then
	ok "$(tail -1 "$WORK/schnittstellen.txt")"
else
	bad "Inventar veraltet -- $WORK/schnittstellen.txt"
	head -10 "$WORK/schnittstellen.txt"
fi

# ------------------------------------------------------------------------------------------------
step "Treffpunkt-Server: Linux-Bau und Selbsttest"
if "$HERE/rendezvous/build_linux.sh" "$WORK/gzh-rendezvous" > "$WORK/rendezvous.txt" 2>&1 \
	&& "$WORK/gzh-rendezvous" --selftest >> "$WORK/rendezvous.txt" 2>&1; then
	ok "$(tail -1 "$WORK/rendezvous.txt")"
else
	bad "Treffpunkt-Server -- $WORK/rendezvous.txt"
	tail -5 "$WORK/rendezvous.txt"
fi

# ------------------------------------------------------------------------------------------------
step "Dateisystem: VfsDump gegen die alte Vorrangregel"
if "$HERE/dateisystem/pruefen.sh" "$WORK/dateisystem" > "$WORK/dateisystem.txt" 2>&1; then
	ok "$(tail -1 "$WORK/dateisystem.txt")"
else
	bad "Dateisystem -- $WORK/dateisystem.txt"
	tail -8 "$WORK/dateisystem.txt"
fi

# ------------------------------------------------------------------------------------------------
step "Logik-Kennung passt zu den Referenzreplays (tests/replays)"
if python3 "$HERE/determinism/logikkennung.py" --pruefen > "$WORK/logikkennung.txt" 2>&1; then
	ok "$(tail -1 "$WORK/logikkennung.txt")"
else
	bad "$(tail -1 "$WORK/logikkennung.txt")"
fi

# ------------------------------------------------------------------------------------------------
step "Karteneditor, GUI-Editor und Babylon: jeder Text hat eine deutsche Übersetzung"
if python3 "$HERE/mapeditor/lang_check.py" > "$WORK/lang.txt" 2>&1; then
	ok "$(tr '\n' ' ' < "$WORK/lang.txt")"
else
	bad "Übersetzungen fehlen in Code/Tools/<Editor>/App/Resources/de.lang -- $WORK/lang.txt"
	head -10 "$WORK/lang.txt"
fi

# ------------------------------------------------------------------------------------------------
step "Schichten unter Code/: Core, Renderer, GameEngine, Platform, darüber App, Games, Tools, Tests"
# Eine Include-Zeile zeigt nur in dieselbe oder eine tiefere Schicht; die bekannten Abweichungen stehen mit
# Grund in tools/schichten/ausnahmen.txt. Eine neue Abweichung oder eine veraltete Ausnahme schlägt fehl.
if python3 "$HERE/schichten/pruefen.py" > "$WORK/schichten.txt" 2>&1; then
	ok "$(tail -1 "$WORK/schichten.txt")"
else
	bad "Schichtung -- $WORK/schichten.txt"
	head -10 "$WORK/schichten.txt"
fi

# ------------------------------------------------------------------------------------------------
step "Spiel-Ordner enthalten nur ihre drei Dateien"
# Der Code liegt einmal unter Code/; Code/Games/Generals und Code/Games/ZeroHour enthalten nur
# Parameter, GameEdition.cpp und Programmsymbol, Generals/ und GeneralsMD/ gibt es im Hauptverzeichnis
# nicht mehr. Die frühere Kennzahl der Angleichung (Vergleich der zwei Bäume) ist entfallen.
if python3 "$ROOT/tools/mdsync/treediff.py" --overlap > "$WORK/overlap.txt" 2>&1; then
	ok "$(tail -1 "$WORK/overlap.txt")"
else
	bad "Spiel-Ordner -- $WORK/overlap.txt"
	head -5 "$WORK/overlap.txt"
fi

# ------------------------------------------------------------------------------------------------
step "Texturstufen-Werkzeug, Selbsttest"
# Ohne EA-Daten: BIG-Archive, DDS-Kopf, Auswahl- und Rückfallregeln; mit numpy und Pillow auch
# Vergrößern, Mip-Kette, 1-Bit-Alpha, DXT-Kodierung, Farbangleichung der KI und Mauszeiger (sonst übersprungen).
# Dazu der KI-Satz aus vorhandenen Ergebnissen (ki_satz.py) und die Kennzahlen alt/neu gegen SD
# (stufen_kennzahlen.py, braucht numpy und Pillow).
if python3 "$ROOT/tools/assets/build_texture_tiers.py" --selftest > "$WORK/texture_tiers.txt" 2>&1 \
   && python3 "$ROOT/tools/assets/ki_satz.py" --selftest >> "$WORK/texture_tiers.txt" 2>&1 \
   && { ! python3 -c "import numpy, PIL" 2> /dev/null \
        || python3 "$ROOT/tools/assets/stufen_kennzahlen.py" --selftest >> "$WORK/texture_tiers.txt" 2>&1; }; then
	ok "$(grep -c "^Selbsttest: bestanden" "$WORK/texture_tiers.txt") Selbsttests bestanden (Stufen, KI-Satz, Kennzahlen)"
else
	bad "Selbsttest -- $WORK/texture_tiers.txt"
	grep FEHLGESCHLAGEN "$WORK/texture_tiers.txt" | head -5
fi

# ------------------------------------------------------------------------------------------------
step "Texturpaket-Werkzeug, Selbsttest"
# Ohne EA-Daten: Auswahl der höchsten Fassung, Originalgrößen im Manifest, Ausschlüsse; mit numpy und
# Pillow auch das Ergänzen der Mip-Kette.
if python3 "$ROOT/tools/assets/build_texture_package.py" --selftest > "$WORK/texture_package.txt" 2>&1; then
	ok "$(tail -1 "$WORK/texture_package.txt")"
else
	bad "Selbsttest -- $WORK/texture_package.txt"
	grep FEHLGESCHLAGEN "$WORK/texture_package.txt" | head -5
fi

# ------------------------------------------------------------------------------------------------
step "Geräteschicht: GPU_-Aufzählungen entsprechen den D3D11-Headern (tools/vulkan/gputypes_erzeugen.py)"
if python3 "$HERE/vulkan/gputypes_erzeugen.py" --pruefen > "$WORK/gputypes.txt" 2>&1; then
	ok "$(tail -1 "$WORK/gputypes.txt")"
else
	bad "gputypes -- $WORK/gputypes.txt"
	tail -3 "$WORK/gputypes.txt"
fi

# ------------------------------------------------------------------------------------------------
step "Vulkan: Shader-Varianten nach SPIR-V (DXC), Module unter der Validierungsschicht"
# Jede Variante, die das D3D11-Backend baut, mit DXC (Wine) nach SPIR-V, spirv-val, Bindungsbereiche;
# danach jedes Modul in Mesa lavapipe mit VK_LAYER_KHRONOS_validation. Rund 20 Sekunden.
if "$HERE/vulkan/pruefen.sh" "$WORK/vulkan" > "$WORK/vulkan.txt" 2>&1; then
	ok "$(grep -E '^[0-9]+ Varianten' "$WORK/vulkan.txt" | head -1); $(tail -1 "$WORK/vulkan.txt")"
else
	bad "Vulkan -- $WORK/vulkan.txt"
	grep -E 'FEHLER|Fehler|fehlt|nicht gefunden' "$WORK/vulkan.txt" | head -8
fi

# ------------------------------------------------------------------------------------------------
if [ "$FULL" = "1" ]; then
	step "Shader übersetzen (glslangValidator)"
	shader_fails=0
	# Seit 2026-09-23 ein gemeinsamer Datenordner fuer beide Spiele (Data/). Seit TODO 67 Teil 2
	# (2026-09-26) nur noch Shader Model 4 (sm4/); die D3D9-Quellen daneben sind entfernt.
	for f in "$ROOT"/Data/Shaders/sm4/*.hlsl; do
		[ -e "$f" ] || continue
		case "$f" in *.vs.hlsl) stage=vert ;; *.ps.hlsl) stage=frag ;; *) continue ;; esac
		if ! (cd "$(dirname "$f")" && glslangValidator -D -V -e main -S "$stage" "$(basename "$f")" \
		      -o /dev/null > /dev/null 2>&1); then
			echo "   $f"
			shader_fails=$((shader_fails + 1))
		fi
	done
	[ "$shader_fails" = "0" ] && ok "alle Shader übersetzen" || bad "$shader_fails Shader übersetzen nicht"

	step "Syntaxprüfung (clang, --target=x86_64-w64-mingw32)"
	# Seit Phase 8 Schritt 7 ein Durchgang über Code/ (früher je Baum einer, die gemeinsamen Dateien
	# also doppelt; die Zahl halbiert sich dadurch gegenüber dem alten Stand).
	export SYNTAXCHECK_DIR="$WORK/sc"
	"$ROOT/tools/syntaxcheck/setup.sh" > /dev/null 2>&1
	# Dateiliste als Array: Pfade mit Leerzeichen bleiben ganz (der Code hat keine mehr, Werkzeuge sollen sie aber vertragen).
	mapfile -t files < <(cd "$ROOT/Code" && find Core Renderer GameEngine Platform App -not -path 'Core/EngineCore/*' \
	        \( -name '*.cpp' -o -name '*.CPP' \) 2>/dev/null | LC_ALL=C sort)
	(cd "$ROOT" && "$ROOT/tools/syntaxcheck/checkall.sh" "${files[@]}") 2>&1 \
	    | grep -E '^FAIL|fertig' > "$WORK/syntax.txt"
	n=$(grep -c '^FAIL' "$WORK/syntax.txt")
	# Ein Teil des Bestands übersetzt mit clang/MinGW grundsätzlich nicht (alte MSVC-Konstrukte,
	# ältere MinGW-Header). Diese Dateien stehen in tools/syntaxcheck/baseline.txt; fehlschlagen
	# lässt den Prüfgang nur eine Datei, die dort nicht steht.
	baseline="$ROOT/tools/syntaxcheck/baseline.txt"
	sed -n 's/^FAIL //p' "$WORK/syntax.txt" | LC_ALL=C sort > "$WORK/syntax_fail.txt"
	grep -v '^#' "$baseline" | LC_ALL=C sort > "$WORK/syntax_base.txt"
	neu=$(LC_ALL=C comm -23 "$WORK/syntax_fail.txt" "$WORK/syntax_base.txt")
	behoben=$(LC_ALL=C comm -13 "$WORK/syntax_fail.txt" "$WORK/syntax_base.txt")
	if [ -n "$neu" ]; then
		bad "neu nicht übersetzbar (Einzelprüfung: tools/syntaxcheck/check.sh <Pfad>):"
		printf '%s\n' "$neu" | sed 's/^/          /'
	else
		ok "$n nicht übersetzbare Dateien, alle bekannt (tools/syntaxcheck/baseline.txt)"
	fi
	if [ -n "$behoben" ]; then
		echo "   HINWEIS gehen jetzt durch, aus baseline.txt streichen:"
		printf '%s\n' "$behoben" | sed 's/^/          /'
	fi

	step "Fuzzer mit geladener Spiellogik: map.ini, .map, Replays, Spielstände, LAN-Lobby, Netzspiel (MinGW, Wine)"
	# map.ini: alle Blockparser und Modul-Feldparser; .map: Kartenliste, Seiten, Skripte, Trigger
	# (tools/fuzz/engine/README.md)
	if ENGINEFUZZ_DIR="$WORK/enginefuzz" "$ROOT/tools/fuzz/engine/run.sh" ini 200000 1 > "$WORK/inifuzz.txt" 2>&1; then
		ok "$(grep '^[0-9]* inputs' "$WORK/inifuzz.txt")"
	else
		bad "INI-Fuzzer -- $WORK/inifuzz.txt"
		grep -v '^$' "$WORK/inifuzz.txt" | tail -12
	fi
	if ENGINEFUZZ_DIR="$WORK/enginefuzz" "$ROOT/tools/fuzz/engine/run.sh" map 50000 1 > "$WORK/mapfuzz.txt" 2>&1; then
		ok "$(grep '^[0-9]* inputs' "$WORK/mapfuzz.txt")"
	else
		bad "Karten-Fuzzer -- $WORK/mapfuzz.txt"
		grep -v '^$' "$WORK/mapfuzz.txt" | tail -12
	fi
	if ENGINEFUZZ_DIR="$WORK/enginefuzz" "$ROOT/tools/fuzz/engine/run.sh" replay 100000 1 > "$WORK/replayfuzz.txt" 2>&1; then
		ok "$(grep '^[0-9]* inputs' "$WORK/replayfuzz.txt")"
	else
		bad "Replay-Fuzzer -- $WORK/replayfuzz.txt"
		grep -v '^$' "$WORK/replayfuzz.txt" | tail -12
	fi
	if ENGINEFUZZ_DIR="$WORK/enginefuzz" "$ROOT/tools/fuzz/engine/run.sh" net 100000 1 > "$WORK/netfuzz.txt" 2>&1; then
		ok "$(grep '^[0-9]* inputs' "$WORK/netfuzz.txt")"
	else
		bad "Netzspiel-Fuzzer -- $WORK/netfuzz.txt"
		grep -v '^$' "$WORK/netfuzz.txt" | tail -12
	fi
	if ENGINEFUZZ_DIR="$WORK/enginefuzz" "$ROOT/tools/fuzz/engine/run.sh" lan 100000 1 > "$WORK/lanfuzz.txt" 2>&1; then
		ok "$(grep '^[0-9]* inputs' "$WORK/lanfuzz.txt")"
	else
		bad "LAN-Lobby-Fuzzer -- $WORK/lanfuzz.txt"
		grep -v '^$' "$WORK/lanfuzz.txt" | tail -12
	fi
	# mehrere Spieler einer Netzpartie in einem Prozess über die Loopback-Schnittstelle
	if ENGINEFUZZ_DIR="$WORK/enginefuzz" "$ROOT/tools/fuzz/engine/run.sh" lockstep 1 1 > "$WORK/lockstep.txt" 2>&1; then
		ok "Lockstep: $(grep -c '  ok:' "$WORK/lockstep.txt") Prüfungen bestanden"
	else
		bad "Lockstep-Test -- $WORK/lockstep.txt"
		grep -E 'FAILED|differs|Harness:' "$WORK/lockstep.txt" | head -12
	fi
	if ENGINEFUZZ_DIR="$WORK/enginefuzz" "$ROOT/tools/fuzz/engine/run.sh" save 100000 1 > "$WORK/savefuzz.txt" 2>&1; then
		ok "$(grep '^[0-9]* inputs' "$WORK/savefuzz.txt")"
	else
		bad "Spielstand-Fuzzer -- $WORK/savefuzz.txt"
		grep -v '^$' "$WORK/savefuzz.txt" | tail -12
	fi
	# W3D-Modelle und DDS-Texturen (WW3D2), Saaten aus eigener Vorschrift (w3dseeds.py)
	if ENGINEFUZZ_DIR="$WORK/enginefuzz" "$ROOT/tools/fuzz/engine/run.sh" w3d 20000 1 > "$WORK/w3dfuzz.txt" 2>&1; then
		ok "$(grep '^[0-9]* inputs' "$WORK/w3dfuzz.txt")"
	else
		bad "W3D-Fuzzer -- $WORK/w3dfuzz.txt"
		grep -v '^$' "$WORK/w3dfuzz.txt" | tail -12
	fi
	# Transport ueber echte UDP-Sockets unter Wine: Zustellung, Netzwerkemulator
	if ENGINEFUZZ_DIR="$WORK/enginefuzz" "$ROOT/tools/fuzz/engine/run.sh" udp 1 1 > "$WORK/udptest.txt" 2>&1; then
		ok "$(grep '^UdpTest:' "$WORK/udptest.txt")"
	else
		bad "UdpTest -- $WORK/udptest.txt"
		grep -E 'FAILED|UdpTest' "$WORK/udptest.txt" | tail -12
	fi

	step "MapEditorTests als Windows-Programm (MinGW, Wine)"
	if "$HERE/mapeditor/linux_build.sh" --mingw "$WORK/mapeditor-mingw" > "$WORK/mapeditor-mingw.log" 2>&1 \
	   && (cd "$WORK/mapeditor-mingw" && WINEPREFIX="$WORK/mapeditor-mingw/wineprefix" WINEDEBUG=-all \
	       "$(command -v wine64 || echo /usr/lib/wine/wine64)" MapEditorTests.exe > run.txt 2>&1); then
		ok "$(tail -1 "$WORK/mapeditor-mingw/run.txt")"
	else
		bad "MinGW -- $WORK/mapeditor-mingw.log, $WORK/mapeditor-mingw/run.txt"
	fi

	step "GuiEditorTests als Windows-Programm (MinGW, Wine)"
	if "$HERE/guieditor/linux_build.sh" --mingw "$WORK/guieditor-mingw" > "$WORK/guieditor-mingw.log" 2>&1 \
	   && (cd "$WORK/guieditor-mingw" && WINEPREFIX="$WORK/guieditor-mingw/wineprefix" WINEDEBUG=-all \
	       "$(command -v wine64 || echo /usr/lib/wine/wine64)" GuiEditorTests.exe wnd > run.txt 2>&1); then
		ok "$(tail -1 "$WORK/guieditor-mingw/run.txt")"
	else
		bad "MinGW -- $WORK/guieditor-mingw.log, $WORK/guieditor-mingw/run.txt"
	fi

	step "BabylonTests als Windows-Programm (MinGW, Wine)"
	if "$HERE/babylon/linux_build.sh" --mingw "$WORK/babylon-mingw" > "$WORK/babylon-mingw.log" 2>&1 \
	   && (cd "$WORK/babylon-mingw" && WINEPREFIX="$WORK/babylon-mingw/wineprefix" WINEDEBUG=-all \
	       "$(command -v wine64 || echo /usr/lib/wine/wine64)" BabylonTests.exe > run.txt 2>&1); then
		ok "$(tail -1 "$WORK/babylon-mingw/run.txt")"
	else
		bad "MinGW -- $WORK/babylon-mingw.log, $WORK/babylon-mingw/run.txt"
	fi

	step "Babylon mit echten Tabellenprogrammen (LibreOffice, Gnumeric; Apache OpenOffice, falls geholt)"
	# tools/babylon/office_test.sh: exportieren, Office füllt und speichert, wieder einlesen. Apache OpenOffice
	# holt tools/babylon/openoffice_holen.sh einmal nach $WORK/openoffice.
	if command -v soffice > /dev/null && python3 -c "import uno" 2> /dev/null; then
		bb_aoo=()
		[ -x "$WORK/openoffice/opt/openoffice4/program/soffice" ] && bb_aoo=(--openoffice "$WORK/openoffice/opt/openoffice4/program")
		if "$HERE/babylon/office_test.sh" "${bb_aoo[@]}" "$WORK/babylon-office" > "$WORK/babylon-office.txt" 2>&1; then
			ok "$(tail -1 "$WORK/babylon-office.txt")"
		else
			bad "$WORK/babylon-office.txt"
			grep -m8 FAIL "$WORK/babylon-office.txt"
		fi
	else
		echo "   HINWEIS nicht gelaufen: LibreOffice mit python3-uno fehlt"
	fi

	step "Fuzzing der Dateileser (libFuzzer, ASan, UBSan; 60 s je Ziel)"
	# Kompression, W3D-Chunks, WAV, .big-Verzeichnis, Stringtabellen, TGA, Karten (Kartenmodell des neuen Editors); Funde landen unter $WORK/fuzz/funde (tools/fuzz/linux/run.sh).
	if FUZZ_DIR="$WORK/fuzz" "$ROOT/tools/fuzz/linux/run.sh" 60 > "$WORK/fuzz.txt" 2>&1; then
		ok "$(grep -c '^   OK' "$WORK/fuzz.txt") Ziele ohne Fund"
	else
		bad "Fuzzing -- $WORK/fuzz.txt"
		grep -E "FEHLER|runtime error|ERROR" "$WORK/fuzz.txt" | head -6
	fi

	step "Fehlersuche mit clang-Warnungen (Speicher, Klammerung, return)"
	# Ausgewaehlte Warnungen, die fast immer echte Fehler sind (tools/syntaxcheck/befunde.sh); bekannte,
	# eingeordnete Befunde stehen in befunde_baseline.txt.
	"$ROOT/tools/syntaxcheck/befunde.sh" "$WORK/befunde.txt"
	grep -v '^#' "$ROOT/tools/syntaxcheck/befunde_baseline.txt" | LC_ALL=C sort > "$WORK/befunde_base.txt"
	neu=$(LC_ALL=C comm -23 "$WORK/befunde.txt" "$WORK/befunde_base.txt")
	if [ -n "$neu" ]; then
		bad "neue clang-Befunde (Einzelpruefung: SYNTAXCHECK_WARNINGS=\"\$(cat tools/syntaxcheck/befunde_warnungen.txt)\" tools/syntaxcheck/check.sh <Pfad>):"
		printf '%s\n' "$neu" | sed 's/^/          /'
	else
		ok "$(wc -l < "$WORK/befunde.txt") Befunde, alle bekannt (tools/syntaxcheck/befunde_baseline.txt)"
	fi
fi

# ------------------------------------------------------------------------------------------------
WINE_DIR="${CNC_WINE_DIR:-${TMPDIR:-/tmp}/cnc-wine}"
if [ "$FULL" = "1" ]; then
	step "Menueablauf: Spiel unter Wine, Testablauf durch Hauptmenue, LAN- und Skirmish-Menue"
	# Testablaeufe und -echteEingabeAus ignorieren die echte Maus; das Hauptmenue muss trotzdem erscheinen
	# (tools/wine/menuetest.sh, unter Windows tools/abnahme/menue_test.py). Der Bau ist inkrementell.
	if ! CNC_WINE_DIR="$WINE_DIR" timeout 3600 "$ROOT/tools/wine/build_game.sh" > "$WORK/spiel-bau.txt" 2>&1; then
		bad "Bau -- $WORK/spiel-bau.txt"
		tail -12 "$WORK/spiel-bau.txt"
	else
		SPIEL_GEBAUT=1
		if CNC_WINE_DIR="$WINE_DIR" "$ROOT/tools/wine/menuetest.sh" > "$WORK/menuetest.txt" 2>&1; then
			ok "$(tail -1 "$WORK/menuetest.txt")"
		else
			bad "$WORK/menuetest.txt"
			cat "$WORK/menuetest.txt"
		fi
	fi
	if [ "${SPIEL_GEBAUT:-0}" = 1 ]; then
		step "Speichern und Laden: nach dem Laden rechnet das Spiel wie ohne Speichern (CRC je Frame)"
		# tools/wine/speichern_laden.sh: Gefecht zweier Computerspieler, einmal durchgehend, einmal bei Frame 608
		# gespeichert und geladen; bei einer Abweichung stehen die ersten abweichenden Objekte im Ergebnis.
		if CNC_WINE_DIR="$WINE_DIR" "$ROOT/tools/wine/speichern_laden.sh" "$WORK/speichern_laden" > "$WORK/speichern_laden.txt" 2>&1; then
			ok "$(tail -1 "$WORK/speichern_laden.txt")"
		else
			bad "$WORK/speichern_laden.txt"
			head -40 "$WORK/speichern_laden.txt"
		fi
	fi
	step "Fassung nur mit Vulkan: Bau ohne Direct3D 11 (GZH_RENDER_D3D11=0) und Start unter Wine"
	# Eigene Ablage neben der des Standardbaus; gelinkt ohne d3d11, dxgi und d3dcompiler. Ohne Vulkan unter Wine startet
	# keine Schicht und der Rauchtest scheitert.
	VK_WINE_DIR="${WINE_DIR}-nur-vulkan"
	if ! CNC_WINE_DIR="$VK_WINE_DIR" timeout 3600 "$ROOT/tools/wine/build_game.sh" --nur-vulkan > "$WORK/vulkan-bau.txt" 2>&1; then
		bad "Bau -- $WORK/vulkan-bau.txt"
		tail -12 "$WORK/vulkan-bau.txt"
	elif CNC_WINE_DIR="$VK_WINE_DIR" "$ROOT/tools/wine/smoketest.sh" --frames 60 > "$WORK/vulkan-start.txt" 2>&1; then
		ok "$(tail -1 "$WORK/vulkan-start.txt")"
	else
		bad "$WORK/vulkan-start.txt"
		tail -20 "$WORK/vulkan-start.txt"
	fi
fi

if [ "$EDITOR" = "1" ] || { [ "$FULL" = "1" ] && [ -f "$WINE_DIR/qt/win/bin/Qt6Widgets.dll" ] && [ -f "$WINE_DIR/libengine.a" ]; }; then
	step "Karteneditor: Bau und Selbsttest unter Wine (Engine, 3D-Ansicht, Bildinhalt)"
	# Baut nur, was sich geaendert hat (make mit -MMD), und prueft dann die Zeilen FAIL, Abstuerze und
	# den Rueckgabewert des Selbsttests (tools/wine/README.md).
	if ! "$ROOT/tools/wine/build_mapeditor.sh" "$WINE_DIR" > "$WORK/mapeditor-bau.txt" 2>&1; then
		bad "Bau -- $WORK/mapeditor-bau.txt"
		tail -12 "$WORK/mapeditor-bau.txt"
	elif CNC_WINE_DIR="$WINE_DIR" "$ROOT/tools/wine/run_mapeditor.sh" > "$WORK/mapeditor-lauf.txt" 2>&1; then
		ok "$(tail -1 "$WORK/mapeditor-lauf.txt")"
	else
		bad "Selbsttest -- $WORK/mapeditor-lauf.txt"
		tail -12 "$WORK/mapeditor-lauf.txt"
	fi
	if [ -f "$WINE_DIR/mapeditor/GuiEditor.exe" ]; then
		step "GUI-Editor: Selbsttest unter Wine (Fenstersystem des Spiels, Layouts der Testdaten)"
		if CNC_WINE_DIR="$WINE_DIR" "$ROOT/tools/wine/run_guieditor.sh" > "$WORK/guieditor-lauf.txt" 2>&1; then
			ok "$(tail -1 "$WORK/guieditor-lauf.txt")"
		else
			bad "Selbsttest -- $WORK/guieditor-lauf.txt"
			tail -12 "$WORK/guieditor-lauf.txt"
		fi
	fi
elif [ "$FULL" = "1" ]; then
	step "Karteneditor: Selbsttest unter Wine"
	echo "   HINWEIS nicht gelaufen: Qt oder Engine fehlen in $WINE_DIR. Einmal tools/gate.sh --editor"
	echo "           (oder tools/wine/build_mapeditor.sh) ausfuehren, danach laeuft der Schritt mit --full mit."
fi

# ------------------------------------------------------------------------------------------------
printf '\n'
if [ "$fails" = "0" ]; then
	echo "Prüfgang bestanden. Windows-Build, ctest, Replay-CRC und Bildvergleich stehen weiter aus."
	exit 0
fi
echo "Prüfgang NICHT bestanden: $fails Schritt(e) fehlgeschlagen."
exit 1
