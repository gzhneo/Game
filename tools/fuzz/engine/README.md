# Fuzzer mit geladener Spiellogik (INI, Karten, Replays, Spielstände, LAN-Lobby, Netzspiel, W3D-Modelle)

Prüft Leser der Spiellogik so, wie Daten von Mitspielern sie erreichen: die `map.ini` einer Karte
(alle Blockparser und Modul-Feldparser samt W3D-Zeichenmodulen) und die `.map`-Datei selbst
(Kartenliste, Seiten, Teams, Skripte, Polygon-Trigger, Wegpunkte, Höhenkarte, Blend-Tiles, Beleuchtung). Beides kommt mit jeder übertragenen Karte vom Host. Dazu
Replays (getauscht zwischen Spielern), der Spielinfo-Text, den auch ein LAN-Host schickt, und
Spielstände (ebenfalls getauscht; das Lade-Menü liest jede Datei im Save-Ordner) und die Pakete der
LAN-Lobby, die jeder Rechner im Netz schicken kann, und die Pakete der Mitspieler im Netzspiel.

    tools/fuzz/engine/run.sh ini [Eingaben] [Saat]    map.ini (Standard 20000 Eingaben, Saat 1)
    tools/fuzz/engine/run.sh map [Eingaben] [Saat]    .map-Dateien
    tools/fuzz/engine/run.sh replay [Eingaben] [Saat] Replays und Spielinfo-Texte
    tools/fuzz/engine/run.sh save [Eingaben] [Saat]   Spielstände
    tools/fuzz/engine/run.sh lan [Eingaben] [Saat]    Pakete der LAN-Lobby
    tools/fuzz/engine/run.sh net [Eingaben] [Saat]    Pakete im Netzspiel (ConnectionManager)
    tools/fuzz/engine/run.sh w3d [Eingaben] [Saat]    W3D-Modelle und DDS-Texturen (WW3D2)
    tools/fuzz/engine/run.sh lockstep                 Netzpartie mit mehreren Spielern (Test, kein Fuzzer)
    tools/fuzz/engine/run.sh <art> --run <Datei>      eine Datei mit Debug-Log laden (Nachstellen)
    HARNESS_VERBOSE=1 ...                             Debug-Log und jede Assertion ausgeben
    HARNESS_ASSERT_STACK=1 ...                        dazu die Aufrufkette der ersten Assertion
    ENGINEFUZZ_DIR=<Ordner>                           Ablage (Standard /tmp/cnc-enginefuzz)
    HARNESS_SECONDS=<n>                               Fuzz-Schleife nach n Sekunden beenden (Nachtlauf)
    ENGINEFUZZ_RUN=<Ordner>, ENGINEFUZZ_NOBUILD=1     eigener Arbeitsordner, ohne Bau (parallele Läufe, ../nachtlauf.sh)

## Aufbau

- `run.sh` baut die ganze GameEngine, den W3D-Teil der Geräteschicht (`W3DDevice`, `WW3D2`; gezeichnet
  wird nichts) und die Bibliotheken mit MinGW als Debug-Konfiguration (`_DEBUG`: Speicherpool mit
  Grenzprüfung; `-fsanitize=bounds` im Trap-Modus) und führt einen Harness unter Wine aus. Der
  Engine-Code ist mit `-fsanitize-coverage=trace-pc` übersetzt: `Harness.cpp` zählt die erreichten
  Kanten (64K-Tabelle, Trefferklassen wie AFL), und jede Eingabe, die neue Kanten erreicht, wird in
  den Korpus aufgenommen und weiter mutiert. Dazu `-fsanitize-coverage=trace-cmp`: jeder Vergleich
  mit einer Konstanten und jeder `switch`-Fall legt die Konstante in ein Wörterbuch (bis 4096 Werte,
  gesammelt ab dem Beginn der Fuzz-Schleife, also aus den geprüften Lesern;
  Chunk-Versionen, Befehlsnummern, Grenzwerte). Die Mutatoren nehmen für 32-Bit-Felder jedes zweite
  Mal einen Wert daraus, der INI- und der Spielinfo-Mutator setzen ihn als Zahl ein. Gemessen (gleiche
  Saaten, erreichte Kanten ohne/mit Wörterbuch): Karten 6.685/6.683, Replays 3.368/3.396, Spielstände
  1.485/1.426, INI 8.788 nach 300.000/8.653 nach 100.000 Eingaben. Der Gewinn ist klein; beim Karten-
  und INI-Fuzzer ist das Wörterbuch voll, beim Spielstand-Fuzzer mit 16 Werten zu klein, um zu helfen. Der erste Bau dauert rund zehn Minuten, danach übersetzt `make` nur
  Geändertes (`syncmirror.py` hält die Zeitstempel des Spiegels; geänderte Schalter bauen alles neu).
  Gearbeitet wird in `<Ablage>/run/`.
