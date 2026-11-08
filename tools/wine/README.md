# Spiel unter Linux bauen und unter Wine starten

EA hat keinen Spielinhalt freigegeben, nur den Quelltext. Dieses Verzeichnis baut das Spiel ohne Windows
(MinGW-Cross-Compiler) und startet es unter Wine mit Direct3D 11 über Mesa-Software-Rendering (llvmpipe) auf
Xvfb. Spieldaten (`Data\INI`, `.big`-Archive, ...) sind nicht enthalten; sie liegen in einem Datenordner,
den `run_game.sh` als Arbeitsverzeichnis benutzt (synthetische Testdaten: `tools/wine/testdaten/`).

## Bauen

    tools/wine/build_game.sh [--debug] [--generals] [--nur-deps] [--nur-vulkan|--nur-d3d11] [Ablage]

Standardablage `${TMPDIR:-/tmp}/cnc-wine` (auch `CNC_WINE_DIR`). Ergebnis: `<Ablage>/game/GZHNeoZH.exe`
(Zero Hour, Master), daneben `Shaders\` (Kopie von `Data/Shaders`) und `ProjectData\` (Kopie von `Data/INI`,
`Data/Textures`) wie die Post-Build-Schritte in `Code/App/CMakeLists.txt`. `--generals` baut zusätzlich
`<Ablage>/game-generals/GZHNeo.exe`: die Engine ist für beide Spiele dieselbe, nur `GameEdition.cpp`, der Titel
in `CpuCheck.cpp` und die Ressourcen unterscheiden sich.

| Schalter | Wirkung |
|---|---|
| ohne | `-O2 -DNDEBUG -DIG_DEBUG_STACKTRACE` (wie Release) |
| `--debug` | `-O1 -D_DEBUG`: Assertions, `DebugLogFile.txt`, Grenzprüfung im Speicherpool. Erste Wahl, wenn ein Lauf abbricht: die Release-Fassung meldet nur „Uncaught Exception during initialization“ |
| `--nur-deps` | nur DirectXMath, DirectX-Headers, DirectXTex holen und bauen |
| `--nur-vulkan`, `--nur-d3d11` | nur eine Geräteschicht des Renderers (wie `GZH_RENDER_D3D11=OFF` bzw. `GZH_RENDER_VULKAN=OFF`); eigene Ablage angeben. Ohne D3D11 linkt der Bau ohne `d3d11`, `dxgi`, `d3dcompiler` |
| `JOBS=<n>` | parallele Übersetzungen (Standard: Kerne) |

Der Erstbau dauert rund 15 bis 25 Minuten auf vier Kernen (830 Dateien), ein zweiter Lauf ohne Änderungen
Sekunden: Makefile mit `-MMD`-Abhängigkeiten, der Spiegel bleibt über `tools/fuzz/engine/syncmirror.py`
zeitstempelstabil. Andere Schalter (`--debug` <-> Release) übersetzen alles neu; dafür getrennte Ablagen nehmen.

### Was der Bau anders macht als der MSVC-Build

- **Quellliste** aus den `CMakeLists.txt` (`quellen.py`): Bibliotheken, `GameEngine`, `GameEngineDevice`,
  `WW3D2`, `Code/App`, dazu die `GameEdition.cpp` beider Spiele. Ausgelassen wird nur, was durch stumme
  Klassen ersetzt ist: `FFmpegFile.cpp`, `FFmpegVideoPlayer.cpp`, `Mp3Parser.cpp`, `XAudio2AudioManager.cpp`.
- **Stumme Medien** (`-DGZH_NULL_MEDIA`, nur dieser Bau): `Code/Platform/NullMedia/`
  (`NullAudioManager.h`, `NullVideoPlayer.h`), eingehängt in `Win32GameEngine.h` und `W3DGameClient.h`. Kein
  Ton, keine Videos; die Definitionen aus den INI-Dateien laden wie sonst. Niemand sonst braucht Symbole aus
  `FFmpegFile`/`Mp3Parser`.
- **DirectXTex** (Tag `oct2025`), DirectXMath (`apr2025`) und DirectX-Headers (`v1.619.5`) werden von
  github.com/microsoft geklont und mit CMake für MinGW gebaut (`<Ablage>/deps`); nur die Bibliothek.
- **Schalter:** `-std=c++20 -fpermissive -fms-extensions -finput-charset=cp1252 -fno-delete-null-pointer-checks
  -fno-strict-aliasing -fwrapv -ffp-contract=off`, Definitionen wie in der Wurzel-`CMakeLists.txt`. Kein
  `-mavx2` (`CpuCheck.cpp` bricht ohne AVX2 ohnehin ab und wird ohne AVX2-Code übersetzt).
- **Compat-Header** `compat/`: `gzh_mingw_compat.h` (`SetThreadDescription`, `D3D11_RLDO_IGNORE_INTERNAL`),
  Großschreibung von `Windows.h` (DirectXTex); `wrl\client.h` legt `build_game.sh` in der Ablage an.
- **Quelländerungen für MinGW** (verhalten sich unter MSVC unverändert): `CpuTopology.cpp` liest
  `EfficiencyClass` über den Byte-Versatz (die MinGW-Header kennen den Namen nicht), `WinMain.cpp` nennt die
  lokale `strtrim` in `trimToken` um (kollidierte mit `WWLib/trim.h`) und ruft `_set_se_translator` nur unter
  MSVC auf.
- **Linkreihenfolge:** Main-Objekte, dann die Engine-Bibliotheken als eine `--start-group`, dann
  `GameEdition`; statisch (`-static -static-libgcc -static-libstdc++`).

## Starten

    tools/wine/run_game.sh [--generals] [--zeit <s>] [--auflosung BxH] [--log Datei] [--fotos <Ordner> <s>] <Datenordner> [Spielschalter ...]

Legt Verknüpfungen auf Exe, `Shaders` und `ProjectData` in den Datenordner (vorhandene Dateien bleiben),
startet mit dem Datenordner als Arbeitsverzeichnis unter eigenem Xvfb (1280x720x24, eigenes `WINEPREFIX`
`<Ablage>/wineprefix`), Software-OpenGL (`LIBGL_ALWAYS_SOFTWARE=1`), Zeitlimit 120 s, Ausgabe nach
`<Datenordner>/wine.log`. Rückgabewert: der des Spiels, 124 bei Zeitlimit. `--fotos <Ordner> <s>` nimmt alle `<s>` Sekunden ein
Bildschirmfoto des Xvfb-Displays auf (`foto_NNNN.png`), auch im Menü und unabhängig von den Bildschaltern des Spiels. Bei Zeitlimit entstehen vor dem
Beenden ein Bildschirmfoto (`wine.png`) und die Fensterliste im Log: ein Fehlerdialog des Spiels wartet sonst
endlos auf einen Klick. Standardschalter des Spiels: `-win -noaudio -novideo -nologo -noshellmap
-ignoreAsserts`.
`--menue` lässt `-noshellmap` weg (Hauptmenü mit Shell-Karte). Vor jedem Start setzt das Skript im Wine-Präfix
in `Options.ini` (Ordner `GZH Neo/Zero Hour`, mit `--generals`
`GZH Neo/Generals`) die Zeile `HardwareHint = no`, sonst wartet der CPU-Hinweisdialog auf
einen Klick; andere Werte und das Zeilenende bleiben. `WINEPREFIX_CNC` wählt ein eigenes Präfix (parallele
Läufe verschiedener Agenten: eigenes Präfix und eigenen Datenordner nehmen, die Exe-Verknüpfung im Datenordner
zeigt sonst auf den zuerst gestarteten Bau). Fehlt der Datenordner, endet das Skript mit 2 und nennt den
Generator.

**Generals mit dem Zero-Hour-Datensatz:** `GZHNeo.exe` startet mit demselben Datenordner und läuft ein Skirmish
(`-skirmish TestSkirmish -skirmishAI 2`) im Debug-Bau bis `-maxFrames`; Unterschiede beim Start sind nur
Spielname, Benutzerordner (`GZH Neo/Generals`), die Archivnamen des
Fingerabdrucks und der Verzicht auf `Weather.ini`.


Die Absturzinfos des Spiels landen im Wine-Präfix unter
`drive_c/users/<Nutzer>/Documents/GZH Neo/Zero Hour/` (`ReleaseCrashInfo.txt`,
mit `--debug`-Bau auch `DebugLogFile.txt` im Arbeitsverzeichnis).

## Texturpaket und Modellprüfung

    tools/wine/texturpaket_test.sh [--frames N]

Baut aus den Testdaten eine Stufe UHD (`texturpaket_stufe.py`: Texturen vierfach vergrößert, Geländekacheln mit
`ZHTS4`, die Wassertextur `testwater` als DXT1-DDS mit voller Mip-Kette), daraus das Paket
(`tools/assets/build_texture_package.py`) und startet vier Skirmish-Läufe mit `-logTextureStats`: ohne Paket und
mit `-textureQuality sd|hd|uhd`, dazu UHD mit einem Mod (`-mod`), der die Wassertextur in Originalgröße ersetzt.
Geprüft: Zeile `TEXTURPAKET <Stufe>`, SD so viele Texel wie ohne Paket, HD und UHD mehr, mindestens eine DXT-Textur,
Geländeatlas-Faktor 1/1/2/4, mit Mod `TEXTURPAKET ersetzt 1` und weniger Texel als ohne Mod, jeder Lauf endet mit 0. Ablage
`<CNC_WINE_DIR>/testdata-paket`.

`run_game.sh <Datenordner> -modellPruefung <Datei>` lädt jedes Modell unter `Art\W3D` und jede DDS unter
`Art\Textures` und beendet das Spiel nach dem Start; die Zeilen `MODELCHECK` stehen in `<Datei>` (Testdaten:
23 Modelle, nichts abgewiesen).

## Dateisystem gegen VfsDump

    tools/wine/dateisystem_test.sh

Vergleicht die Auflösung des Spiels (`-dateiIndexAusgeben`) mit `VfsDump`, das die alte Vorrangregel nachbildet: SD, HD mit
Mod-Ordner, `-mod <ZIP>` (je ohne freien Inhalt); dazu UHD mit Texturpaket, Mod und freiem Inhalt mit gezielten Prüfungen der
Ränge. Daten: Testdaten, die Archive aus `tools/dateisystem/beispiel.py` und ein Testpaket unter `ProjectData\Inhalt`
.

## Menüs durchklicken

    tools/wine/klicktest.sh <Datenordner> <Ausgabeordner> <Schritt>...

Startet das Spiel mit Hauptmenü (`run_game.sh --menue`), findet Xvfb-Display und Spielfenster und führt die Schritte
aus: `X,Y` klickt an der Stelle im 800 x 600-Layout (auf das Fenster umgerechnet, Taste kurz gehalten), `rX,Y` klickt dort
rechts, `w<s>` wartet,
`s<Name>` speichert ein Bildschirmfoto `<Ausgabeordner>/<Name>.png`, `k<Taste>` drückt eine Taste (xdotool-Name,
etwa `kEscape`). Das Hauptmenü erscheint erst nach der ersten Mausbewegung, also mit `w45 400,300 w3` beginnen. Am
Ende beendet das Skript nur den eigenen Spielprozess und nennt Absturzberichte. Braucht `xdotool` (der Sitzungs-Hook
installiert es). Beispiel, Skirmish starten und Spielmenü öffnen:

    tools/wine/klicktest.sh daten fotos w45 400,300 w3 650,318 w40 666,530 w150 kEscape w15 smenue

Software-Rendering ist langsam (Release rund 10, Debug rund 2 Bilder je Sekunde); Wartezeiten großzügig wählen, ein
Klick vor dem Erscheinen eines Menüs geht ins Leere. Mehrere Läufe gleichzeitig sind möglich (`run_game.sh` nennt seine
Anzeige); `KLICK_NETNS=<Name>` startet das Spiel in einem Netz-Namensraum, `WINEPREFIX_CNC` wählt das Präfix,
`KLICK_SCHALTER` hängt Spielschalter an, `KLICK_FAKTOR` (ganze Zahl) vervielfacht alle Wartezeiten (Debug-Bau: 2).

    tools/wine/lan_zwei.sh <Datenordner> <Ausgabeordner>

LAN-Partie zweier Instanzen (root, `iproute2`): je ein Netz-Namensraum (10.77.0.1/.2, veth), eigenes Präfix
(`<Ablage>/wineprefix-lan1`, `-lan2`) und eigene Datenkopie; Instanz 1 eröffnet, Instanz 2 tritt bei und nimmt an,
Instanz 1 startet; in der Partie gibt jeder Spieler einen Bauauftrag und schickt seinen Schützen los (Gastgeber mit
Linksklick, klassische Steuerung; Gast mit Rechtsklick, alternative Maussteuerung in seinem Präfix) und lässt ihn mit
gehaltener Strg-Taste die eigene Kommandozentrale angreifen (`d<Taste>` hält eine Taste, `u<Taste>` lässt sie los). Am Ende vergleicht das Skript die Replays beider
Instanzen (gleicher Befehlsstrom, `replay1.txt`, `replay2.txt`). Bildschirmfotos unter `<Ausgabeordner>/host` und `/gast`,
Dauer rund 14 Minuten. `LAN_HOST_PARTIE` und `LAN_GAST_PARTIE` ersetzen die Schritte in der Partie. `LAN_KARTE=versus`
spielt auf der Karte `TestVersus` (Basen in Sichtweite, feste Startpositionen) ein Gefecht: jeder Schütze greift die
gegnerische Kommandozentrale an. `LAN_MODUS=internet` nimmt die Lobby im Internet-Modus über einen Treffpunkt-Server im
Namensraum des Gastgebers (öffentlich gelistetes Spiel); `LAN_SCHALTER` hängt Spielschalter an, etwa `-netzNurRelais`
für den Weg über das Relais. `klicktest.sh`: `mX,Y` bewegt nur die Maus (vor dem Klick auf ein Kontrollkästchen); `KLICK_GENERALS=1` startet Generals
statt Zero Hour.

    tools/wine/lan_zoom.sh <Ausgabeordner> [Eintrag]

Höchstwert des Gastgebers fürs Herauszoomen (root, `iproute2`): zwei Instanzen wie `lan_austritt.sh`
(Namensräume `gzh-zoom1`/`-zoom2`, Präfixe `<Ablage>/wineprefix-zoom1`, `-zoom2`), beide mit `-maxZoom 1.25`. Der Gastgeber wählt
in der Lobby den Eintrag (0 = 100 %, 1 = 105 %, 2 = 110 % Vorgabe, ...), beide zoomen in der Partie ganz hinaus (`rad -40`), danach
spielt der Gastgeber sein Replay mit `-maxZoom 1.25` ab. Erwartet: alle drei Höhen auf 1 % bei 310 × Wert (das Replay kann über anderem Gelände stehen), `ZOOMLIMIT`
in allen drei Protokollen (unter 125 %), `ZM=<Prozent>;` im Replay-Kopf (keiner bei 100 %), CRC-Verläufe gleich. Ergebnis in
`<Ausgabeordner>/ergebnis.txt`, Exit-Code 0 bei Erfolg. Dauer rund 10 Minuten.

    tools/wine/lan_austritt.sh [--emulator] <Ausgabeordner> [Runden]

Austritt aus einer LAN-Partie (root, `iproute2`): wie `lan_zwei.sh` zwei Instanzen in eigenen Netz-Namensräumen
(`gzh-aus1`/`-aus2`, Präfixe `<Ablage>/wineprefix-aus1`, `-aus2`), aber über Testabläufe (`-testablauf`) statt Mausklicks und
mit einem frisch erzeugten Datensatz. Der Gast verlässt bei Logik-Frame 200 über das Spielmenü, der Host gibt in den Frames um
den Austritt Baubefehle (Knopf 1 der Kommandozentrale); beide schreiben die Prüfsumme jedes Frames (`-crcLog`,
`-crcLogInterval 1`). Je Runde vergleicht das Skript die gemeinsamen Frames (`<Ausgabeordner>/rundeN/ergebnis.txt`), Exit-Code 0
ohne Abweichung. `--emulator` schaltet den Netzwerkemulator des Spiels zu (Vorgabe `-netzLatenz 80 -netzSchwankung 20
-netzVerlust 2`, `AUSTRITT_EMULATOR` ersetzt die Schalter); `tc netem` gibt es im Kernel der Cloud-Umgebung nicht. Dauer rund
sechs Minuten je Runde; den Bau wählt `CNC_WINE_DIR`.

## Speichern und Laden

    tools/wine/speichern_laden.sh [--generals] [--ki K1,K2] [--seed N] <Ausgabeordner> [Speicher-Frame] [Frames danach]

Ein Gefecht zweier Computerspieler auf `TestSkirmish` (Vorgabe `K1,K2`, Startwert 2) läuft zweimal: einmal durchgehend, einmal
nach dem Speicher-Frame (Vorgabe 600 Logik-Frames, gespeichert wird bei Frame 608) über den Testablauf gespeichert und sofort
geladen (`speichern`, `laden`). Beide schreiben die Prüfsumme jedes Frames; nach dem Laden muss das Spiel genau so rechnen wie
ohne Speichern (Vorgabe 600 Frames danach). Bei einer Abweichung wiederholt das Skript beide Läufe mit vollen Tiefenabzügen
des letzten gleichen und des ersten abweichenden Frames (`-deepCRCFrames`, `-deepCRCFull`) und hängt die Ausgabe von
`tools/determinism/deepcrc_diff.py` an `<Ausgabeordner>/ergebnis.txt` an: abweichende Objekte und Abschnitte, darunter
`MARKER:PartitionOrder` (Reihenfolgen der Partition) und `MARKER:PathfindCells` (Markierungen und Zonen der Wegfinder-Zellen).
Sind dort nur der Spielstand-Teil und keine Objekte verschieden, fehlt ein Zustand ganz im Spielstand. Exit-Code 0 ohne
Abweichung. Dauer rund fünf Minuten (ohne Abweichung), im Prüfgang mit `--full`.

## Zuschauen

    tools/wine/zuschauen.sh <Ausgabeordner> [Runden] [Logik-Frames]

Wie `tools/netz/zuschauen_test.py` (Variante „direkt“) unter Windows: Gastgeber und Mitspieler spielen eine LAN-Partie über die
Szenen `tools/netz/szenen/zuschauen_*.txt`, eine dritte Instanz tritt zweimal nacheinander als Zuschauer bei. Root und `iproute2`:
jede Instanz läuft in einem eigenen Netz-Namensraum (`gzh-zu1` bis `-zu3`, 10.78.0.1 bis .3 an der Bridge `gzh-zbr`), mit eigenem
Präfix (`<Ablage>/wineprefix-zu1` bis `-zu3`) und frisch erzeugtem Datensatz. Loopback-Instanzen (`-netzLokal`) gehen unter Wine nicht
(Wine bindet Broadcast-Sockets an alle Adressen, die zweite Instanz bekommt den Lobby-Port nicht). Ergebnis je Runde in
`<Ausgabeordner>/rundeN/ergebnis.txt` (Spieler gleich, jeder Zuschauer wurde Zuschauer und hat die CRCs des Gastgebers);
`ZUSCHAUEN_SCHALTER` hängt Spielschalter an, `ZUSCHAUEN_VORLAUF` setzt die Sekunden bis zum ersten Zuschauer (Vorgabe 20),
`ZUSCHAUEN_FRAMES` die Logik-Frames je Zuschauer (Vorgabe ein Viertel der Partie), `ZUSCHAUEN_KI=1` lässt den Gastgeber gegen eine KI
spielen statt gegen die zweite Instanz (zwei Instanzen, weniger Last). Die Partie muss beide Zuschauer überdauern; unter Wine mit
Software-Rendering reicht `zuschauen.sh <Ordner> 2 6000` mit `ZUSCHAUEN_FRAMES=450` (eine Runde rund 15 bis 25 Minuten), sonst meldet
die Auswertung „Partie zu kurz“. Eine Sperre verhindert zwei gleichzeitige Läufe. Stand 2026-10-10: bestanden (R-508).

## Lecksuche mit Benutzerkarte

    tools/wine/build_game.sh --debug /tmp/cnc-wine-dbg
    tools/wine/lecks_benutzerkarte.sh <Ausgabeordner>

Der Debug-Bau startet bis ins Hauptmenü und beendet sich, einmal ohne und einmal mit einer Karte im Ordner `Maps` der
Benutzerdaten, die nicht im Kartenkatalog der Testdaten steht. Das Hauptmenü liest sie aus der Datei (`MapCache::addMap`, `loadMap`),
derselbe Weg wie bei einer per Kartentransfer empfangenen Karte. Beide Leckberichte („Leaked a block …“) müssen leer sein (R-568).
`CNC_WINE_DIR` wählt den Debug-Bau (Vorgabe `${TMPDIR:-/tmp}/cnc-wine-dbg`); ein Lauf dauert rund zwei Minuten.

## Kartentransfer mit Lecksuche

    tools/wine/lan_kartentransfer.sh <Ausgabeordner>

Zwei Instanzen des Debug-Baus in eigenen Netz-Namensräumen (10.76.0.1 Host, 10.76.0.2 Gast; root und `iproute2`): der Host
eröffnet ein LAN-Spiel mit einer Benutzerkarte samt `map.str` und Vorschaubild, die nur er hat, der Gast tritt bei und bekommt die
Karte beim Start übertragen; beide spielen 200 Logik-Frames, verlassen die Partie und beenden das Spiel. Geprüft werden die
angekommenen Dateien und beide Leckberichte (R-568). Rund 15 Minuten.

## Auswahlrahmen

    tools/wine/auswahlrahmen.sh <Ausgabeordner> [BxH ...]

Je Auflösung (Vorgabe 3440x1440, 2560x1440, 1024x768) ein Skirmish mit dem lokalen Spieler als Mitspieler (`-skirmishAI H1,K2`);
der Testablauf-Befehl `rahmen [Rand]` zieht mit der linken Maustaste über die normale Eingabe einen Rahmen um alle eigenen
Einheiten im Bild (ihre projizierten Mittelpunkte plus Rand) und prüft, dass jede danach ausgewählt ist, einmal an der
Startposition und einmal herausgezoomt, dazu je ein großer Rahmen mit den Einheiten nahe der oberen, unteren, linken und
rechten Kante (`rahmen 12 oben` usw.). Nach jedem Rahmen prüft `zeichenbereich`, dass der Boden unter den Rändern der ganzen
3D-Ansicht im Bereich liegt, der je Bild gezeichnet wird. Bei breiten Auflösungen reicht die 3D-Ansicht unter die Steuerleiste
(R-587); dort legt `streifen` eine eigene Einheit per Kamera in den Seitenstreifen neben der Leiste und `streifen mitte` hinter die
Leiste und löst „alle im Bild“ aus: neben der Leiste muss sie ausgewählt sein, dahinter nicht. Rund drei Minuten. Denselben Ablauf
für Windows mit den echten Daten (Skirmish über das Menü) enthält `tests/render/auswahlrahmen_ablauf.txt`.

## Karteneditor: Bau und Selbsttest

    tools/wine/build_mapeditor.sh [--nur-qt] [Ablage]
    tools/wine/run_mapeditor.sh [--zeit <s>] [--bilder <Ordner>]
    tools/gate.sh --editor

`build_mapeditor.sh` baut `MapEditor.exe` und `MapEditorEngine.dll` (`Code/Tools/Editors/MapEditor`) in dieselbe Ablage
wie das Spiel, nach `<Ablage>/mapeditor`:

- **Engine:** ruft `build_game.sh` auf und nimmt dessen Bibliotheken und Schalter (`<Ablage>/schalter.sh`); die DLL
  entsteht wie unter MSVC aus `Engine/*.cpp` mit `MAPEDITOR_ENGINE_BUILD`, statisch gegen `libdevice.a`,
  `libengine.a` und DirectXTex.
- **Qt 6.8.3** (qtbase, qtsvg): die fertigen Pakete kommen nur über den Installer von download.qt.io, der hinter
  dem Proxy der Cloud-Umgebung nicht erreichbar ist. Das Skript klont deshalb die Quellen von github.com/qt und baut
  sie einmal (Linux-Werkzeuge `moc`/`rcc`, dann die Windows-Bibliotheken mit MinGW), ohne OpenGL, Netzwerk, SQL und
  Druck. Rund 20 bis 30 Minuten auf vier Kernen; danach liegt Qt unter `<Ablage>/qt` (3 GB mit Bauordnern).
- **Editor-Code** streng wie unter MSVC (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`); die
  Compression-Bibliothek kommt als eigene Objekte in die Exe (die Engine ersetzt den globalen `operator new`,
  deshalb bleibt sie in der DLL). `make` mit `-MMD`: ein zweiter Lauf übersetzt nur, was sich geändert hat, auch
  nach geänderten Headern.

`run_mapeditor.sh` erzeugt frische Testdaten (`tools/mapeditor/testdaten.py`) und startet
`MapEditor.exe --selftest` mit Engine und 3D-Ansicht unter eigenem Xvfb. Bestanden ist der Lauf nur, wenn:

- der Selbsttest mit 0 endet;
- keine Zeile `FAIL` meldet;
- Wine keinen Absturz meldet.

Bilder, `selftest.log` und `engine.log` liegen danach unter `<Ablage>/mapeditor-lauf`. Der Selbsttest prüft seit
R-329 auch den Bildinhalt der 3D-Ansicht: die Bildmitte darf weder überwiegend schwarz noch magentafarben
(fehlende Textur) noch einfarbig sein. Einstellungen des Nutzers bleiben unberührt.

**GUI-Editor** (`Code/Tools/Editors/GuiEditor`): `build_mapeditor.sh` baut `GuiEditor.exe` mit, neben
dieselbe DLL und dasselbe Qt. `tools/wine/run_guieditor.sh [--zeit <s>] [--bilder <Ordner>]` erzeugt die Testdaten des
Spiels (`tools/wine/testdaten/generate.py`, mit Fensterlayouts und Bildern) und startet `GuiEditor.exe --selftest`.
Der Lauf liest Optionsmenü, Steuerleiste und Endbildschirm mit dem Leser des Spiels und zeichnet sie mit dessen
Fenstersystem; die ersten beiden müssen sichtbar sein, der Endbildschirm (alle Fenster versteckt) leer. Danach prüft er
den Betrachter: Liste der Layouts, versteckte Fenster, Klick-Auswahl, Eigenschaften, Auflösung, Sprache und Thema,
Öffnen als Datei; dann das Bearbeiten: neues Layout mit allen Fenstertypen, Ziehen und Größe mit der Maus, Rückgängig,
Eigenschaften, Zwischenablage, Baum, Speichern und Wiederlesen, Vergleich mit der gespeicherten Fassung, Layout-Schemata, Test-Modus (Knopf, Kästchen, Textfeld bedienen). Bilder (auch `Window.png`, das ganze Fenster) und
`selftest.log` liegen unter
`<Ablage>/guieditor-lauf`.

`tools/gate.sh --editor` baut und prüft beide Editoren immer. `tools/gate.sh --full` nimmt den Schritt mit, sobald Qt und
Engine in der Ablage gebaut sind; sonst gibt der Prüfgang einen Hinweis aus.

## Voraussetzungen

Pakete stehen in `tools/linux_einrichten.sh`: MinGW, `wine64`, `xvfb`, Mesa (`libgl1-mesa-dri`,
`libglx-mesa0`), X11-Bibliotheken, `x11-apps`/`imagemagick` für Bildschirmfotos, `ninja-build` für den Qt-Bau.

## Rauchtest Direct3D 11 (ohne Spieldaten)

    tools/wine/smoke_d3d11.sh [Ablage]

Baut `d3d11_smoke.cpp`, öffnet unter Wine/Xvfb ein Fenster, legt Gerät und Flip-Swapchain an (wie das Backend
der Engine), löscht, liest einen Pixel zurück und übersetzt alle Shader aus `Data/Shaders/sm4` mit `D3DCompile`.
Stand: Feature-Level 11_1, Rückleseprobe stimmt, 29 von 29 Shadern übersetzen.

## Start ohne Daten (Stand)

Release-Bau: „Technical Difficulties...“ (allgemeiner Fehlerdialog, `ReleaseCrashInfo.txt`: „Uncaught Exception
during initialization“). Der `--debug`-Bau nennt die Ursache: `DebugLogFile.txt` und Dialog „Assertion Failure“:
`INI::load, cannot open file 'Data\\INI\\Default\\GameData.ini'` (davor: „no Generals base archives found“).
