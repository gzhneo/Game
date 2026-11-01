# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Worktree sicher entfernen (2026-09-26, aus dem Sitzungs-Scratchpad uebernommen).
#
# Die Exe-Ordner der Build-Verzeichnisse enthalten Junctions auf die Asset-Verzeichnisse. "git worktree remove
# --force" und "rm -r" folgen ihnen und loeschen die Spieldaten. Ablauf:
#   1. Branch pruefen (-Branch) und offene Aenderungen ausschliessen
#   2. render-out des Worktrees nach render-out\archiv\<Branch> im Hauptverzeichnis verschieben
#   3. alle Junctions einzeln per "cmd /c rmdir" loesen (folgt ihnen nicht); bleibt einer, Abbruch
#   4. git worktree unlock / remove --force
#   5. mit -Reste: Ordner unter .claude\worktrees, die kein registrierter Worktree mehr sind, ebenso behandeln
# Vorher und nachher wird die Dateizahl der Asset-Verzeichnisse verglichen.
#
# Aufruf:  powershell -File tools\git\worktree_entfernen.ps1 -Pfad .claude\worktrees\agent-xyz -Branch mein-branch
#          powershell -File tools\git\worktree_entfernen.ps1 -Reste
param(
    [string]$Pfad,
    [string]$Branch,
    [switch]$Reste,
    [string[]]$Assets = @()
)
. (Join-Path $PSScriptRoot '..\pfade.ps1')
if (-not $Assets) { $Assets = @(@($env:GZH_ASSETS_GENERALS, $env:GZH_ASSETS_ZH) | Where-Object { $_ }) }
$ErrorActionPreference = 'Continue'
if (-not $Assets) { 'WARNUNG: keine Asset-Ordner zum Vergleich (-Assets oder GZH_ASSETS_GENERALS/GZH_ASSETS_ZH)' }
$repo = (git rev-parse --path-format=absolute --git-common-dir).Trim() -replace '[\\/]\.git$', ''
$wurzel = Join-Path $repo '.claude\worktrees'
$archiv = Join-Path $repo 'render-out\archiv'

function Zaehle-Assets {
    $e = @{}
    foreach ($a in $Assets) { $e[$a] = (Get-ChildItem $a -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object).Count }
    return $e
}
function Loese-Links($p) {
    $links = @(Get-ChildItem -Path $p -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue |
               Where-Object { $_.PSIsContainer } | Sort-Object { $_.FullName.Length } -Descending)
    foreach ($l in $links) { cmd /c rmdir "$($l.FullName)" | Out-Null }
    $rest = @(Get-ChildItem -Path $p -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue)
    return @($links.Count, $rest)
}

$vorher = Zaehle-Assets
foreach ($a in $Assets) { "Assets vorher {0}: {1} Dateien" -f $a, $vorher[$a] }

if ($Pfad) {
    $p = (Resolve-Path $Pfad -ErrorAction SilentlyContinue).Path
    if (-not $p) { "{0}: fehlt" -f $Pfad }
    else {
        $ist = (git -C $p rev-parse --abbrev-ref HEAD).Trim()
        if ($Branch -and $ist -ne $Branch) { "{0}: Branch {1} statt {2}, abgebrochen" -f $p, $ist, $Branch }
        elseif ((git -C $p status --porcelain --untracked-files=no | Measure-Object).Count -ne 0) { "{0}: offene Aenderungen, abgebrochen" -f $p }
        else {
            $ro = Join-Path $p 'render-out'
            if ((Test-Path $ro) -and (Get-ChildItem $ro -Force | Measure-Object).Count -gt 0) {
                # Branch names like abnahme/nachtrag: one folder, Move-Item does not create parent folders
                $ziel = Join-Path $archiv ($ist -replace '[\\/]', '-')
                if (Test-Path $ziel) { $ziel = $ziel + '-' + (Split-Path $p -Leaf) }
                New-Item -ItemType Directory -Force $archiv | Out-Null
                Move-Item -LiteralPath $ro -Destination $ziel
                "render-out nach {0}" -f $ziel
            }
            if ((Test-Path $ro) -and (Get-ChildItem $ro -Force | Measure-Object).Count -gt 0) { "{0}: render-out nicht verschoben, Worktree NICHT entfernt" -f $p }
            elseif (($r = Loese-Links $p)[1].Count -ne 0) { "{0}: {1} Links uebrig, NICHT entfernt" -f $p, $r[1].Count; $r[1] | ForEach-Object { '    ' + $_.FullName } }
            else {
                try { git -C $repo worktree unlock $p 2>$null | Out-Null } catch { }
                git -C $repo worktree remove --force $p
                "{0} ({1}): {2} Links geloest, Worktree entfernt" -f $p, $ist, $r[0]
            }
        }
    }
}

if ($Reste -and (Test-Path $wurzel)) {
    $registriert = @(git -C $repo worktree list --porcelain | Where-Object { $_ -like 'worktree *' } |
                     ForEach-Object { (Resolve-Path ($_.Substring(9))).Path.ToLower() })
    foreach ($d in Get-ChildItem $wurzel -Directory -Force) {
        if ($registriert -contains $d.FullName.ToLower()) { continue }
        $r = Loese-Links $d.FullName
        if ($r[1].Count -ne 0) { "{0}: {1} Links uebrig, NICHT geloescht" -f $d.Name, $r[1].Count; continue }
        $dateien = (Get-ChildItem $d.FullName -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object).Count
        Remove-Item -LiteralPath $d.FullName -Recurse -Force -ErrorAction SilentlyContinue
        "{0}: Rest mit {1} Dateien, {2} Links geloest -> {3}" -f $d.Name, $dateien, $r[0], $(if (Test-Path $d.FullName) { 'noch da (gesperrt)' } else { 'geloescht' })
    }
    git -C $repo worktree prune
}

$nachher = Zaehle-Assets
foreach ($a in $Assets) { "Assets nachher {0}: {1} Dateien ({2})" -f $a, $nachher[$a], $(if ($nachher[$a] -eq $vorher[$a]) { 'unveraendert' } else { 'ABWEICHUNG' }) }