- `Harness.cpp` legt die Datenspeicher der Spiellogik an (Objekte und Module, Waffen, Rüstungen,
  Locomotor, OCL, FX, Partikel, Upgrades, Wissenschaften, Spezialfähigkeiten, KI, Kisten, Gelände,
  Straßen, Animationen, Bilder, Ränge, Spielerschablonen, `GameData`, Dateisysteme), lädt
  `seeds/base/` als Spieldaten und meldet Abstürze (Eingabe als `crash-<n>.*`, Rückgabewert 3) und
  Hänger von 20 s (`hang-<n>.*`, Rückgabewert 4) jeweils mit aufgelöster Aufrufkette.
- `IniFuzz.cpp` mutiert `map.ini`-Dateien zeilen- und tokenweise und lädt sie wie
  `GameLogic::loadMapINI` (`INI_LOAD_CREATE_OVERRIDES`, `WeaponStore::postProcessLoad`, Zurücksetzen am
  Partieende). Blöcke, deren Speicher der Harness nicht anlegt (Ton, Fenster, Video, Oberfläche),
  werden übersprungen. `seedgen.py` erzeugt je registriertem Modul (`ModuleFactory` und
  `W3DModuleFactory`, also auch die `W3D*Draw`-Module) ein Objekt mit allen Feldern; der Harness behält
  beim Start nur die Felder, mit denen die Datei sauber lädt (217 von 218 Modulen).
