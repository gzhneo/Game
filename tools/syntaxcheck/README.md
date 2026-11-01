# Syntaxpruefung ohne Windows (Hilfswerkzeug)

Die Render-Dateien unter `Code/Renderer/WW3D2` und
`Code/Platform` lassen sich nur unter Windows mit MSVC bauen. Fuer
Umbauten wie die DirectX-11-Vorbereitung ist es aber
schon viel wert, die Dateien wenigstens **uebersetzen** zu koennen: Tippfehler, falsche
Signaturen, vergessene Casts und nicht mehr passende Aufrufe fallen dabei auf.

Dieses Verzeichnis baut dafuer eine Pruefumgebung aus frei verfuegbaren Teilen:

- **clang** als Uebersetzer, mit `--target=x86_64-w64-mingw32`,
- die **MinGW-w64-Windows-Header** (`d3d9.h`, `d3d11.h`, `dxgi.h`, `d3dcompiler.h`),
- einen **Symlink-Spiegel** der Quellen, in dem Gross-/Kleinschreibung der Includes
  und ein paar MSVC-Eigenheiten geradegezogen werden, ohne das Repository anzufassen.

Es wird **nicht gelinkt und nichts ausgefuehrt** (`-fsyntax-only`); das Ergebnis ist
keine Ersetzung fuer den Build und den Starttest unter Windows.

## Voraussetzungen (Debian/Ubuntu)

    apt-get install -y clang mingw-w64-x86-64-dev

## Benutzung

    tools/syntaxcheck/setup.sh                     # Spiegel bauen (nach jeder Aenderung erneut)
    tools/syntaxcheck/check.sh Renderer/WW3D2/Common/renderdevice.cpp
    tools/syntaxcheck/checkall.sh Renderer/WW3D2/*.cpp

Pfade sind relativ zum Spiegel. Keine Ausgabe heisst fehlerfrei.
`SYNTAXCHECK_DIR` waehlt einen anderen Ablageort fuer den Spiegel.

### Ein Quellordner

Seit Phase 8 Schritt 7 liegt der gesamte Quelltext beider
Spiele unter `Code/`; die frueheren Baeume `Generals/Code` und `GeneralsMD/Code` gibt es nicht mehr.
`setup.sh` spiegelt deshalb nur `SYNTAXCHECK_SHARED` (Vorgabe `Code`); `Tools` ueberspringt der
Spiegel. `SYNTAXCHECK_SOURCE` ist nur noch ein optionaler zweiter Quellordner (Vorgabe: keiner) --
ein schon vorhandener Eintrag wird dabei nie ueberschrieben, der erste Aufruf gewinnt. Unterhalb von
`tree/` steht der Quelltext in derselben Struktur wie frueher ein Baum; alle `-I`-Listen und alle
Pfadangaben an `check.sh` bleiben unveraendert.

Jede Datei im Spiegel gehoert **beiden** Spielen. Wer sie ueber `tools/refactor/` aendert, aendert
sie fuer beide.

## Was die Skripte tun

| Datei | Aufgabe |
|---|---|
| `mktree.py` | Symlink-Spiegel eines Quellbaums, jede Datei zusaetzlich unter Kleinschreibnamen; zweimal aufgerufen (siehe oben) |
| `mkalias.py` | fuer jede in den Quellen benutzte Include-Schreibweise einen passenden Symlink |
| `patches.py` | MSVC-Eigenheiten im Spiegel geradeziehen (funktionaler Cast mit mehrwortigem Typnamen, `false` als Nullzeiger, `enum X;` ohne Basistyp, `friend class` als Deklaration) |
| `gen_fwd.py` | Sammel-Vorwaertsdeklaration fuer `friend class`-Namen; Aliase fuer Systemheader in Grossschreibung |
| `fixmirror.py` | Zweiphasenlookup in Templates: setzt im Spiegel `this->` vor Basisklassen-Member |

## Naehe zum echten Build

`check.sh` uebernimmt, was der MSVC-Build je Datei setzt: C++20, die globalen Definitionen der
Wurzel-`CMakeLists.txt` (`WIN32_LEAN_AND_MEAN`, `_WIN32_WINNT=0x0A00`, ...), fuer
`GameEngineDevice` das per `/FI` vorweg eingebundene `ForceOrder.h` (sonst gewinnt das `BitTest`
aus `winnt.h` gegen das aus `Lib/BaseType.h`) und fuer `Main` den eigenen Ordner und
`GAME_EDITION_NAME`. Was MinGW gegenueber dem Windows-10-SDK fehlt, ergaenzt
`compat/mingw_compat.h` (nur Deklarationen, `SetThreadDescription`, `D3D11_RLDO_IGNORE_INTERNAL`);
`x3daudio.h` kommt aus `tools/linuxtests/shim` (per `-idirafter`, echte MinGW-Header haben Vorrang).
Mit diesem Stand (2026-09-27) fallen 29 von 832 Dateien aus, vorher 83.

## Grenzen

Einige Dateien bleiben ungeprueft, Liste in `baseline.txt`: alte MSVC-Konstrukte (Spezialisierung
ohne `template<>`, `static_cast` von `void*` auf Funktionszeiger, `enum` unvollstaendig als Typ,
CP1252-Zeichen in Zeichenliteralen), fehlende Fremdheader (FFmpeg, DirectXTex) und die aeltere
XAudio2-Schnittstelle von MinGW. Unter MSVC uebersetzen sie. `tools/gate.sh --full` schlaegt fehl,
sobald eine Datei **ausserhalb** von `baseline.txt` ausfaellt, und nennt Eintraege, die inzwischen
durchgehen -- die dann aus der Liste streichen.
