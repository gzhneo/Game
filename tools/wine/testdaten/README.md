# Testdaten ohne EA-Dateien

`generate.py` schreibt einen vollständigen, minimalen Datenordner, mit dem `GZHNeoZH.exe` ohne
EA-Archive startet und eine KI-gegen-KI-Partie (Skirmish) durchspielt. Alles wird beim Aufruf aus dem
Skript berechnet (nur Standardbibliothek, bei jedem Lauf dieselben Bytes): INI-Werte, Texte,
Fensterlayouts, Rauschtexturen, Quader-Modelle im W3D-Format, zwei Karten. Nichts stammt aus EA-Dateien
oder ist danach nachgebaut; Namen und Zahlen sind erfunden (zwei Seiten „TestAlpha“ und „TestBravo“).
Die Ausgabe gehört nicht ins Repository.

    python3 tools/wine/testdaten/generate.py <Zielordner> [--polyhaven [1k|2k|4k]]
    python3 tools/wine/testdaten/pruefen.py [Datenordner]      Parser der Engine ohne Exe (Fuzz-Harness)

`--polyhaven` ersetzt die Rauschtexturen des Geländes und die einfarbigen Modelle durch CC0-Fototexturen von Poly
Haven (Gelände 256 Pixel je Kachel, Modelle 512 x 512), Fels und Nachschubhaufen durch Poly-Haven-Modelle; mit Blender werden Gebäude, Fahrzeuge und der Schütze selbst gebaut
(Texturen von Poly Haven). Braucht Pillow und beim ersten Lauf Netz; danach kommt alles
aus dem Zwischenspeicher. Spiellogik und Karten bleiben gleich, ohne den Schalter ist die Ausgabe unverändert.

Der Zielordner ist der Spielordner (neben `GZHNeoZH.exe`). Die Engine liest lose Dateien vor Archiven,
Archive braucht der Datensatz nicht.

## Dateien

| Datei | Inhalt |
|---|---|
| `generate.py` | Aufruf, schreibt alle Dateien |
| `gen_logic.py` | Spiellogik: `GameData`, Seiten (`PlayerTemplate`), Objekte, Waffen, Rüstung, Locomotor, KI-Daten, Kommandoknöpfe, Kommandosätze, Texte (`Generals.str`) |
| `gen_ui.py` | Oberfläche und Sonstiges: Audio-INIs (mit stummer Musikdatei), Maus, Sprache, Detailstufen, Gelände, Wasser, Straßen |
| `gen_wnd.py` | Fensterlayouts (`.wnd`): Hauptmenü, Skirmish, Kartenwahl, Optionen, Tastenbelegung, Spielmenü, Dialoge, Speichern/Laden, LAN-Lobby und -Einstellungen, Direktverbindung, Steuerleiste, Ladebildschirme, ... |
| `gen_assets.py` | Texturen (Gelände, Wasser, Oberflächenatlas), W3D-Modelle, Bilddefinitionen |
| `gen_ui_art.py` | Oberflächenbilder: Knöpfe, Rahmen, Logo, Einheitenbilder, Symbole, Ladebild, `MappedImage`-Einträge |
| `gen_polyhaven.py` | Poly Haven (`--polyhaven`): Abruf, Zwischenspeicher, Texturen nahtlos verkleinern, glTF-Modelle nach W3D |
| `blender_modelle.py` | Blender-Skript (`--polyhaven`): Gebäude, Fahrzeuge und Schütze aus Grundformen, Texturen je Seite gebacken |
| `gen_map.py` | Karten im Chunk-Format der `.map`-Dateien und `Maps\MapCache.ini` |
| `gen_util.py` | Hilfen: Schreiben, TGA, W3D, Chunk-Container, CRC |
| `pruefen.py` | lädt die erzeugten INI-Dateien und Karten mit den echten Parsern (Fuzz-Harness, direkt unter Wine) |