- `MapFuzz.cpp` mutiert `.map`-Dateien entlang ihrer Chunk-Struktur (Werte in Datenchunks, Längen,
  Versionen, Größen, Chunks doppeln oder entfernen, Datei kürzen) und liest sie wie das Spiel:
  die Kartenliste (`MapCache::updateCache` aus `<Nutzerdaten>\Maps\`, zweimal, damit auch die
  geschriebene `MapCache.ini` wieder gelesen wird), die logischen Kartendaten wie
  `W3DTerrainLogic::loadMap` (`WorldHeightMap` mit `logicalDataOnly`: Größe, `WorldInfo`, Objekte,
  `PolygonTriggers`, `SidesList` mit Spielern, Teams, Baulisten und Skripten; danach
  `prepareForMP_or_Skirmish` und `validateSides`), `TerrainLogic::loadMap` (Wegpunkte) und das Gelände
  wie `W3DTerrainVisual::load` (`WorldHeightMap`: Höhenkarte, Blend-Tiles, Klippen, Beleuchtung;
  Texturen werden nicht angelegt). Seed ist die Beispielkarte des Karteneditors.
- `ReplayFuzz.cpp` mutiert Replays (Kopffelder, 32-Bit-Werte und Bytes im Befehlsteil, Kürzen,
  Kopieren) und liest sie wie die Replay-Liste (`readReplayHeader`) und die Wiedergabe im
  Analysemodus (`analyzeReplay`, `update` bis zum Ende: Kopf, Spielinfo, jeder Befehl mit seinen
  Argumenten; ausgeführt wird nichts). Jede zweite Eingabe ist nur ein mutierter Spielinfo-Text für
  `ParseAsciiStringToGameInfo` (derselbe Weg wie die Spieloptionen eines LAN-Hosts). Seeds sind die
  ersten 16 KB zweier Referenz-Replays aus `tests/replays`.
- `SaveFuzz.cpp` erzeugt zwei Spielstände mit dem Speichercode der Engine (`XferSave`: Kopf,
  Spielinfo, Kampagne, einmal mit Challenge-Spielinfo) und mutiert sie (32-Bit-Werte, Bytes,
  eingefügte Zeichenketten samt Pfaden, Kürzen, Kopieren). Gelesen wird wie im Lade-Menü
  (`GameState::getSaveGameInfoFromFile`) und wie die Blockschleife von `GameState::xferSaveData`
  mit den Blöcken, die ohne laufende Partie laden (Spielinfo, Kampagne mit `SkirmishGameInfo`); die
  übrigen Blöcke werden übersprungen wie unbekannte. Die Karte im Spielstand (`GameStateMap`) und
  alle Blöcke einer laufenden Partie sind nicht geprüft.
- `LanFuzz.cpp` schickt gefälschte Lobby-Pakete an `GameLobby::update`, wie sie jeder Rechner im Netz
  senden kann: ein neues `GameLobby` je Eingabe in einer von vier Rollen (in der Lobby, Gastgeber eines
  Spiels, Spieler, der nach dem ersten Paket dem ersten bekannten Spiel beitritt, Spieler mit
  Direktverbindung zur zweiten Adresse), dann bis zu 32 Pakete
  von vier Absenderadressen. Die Handler (`GameLobbyReceive.cpp`) und die Spieloptionen
  (`GameLobby::OnGameOptions`) laufen wie im Spiel; Rückrufe, die nur Menüs aktualisieren, Einstellungen
  schreiben oder das Spiel starten, sind leer, und es wird kein Socket geöffnet (nichts geht hinaus).
  Seeds sind drei Abläufe (Lobby, Beitritt beim Gastgeber, Beitritt zu einem Spiel), vom Harness gebaut.
  Mutiert werden Nachrichtentyp, Werte im Datenteil, Namen und Optionstexte ohne Ende, Absender,
  Reihenfolge und Rolle.
- `NetFuzz.cpp` schickt Pakete von Mitspielern an `ConnectionManager::update`, vor und in der
  Partie: drei Spieler (Platz 0 Gastgeber und Packet Router, Platz 1 der Harness, Platz 2 ein weiterer),
  dazu eine vierte, fremde Adresse; ein neuer `ConnectionManager` je Eingabe mit einer Transportschicht
  ohne Socket. Danach holt der Harness fertige Frames ab wie `Network::update` (Spielbefehle werden zu
  Spielnachrichten, `PLAYERLEAVE` wird ausgeführt). Seeds: ein Paket je Befehlsart (Fortschritt,
  Dateitransfer, Frames, Spielbefehl, Vorlauf, Chat, Bestätigungen, Trennen, Abstimmung, Verlassen,
  Wiederholung, zerlegte Befehle), mit der Paketklasse der Engine gebaut. Ohne Gerät ersetzt der Harness
  Fenstermanager, Spielanzeige und `TheNetwork` durch leere Klassen; Assertions der Startpakete
  (fehlendes Tonereignis in den Testdaten) werden nur gemeldet.
- `LockstepTest.cpp` spielt eine Netzpartie mit mehreren Spielern in einem Prozess: je Spieler ein
  eigener `Engine::EngineContext` (Netz, Logik-Frame, Befehlsliste, Spielinfo), echte Sockets auf der
  Loopback-Schnittstelle, gestartet wie aus der Lobby (`attachTransport`, versiegelt mit den Schlüsseln der
  Platzliste). Der Test ist die Spielschleife: Frame-Takt, ab und zu ein Spielbefehl, `Network::update`, bei
  fertigem Frame die Befehle abholen und den Frame weiterzählen. Szenarien: zwei Spieler; drei Spieler mit
  Emulator (Verzögerung, Verlust, Doppel, Umordnung); drei Spieler, einer geht. Geprüft: alle erreichen
  Frame 300, alle führen dieselben Befehle in denselben Frames aus, jeder Befehl genau einmal, der Austritt
  wird bei allen im selben Frame ausgeführt, jede Chatzeile kommt bei jedem genau einmal an, die Meldungen
  an die Oberfläche (`LockstepEvents`) stimmen. Läuft in Echtzeit (rund 30 s); `NetHarness.h` enthält die
  leeren Fenster- und Anzeigeklassen, die er mit `NetFuzz.cpp` teilt.
- `W3DFuzz.cpp` lädt mutierte `.w3d`-Dateien wie ein Mod sie mitbringt (`WW3DAssetManager::Load_3D_Assets`: Netze mit
  Materialien, Skin und AABB-Baum, Hierarchien, klassische, komprimierte und Morph-Animationen, HLOD, Aggregate, Kästen,
  Null-Objekte), legt von jedem Prototyp ein Objekt an und spielt jede geladene Animation auf jedem Modell mit Knochen
  ab (Knochen abfragen bei mehreren Frames, auch außerhalb der Animation); danach `Free_Assets`. Jede zweite
  Eingabeart ist eine DDS-Textur (`DDSFileClass` mit Reduktion 0 bis 3, jede Stufe gelesen und nach A8R8G8B8 kopiert
  wie der Texturlader). Mutiert wird entlang der Chunk-Struktur (Werte, IDs, Längen, Chunks doppeln, entfernen,
  verkürzen, verlängern, Chunks anderer Dateien einsetzen) bzw. an den Feldern des DDS-Kopfs. Saaten erzeugt
  `w3dseeds.py` ganz aus eigener Vorschrift (Requisit, Baum mit Hierarchie und HLOD, Skin mit AABB-Baum, klassische,
  Zeitcode-, Delta- und Morph-Animation, Kasten, Null-Objekt; DXT1, DXT5, BC7 mit DX10-Kopf, RGBA, Würfeltextur). Ohne
  Gerät werden die Texturen der Modelle nicht angelegt. Beim Nachstellen (`--run`) nennt die Ausgabe die geladenen
  Modelle und Animationen. Ein Stapelüberlauf wird wie ein Absturz gemeldet (`SetThreadStackGuarantee`); bleibt der
  Hauptthread stehen, beendet der Wächter den Prozess nach spätestens 50 s.
- `stubs.cpp` ersetzt, was ohne Programm fehlt oder MinGW nicht übersetzt (`Debug.cpp`: Assertions
  werden gezählt; `WinMain`; `texutil.cpp` und das D3D11-Backend).
- `seeds/` sind eigens geschriebene Testdaten, keine Spieldaten.

## Grenzen

- Kein AddressSanitizer (MinGW); Überläufe innerhalb eines Speicherpool-Blocks fallen erst auf, wenn
  sie die Grenzmarken treffen oder abstürzen.
- Die Abdeckung zählt nur Kanten im Engine-Code. Das Wörterbuch kennt nur Zahlen, keine
  Zeichenketten (Vergleiche mit `strcmp`/`stricmp` meldet GCC nicht); die Namen der INI-Felder
  liefert `seedgen.py`.
- Ohne Gerät: Zeichenmodule werden gelesen, aber nicht angelegt (kein Drawable), Texturen und
  Modelle nicht geladen; die INI-Blöcke von Ton, Oberfläche und Video werden nicht geprüft. Ein
  Absturz an einem fehlenden Subsystem ist ein Harness-Fehler, kein Fund.
- Übersetzt mit `-fno-delete-null-pointer-checks`.

## Funde

INI: R-319 (`LocomotorStore::reset`), R-330 (Tabellen ohne Endeintrag), R-331 (unbekannter Modulname,
Felder in Modulen ohne Daten), R-332 (Parserfehler beendete das Spiel, jetzt Abbruch des Kartenstarts);
offen R-333 (Waffen aus `map.ini` bleiben). Karten: R-334 (Chunk-Leser lasen über das Chunk-Ende,
Chunk-Größen und Skript-Parameter ungeprüft), R-335 (`normalizeAngle` hing bei unendlichem Winkel),
R-336 (vergiftete `MapCache.ini` beendete das Spiel bei jedem Start, Überläufe in `QuotedPrintable`);
R-337 (Kartenliste fand die `map.str` von Nutzerkarten nicht). Replays: R-338 (`format` warf bei
langem Text). Spielstände: R-350 (eingebettete Karte unter fremdem Pfad geschrieben, negative
Blockgröße ließ Lade-Menü und Laden endlos kreisen, Slotwerte ungeprüft). LAN-Lobby: R-354 (fremde
Spielankündigung löschte das laufende Spiel, Absturz). Netzspiel: R-355 (jeder konnte jeden aus dem Spiel
werfen und fremde Einheiten zerstören lassen). Nicht erreicht: `Network` selbst (Vorlauf, Zerstören eines Spielers). W3D-Modelle und DDS (2026-10-06): R-590
(Animationskanäle), R-591 (komprimierte Animation, doppeltes `delete`), R-592 (HLOD), R-593 (DDS-Leser), R-594
(Netz-Lader), R-595 (Hierarchie, Knochenindex), R-596 (AABB-Baum), R-597 (Namensfelder der übrigen Prototypen),
R-598 (Chunk-Länge gegen die Dateigröße), R-599 (leeres Netz), aus den Nachläufen R-570 bis R-581.
