# Befehle aus fremder Hand: Replays mit veränderten Befehlen

Im Netzspiel führt jeder Rechner die Befehle aller Mitspieler aus
(`GameLogic::logicMessageDispatcher`); ein Replay enthält dieselben Befehle, und die Wiedergabe führt
sie genauso aus. Ein manipuliertes Replay prüft also die Befehlsausführung so, wie ein manipulierter
Mitspieler sie erreicht. Der Leser selbst ist mit `tools/fuzz/engine/run.sh replay` geprüft; hier
geht es um das Ausführen, und das braucht eine laufende Partie mit den Spieldaten, also Windows.

    python tools/fuzz/replaycmd/replaycmd.py selftest <rep>...     Format lesen und unverändert schreiben
    python tools/fuzz/replaycmd/replaycmd.py dump <rep>            Befehle auflisten
    python tools/fuzz/replaycmd/replaycmd.py types                 Netzwerk-Befehle mit Nummer
    python tools/fuzz/replaycmd/replaycmd.py mutate <rep> <Ordner> [--count N] [--seed S] [--from-frame F]
    python tools/fuzz/replaycmd/replaycmd.py play --exe <exe> <Ordner> [--timeout S] [--extra ...]

- **mutate** (Linux oder Windows): Die Referenzreplays (KI gegen KI, lokaler Spieler Beobachter)
  enthalten nur Kamera- und CRC-Befehle. Jede Variante bekommt deshalb ein bis acht eingefügte Befehle
  eines KI-Spielers (meist nach einer Auswahl von Objektnummern `MSG_CREATE_SELECTED_GROUP`), Typ aus
  allen Netzwerk-Befehlen von `MessageStream.h`, Argumentliste zufällig aus üblichen Werten (kleine
  Zahlen, Objektnummern bis 3000, Orte auf der Karte) und Grenzwerten (-1, `INT_MIN`, NaN, unendlich,
  1e9). Dazu werden vorhandene Befehle verändert (Argumentwert, Spielernummer, Argumentliste gekürzt,
  verlängert, Typ getauscht). `manifest.txt` nennt je Variante, was geändert ist.
- **play** (Windows): spielt jede Variante mit `-runner -replay -mehrfach` ab (Fenster, `tools/determinism/runner.py`
  `run_game`, mit `--parallel N` mehrere Spiele gleichzeitig) und schreibt `bericht.txt`: OK, Abweichung (erwartet, Exit-Code 2: die
  veränderten Befehle ändern die Partie), ABSTURZ (anderer Exit-Code; bei `--parallel 1` mit Absturzbericht unter `Crashes\`, darin
  `Stack.txt` mit der Aufrufkette), HAENGER (Zeitlimit). Debug-Build mit `--extra -ignoreAsserts` laufen lassen: jede Assertion
  steht danach mit ihrer Variante in `assertions.txt` (aus den Protokollen `m<pid>_DebugLogFileD.txt`), die häufigsten am Ende der Ausgabe.

Vorschlag für einen Lauf (Windows, Debug-Build, Zero Hour):

    python tools/fuzz/replaycmd/replaycmd.py mutate tests/replays/zh/x64/e0zh-green-pastures-2ai-seed1.rep fuzzrep --count 300 --from-frame 300
    python tools/fuzz/replaycmd/replaycmd.py play --exe build-x64/GeneralsMD/Code/Main/Debug/GZHNeoZH.exe fuzzrep --timeout 900 --extra -ignoreAsserts

Jede auffällige Variante ist ein Befund: Variante, Zeile aus `manifest.txt`,
Aufrufkette aus dem Absturzbericht.

## Grenzen

- Die Befehle kommen von KI-Spielern einer laufenden Partie; die Wiedergabe im Beobachtermodus
  prüft keine Befehle eines lokalen menschlichen Spielers.
- Nach dem ersten veränderten Befehl weicht die Partie ab; spätere Befehle der Datei treffen auf
  einen anderen Zustand (das ist gewollt, aber ein Fund ist nicht immer dem ersten Befehl zuzuordnen:
  `manifest.txt` und Frame im Absturzbericht vergleichen).
