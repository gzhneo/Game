# Referenzreplays (E-0.3)

Replays (`*.rep`) mit ihrem CRC-Verlauf (`*.crc`) als Regressionsreferenz für die Spiellogik.
`ctest -L assets` spielt sie mit `-runner` ab (`ReplayDeterminism`, `ReplayDeterminismZH`) und
vergleicht den CRC-Verlauf; Werkzeug: `tools/determinism/runner.py`.

## Ordner

```
tests/replays/x64/   Generals, x64
tests/replays/zh/x64 Zero Hour, x64
tests/replays/neueki/x64     Generals, x64, neue KI (TODO 50, Abschnitt „Neue KI“)
tests/replays/neueki/zh/x64  Zero Hour, x64, neue KI
```

**Nur x64:** Der 32-Bit-Build ist seit 2026-09-26 entfernt, die x86-Referenzen mit ihm (Tag
`x86_Letzter_Stand`). Die Ordnernamen tragen die Architektur weiter, weil Replays an sie gebunden sind:
x86 und x64 rechneten die Spiellogik verschieden (`float`-Mathe der Windows-Laufzeit). Innerhalb von x64 ist der Verlauf reproduzierbar, und Debug und Release müssen
übereinstimmen (eine Abweichung war ein Fehler: TODO 36).

## Abweichungen eingrenzen

```
GZHNeo.exe -runner -replay <rep> -crcLog lauf.crc -crcLogInterval 1 -deepCRCFrames 382 383 -ignoreAsserts
python tools/determinism/deepcrc_diff.py a.crc.f383.deep b.crc.f383.deep
```

`-deepCRCFrames von bis` schreibt für jeden Logik-Frame im Bereich den vollständigen Deep-CRC-Zustand
(`<crcLog>.f<Frame>.deep`) und einen Index `Offset ID Name` (`.deep.idx`). `deepcrc_diff.py` vergleicht
zwei Abzüge Objekt für Objekt und zeigt die erste abweichende Byteposition. Vorgehen: mit
`-crcLogInterval 1` den ersten abweichenden Frame suchen, dann den Frame davor und diesen abziehen.

## Aufnehmen

```
python tools/determinism/runner.py record --exe build-x64/Generals/Code/Main/Debug/GZHNeo.exe ^
    --map "Alpine Assault" --ai 2 --seed 1 --frames 9000 ^
    --out tests/replays/x64/e0-alpine-assault-2ai-seed1 --extra -ignoreAsserts
```

`--map` ist der Kartenordner (`Maps\<Name>\<Name>.map`). Die drei Generals-Referenzen: `Alpine
Assault` (2 KI, Seed 1, 9.000 Frames), `Tournament Lake` (3 KI, Seed 2, 18.000), `Twilight Flame`
(7 KI, Seed 3, 36.000).
Absichtliche Logikänderungen erneuern die Referenzen. **Danach** `python tools/determinism/logikkennung.py` aufrufen
und `Code/GameEngine/Common/LogicVersion.h` mit committen: die Logik-Kennung ist ein Hash über die CRC-Zeilen
dieser Dateien und entscheidet, welche Stände miteinander spielen und ihre Replays abspielen (der Prüfgang
`tools/gate.sh` schlägt fehl, solange sie nicht passt).

**Neu aufgenommen 2026-09-29** (alle acht Sätze der klassischen und der neuen KI, Release, dieselben Parameter): die Abhilfe gegen Deadlocks
fahrender Fahrzeuge ändert die Logik ab Frame 3.600. Ein altes Replay ohne die Abhilfe läuft mit `-noDeadlockFix` weiter synchron.

**Stand der Generals-Referenzen:** `x64` ist nach der Angleichung Phase 5 (Generals rechnet seitdem
die Zero-Hour-Spiellogik) am 2026-09-22 neu aufgenommen (06898367a). Die x86-Referenzen sind am
2026-09-26 mit dem 32-Bit-Build entfallen.

## Prüfen

```
python tools/determinism/runner.py check --exe build-x64/Generals/Code/Main/Debug/GZHNeo.exe ^
    --all tests/replays/x64 --extra -ignoreAsserts
```

