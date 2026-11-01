# Einheitentests unter Linux ausführen

`tools/syntaxcheck` übersetzt Dateien nur (`-fsyntax-only`) — es linkt nicht und führt nichts aus.
Damit fallen fehlende Symbole, falsche Quelllisten in CMake und jeder Laufzeitfehler durch. Dieses
Verzeichnis schließt die Lücke für die Einheitentests: Es baut
`Code/Tests/UnitTests` (je Spiel; unter CMake seit 2026-09-23 ein Ziel `UnitTests` für beide) mit dem
**MinGW-Cross-Compiler** gegen die echten
Windows-Header zu einer `.exe` und führt sie unter **Wine** aus.

Damit sind die zwoelf Tests auch ohne Windows-Rechner nachvollziehbar — und sie decken die Bibliotheken ab, die
die Angleichung angefasst hat, sowie die Stellen, an denen
Phase 4 den Determinismus berührt:

| Test | prüft |
|---|---|
| `Vector4_AddSub` | WWMath (R-21a, der 1997er Tippfehler) |
| `Matrix3D_GetInverse_RoundTrip` | WWMath |
| `BezierSegment_StraightLine` | GameEngine/Common |
| `StlStyleStringHash_ContentBased` | `STLTypedefs.h` |
| `WavParser_Pcm`, `WavParser_ImaAdpcm` | GameEngineDevice, dazu `AsciiString` |
| `CompressionManager_RefPack`, `CompressionManager_ZLib` | Compression (die E-8.4-Dekoder aus R-43) |
| `CRC_KnownVector` | `Common/crc.h` -- die Prüfsumme, mit der der Replay-Test misst; Zero Hour hat dafür eine zweite, inline Fassung |
| `RealToInt_Truncation` | `REAL_TO_INT` aus `Lib/BaseType.h`, in der ganzen Spiellogik benutzt |
| `RealToIntCeilFloor_ExactIntegers` | `REAL_TO_INT_CEIL`/`_FLOOR` -- deckt R-67 auf (Pathfinder, PartitionManager) |
| `Coord2D_ToAngle` | `Coord2D::toAngle` aus `Lib/BaseType.h` (R-68), Vergleich gegen `atan2` in `double` |
| `KeyDefs_DInput` | Scancode-Zuordnung der Tastatur (`GameClient/KeyScanCode.h`) |
| `StrTrim_BothEnds` | `strtrim`/`wcstrim` aus WWLib (R-261: hinterer Leerraum nach führendem blieb stehen) |

## Voraussetzungen (Debian/Ubuntu)

    apt-get install -y g++-mingw-w64-x86-64 libz-mingw-w64-dev wine64 clang mingw-w64-x86-64-dev

## Benutzung

    tools/linuxtests/run.sh

Ausgabe wie unter Windows (`[PASS] …`, am Ende `All 12 tests passed.`), Rückgabewert 0 bei Erfolg.
`LINUXTESTS_DIR` wählt einen anderen Ablageort für Spiegel und Objektdateien.

`RenderUnitTests` lesen Geländekacheln aus DDS mit DirectXTex. Die MinGW-Fassung baut
`tools/wine/build_game.sh --nur-deps` beim ersten Lauf (holt DirectXMath, DirectX-Headers und DirectXTex von
github.com/microsoft) in die Wine-Ablage `CNC_WINE_DIR`, ohne sie nach `<LINUXTESTS_DIR>/wine`; danach kostet der
Schritt Sekunden.

Stand 2026-09-17: **12 von 12** (damals je Baum; seit Phase 8 Schritt 6/7 gibt es den Test einmal). Gegenprobe, dass der Lauf überhaupt fehlschlagen kann:
`W -= v.W` in `vector4.h` zu `W = v.W` geändert — 2 Tests fallen um, Rückgabewert 1.

## Wie es funktioniert

