# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Build-Ordner sicher loeschen: Die Exe-Ordner enthalten Junctions auf die Asset-Verzeichnisse;
# "rm -r" oder Remove-Item -Recurse folgen ihnen und loeschen die Spieldaten.
# Jede Junction wird einzeln per "cmd /c rmdir" geloest; bleibt eine uebrig, wird der Ordner nicht geloescht.
# Vorher/nachher Dateizahl der Asset-Verzeichnisse; fehlt ein Asset-Verzeichnis oder scheitert das Zaehlen, wird nichts
# geloescht (ohne Vergleich waere ein Verlust der Spieldaten nicht zu sehen).
#
# Aufruf:  powershell -File tools\git\buildordner_entfernen.ps1 build build-asan
#          powershell -File tools\git\buildordner_entfernen.ps1 build -Assets 'D:\a,D:\b'   (unter -File als ein Text)
param(
    [Parameter(Mandatory = $true, Position = 0, ValueFromRemainingArguments = $true)][string[]]$Ordner,
    # Nur benannt (-Assets), nie positionsweise: sonst landet der erste Ordnername hier (2026-09-26 passiert).
    [Parameter(DontShow = $true)][string[]]$Assets = @()
)
. (Join-Path $PSScriptRoot '..\pfade.ps1')
if (-not $Assets) { $Assets = @(@($env:GZH_ASSETS_GENERALS, $env:GZH_ASSETS_ZH) | Where-Object { $_ }) }
# Unter "powershell -File" kommt eine Liste als ein einziger Text an: an Kommas trennen.
$Assets = @($Assets | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim().Trim("'", '"') } | Where-Object { $_ })
$ErrorActionPreference = 'Continue'
if (-not $Assets) { 'WARNUNG: keine Asset-Ordner zum Vergleich (-Assets oder GZH_ASSETS_GENERALS/GZH_ASSETS_ZH)' }

# Dateizahl je Asset-Verzeichnis; $null, wenn das Verzeichnis fehlt oder nicht vollstaendig lesbar ist.
function Zaehle-Assets {
    $e = @{}
    foreach ($a in $Assets) {
        if (-not (Test-Path -LiteralPath $a -PathType Container)) { $e[$a] = $null; continue }
        $fehler = $null
        $anzahl = (Get-ChildItem -LiteralPath $a -Recurse -File -Force -ErrorAction SilentlyContinue -ErrorVariable fehler | Measure-Object).Count
        $e[$a] = if ($fehler) { $null } else { $anzahl }
    }
    return $e
}
$vorher = Zaehle-Assets
foreach ($a in $Assets) { "Assets vorher {0}: {1} Dateien" -f $a, $vorher[$a] }
$unzaehlbar = @($Assets | Where-Object { $null -eq $vorher[$_] })
if ($unzaehlbar) {
    "FEHLER: Asset-Verzeichnis fehlt oder laesst sich nicht zaehlen, nichts geloescht: $($unzaehlbar -join ', ')"
    exit 1
}

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
$abweichung = $false
foreach ($a in $Assets) {
    $gleich = $null -ne $nachher[$a] -and $nachher[$a] -eq $vorher[$a]
    if (-not $gleich) { $abweichung = $true }
    "Assets nachher {0}: {1} Dateien ({2})" -f $a, $nachher[$a], $(if ($gleich) { 'unveraendert' } else { 'ABWEICHUNG' })
}
if ($abweichung) { exit 2 }
