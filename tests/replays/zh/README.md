# Referenzreplays Zero Hour (E-0.3)

Hier liegen die Zero-Hour-Referenzreplays (`*.rep`) mit ihrem CRC-Verlauf (`*.crc`), analog zu
`tests/replays/` für Generals und wie dort unter `x64/` (die x86-Referenzen sind am 2026-09-26 mit dem
32-Bit-Build entfallen; `tests/replays/README.md`). Aufgenommen mit `GZHNeoZH.exe`:

```
python tools/determinism/runner.py record --exe build-x64/GeneralsMD/Code/Main/Debug/GZHNeoZH.exe ^
    --map "Tournament Lake" --ai 3 --seed 2 --frames 18000 ^
    --out tests/replays/zh/x64/e0zh-tournament-lake-3ai-seed2 --extra -ignoreAsserts
```

**Neu aufgenommen am 2026-09-17** (x86 und x64) nach R-67: `fast_float_ceil`/`_floor`
in `Lib/BaseType.h` zählten bei ganzzahligem Argument eins zu viel, die Korrektur ändert die
Zellzuordnung im PartitionManager und damit den CRC-Verlauf ab Frame 0. Die Generals-Referenzen
in `tests/replays/x86` und `x64` sind davon nicht betroffen (dort war die Rechnung immer richtig).
Seitdem geändert: `x64/e0zh-tournament-lake-3ai-seed2` ab Frame 16860 nach R-70 (2026-09-19; vorher
rechneten Debug und Release dort verschieden). Die Angleichung Phase 5 hat die Zero-Hour-Logik nicht
verändert. **x86 am 2026-09-23 neu aufgenommen:** bei R-70 war nur `x64` erneuert worden; die alten
x86-Aufnahmen wichen in `e0zh-tournament-lake-3ai-seed2` ab Frame 17190 und in
`e0zh-fortress-avalanche-7ai-seed3` ab Frame 34860 ab (bis dahin CRC-gleich mit den neuen,
`e0zh-green-pastures-2ai-seed1` durchgehend gleich). Neu aufgenommen mit x86 Debug (Stand Phase 8
Schritt 4), im zweiten Lauf CRC-gleich.

Die drei Referenzen (erstmals 2026-09-15): `Green Pastures` (Zero-Hour-eigene Karte, 2 KI, Seed 1, 9.000
Frames), `Tournament Lake` (3 KI, Seed 2, 18.000), `Fortress Avalanche` (8er-Karte, 7 KI, Seed 3,
36.000). Die Generalsherausforderung aus dem E-0.3-Plan lässt sich mit `-skirmish` nicht aufnehmen
und fehlt weiterhin.

Geprüft wird alles auf einmal mit

```
python tools/determinism/runner.py check --exe <Pfad>/GZHNeoZH.exe --all tests/replays/zh/x64
```

bzw. über das ctest-Ziel `ReplayDeterminismZH` (Label `assets`), das nur existiert, wenn im Ordner der
Build-Architektur mindestens eine `.rep` liegt.

Die Generals-Replays aus `tests/replays/` gelten **nicht** für Zero Hour (andere Engine-Daten, andere
INI-Sätze).
