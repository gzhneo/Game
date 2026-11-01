# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Build-Ordner sicher loeschen: Die Exe-Ordner enthalten Junctions auf die Asset-Verzeichnisse;
# "rm -r" oder Remove-Item -Recurse folgen ihnen und loeschen die Spieldaten.
# Jede Junction wird einzeln per "cmd /c rmdir" geloest; bleibt eine uebrig, wird der Ordner nicht geloescht.
# Vorher/nachher Dateizahl der Asset-Verzeichnisse.
#
# Aufruf:  powershell -File tools\git\buildordner_entfernen.ps1 build build-asan
param(
    [Parameter(Mandatory = $true, Position = 0, ValueFromRemainingArguments = $true)][string[]]$Ordner,
    # Nur benannt (-Assets), nie positionsweise: sonst landet der erste Ordnername hier (2026-09-26 passiert).
    [Parameter(DontShow = $true)][string[]]$Assets = @()
)
. (Join-Path $PSScriptRoot '..\pfade.ps1')
if (-not $Assets) { $Assets = @(@($env:GZH_ASSETS_GENERALS, $env:GZH_ASSETS_ZH) | Where-Object { $_ }) }
$ErrorActionPreference = 'Continue'
if (-not $Assets) { 'WARNUNG: keine Asset-Ordner zum Vergleich (-Assets oder GZH_ASSETS_GENERALS/GZH_ASSETS_ZH)' }

function Zaehle-Assets {
    $e = @{}
    foreach ($a in $Assets) { $e[$a] = (Get-ChildItem $a -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object).Count }
    return $e
}
$vorher = Zaehle-Assets
foreach ($a in $Assets) { "Assets vorher {0}: {1} Dateien" -f $a, $vorher[$a] }

foreach ($o in $Ordner) {
    $p = (Resolve-Path $o -ErrorAction SilentlyContinue).Path
    if (-not $p) { "{0}: fehlt" -f $o; continue }
    if (-not (Test-Path (Join-Path $p 'CMakeCache.txt'))) { "{0}: kein Build-Ordner (CMakeCache.txt fehlt), uebersprungen" -f $o; continue }
    $links = @(Get-ChildItem -Path $p -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue |
               Where-Object { $_.PSIsContainer } | Sort-Object { $_.FullName.Length } -Descending)
    foreach ($l in $links) { cmd /c rmdir "$($l.FullName)" | Out-Null }
    $rest = @(Get-ChildItem -Path $p -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue)
    if ($rest.Count -ne 0) { "{0}: {1} Links uebrig, NICHT geloescht" -f $o, $rest.Count; $rest | ForEach-Object { '    ' + $_.FullName }; continue }
    cmd /c rmdir /s /q "$p"
    "{0}: {1} Links geloest -> {2}" -f $o, $links.Count, $(if (Test-Path $p) { 'noch da (gesperrt?)' } else { 'geloescht' })
}

$nachher = Zaehle-Assets
foreach ($a in $Assets) { "Assets nachher {0}: {1} Dateien ({2})" -f $a, $nachher[$a], $(if ($nachher[$a] -eq $vorher[$a]) { 'unveraendert' } else { 'ABWEICHUNG' }) }
