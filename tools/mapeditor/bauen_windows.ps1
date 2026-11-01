<#
  Copyright (C) 2026 GZH Neo
  Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
  (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.

  Baut den neuen Karteneditor (MapEditor.exe, Qt 6) unter Windows.

  Ablauf:
    1. Konfigurieren mit dem Preset x64-editor (Build-Ordner build-x64-editor). Dabei baut vcpkg
       Qt 6 aus dem Manifest-Feature "editor". Beim ersten Mal dauert das lange (Qt Debug und
       Release); danach kommt Qt aus dem vcpkg-Binaercache (%LOCALAPPDATA%\vcpkg\archives) und
       das Konfigurieren dauert nur noch Sekunden.
    2. Bauen des Ziels MapEditor (Release, mit -Config Debug auch Debug).
    3. Pruefen, dass die Qt-Plugins neben der Exe liegen (platforms\qwindows.dll usw.); fehlen
       sie, werden sie aus vcpkg kopiert.
    4. Selbsttest: MapEditor.exe --selftest zeichnet alle Seiten in Hell/Dunkel und Deutsch/
       Englisch als PNG nach render-out\mapeditor\selbsttest_<Zeit> (ohne sichtbares Fenster,
       ohne die Einstellungen des Nutzers anzufassen). Gibt es die Zero-Hour-Daten des Builds
       (GENERALSZH_ASSETS_DIR), startet er dazu die Engine und speichert die 3D-Ansicht einer
       EA-Karte (map3d_*.png); die Karte kommt aus den Archiven (Kopie unter render-out).

  Parallelitaet: vcpkg (Qt), MSBuild und der Compiler nutzen alle logischen Kerne
  ([Environment]::ProcessorCount, bei 16 Kernen mit SMT also 32). Qt braucht beim Uebersetzen
  viel Speicher, grob 1 bis 1,5 GB je Job; bei knappem RAM mit -Jobs begrenzen.

  Aufruf (im Repo-Ordner):
    tools\mapeditor\bauen_windows.cmd
    tools\mapeditor\bauen_windows.cmd -Config Debug
    tools\mapeditor\bauen_windows.cmd -Jobs 16 -Starten
    powershell -ExecutionPolicy Bypass -File tools\mapeditor\bauen_windows.ps1 -OhneSelbsttest

  Schalter:
    -Config Release|Debug   Konfiguration (Vorgabe Release)
    -Jobs N                 parallele Jobs (Vorgabe: alle logischen Kerne)
    -Neu                    neu konfigurieren (cmake --fresh), etwa nach einem abgebrochenen Lauf
    -OhneSelbsttest         Selbsttest auslassen
    -Ohne3D                 Selbsttest ohne Engine und 3D-Ansicht
    -Karte <Pfad>           diese Karte fuer den 3D-Test (Vorgabe: Tournament Desert aus den Archiven)
    -Starten                den Editor nach dem Bau starten
    -CMake <Pfad>           cmake.exe (Vorgabe: aus PATH, sonst VS 2026 Build Tools)
#>
param(
	[ValidateSet("Release", "Debug")]
	[string]$Config = "Release",
	[int]$Jobs = 0,
	[switch]$Neu,
	[switch]$OhneSelbsttest,
	[switch]$Ohne3D,
	[string]$Karte = "",
	[string]$Python = "python",
	[switch]$Starten,
	[string]$CMake = ""
)

$ErrorActionPreference = "Continue"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $Root
$Start = Get-Date
$Stempel = $Start.ToString("yyyy-MM-dd_HHmmss")
$Build = Join-Path $Root "build-x64-editor"
$LogDir = Join-Path $Root "render-out\mapeditor"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$script:Fehler = 0

function Titel([string]$t) { Write-Host ""; Write-Host "== $t" -ForegroundColor Cyan }
function Gut([string]$t) { Write-Host "[OK] $t" -ForegroundColor Green }
function Schlecht([string]$t) { Write-Host "[FEHLER] $t" -ForegroundColor Red; $script:Fehler++ }
function Hinweis([string]$t) { Write-Host "  $t" -ForegroundColor DarkGray }
function Dauer([datetime]$t0) { $d = (Get-Date) - $t0; return ("{0:00}:{1:00}:{2:00}" -f [int][math]::Floor($d.TotalHours), $d.Minutes, $d.Seconds) }
function CacheWert([string]$name) {
	$cache = Join-Path $Build "CMakeCache.txt"
	if (-not (Test-Path $cache)) { return "" }
	$z = Select-String -Path $cache -Pattern "^$name`:[A-Z]+=(.*)$" | Select-Object -First 1
	if ($z) { return $z.Matches[0].Groups[1].Value.Trim() }
	return ""
}

# ----------------------------------------------------------------------------------------------
# Werkzeuge und Parallelitaet
if ($CMake -eq "") {
	$c = Get-Command cmake -ErrorAction SilentlyContinue
	if ($c) { $CMake = $c.Source }
	else { $CMake = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" }
}
if (-not (Test-Path $CMake)) {
	Schlecht "cmake nicht gefunden ($CMake); mit -CMake angeben"
	exit 1
}

$Kerne = [Environment]::ProcessorCount
if ($Jobs -le 0) { $Jobs = $Kerne }
# vcpkg builds each port with this many jobs (Qt uses Ninja inside vcpkg).
$env:VCPKG_MAX_CONCURRENCY = "$Jobs"
# MSBuild: one shared pool of compiler processes across projects instead of /m times /MP.
$env:UseMultiToolTask = "true"
$env:EnforceProcessCountAcrossBuilds = "true"

# CMake takes RC, CC and CXX from the environment as compiler paths; a leftover value from another
# script (such as RC=0 from a batch file without setlocal) breaks the configure step.
foreach ($name in @("RC", "CC", "CXX")) {
	$wert = [Environment]::GetEnvironmentVariable($name)
	if ($wert -and -not (Test-Path $wert) -and -not (Get-Command $wert -ErrorAction SilentlyContinue)) {
		Write-Host "  Umgebungsvariable $name=$wert ist kein Programm und wird fuer diesen Lauf entfernt." -ForegroundColor Yellow
		Remove-Item "Env:$name"
	}
}

$ramGB = 0
try { $ramGB = [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB) } catch { }

Titel "Karteneditor bauen"
Hinweis "Repo:       $Root"
Hinweis "Build:      $Build ($Config)"
Hinweis "CMake:      $CMake"
Hinweis "Jobs:       $Jobs (logische Kerne: $Kerne, RAM: $ramGB GB)"
if ($ramGB -gt 0 -and $ramGB -lt $Jobs) {
	Write-Host "  Hinweis: wenig RAM fuer $Jobs Jobs; bricht der Qt-Bau mit Speichermangel ab, mit -Jobs $([math]::Max(4, $ramGB / 2)) neu starten." -ForegroundColor Yellow
}

# ----------------------------------------------------------------------------------------------
# 1. Konfigurieren (vcpkg baut dabei Qt)
Titel "1. Konfigurieren (Preset x64-editor, vcpkg-Feature editor)"
$ersterLauf = -not (Test-Path (Join-Path $Build "vcpkg_installed\x64-windows\share\Qt6\Qt6Config.cmake"))
if ($ersterLauf) {
	Write-Host "  Qt ist noch nicht installiert: vcpkg baut es jetzt. Mit $Jobs Jobs grob 20 bis 60 Minuten;" -ForegroundColor Yellow
	Write-Host "  steht Qt schon im vcpkg-Binaercache, geht es in wenigen Minuten." -ForegroundColor Yellow
}
$t0 = Get-Date
$confLog = Join-Path $LogDir "bauen_konfigurieren_$Stempel.log"
$confArgs = @("--preset", "x64-editor")
if ($Neu) { $confArgs += "--fresh" }
# ForEach-Object turns stderr records into plain lines (no NativeCommandError decoration).
& $CMake @confArgs 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $confLog
$rc = $LASTEXITCODE
if ($rc -ne 0) {
	Schlecht "Konfigurieren, Exit $rc, $(Dauer $t0) (Log: $confLog)"
	Hinweis "vcpkg-Fehler stehen meist in build-x64-editor\vcpkg-manifest-install.log und den dort genannten Port-Logs."
	exit 1
}
Gut "Konfigurieren, $(Dauer $t0)"

# ----------------------------------------------------------------------------------------------
# 2. Bauen
Titel "2. Bauen (MapEditor, $Config)"
$t0 = Get-Date
$bauLog = Join-Path $LogDir "bauen_$($Config.ToLower())_$Stempel.log"
& $CMake --build $Build --config $Config --target MapEditor --parallel $Jobs -- /nologo /v:minimal 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $bauLog
$rc = $LASTEXITCODE
if ($rc -ne 0) {
	Schlecht "Bau $Config, Exit $rc, $(Dauer $t0) (Log: $bauLog)"
	exit 1
}
$warn = @(Select-String -Path $bauLog -Pattern "warning C\d+|warning LNK\d+" | ForEach-Object { $_.Line.Trim() } | Sort-Object -Unique)
Gut "Bau $Config, $($warn.Count) Warnungen, $(Dauer $t0)"

$exe = Get-ChildItem -Path (Join-Path $Build "Code\Tools\MapEditor") -Recurse -Filter MapEditor.exe -ErrorAction SilentlyContinue |
	Where-Object { $_.Directory.Name -eq $Config } | Select-Object -First 1
if (-not $exe) {
	Schlecht "MapEditor.exe nicht gefunden unter build-x64-editor\Code\Tools\MapEditor\$Config"
	exit 1
}
$ExeDir = $exe.Directory.FullName

# ----------------------------------------------------------------------------------------------
# 3. Qt-Laufzeit neben der Exe
Titel "3. Qt-Laufzeit"
$installed = CacheWert "VCPKG_INSTALLED_DIR"
if ($installed -eq "") { $installed = Join-Path $Build "vcpkg_installed" }
$installed = Join-Path ($installed -replace "/", "\") "x64-windows"
$quelle = if ($Config -eq "Debug") { Join-Path $installed "debug" } else { $installed }

$dllFehlt = -not (Get-ChildItem -Path $ExeDir -Filter "Qt6Core*.dll" -ErrorAction SilentlyContinue)
if ($dllFehlt) {
	# Normally vcpkg's app-local step copies the DLLs; this covers a build without it.
	Copy-Item -Path (Join-Path $quelle "bin\*.dll") -Destination $ExeDir -Force -ErrorAction SilentlyContinue
	Hinweis "DLLs aus $quelle\bin kopiert"
}
$pluginsFehlen = -not (Get-ChildItem -Path (Join-Path $ExeDir "platforms") -Filter "qwindows*.dll" -ErrorAction SilentlyContinue)
if ($pluginsFehlen) {
	$pluginQuelle = Join-Path $quelle "Qt6\plugins"
	foreach ($ordner in @("platforms", "styles", "imageformats", "iconengines")) {
		$von = Join-Path $pluginQuelle $ordner
		if (Test-Path $von) {
			$nach = Join-Path $ExeDir $ordner
			New-Item -ItemType Directory -Force -Path $nach | Out-Null
			Copy-Item -Path (Join-Path $von "*.dll") -Destination $nach -Force
		}
	}
	Hinweis "Qt-Plugins aus $pluginQuelle kopiert"
}
if (-not (Test-Path (Join-Path $ExeDir "MapEditorEngine.dll"))) { Schlecht "MapEditorEngine.dll fehlt in $ExeDir (3D-Ansicht)" }
if (-not (Test-Path (Join-Path $ExeDir "Shaders\sm4"))) { Schlecht "Shaders\sm4 fehlt in $ExeDir (3D-Ansicht)" }
if (-not (Get-ChildItem -Path $ExeDir -Filter "Qt6Core*.dll" -ErrorAction SilentlyContinue)) { Schlecht "Qt6Core.dll fehlt in $ExeDir" }
elseif (-not (Get-ChildItem -Path (Join-Path $ExeDir "platforms") -Filter "qwindows*.dll" -ErrorAction SilentlyContinue)) { Schlecht "platforms\qwindows.dll fehlt in $ExeDir" }
else { Gut "Qt-DLLs und Plugins liegen neben der Exe" }

# ----------------------------------------------------------------------------------------------
# 4. Selbsttest
if (-not $OhneSelbsttest) {
	Titel "4. Selbsttest"
	$shots = Join-Path $LogDir "selbsttest_$Stempel"
	$t0 = Get-Date
	$argumente = @("--selftest", "`"$shots`"")
	$daten = (CacheWert "GENERALSZH_ASSETS_DIR") -replace "/", "\"
	if (-not $Ohne3D -and $daten -ne "" -and (Test-Path $daten)) {
		if ($Karte -eq "") {
			# A real EA map from the archives, copied once (the archives are only read).
			$kopie = Join-Path $LogDir "karten_zh"
			if (-not (Get-ChildItem -Path $kopie -Recurse -Filter *.map -File -ErrorAction SilentlyContinue | Select-Object -First 1)) {
				& $Python (Join-Path $PSScriptRoot "big_maps.py") $kopie $daten *> (Join-Path $LogDir "karten_zh.log")
			}
			$alle = @(Get-ChildItem -Path $kopie -Recurse -Filter *.map -File -ErrorAction SilentlyContinue)
			$wahl = $alle | Where-Object { $_.Name -like "*Tournament Desert*" } | Select-Object -First 1
			if (-not $wahl) { $wahl = $alle | Select-Object -First 1 }
			if ($wahl) { $Karte = $wahl.FullName }
		}
		if ($Karte -ne "") { $argumente += "`"$Karte`"" }
		$argumente += @("--data", "`"$daten`"")
		Hinweis "3D-Test: Daten $daten"
		if ($Karte -ne "") { Hinweis "3D-Test: Karte $Karte" } else { Hinweis "3D-Test: Demokarte (keine EA-Karte gefunden)" }
	} else {
		Hinweis "3D-Test ausgelassen (-Ohne3D oder keine Zero-Hour-Daten in GENERALSZH_ASSETS_DIR)"
	}
	# GUI program: Start-Process -Wait, the result comes from its exit code and selftest.log.
	$p = Start-Process -FilePath $exe.FullName -ArgumentList $argumente -WorkingDirectory $ExeDir -Wait -PassThru
	$log = Join-Path $shots "selftest.log"
	if (Test-Path $log) {
		Get-Content $log | Where-Object { $_ -match "^FAIL|^engine:|map3d|images" } | ForEach-Object { Hinweis $_ }
	}
	if ($p.ExitCode -eq 0) { Gut "Selbsttest, Bilder in $shots, $(Dauer $t0)" }
	else { Schlecht "Selbsttest, Exit $($p.ExitCode) (Log: $log)" }
}

# ----------------------------------------------------------------------------------------------
Titel "Ergebnis"
Write-Host "  Editor: $($exe.FullName)"
Write-Host "  Gesamtdauer: $(Dauer $Start)"
if ($script:Fehler -eq 0) { Gut "fertig" } else { Schlecht "$($script:Fehler) Fehler, siehe oben" }

if ($Starten -and $script:Fehler -eq 0) {
	Start-Process -FilePath $exe.FullName -WorkingDirectory $ExeDir
}
exit $(if ($script:Fehler -eq 0) { 0 } else { 1 })
