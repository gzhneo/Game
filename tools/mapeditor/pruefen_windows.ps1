<#
  Copyright (C) 2026 GZH Neo
  Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
  (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.

  Pruefung des neuen Karteneditors unter Windows,
  Ergebnis in EINER Berichtsdatei (Markdown) fuer die Cloud-Sitzung, die kein Windows hat.

  Ablauf (jeder Schritt laeuft weiter, auch wenn ein frueherer fehlschlaegt):
    1. Stand: Branch, Commit, ungesicherte Aenderungen, Werkzeuge
    2. Bau: MapEditorModel, MapTool, MapEditorTests in Debug und Release (MSVC /W4 /WX);
       fehlt der Build-Ordner, wird er mit dem Preset x64-debug angelegt
    3. ctest -R MapEditorTests (Debug und Release)
    4. Karten suchen: lose Karten in den Asset-Ordnern und im Benutzerordner beider Spiele, dazu alle
       Karten aus den .big-Archiven (nur gelesen, Kopien unter render-out)
    5. MapTool check ueber alle gefundenen Karten: lesen, schreiben, vergleichen (Release)
    6. MapTool info fuer je eine Karte pro Quelle (Stichprobe fuer den Bericht)

  Aufruf (im Repo-Ordner):
    tools\mapeditor\pruefen_windows.cmd
    powershell -ExecutionPolicy Bypass -File tools\mapeditor\pruefen_windows.ps1
    powershell -ExecutionPolicy Bypass -File tools\mapeditor\pruefen_windows.ps1 -OhneBau
    powershell -ExecutionPolicy Bypass -File tools\mapeditor\pruefen_windows.ps1 -Karten D:\meine\karten

  Startet kein Spiel und kein Fenster. Die EA-Installation und die Asset-Ordner werden nur gelesen.
  Der Build-Ordner wird mitbenutzt (Standard build-x64): nicht gleichzeitig mit einer anderen
  Sitzung im selben Ordner bauen.
#>
param(
	[string]$BuildDir = "build-x64",
	[switch]$OhneBau,                 # vorhandene Programme nehmen (Bericht vermerkt das)
	[string[]]$Karten = @(),          # weitere Ordner mit .map oder .big
	[string]$Generals = "",           # Asset-Ordner Generals (Vorgabe: aus CMakeCache.txt)
	[string]$ZeroHour = "",           # Asset-Ordner Zero Hour (Vorgabe: aus CMakeCache.txt)
	[string]$CMake = "",
	[string]$Python = "python",
	[string]$Bericht = ""
)

$ErrorActionPreference = "Continue"
# PowerShell variable names ignore case: $zeilen in a loop is the same variable as $Zeilen.
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $Root
$Start = Get-Date
$Stempel = $Start.ToString("yyyy-MM-dd_HHmmss")
$Out = Join-Path $Root "render-out\mapeditor\$Stempel"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
if ($Bericht -eq "") { $Bericht = Join-Path $Root "render-out\mapeditor\bericht_mapeditor_$Stempel.md" }
$Build = Join-Path $Root $BuildDir

$script:BerichtZeilen = New-Object System.Collections.Generic.List[string]
$script:Uebersicht = New-Object System.Collections.Generic.List[string]
$script:Fehler = 0

function Schreibe([string]$t) { $script:BerichtZeilen.Add($t) }
function Ergebnis([string]$schritt, [bool]$ok, [string]$kurz) {
	$z = if ($ok) { "OK" } else { "FEHLER" }
	if (-not $ok) { $script:Fehler++ }
	$script:Uebersicht.Add("| $schritt | $z | $kurz |")
	Write-Host ("[{0}] {1}: {2}" -f $z, $schritt, $kurz)
}
function Block([string]$titel, $inhalt, [int]$max = 80) {
	$liste = @($inhalt)
	Schreibe ""
	Schreibe "<details><summary>$titel</summary>"
	Schreibe ""
	Schreibe '```'
	if ($liste.Count -gt $max) {
		foreach ($z in $liste[0..($max - 1)]) { Schreibe ([string]$z) }
		Schreibe "... ($($liste.Count - $max) weitere Zeilen, vollstaendig in $Out)"
	} else {
		foreach ($z in $liste) { Schreibe ([string]$z) }
	}
	Schreibe '```'
	Schreibe "</details>"
}
function Lauf([string]$name, [string]$exe, [string[]]$argumente) {
	$log = Join-Path $Out "$name.log"
	& $exe @argumente *> $log
	return @{ rc = $LASTEXITCODE; log = $log }
}
function CacheWert([string]$name) {
	$cache = Join-Path $Build "CMakeCache.txt"
	if (-not (Test-Path $cache)) { return "" }
	$z = Select-String -Path $cache -Pattern "^$name`:[A-Z]+=(.*)$" | Select-Object -First 1
	if ($z) { return $z.Matches[0].Groups[1].Value.Trim() }
	return ""
}

# ----------------------------------------------------------------------------------------------
# Werkzeuge
if ($CMake -eq "") {
	$c = Get-Command cmake -ErrorAction SilentlyContinue
	if ($c) { $CMake = $c.Source }
	else { $CMake = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" }
}
$CTest = Join-Path (Split-Path $CMake) "ctest.exe"

# ----------------------------------------------------------------------------------------------
# 1. Stand
Schreibe "## Stand"
Schreibe ""
Schreibe "- Branch: ``$((git rev-parse --abbrev-ref HEAD) 2>$null)``"
Schreibe "- Commit: ``$((git log -1 --format='%h %s') 2>$null)``"
$offen = @(git status --porcelain 2>$null)
Schreibe "- Ungesicherte Aenderungen: $($offen.Count)"
Schreibe "- Build-Ordner: ``$BuildDir``"
Schreibe "- CMake: $CMake ($((& $CMake --version 2>$null | Select-Object -First 1)))"
Schreibe "- Python: $((& $Python --version 2>&1 | Select-Object -First 1))"
if ($offen.Count -gt 0) { Block "git status" $offen 40 }

# ----------------------------------------------------------------------------------------------
# 2. Bau
Schreibe ""
Schreibe "## Bau"
$bauOk = @{ Debug = $true; Release = $true }
if ($OhneBau) {
	Schreibe ""
	Schreibe "Bau uebersprungen (-OhneBau): getestet werden die vorhandenen Programme."
} else {
	if (-not (Test-Path (Join-Path $Build "CMakeCache.txt"))) {
		$r = Lauf "configure" $CMake @("--preset", "x64-debug", "-B", $Build)
		Ergebnis "Konfigurieren" ($r.rc -eq 0) "Preset x64-debug nach $BuildDir, Exit $($r.rc)"
		if ($r.rc -ne 0) { Block "Konfigurieren (Ende)" (Get-Content $r.log -Tail 40) }
	} else {
		# Neu konfigurieren, damit die neuen CMakeLists (MapEditor) sicher gelesen sind.
		$r = Lauf "configure" $CMake @("-S", $Root, "-B", $Build)
		Ergebnis "Konfigurieren" ($r.rc -eq 0) "$BuildDir, Exit $($r.rc)"
		if ($r.rc -ne 0) { Block "Konfigurieren (Ende)" (Get-Content $r.log -Tail 40) }
	}
	foreach ($k in @("Debug", "Release")) {
		$t0 = Get-Date
		$r = Lauf "build_$k" $CMake @("--build", $Build, "--config", $k, "--target", "MapTool", "MapEditorTests", "--", "/m", "/nologo", "/v:minimal")
		$bauOk[$k] = ($r.rc -eq 0)
		$warn = @(Select-String -Path $r.log -Pattern "warning C\d+|warning LNK\d+" | ForEach-Object { $_.Line.Trim() } | Sort-Object -Unique)
		$fehl = @(Select-String -Path $r.log -Pattern "error C\d+|error LNK\d+|fatal error|error MSB" | ForEach-Object { $_.Line.Trim() } | Sort-Object -Unique)
		Ergebnis "Bau $k" $bauOk[$k] ("Exit {0}, {1} Warnungen, {2} Fehler, {3:N0} s" -f $r.rc, $warn.Count, $fehl.Count, ((Get-Date) - $t0).TotalSeconds)
		if ($fehl.Count -gt 0) { Block "Fehler $k" $fehl 60 }
		if ($warn.Count -gt 0) { Block "Warnungen $k" $warn 60 }
		if ($r.rc -ne 0 -and $fehl.Count -eq 0) { Block "Bau $k (Ende)" (Get-Content $r.log -Tail 40) }
	}
}

# ----------------------------------------------------------------------------------------------
# 3. ctest
Schreibe ""
Schreibe "## MapEditorTests"
foreach ($k in @("Debug", "Release")) {
	if (-not $bauOk[$k]) { Ergebnis "ctest $k" $false "uebersprungen, Bau fehlgeschlagen"; continue }
	if (-not (Test-Path $CTest)) { Ergebnis "ctest $k" $false "ctest.exe nicht gefunden neben $CMake"; continue }
	$r = Lauf "ctest_$k" $CTest @("--test-dir", $Build, "-C", $k, "-R", "MapEditorTests", "-V")
	$zeilen = @(Get-Content $r.log)
	$summe = ($zeilen | Select-String -Pattern "checks, \d+ failures" | Select-Object -Last 1)
	$kurz = if ($summe) { $summe.Line.Trim() } else { "keine Summenzeile" }
	$ok = ($r.rc -eq 0) -and ($zeilen | Select-String -Pattern "tests passed" -Quiet)
	Ergebnis "ctest $k" $ok "Exit $($r.rc), $kurz"
	if (-not $ok) { Block "ctest $k" $zeilen 120 }
}

# ----------------------------------------------------------------------------------------------
# 4. Karten suchen
Schreibe ""
Schreibe "## Karten"
Schreibe ""
if ($Generals -eq "") { $Generals = CacheWert "GENERALS_ASSETS_DIR" }
if ($ZeroHour -eq "") { $ZeroHour = CacheWert "GENERALSZH_ASSETS_DIR" }
$dokumente = [Environment]::GetFolderPath("MyDocuments")
$quellen = New-Object System.Collections.Generic.List[object]
function Quelle([string]$name, [string]$pfad) {
	if ($pfad -ne "" -and (Test-Path $pfad)) { $script:quellen.Add(@{ name = $name; pfad = (Resolve-Path $pfad).Path }) }
	Schreibe ("- {0}: ``{1}`` {2}" -f $name, $pfad, $(if ($pfad -ne "" -and (Test-Path $pfad)) { "" } else { "(nicht vorhanden)" }))
}
Quelle "Generals-Assets" $Generals
Quelle "ZeroHour-Assets" $ZeroHour
if ($dokumente -ne "") {
	Quelle "Generals-Benutzerkarten" (Join-Path $dokumente "GZH Neo\Generals\Maps")
	Quelle "ZeroHour-Benutzerkarten" (Join-Path $dokumente "GZH Neo\Zero Hour\Maps")
	# maps of EA's original game, read only
	Quelle "Generals-Benutzerkarten-EA" (Join-Path $dokumente "Command and Conquer Generals Data\Maps")
	Quelle "ZeroHour-Benutzerkarten-EA" (Join-Path $dokumente "Command and Conquer Generals Zero Hour Data\Maps")
}
$i = 0
foreach ($k in $Karten) { $i++; Quelle "Zusatz$i" $k }

$kartenOrdner = Join-Path $Out "karten"
$pruefListe = New-Object System.Collections.Generic.List[object]
foreach ($q in $quellen) {
	# Lose Karten direkt pruefen (nur gelesen).
	$lose = @(Get-ChildItem -Path $q.pfad -Recurse -Filter *.map -File -ErrorAction SilentlyContinue)
	if ($lose.Count -gt 0) { $pruefListe.Add(@{ name = "$($q.name) (lose)"; pfad = $q.pfad; anzahl = $lose.Count }) }
	# Karten aus .big-Archiven in eine Kopie.
	$ziel = Join-Path $kartenOrdner $q.name
	$r = Lauf "big_$($q.name)" $Python @((Join-Path $PSScriptRoot "big_maps.py"), $ziel, $q.pfad)
	$n = @(Get-ChildItem -Path $ziel -Recurse -Filter *.map -File -ErrorAction SilentlyContinue).Count
	if ($n -gt 0) { $pruefListe.Add(@{ name = "$($q.name) (.big)"; pfad = $ziel; anzahl = $n }) }
	if ($r.rc -ne 0) { Block "big_maps $($q.name)" (Get-Content $r.log -Tail 20) }
}
Schreibe ""
Schreibe "| Quelle | Karten |"
Schreibe "|---|---|"
foreach ($p in $pruefListe) { Schreibe "| $($p.name) | $($p.anzahl) |" }
# Sum by hand: Measure-Object -Property does not read hashtable keys in Windows PowerShell 5.1.
$gesamt = 0
foreach ($p in $pruefListe) { $gesamt += [int]$p.anzahl }
Ergebnis "Karten gefunden" ($gesamt -gt 0) "$gesamt Karten aus $($pruefListe.Count) Quellen"

# ----------------------------------------------------------------------------------------------
# 5. MapTool check
Schreibe ""
Schreibe "## Rundlauf (MapTool check)"
Schreibe ""
Schreibe "OK = Datei gleich; DATA = Chunk-Daten gleich, nur die gepackte Datei anders; UPGR = aeltere"
Schreibe "Abschnittsversionen in der neuesten geschrieben, Inhalt nach dem Zuruecklesen gleich; DIFF = Chunk-Daten"
Schreibe "anders; FAIL = nicht lesbar oder nicht schreibbar."
$konfig = if ($bauOk["Release"]) { "Release" } else { "Debug" }
$mapTool = Get-ChildItem -Path $Build -Recurse -Filter MapTool.exe -File -ErrorAction SilentlyContinue |
	Where-Object { $_.Directory.Name -eq $konfig } | Select-Object -First 1
if (-not $mapTool) {
	Ergebnis "MapTool check" $false "MapTool.exe ($konfig) nicht gefunden"
} else {
	Schreibe ""
	Schreibe "Programm: ``$($mapTool.FullName)``"
	Schreibe ""
	Schreibe "| Quelle | Karten | OK | DATA | UPGR | DIFF | FAIL | Warnungen | Zeit |"
	Schreibe "|---|---|---|---|---|---|---|---|---|"
	$status = @("OK", "DATA", "UPGR", "DIFF", "FAIL")
	$auffaellig = New-Object System.Collections.Generic.List[string]
	$hochstufungen = @{}
	$summen = @{ OK = 0; DATA = 0; UPGR = 0; DIFF = 0; FAIL = 0 }
	$j = 0
	foreach ($p in $pruefListe) {
		$j++
		$t0 = Get-Date
		$r = Lauf "check_$j" $mapTool.FullName @("check", $p.pfad)
		$ausgabe = @(Get-Content $r.log)
		$z = @{ OK = 0; DATA = 0; UPGR = 0; DIFF = 0; FAIL = 0 }
		$warn = 0
		$aktuell = ""
		foreach ($l in $ausgabe) {
			$kopfzeile = $false
			foreach ($st in $status) {
				if ($l.StartsWith($st + " ")) { $z[$st]++; $summen[$st]++; $aktuell = $st; $kopfzeile = $true }
			}
			if ($kopfzeile) {
				if ($aktuell -ne "OK" -and $aktuell -ne "UPGR") { $auffaellig.Add("[$($p.name)] $l") }
				continue
			}
			if ($l -match "^\s+warning:") { $warn++ }
			if ($l -match "^\s+upgrade: (.*?)( \(\d+x\))?$") {
				$art = $Matches[1]
				if ($hochstufungen.ContainsKey($art)) { $hochstufungen[$art]++ } else { $hochstufungen[$art] = 1 }
			}
			# Details only for maps that are not OK or upgraded; warnings always.
			if ($l -match "^\s+" -and (($aktuell -ne "OK" -and $aktuell -ne "UPGR") -or $l -match "^\s+warning:")) {
				$auffaellig.Add("    $($l.Trim())")
			}
		}
		Schreibe ("| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} | {8:N0} s |" -f $p.name, $p.anzahl, $z.OK, $z.DATA, $z.UPGR, $z.DIFF, $z.FAIL, $warn, ((Get-Date) - $t0).TotalSeconds)
	}
	$ok = ($summen.DIFF -eq 0) -and ($summen.FAIL -eq 0) -and ($gesamt -gt 0)
	Ergebnis "MapTool check" $ok ("{0} OK, {1} DATA, {2} UPGR, {3} DIFF, {4} FAIL" -f $summen.OK, $summen.DATA, $summen.UPGR, $summen.DIFF, $summen.FAIL)
	if ($hochstufungen.Count -gt 0) {
		Schreibe ""
		Schreibe "Hochstufungen (Karten je Art; UPGR heisst: nur solche Abschnitte weichen ab, Inhalt nach dem Zuruecklesen gleich):"
		Schreibe ""
		Schreibe "| Art | Karten |"
		Schreibe "|---|---|"
		foreach ($art in ($hochstufungen.Keys | Sort-Object)) { Schreibe "| $art | $($hochstufungen[$art]) |" }
	}
	if ($auffaellig.Count -gt 0) { Block "DATA, DIFF, FAIL mit Einzelheiten; Warnungen" $auffaellig 400 }

	# ------------------------------------------------------------------------------------------
	# 6. Stichprobe
	Schreibe ""
	Schreibe "## Stichprobe (MapTool info)"
	foreach ($p in $pruefListe) {
		$karte = Get-ChildItem -Path $p.pfad -Recurse -Filter *.map -File -ErrorAction SilentlyContinue | Select-Object -First 1
		if (-not $karte) { continue }
		$r = Lauf "info_$($p.name -replace '[^A-Za-z0-9]', '_')" $mapTool.FullName @("info", $karte.FullName)
		Block "$($p.name): $($karte.Name)" (Get-Content $r.log) 60
	}
}

# ----------------------------------------------------------------------------------------------
# Bericht
$dauer = "{0:N0} s" -f ((Get-Date) - $Start).TotalSeconds
$kopf = New-Object System.Collections.Generic.List[string]
$kopf.Add("# Pruefbericht Karteneditor (Windows) $Stempel")
$kopf.Add("")
$kopf.Add("Ergebnis: **$(if ($script:Fehler -eq 0) { 'bestanden' } else { "$($script:Fehler) Schritt(e) mit Fehler" })**, Dauer $dauer.")
$kopf.Add("Protokolle: ``$Out``")
$kopf.Add("")
$kopf.Add("| Schritt | Ergebnis | Kurz |")
$kopf.Add("|---|---|---|")
foreach ($z in $script:Uebersicht) { $kopf.Add($z) }
$kopf.Add("")
$alles = $kopf + $script:BerichtZeilen
[System.IO.File]::WriteAllLines($Bericht, [string[]]$alles, (New-Object System.Text.UTF8Encoding($false)))
Write-Host ""
Write-Host "Bericht: $Bericht"
Write-Host "Bitte den Inhalt dieser Datei in die Sitzung kopieren."
if ($script:Fehler -eq 0) { exit 0 } else { exit 1 }
