# Fuzzing (E-8.4)

Ziele in diesem Ordner prüfen Parser, die **Dateien von außen** lesen: Archive, Replays,
Spielstände, Karten. Genau dort liegt der Code von 1995 bis 2003, der seinen Eingaben vertraut.

## Ziele

| Ziel | Prüft | Stand |
|---|---|---|
| `FuzzCompression.cpp` | `CompressionManager::decompressData` (EA RefPack, BTree, Huffman, ZLib) | fertig, alle Funde behoben (siehe unten) |
| `FuzzW3DChunks.cpp` | `ChunkLoadClass` (WWLib), Grundlage aller W3D-Modelle, -Animationen, -Hierarchien | fertig 2026-09-27, Funde behoben |
| `FuzzWavParser.cpp` | `ParseWavAndDecode` (PCM, IMA ADPCM) | fertig 2026-09-27, Funde behoben (R-297) |
| `engine/` (INI) | `INI::load` mit geladener Spiellogik, wie eine `map.ini` sie erreicht (MinGW, Wine) | fertig 2026-09-29, Funde R-319, R-330 bis R-332 behoben, siehe `engine/README.md` |
| `FuzzMapReader.cpp` | Kartenmodell des neuen Editors (`Code/Tools/Editors/MapEditor/Model`): Chunk-Container, Wörterbücher, alle Kartenabschnitte; prüft zusätzlich, dass Geschriebenes beim zweiten Schreiben gleich bleibt | fertig 2026-09-27; erster Fund (Namenstabelle mit doppelter ID) behoben |
| `FuzzBabylon.cpp` | Leser des Übersetzungswerkzeugs Babylon: `main.db` (mit Schreib-Lese-Stabilität), Tabellen `.xlsx`/`.ods`/`.csv`, `Generals.str`, `.csf` als Quelle | fertig 2026-10-10, ohne Fund |
| `engine/` (Karten, Replays, Spielstände, LAN-Lobby, Netzspiel) | `.map` (`DataChunk`), Replays, Spielstände (`XferLoad`), Lobby- und Spielpakete mit geladener Spiellogik | fertig 2026-09-29 bis 2026-10-01, siehe `engine/README.md` |
| `FuzzBigArchive.cpp`, `FuzzZipArchive.cpp`, `FuzzTextFiles.cpp`, `FuzzTarga.cpp`, `FuzzSession.cpp` | `.big`-Index, ZIP-Mods, Stringtabellen, TGA, Sitzungsnachrichten | fertig, siehe unten |
| `engine/W3DFuzz.cpp` | W3D-Lader im Spiel (Netze, Hierarchien, Animationen, HLOD, Aggregate, Kästen, ...), DDS-Leser (`DDSFileClass`) | fertig 2026-10-06, Funde R-590 bis R-599 und R-570 bis R-581 behoben |

## Nachtlauf

    tools/fuzz/nachtlauf.sh [Sekunden je Ziel, Vorgabe 3600] [gleichzeitig, Vorgabe Kerne/2]

lässt jedes Ziel eine feste Zeit laufen: erst die libFuzzer-Ziele (`linux/run.sh` mit `FUZZ_PARALLEL`), dann die
Engine-Fuzzer unter Wine (`engine/run.sh`, einmal gebaut, dann mit `HARNESS_SECONDS` in getrennten Arbeitsordnern
parallel, jede Art mit eigenem Wine-Präfix). Die Saat der Engine-Fuzzer ist das Datum (`NACHTLAUF_SAAT`), jede Nacht kommen also andere Folgen;
Korpus und Funde bleiben unter `NACHTLAUF_DIR` (Vorgabe `/tmp/cnc-nachtlauf`), die Fundliste steht in
`ergebnis.txt`. `NACHTLAUF_TEIL=libfuzzer|engine` nimmt nur einen Teil. 
## Unter Linux mit libFuzzer (Cloud-Sitzungen, Prüfgang)

    tools/fuzz/linux/run.sh [Sekunden je Ziel]

baut die Ziele nativ mit clang, libFuzzer, AddressSanitizer und UBSan (strikt: der erste Fall von
undefiniertem Verhalten zählt als Fund) und lässt jedes laufen (`FUZZ_PARALLEL=<n>` mehrere gleichzeitig,
`FUZZ_ZIELE="fuzz_w3d ..."` nur diese); `tools/gate.sh --full` ruft es mit 60 s
je Ziel auf. Die Quellen kommen aus dem Spiegel der Syntaxprüfung (Schreibweisen der Includes);
`linux/native_compat.h` ergänzt MSVC-Schreibweisen, `linux/shim/` ersetzt `ForceOrder.h` und den
XAudio2-Manager (nur `WaveFormatInfo`) für den WAV-Parser. Saatdateien unter `seeds/`; für W3D liegt
dort `ShatterPlanes0.w3d` aus `WW3D2/RequiredAssets` und selbst erzeugte Chunks.