1. **Spiegel.** `tools/syntaxcheck/setup.sh` baut den Symlink-Spiegel mit den
   Groß-/Kleinschreib-Aliasen (`Lib/Basetype.h` -> `Lib/BaseType.h`) und den Harness-Patches
   aus `patches.py`. Dasselbe Werkzeug wie die Syntaxprüfung, eine Quelle der Wahrheit.
   Der Quelltext kommt aus `Code/` (`SYNTAXCHECK_SHARED`), siehe `tools/syntaxcheck/README.md`.
2. **Systemheader.** `sysalias.py` legt Aliase für Systemheader an, die der Bestand anders
   schreibt als MinGW sie ablegt (`<EXCPT.H>`, `<TCHAR.H>`). Durchsucht wird `Code/`.
3. **Bibliotheken.** `sources.py` liest die Quelllisten aus den `CMakeLists.txt` der
   Bibliotheken, damit hier keine zweite Liste entsteht, die veralten kann. Übersetzt wird zu
   `libengine.a`; der Linker nimmt daraus nur, was er braucht.
   `WWDownload` (EA-FTP-Patchdownload) ist seit 2026-09-26 entfernt (TODO 69 Runde A).
4. **Testziel.** Quellen wie in `Code/Tests/UnitTests/CMakeLists.txt`, dazu `poolstubs.cpp`.
5. **Ausführen** unter Wine, in einem eigenen `WINEPREFIX`.

Statisch gelinkt (`-static`), sonst sucht die `.exe` `libstdc++-6.dll` und Wine bricht mit 53 ab,
bevor ein Test läuft.

## Was hier Ersatz ist — und was nicht

Geprüft wird **echter Bestandscode**: WWMath, Compression, Bezier, WavParser und `AsciiString`
werden aus dem Repo übersetzt und ausgeführt. Ersetzt sind nur Dinge, die der Test ohnehin nicht
prüft:

- `poolstubs.cpp` reicht den Speicherpool an `malloc`/`free` durch. `GameMemory.cpp` zöge über
  `GlobalData.h`/`ClientRandomValue.h` die halbe Engine mit — aus demselben Grund steht in
  `Code/Tests/UnitTests/UnitTestStubs.cpp` schon ein Stub für die `new`/`delete`-Overloads. Die
  Klassenlayouts kommen unverändert aus den echten Headern.
- `shim/xaudio2.h` und `shim/x3daudio.h`: MinGW liefert die XAudio2-Header nicht mit.
  `WavParser.h` bindet `XAudio2AudioManager.h` nur wegen `WaveFormatInfo` ein. Der Stub
  deklariert nur; gelinkt wird gegen XAudio2 nie. Weicht die Schnittstelle ab, scheitert
  `XAudio2SoundCallback` am `override` — der Fehler fällt sofort auf.

## Grenzen

- **Kein Ersatz für den Windows-Build.** MinGW ist nicht MSVC: andere STL, andere Warnungen,
  kein `/permissive-`. Was hier läuft, kann unter MSVC trotzdem scheitern (und umgekehrt).
- 22 der 145 Bibliotheksquellen übersetzt MinGW nicht (alte MSVC-Konstrukte: `_asm`,
  `__try/__except`, unvollständige `enum`-Vorwärtsdeklarationen). Sie landen in `skipped.txt`.
  Solange keines der ausgelassenen Symbole gebraucht wird, linkt es; sonst bricht der Lauf
  hörbar ab. Die Liste ist damit auch eine Messlatte für TODO 1 (C++20-Modernisierung).
- Das Spiel selbst lässt sich so **nicht** bauen oder starten: Direct3D, MFC-Werkzeuge und
  die Spieldaten (`*.big`) fehlen. Für einen Replay- oder Bildvergleich braucht es weiter
  Windows. DXVK hilft dabei nicht — es übersetzt D3D nach Vulkan, ersetzt aber weder Compiler
  noch Spieldaten.