Debug-Builds brauchen `-ignoreAsserts`. Der Vergleich deckt den gemeinsamen Abschnitt ab und muss die
ganze Referenz erreichen; ein vorzeitig endender Lauf ist ein Fehler.

Die erste Aufnahme `skirmish-usa-vs-gla-2026-09-12.rep` (x86, vor X-01, ohne Referenz-CRC, lief
nicht mehr synchron) ist am 2026-09-26 mit dem 32-Bit-Build entfallen.

## Testhilfen

`testhilfen/x64` und `testhilfen/zh/x64` enthalten je ein kurzes Skirmish (3.600 Logik-Frames), in dem
`testhilfen/ablauf-2ki.txt` jede Testhilfe, die die Spiellogik berührt, als Spielbefehl
`MSG_TESTHILFE` auslöst. Sie prüfen, dass Replays mit
Testhilfen synchron ablaufen. Nicht Teil von `ctest -L assets`; prüfen mit
`runner.py check --all tests/replays/testhilfen/x64` bzw. `.../zh/x64`.

## Neue KI (TODO 50)

`neueki/x64` (Generals) und `neueki/zh/x64` (Zero Hour) enthalten Partien, in denen die **neue KI**
(`AICommanderPlayer`) ein Bündnis führt: Tournament Lake, `N1,N1,K2,K2`, Seed 7,
27.000 Logik-Frames (15 Spielminuten; darin Gegnerwahl im Bündnis, Hilfe-Einsätze, Angriffe und die
Käufe des späten Spiels). Sie liegen getrennt von den Referenzen der klassischen KI:

- Die Referenzen der **klassischen** KI dürfen sich durch Arbeit an der neuen KI nie ändern.
- Die Referenzen der **neuen** KI zeigen jede Änderung an ihr, auch ungewollte. Eine *gewollte*
  Änderung der neuen KI nimmt sie im selben Commit bewusst neu auf (wie Nr. 49 für die klassische).

`ctest -L assets` prüft sie als `ReplayNeueKI` bzw. `ReplayNeueKIZH`. Von Hand:

```
python tools/determinism/runner.py check --exe build-x64/GeneralsMD/Code/Main/Release/GZHNeoZH.exe ^
    --all tests/replays/neueki/zh/x64
```

Neu aufnehmen (die Belegung geht wie `-skirmishAI` über `--ai`):

```
python tools/determinism/runner.py record --exe build-x64/GeneralsMD/Code/Main/Release/GZHNeoZH.exe ^
    --map "Tournament Lake" --ai "N1,N1,K2,K2" --seed 7 --frames 27000 ^
    --out tests/replays/neueki/zh/x64/nk-zh-tournament-lake-n1n1k2k2-seed7
```

Seit R-161 darf dabei ein anderes Spiel laufen (eigene Aufnahmedatei je Prozess).

**Neu aufgenommen 2026-10-10 (alle zwölf Sätze beider Spiele, Release, dieselben Parameter; Testhilfen mit `--extra -ignoreAsserts -testhilfenAblauf tests/replays/testhilfen/ablauf-2ki.txt`):** Spielstände sichern alle Zustände der Zustandsautomaten und der Wegfinder-CRC liest die Mauerstücke richtig (R-513, R-514), die klassische KI baut automatische Einträge und nur baubare Vorrang-Einträge (Nr. 98), die neue KI bekommt Persönlichkeiten, Fristen und Leuchtfeuer (KA-9 bis KA-16). Logik-Kennung `0x62BCB645`.

**Neu aufgenommen 2026-10-08 (Generals):** `e0-alpine-assault-2ai-seed1`, `e0-tournament-lake-3ai-seed2`, `e0-twilight-flame-7ai-seed3` und
`neueki/x64/nk-tournament-lake-n1n1k2k2-seed7` nach der bewussten Logikänderung S5b und S6 (Nachladen startet bei neuem Waffenbonus neu, Abwurfzone ruht bei
Deaktivierung, jetzt auch in Generals); Release, dieselben Parameter. Neu dazu: `e0-tournament-tundra-3ai-seed2`
(3 KI, Seed 2, 6000 Frames, für die Kamerafahrt `tundra_nacht`). Die Zero-Hour-Referenzen blieben gleich.