Funde aus dem ersten Linux-Lauf (2026-09-27): `ChunkLoadClass` las beschädigte Dateien endlos (R-296),
der WAV-Parser las Chunkgrößen fehlausgerichtet (R-297), der Huffman-Dekoder hatte drei Stellen mit
undefiniertem Verhalten (R-299). Beim Durchsehen des DDS-Lesers (nicht nativ baubar) kam R-298 dazu.

`FuzzBigArchive.cpp` prüft das Verzeichnis von `.big`-Archiven (`Engine::readBigArchiveDirectory`, R-312); die
Eingabe wird in Stücken wechselnder Größe gelesen, wie sie ein Dateilesen liefern kann, und jeder angenommene
Eintrag muss in der Eingabe liegen. Saat unter `seeds/big/`. Die echten Archive prüft unter Windows
`tools/abnahme/bigpruefung.py` mit denselben Regeln (Schritt „BIG-Archive“ im Prüfskript).

`FuzzZipArchive.cpp` prüft den ZIP-Leser für Mods (`Engine::readZipArchiveDirectory`, `Engine::extractZipEntry`): Ende-Datensatz,
Zentralverzeichnis, lokale Köpfe und Deflate-Ströme kommen aus der Eingabe; jeder angenommene Eintrag wird entpackt und muss
die angekündigte Größe haben. Saat unter `seeds/zip/` (gespeichert, Deflate mit Ordnereintrag und Kommentar, leer).

`FuzzBabylon.cpp` schickt jede Eingabe durch alle Leser des Übersetzungswerkzeugs Babylon (`Code/Tools/Editors/Babylon`): `main.db`, Übersetzungstabellen `.xlsx`/`.ods`/`.csv` (kommen von Übersetzern), `Generals.str` und
`.csf`/`.str` als Quelle. Eine angenommene Datenbank muss sich stabil schreiben und wieder lesen lassen, eine angenommene
Tabelle wird in eine kleine Datenbank importiert. Saat unter `seeds/babylon/` (je eine Datei jedes Formats, mit
`BabylonTool` erzeugt). Vor der Aufnahme: 30.000 zufällig verfälschte Dateien unter ASan/UBSan und 120 s libFuzzer (191.874 Läufe) ohne Befund (2026-10-10).

`FuzzTextFiles.cpp` schickt jede Eingabedurch beide Stringtabellen-Leser (`Engine::readCsf`, `Engine::readStr`, R-313) und
prüft die Längengrenzen jedes Eintrags. Saat unter `seeds/text/` (eine kleine `.csf`, eine `map.str`).

`FuzzTarga.cpp` lädt jede Eingabe durch den TGA-Leser (`WWLib/TARGA.CPP`, R-316), einmal wie `TextureLoader` (Palette
von 256 Einträgen, Bild vom Leser) und einmal mit beidem vom Leser; die Dateien liefert eine Speicher-`FileFactory`,
`linux/shim/targa_native.h` ersetzt die Windows-Teile von `ffactory.h`. Nur unter Linux (kein CMake-Ziel: das Ziel definiert
`_TheFileFactory` selbst). Saat unter `seeds/tga/`, darunter der RLE-Überlauf aus R-316.

## Zwei Betriebsarten

**Mit libFuzzer** (MSVC, `/fsanitize=fuzzer`):

```
cmake --preset x64-debug -DENABLE_FUZZERS=ON
cmake --build build-x64 --target FuzzCompression
build-x64\tools\fuzz\Debug\FuzzCompression.exe -max_total_time=3600 tools\fuzz\seeds\compression
```

**Ohne libFuzzer** (überall, auch ohne Windows): Die Ziele sind zugleich normale Programme, die
ihre Eingabedateien als Argumente nehmen. `dumbfuzz.py` mutiert Saatdateien und ruft sie damit
auf:

```
python tools/fuzz/dumbfuzz.py --target build/FuzzCompression --seeds tools/fuzz/seeds/compression \
    --runs 20000 --out fuzz-funde
```

Mit einem ASan-Build (`-fsanitize=address` bzw. `/fsanitize=address`) findet auch diese einfache
Variante Zugriffe über Puffergrenzen.

## Funde aus dem ersten Lauf (2026-09-14)

Gegen den ungehärteten Stand: **831 Abstürze in 4.000 Aufrufen** (762 Heap-Overflows, 69 SEGV),
alle in den EA-Dekodern:

- `REF_decode` (RefPack) folgt Längen und Rückverweisen aus der Datei ohne jede Prüfung und
  schreibt über den Zielpuffer hinaus bzw. liest weit hinter der Quelle.
- `BTREE_decode`/`BTREE_chase` läuft an Baumknoten entlang, die aus der Datei stammen; ein Zyklus
  lässt die Rekursion nie enden, geschrieben wird dabei über das Ziel hinaus.

Behoben durch `REF_decode_bounded` und `BTREE_decode_bounded` (Grenzen für Quelle, Ziel,
Rückverweise und Rekursionstiefe); `CompressionManager` benutzt nur noch diese. Für gültige Daten
liefern sie bitgleiche Ergebnisse — geprüft mit je einem Gleichheitstest gegen den Originaldekoder
(500 bzw. 300 zufällige Datensätze, Ausgabe und zurückgelieferte Längen identisch). Danach:
**0 Funde in 10.000 Aufrufen**, und im Langlauf 0 in 60.000.

`HUFF_decode` hatte dieselbe Schwäche und obendrein **zwei Endlosschleifen**, die erst nach der
Speicher-Härtung sichtbar wurden (das Fuzzing meldete sie als Zeitlimit-Treffer): die Schleife in
`SQgetnum`, die keine Eingabe verbraucht, und die Leapfrog-Tabelle, deren `unsigned char`-Zähler
im Kreis läuft. `HUFF_decode_bounded` schließt beides zusammen mit den Speichergrenzen;
Gleichheitstest über 300 Datensätze, danach 40.000 Fuzzing-Aufrufe ohne Fund.

## Ziele mit Engine

Die Ziele, die eine initialisierte Engine brauchen (Dateisystem, `NameKeyGenerator`, Speicherpool), laufen seit
2026-09-29 im Harness unter `engine/` (MinGW, Wine): INI, Karten, Replays, Spielstände, LAN-Lobby, Netzspiel und
seit 2026-10-06 die W3D-Modelle und DDS-Texturen (`engine/README.md`).

## Netzpakete (`netpacket/`)

`tools/fuzz/netpacket/run.sh [Iterationen] [--write-golden]` baut `NetPacketTests.exe` mit MinGW (Spiegel und Bibliotheken von `tools/linuxtests/run.sh`) und führt es unter Wine aus: Rundlauf aller 27 Befehlstypen (schreiben, lesen, erneut schreiben) gegen `golden.txt` -- das Leitungsformat darf sich nicht versehentlich ändern -- und Fuzzing von `NetPacket::ConstructNetCommandMsgFromRawData` und `getCommandList`. Jede Eingabe liegt direkt vor einer mit `VirtualAlloc` gesperrten Seite, ein Lesen hinter dem Paket fällt als Seitenfehler auf; dazu `-fsanitize=bounds` im Trap-Modus (MinGW hat keine ASan-Laufzeit). Stubs für Engine-Singletons in `net_stubs.cpp`. Im Prüfgang mit 50 000 (schnell) bzw. 500 000 Eingaben (`--full`). `--write-golden` nur nach einer gewollten Formatänderung.

**Differenztest** (`NetPacketDiff.cpp`, `NETPACKET_DIFF=<Folgen>`, Standard 2 000, im Prüfgang 2 000 bzw. 20 000 mit `--full`): `run.sh` holt `NetPacket.h/.cpp` aus dem Referenzstand `REF_COMMIT` (Serialisierung vor der tabellengesteuerten Neufassung, EA-Code mit den Korrekturen R-306, R-308, R-309), benennt die Klassen textuell in `RefNetPacket` um und linkt beide Fassungen in dasselbe Programm. Zufällige Befehlsfolgen aller Typen (fortlaufende und springende IDs, Frames, Spieler, Relays) werden mit beiden geschrieben -- Rückgabe von `addCommand`, Paketbytes und Befehlszahl müssen gleich sein --, große Befehle mit beiden in Wrapper-Pakete geteilt, und die Pakete (unverändert und verfälscht) mit beiden gelesen (`getCommandList`, `ConstructNetCommandMsgFromRawData`); die gelesenen Befehle werden über den Referenz-Schreiber verglichen. Die Wrapper-IDs kommen aus einem globalen 16-Bit-Zähler, der Test dreht ihn für die Referenz einmal herum. Beim Teilen bleiben RunAheadMetrics, LoadComplete, TimeOutStart und Wrapper außen vor: die Referenz rechnete deren Größe falsch (R-311), geteilt werden sie im Spiel nie. Der Test fand R-308 und R-309 und sichert die tabellengesteuerte Neufassung ab (3,7 Mio. Befehle, drei Seeds, ohne Unterschied); `--seed <n>` wählt andere Folgen. Braucht die volle Git-Historie.
