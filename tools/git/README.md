# Git-Hilfen

| Skript | Zweck |
|---|---|
| `worktree_entfernen.ps1 -Pfad <worktree> [-Branch <name>]` | Worktree **sicher** entfernen: die Exe-Ordner enthalten Junctions auf die Asset-Verzeichnisse, `git worktree remove --force` und `rm -r` würden ihnen folgen und die Spieldaten löschen. Das Skript prüft Branch und offene Änderungen, verschiebt `render-out` ins Archiv des Hauptverzeichnisses, löst jede Junction einzeln per `cmd /c rmdir`, entfernt erst dann und vergleicht die Dateizahl der Asset-Verzeichnisse vorher/nachher. `-Reste` räumt zusätzlich nicht mehr registrierte Ordner unter `.claude\worktrees` auf. |
| `buildordner_entfernen.ps1 <ordner> ...` | Build-Ordner **sicher** löschen: löst zuerst jede Junction auf die Asset-Verzeichnisse einzeln, löscht nur, wenn keine übrig ist, und vergleicht die Dateizahl der Asset-Verzeichnisse vorher/nachher. Nur Ordner mit `CMakeCache.txt`. |

Aufruf aus dem Repository, PowerShell-Skripte mit `powershell -NoProfile -ExecutionPolicy Bypass -File ...`.
