<#
  Copyright (C) 2026 GZH Neo
  Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
  (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.

  Pruefung unter Windows in einem Durchgang, Ergebnis in EINER Berichtsdatei (Markdown), die man
  weitergeben kann (etwa an eine Cloud-Sitzung, die kein Windows hat).

  Ablauf (jeder Schritt laeuft weiter, auch wenn ein frueherer fehlschlaegt; Ausnahme: ohne
  erfolgreichen Bau werden keine Spiele gestartet, damit nicht mit alten Exes getestet wird):
    1. Stand: Branch, Commit, ungesicherte Aenderungen
    2. Bau: build-x64 Debug/Release, build-x64-tools Debug/Release (fehlende Ordner per Preset)
    3. ctest: ohne Assets (-LE assets) Debug/Release; mit -CtestAssets zusaetzlich die vier Replay-Tests
       (-L assets, ctest -j4); sonst deckt der Replay-Pool (4.) sie ab, nur die langen Debug-Replays brauchen -Voll
       Nebenbei als Hintergrundprozesse ab Skriptbeginn: alle EA-Karten gegen die Grenzen der Kartenleser
       (kartengrenzen.py) und alle .big-Archive gegen die Regeln des Archivlesers (bigpruefung.py),
       dazu alle Fensterlayouts (.wnd) der Archive gegen die Regeln des Layout-Lesers (wndpruefung.py, R-339),
       alles ohne Spielstart. Der Bau von build-x64 und build-x64-tools laeuft je Konfiguration gleichzeitig.
    4. Replays: alle Referenzsaetze (runner.py check), beide Spiele, Debug und Release;
       einzeln verteilt, standardmaessig sieben gleichzeitig (-ReplayParallel), die laengsten zuerst
       (lange Replays -- Referenz-.crc ueber -DebugReplayMaxZeilen Zeilen -- nur in Release, mit -Voll auch in Debug)
       (Dauern des letzten Laufs in render-out\pruefung\replay_dauer.json). Sie laufen weiter, waehrend
       die Schritte 5 und 6 und die optionalen Tests laufen -- das Skript startet in jeder Wartepause die
       naechsten (Runner-Laeufe zaehlen fuer die Warteschlange nicht, Starttests schreiben mit -mehrfach
       ein eigenes Protokoll).
    5. Starttests: Generals und Zero Hour (Debug) gleichzeitig je 60 Frames im Fenster, Protokoll ausgewertet
       (Assertions, Lecks, lebende D3D11-Objekte, D3D11-Pruefschicht, nicht geladene DDS, Absturzberichte)
    6. entfallen (WorldBuilder-Starttest; der alte WorldBuilder ist entfernt, Tag WorldBuilder_Alt_Letzter_Stand)
    optional: -Bildvergleich <Praefix>, -Lecksuche, -Tastatur
    7. Karteneditor (Qt 6): Preset x64-editor bauen und Selbsttest (tools/mapeditor/bauen_windows.ps1); im Standardlauf, mit
       -OhneEditor ausgelassen (der erste Lauf baut Qt und kann bis zu einer Stunde dauern)
  Der Bericht nennt am Anfang die Zeitverteilung je Abschnitt samt Wartezeit auf die Warteschlange.

  Aufruf (im Repo-Ordner, PowerShell):
    powershell -ExecutionPolicy Bypass -File tools\abnahme\pruefung_windows.ps1
    powershell -ExecutionPolicy Bypass -File tools\abnahme\pruefung_windows.ps1 -Schnell
    powershell -ExecutionPolicy Bypass -File tools\abnahme\pruefung_windows.ps1 -Bildvergleich x64ref -Lecksuche

  Regeln: Spiele nur im Fenster (-win), vor jedem Start die Warteschlange
  (tools/abnahme/slot.py, wait_free), die Options.ini des Nutzers bleibt unberuehrt, keine fremden
  Prozesse beenden. Die EA-Installation wird nicht angefasst.
#>
param(
	[switch]$Schnell,                 # nur Stand, Bau und ctest ohne Assets
	[switch]$OhneBau,                 # vorhandene Exes nehmen (Bericht vermerkt das)
	[switch]$Neubau,                  # --clean-first: alles neu uebersetzen, dann zeigt der Bericht alle Warnungen
	[switch]$WorldBuilder,            # ohne Wirkung (der alte WorldBuilder ist entfernt), bleibt fuer alte Aufrufe
	[switch]$OhneWorldBuilder,        # ohne Wirkung, bleibt fuer alte Aufrufe
	[switch]$CtestAssets,             # zusaetzlich ctest -L assets (die vier Replay-Tests, ctest -j4)
	[string]$Bildvergleich = "",      # Referenzpraefix unter render-out, etwa x64ref
	[switch]$Lecksuche,               # tools/lecksuche/lecksuche.py --auswahl abnahme (braucht build-leak)
	[switch]$Tastatur,                # tools/abnahme/tastatur_test.py (Menue und Partie)
	[switch]$Editor,                  # Karteneditor (Preset x64-editor, Qt 6) bauen und Selbsttest (tools/mapeditor/bauen_windows.ps1); laeuft im Standardlauf mit
	[switch]$OhneEditor,              # Karteneditor auslassen (der erste Lauf baut Qt und braucht bis zu einer Stunde)
	[string]$CMake = "",
	[string]$Python = "python",
	[int]$ReplayTimeout = 5400,
	[int]$ReplayParallel = 7,         # gleichzeitige Replays (-runner, je ein Kern); 1 fuer Zeitmessungen
	[switch]$Voll,                    # auch die langen Replays im Debug-Build (sonst nur in Release)
	[int]$DebugReplayMaxZeilen = 1000, # laengere Referenz-.crc gelten als lang und laufen nur in Release
	[int]$StartTimeout = 600,         # Sekunden je Starttest, danach wird das eigene Spiel beendet
	[string]$Bericht = "",
	[string]$Benutzerdaten = ""       # eigener Benutzerordner der Testlaeufe (Unterordner Generals und ZeroHour), etwa fuer eine andere Sprache
)

$ErrorActionPreference = "Continue"
# Note: PowerShell variable names are case-insensitive ($Zeilen and $zeilen are the same variable).
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $Root
. (Join-Path $PSScriptRoot '..\pfade.ps1')
$Start = Get-Date
$Stempel = $Start.ToString("yyyy-MM-dd_HHmmss")
$Out = Join-Path $Root "render-out\pruefung\$Stempel"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
if ($Bericht -eq "") { $Bericht = Join-Path $Root "render-out\pruefung\pruefbericht_$Stempel.md" }

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
	Schreibe ""
	Schreibe "<details><summary>$titel</summary>"
	Schreibe ""
	Schreibe '```'
	$liste = @($inhalt)
	if ($liste.Count -gt $max) {
		foreach ($z in $liste[0..($max - 1)]) { Schreibe ([string]$z) }
		Schreibe "... ($($liste.Count - $max) weitere Zeilen, vollstaendig in $Out)"
	} else {
		foreach ($z in $liste) { Schreibe ([string]$z) }
	}
	Schreibe '```'
	Schreibe "</details>"
}
function Dauer([datetime]$t0) { "{0:N0} s" -f ((Get-Date) - $t0).TotalSeconds }

# Befehl ausfuehren, Ausgabe in Datei, Exit-Code zurueck
function Lauf([string]$name, [string]$exe, [string[]]$argumente, [string]$ordner = $Root) {
	$log = Join-Path $Out "$name.log"
	$alt = Get-Location
	Set-Location $ordner
	& $exe @argumente *> $log
	$rc = $LASTEXITCODE
	Set-Location $alt
	return @{ rc = $rc; log = $log }
}

function WarteAufSlot([int]$parallel = 1) {
	$t = Get-Date
	& $Python -c "import sys; sys.path.insert(0, r'$Root\tools\abnahme'); from slot import wait_free; wait_free(parallel=$parallel)" | Out-Null
	$script:SlotWarten += ((Get-Date) - $t).TotalSeconds
}

# Replays run beside the other tests: ReplayPump starts the next ones and collects finished ones. It is
# called wherever the script waits (start tests, optional tests), no background job
# (Start-Process inside a PowerShell 5.1 job started nothing).
$script:ReplayJobs = @()
$script:ReplayNext = 0
function ReplayPump() {
	$laufend = 0
	for ($k = 0; $k -lt $script:ReplayNext; $k++) {
		$j = $script:ReplayJobs[$k]
		if ($null -ne $j.rc) { continue }
		if ($j.p.HasExited) {
			$j.rc = $j.p.ExitCode; if ($null -eq $j.rc) { $j.rc = -1 }
			$j.sek = ((Get-Date) - $j.t0).TotalSeconds
		} else { $laufend++ }
	}
	while ($script:ReplayNext -lt $script:ReplayJobs.Count -and $laufend -lt [Math]::Max(1, $ReplayParallel)) {
		$j = $script:ReplayJobs[$script:ReplayNext]
		$script:ReplayNext++
		$argumente = @("tools\determinism\runner.py", "check", "--exe", $j.exe, "--replay", $j.rep,
			"--timeout", "$ReplayTimeout", "--extra", "-ignoreAsserts") + (BenutzerArgs $j.exe) | ForEach-Object { '"' + $_ + '"' }
		$j.t0 = Get-Date
		$j.p = Start-Process -FilePath $Python -ArgumentList $argumente -WorkingDirectory $Root -NoNewWindow -PassThru `
			-RedirectStandardOutput $j.log -RedirectStandardError ($j.log + ".err")
		if ($null -eq $j.p) { $j.rc = -1; continue }   # could not start
		$null = $j.p.Handle   # keeps ExitCode available under PowerShell 5.1
		$laufend++
	}
}
# -benutzerdaten <Ordner>\<Spiel> fuer jeden Spielstart, wenn -Benutzerdaten gesetzt ist (ohne Leerzeichen im
# Unterordner, weil Start-Process unter PowerShell 5.1 Argumente nicht in Anfuehrungszeichen setzt)
function BenutzerArgs([string]$exe) {
	if (-not $Benutzerdaten) { return @() }
	$spiel = if ((Split-Path $exe -Leaf) -match "ZH\.exe$") { "ZeroHour" } else { "Generals" }
	return @("-benutzerdaten", (Join-Path $Benutzerdaten $spiel))
}
function ReplaysOffen() { @($script:ReplayJobs | Where-Object { $null -eq $_.rc }).Count }

# Like Lauf, but keeps the replays going while it waits.
function LaufNebenbei([string]$name, [string]$exe, [string[]]$argumente, [string]$ordner = $Root) {
	$log = Join-Path $Out "$name.log"
	$quoted = $argumente | ForEach-Object { '"' + $_ + '"' }
	$p = Start-Process -FilePath $exe -ArgumentList $quoted -WorkingDirectory $ordner -NoNewWindow -PassThru `
		-RedirectStandardOutput $log -RedirectStandardError ($log + ".err")
	if ($null -eq $p) { return @{ rc = -1; log = $log } }
	$null = $p.Handle
	while (-not $p.WaitForExit(2000)) { ReplayPump }
	$rc = $p.ExitCode
	if (Test-Path ($log + ".err")) { Get-Content ($log + ".err") | Add-Content $log }
	return @{ rc = $rc; log = $log }
}

# Background process (checks without a game window, builds): started now, collected later. Output and
# error stream go to <name>.log(.err); HintergrundEnde waits (keeping the replays going) and merges both.
function Hintergrund([string]$name, [string]$exe, [string[]]$argumente, [string]$ordner = $Root) {
	$log = Join-Path $Out "$name.log"
	$quoted = $argumente | ForEach-Object { '"' + $_ + '"' }
	$p = Start-Process -FilePath $exe -ArgumentList $quoted -WorkingDirectory $ordner -NoNewWindow -PassThru `
		-RedirectStandardOutput $log -RedirectStandardError ($log + ".err")
	if ($null -ne $p) { $null = $p.Handle }
	return @{ p = $p; log = $log; t0 = Get-Date }
}
function HintergrundEnde($h) {
	if ($null -eq $h.p) { return @{ rc = -1; log = $h.log; sek = 0.0 } }
	while (-not $h.p.WaitForExit(2000)) { ReplayPump }
	$rc = $h.p.ExitCode
	if (Test-Path ($h.log + ".err")) { Get-Content ($h.log + ".err") | Add-Content $h.log }
	return @{ rc = $rc; log = $h.log; sek = ($h.p.ExitTime - $h.t0).TotalSeconds }
}

# Map and archive checks run beside everything else (they read only the game data).
$script:BgKarten = $null
$script:BgBig = $null
$script:BgWnd = $null
function KartenBigErgebnis() {
	if ($null -ne $script:BgKarten) {
		$r = HintergrundEnde $script:BgKarten
		Schreibe ""
		Schreibe "## Kartengrenzen"
		$summe = (Get-Content $r.log | Select-Object -Last 1)
		Ergebnis "Kartengrenzen (alle EA-Karten)" ($r.rc -eq 0) ("{0}, {1:N0} s (nebenbei)" -f $summe, $r.sek)
		Block "Kartengrenzen" (Get-Content $r.log) 80
		$script:BgKarten = $null
	}
	if ($null -ne $script:BgBig) {
		$r = HintergrundEnde $script:BgBig
		Schreibe ""
		Schreibe "## BIG-Archive"
		$summe = (Get-Content $r.log | Select-Object -Last 1)
		Ergebnis "BIG-Archive (Asset-Ordner)" ($r.rc -eq 0) ("{0}, {1:N0} s (nebenbei)" -f $summe, $r.sek)
		Block "BIG-Archive" (Get-Content $r.log) 80
		$script:BgBig = $null
	}
	if ($null -ne $script:BgWnd) {
		$r = HintergrundEnde $script:BgWnd
		Schreibe ""
		Schreibe "## Fensterlayouts (.wnd)"
		$summe = (Get-Content $r.log | Select-Object -Last 1)
		Ergebnis "Fensterlayouts (alle .wnd der Archive)" ($r.rc -eq 0) ("{0}, {1:N0} s (nebenbei)" -f $summe, $r.sek)
		Block "Fensterlayouts" (Get-Content $r.log) 80
		$script:BgWnd = $null
	}
}

# Time per section for the report; Phase closes the previous section.
$script:Phasen = @()
$script:PhaseName = ""
$script:PhaseStart = Get-Date
$script:SlotWarten = 0.0
function Phase([string]$name) {
	$jetzt = Get-Date
	if ($script:PhaseName -ne "") { $script:Phasen += , @($script:PhaseName, ($jetzt - $script:PhaseStart).TotalSeconds) }
	$script:PhaseName = $name
	$script:PhaseStart = $jetzt
}

# ----------------------------------------------------------------------------------------------
# Werkzeuge finden
if ($CMake -eq "") {
	$c = Get-Command cmake -ErrorAction SilentlyContinue
	if ($c) { $CMake = $c.Source }
	else { $CMake = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" }
}
$CTest = Join-Path (Split-Path $CMake) "ctest.exe"

# ----------------------------------------------------------------------------------------------
# 1. Stand
Phase "Stand"
$t0 = Get-Date
$branch = (git rev-parse --abbrev-ref HEAD) 2>$null
$commit = (git log -1 --format="%h %s") 2>$null
$commitVoll = (git rev-parse HEAD) 2>$null
$offen = @(git status --porcelain 2>$null)
Schreibe "## Stand"
Schreibe ""
Schreibe "- Branch: ``$branch``"
Schreibe "- Commit: ``$commit``"
Schreibe "- Ungesicherte Aenderungen: $($offen.Count)"
Schreibe "- Rechner: $env:COMPUTERNAME, $((Get-CimInstance Win32_Processor | Select-Object -First 1).Name)"
Schreibe "- GPU: $((Get-CimInstance Win32_VideoController | Select-Object -First 1).Name)"
Schreibe "- CMake: $CMake"
Schreibe "- Asset-Kopien: GZH_ASSETS_GENERALS=``$env:GZH_ASSETS_GENERALS``, GZH_ASSETS_ZH=``$env:GZH_ASSETS_ZH``"
# Map, archive and layout checks and new build folders take the asset folders from these variables
if (-not ($env:GZH_ASSETS_GENERALS -and $env:GZH_ASSETS_ZH)) {
	Ergebnis "Asset-Kopien (GZH_ASSETS_GENERALS, GZH_ASSETS_ZH)" $false "nicht gesetzt; Karten-, BIG- und Layoutpruefung laufen ohne Daten"
}
if ($offen.Count -gt 0) { Block "git status" $offen 40 }

# Map files against the reader limits and .big archives against the rules of the archive reader (a rejected
# archive is not loaded at all): needs only the game data, so it runs during the build and the tests.
if (-not $Schnell) {
	$script:BgKarten = Hintergrund "kartengrenzen" $Python @("tools\abnahme\kartengrenzen.py")
	$script:BgBig = Hintergrund "bigpruefung" $Python @("tools\abnahme\bigpruefung.py")
	$script:BgWnd = Hintergrund "wndpruefung" $Python @("tools\abnahme\wndpruefung.py")
}

# ----------------------------------------------------------------------------------------------
# 2. Bau
Phase "Bau"
Schreibe ""
Schreibe "## Bau"
$bauOk = $true
if ($OhneBau) {
	Schreibe ""
	Schreibe "Bau uebersprungen (-OhneBau): getestet werden die vorhandenen Exes."
} else {
	$ziele = @(
		@{ ordner = "build-x64"; preset = "x64-debug"; konfig = @("Debug", "Release") },
		@{ ordner = "build-x64-tools"; preset = "x64-tools"; konfig = @("Debug", "Release") }
	)
	foreach ($z in $ziele) {
		$z.ok = $true
		if (-not (Test-Path (Join-Path $Root $z.ordner))) {
			$r = Lauf "configure_$($z.ordner)" $CMake @("--preset", $z.preset)
			Ergebnis "Konfigurieren $($z.ordner)" ($r.rc -eq 0) "Preset $($z.preset), Exit $($r.rc)"
			if ($r.rc -ne 0) { $bauOk = $false; $z.ok = $false; Block "configure $($z.ordner)" (Get-Content $r.log -Tail 40) }
		}
	}
	# Per configuration both trees build at the same time (they share no files, msbuild -m spreads each over
	# the cores); Debug first, then Release.
	foreach ($k in @("Debug", "Release")) {
		$laeufe = @()
		foreach ($z in $ziele) {
			if (-not $z.ok) { continue }
			$bauArgs = @("--build", $z.ordner, "--config", $k)
			if ($Neubau) { $bauArgs += "--clean-first" }
			$laeufe += , @{ z = $z; h = (Hintergrund "build_$($z.ordner)_$k" $CMake ($bauArgs + @("--", "-m"))) }
		}
		foreach ($l in $laeufe) {
			$z = $l.z
			$r = HintergrundEnde $l.h
			$fehlerZeilen = @(Select-String -Path $r.log -Pattern " error |error LNK|fatal error" | ForEach-Object { $_.Line.Trim() } | Select-Object -Unique)
			# MSBuild repeats a header warning once per project; drop the "[x.vcxproj]" suffix before counting
			$warnungen = @(Select-String -Path $r.log -Pattern ": warning " | ForEach-Object {
				($_.Line -replace '\s*\[[^\]]*\.vcxproj\]\s*$', '' -replace '^\s*\d+>', '' -replace [regex]::Escape("$Root\"), '').Trim()
			} | Sort-Object -Unique)
			$ok = ($r.rc -eq 0)
			if (-not $ok) { $bauOk = $false }
			$warnHinweis = if ($Neubau) { "" } else { " (nur neu uebersetzte Dateien; alle mit -Neubau)" }
			Ergebnis "Bau $($z.ordner) $k" $ok ("Exit $($r.rc), $($fehlerZeilen.Count) Fehler, $($warnungen.Count) Warnungen$warnHinweis, {0:N0} s" -f $r.sek)
			if ($fehlerZeilen.Count -gt 0) { Block "Fehler $($z.ordner) $k" $fehlerZeilen 60 }
			if ($warnungen.Count -gt 0) {
				$warnungen | Set-Content (Join-Path $Out "warnungen_$($z.ordner)_$k.txt")
				$nachNummer = @($warnungen | ForEach-Object { if ($_ -match 'warning (\w+\d+)') { $Matches[1] } else { "?" } } |
					Group-Object | Sort-Object Count -Descending | ForEach-Object { "{0,6}  {1}" -f $_.Count, $_.Name })
				$nachDatei = @($warnungen | ForEach-Object { if ($_ -match '^(.+?)\(\d+') { $Matches[1] } else { "?" } } |
					Group-Object | Sort-Object Count -Descending | ForEach-Object { "{0,6}  {1}" -f $_.Count, $_.Name })
				Block "Warnungen $($z.ordner) $k nach Nummer" $nachNummer 200
				Block "Warnungen $($z.ordner) $k nach Datei" $nachDatei 150
				# Full list once (Debug), so warnings can be worked off without the log files
				if ($k -eq "Debug") { Block "Warnungen $($z.ordner) $k (alle)" $warnungen 4000 }
			}
		}
	}
}

# ----------------------------------------------------------------------------------------------
# 3. ctest
Phase "ctest"
Schreibe ""
Schreibe "## ctest"
$ctestLaeufe = @(@{ k = "Debug"; l = @("-LE", "assets") }, @{ k = "Release"; l = @("-LE", "assets") })
# The four asset tests are replays; the replay pool below covers them (only the long Debug replays need -Voll).
# With -CtestAssets they also run through ctest, the four side by side.
if ($CtestAssets -and -not $Schnell) { $ctestLaeufe += @{ k = "Debug"; l = @("-L", "assets", "-j", "4") } }
foreach ($c in $ctestLaeufe) {
	$t = Get-Date
	$name = "ctest_$($c.k)_$($c.l[0].TrimStart('-'))"
	$r = Lauf $name $CTest (@("--test-dir", "build-x64", "-C", $c.k, "--output-on-failure") + $c.l)
	$summe = (Select-String -Path $r.log -Pattern "tests passed" | Select-Object -Last 1).Line
	Ergebnis "ctest $($c.k) $($c.l -join ' ')" ($r.rc -eq 0) "$summe, $(Dauer $t)"
	if ($r.rc -ne 0) { Block "ctest $($c.k) $($c.l -join ' ') (Ausgabe)" (Get-Content $r.log -Tail 120) 120 }
}

if ($Schnell -or -not $bauOk) {
	KartenBigErgebnis
	if (-not $bauOk) {
		Schreibe ""
		Schreibe "**Abbruch vor den Spieltests: der Bau ist fehlgeschlagen.**"
	}
} else {
	$ExeG = "build-x64\Generals\Code\Main\Debug\GZHNeo.exe"
	$ExeZ = "build-x64\GeneralsMD\Code\Main\Debug\GZHNeoZH.exe"
	$ExeGR = "build-x64\Generals\Code\Main\Release\GZHNeo.exe"
	$ExeZR = "build-x64\GeneralsMD\Code\Main\Release\GZHNeoZH.exe"

	# Neue Absturzberichte gelten als Fehler; Stand vorher merken
	$crashOrdner = @((Split-Path $ExeG), (Split-Path $ExeZ), (Split-Path $ExeGR), (Split-Path $ExeZR)) | ForEach-Object { Join-Path $Root "$_\Crashes" }
	$crashVorher = @{}
	foreach ($d in $crashOrdner) { $crashVorher[$d] = @(if (Test-Path $d) { Get-ChildItem $d -Directory | ForEach-Object Name }) }

	# Warm-up: the first replays of a fresh build all start at once and used to hit the sim thread watchdog;
	# one short start of each exe first (cold start of the graphics stack, shader and file caches) takes a few seconds.
	Phase "Aufwaermstart"
	WarteAufSlot 1
	foreach ($exe in @($ExeG, $ExeZ, $ExeGR, $ExeZR)) {
		$ordner = Join-Path $Root (Split-Path $exe)
		$p = Start-Process -FilePath (Join-Path $ordner (Split-Path $exe -Leaf)) -WorkingDirectory $ordner -PassThru `
			-ArgumentList (@("-win", "-xres", "800", "-yres", "600", "-nologo", "-noaudio", "-fixedTimeStep", "-quitAfter", "5", "-ignoreAsserts", "-mehrfach") + (BenutzerArgs $exe))
		$null = $p.Handle
		if (-not $p.WaitForExit(120000)) { $p.Kill(); $p.WaitForExit() }
	}

	# ------------------------------------------------------------------------------------------
	# 4. Replays: started in the background, reported after the other tests
	Phase "Replays starten"
	$saetze = @(
		@($ExeG, "tests\replays\x64"), @($ExeZ, "tests\replays\zh\x64"),
		@($ExeGR, "tests\replays\x64"), @($ExeZR, "tests\replays\zh\x64"),
		@($ExeGR, "tests\replays\neueki\x64"), @($ExeZR, "tests\replays\neueki\zh\x64"),
		@($ExeG, "tests\replays\testhilfen\x64"), @($ExeZ, "tests\replays\testhilfen\zh\x64")
	)
	# One job per replay; runner.py uses its own temporary CRC log per run, so runs do not collide.
	# Each -runner instance is single-threaded. Durations are only comparable with -ReplayParallel 1.
	# Plain arrays only: generic List/Queue of hashtables break under PowerShell 5.1 (ArgumentException).
	$jobs = @()
	$uebersprungen = @()
	foreach ($s in $saetze) {
		if (-not (Test-Path $s[1])) { continue }
		$konfig = ($s[0] -split '\\')[4]
		foreach ($rep in (Get-ChildItem $s[1] -Filter *.rep | Sort-Object Name)) {
			$crc = [IO.Path]::ChangeExtension($rep.FullName, ".crc")
			if (-not (Test-Path $crc)) { continue }
			# Debug runs the logic about five times slower; the long replays check the same code in Release.
			if ($konfig -eq "Debug" -and -not $Voll -and @(Get-Content $crc).Count -gt $DebugReplayMaxZeilen) {
				$uebersprungen += "$($rep.Name) ($(Split-Path $s[0] -Leaf) Debug)"
				continue
			}
			$jobs += , @{ exe = $s[0]; satz = $s[1]; konfig = $konfig; rep = $rep.FullName; name = $rep.BaseName; rc = $null; sek = 0.0;
				key = "$($s[0])|$($s[1])\$($rep.Name)";
				log = (Join-Path $Out ("replay_{0}_{1}_{2}_{3}.log" -f (($s[0] -split '\\')[1]), $konfig, ($s[1] -replace '[\\]', '_'), $rep.BaseName)) }
		}
	}
	# Longest first (durations of the last run; unknown ones count as longest): the longest replay
	# alone sets the minimum time, so it must not start last.
	$dauerDatei = Join-Path $Root "render-out\pruefung\replay_dauer.json"
	$alteDauer = @{}
	if (Test-Path $dauerDatei) {
		try { (Get-Content $dauerDatei -Raw | ConvertFrom-Json).PSObject.Properties | ForEach-Object { $alteDauer[$_.Name] = [double]$_.Value } } catch { }
	}
	$jobs = @($jobs | Sort-Object -Descending -Property @{ Expression = { if ($alteDauer.ContainsKey($_.key)) { $alteDauer[$_.key] } else { 1e9 } } })
	$replayStart = Get-Date
	$script:ReplayJobs = $jobs
	$script:ReplayNext = 0
	ReplayPump

	# ------------------------------------------------------------------------------------------
	# 5. Starttests mit Protokollauswertung
	Phase "Starttests"
	Schreibe ""
	Schreibe "## Starttests (Debug, 60 Frames, Fenster)"
	$muster = [ordered]@{
		"Assertions"               = "ASSERTION FAILURE"
		"Lecks (Bloecke)"          = "^Leaked a block"
		"Lecksumme"                = "There were \d+ memory leaks|Leaked a total of"
		"lebende D3D11-Objekte"    = "D3D11-LEBEND "
		"D3D11-Pruefschicht"       = "D3D11-Debug-Layer \["
		"DDS nicht geladen"        = "not loaded"
		"Absturz"                  = "EXCEPTION DUMP|^ABORT:|crash report in"
		"Watchdog"                 = "hung for"
		"Testablauf/Kamera-Fehler" = "TESTABLAUF ERROR|CAMERAPATH ERROR"
	}
	# Both games start together: one pass through the queue for two windows; each instance has its own
	# folder and writes its own log (-mehrfach).
	WarteAufSlot 2
	$starts = @()
	foreach ($exe in @($ExeG, $ExeZ)) {
		$ordner = Join-Path $Root (Split-Path $exe)
		$datei = Split-Path $exe -Leaf
		$p = Start-Process -FilePath (Join-Path $ordner $datei) -WorkingDirectory $ordner -PassThru `
			-ArgumentList (@("-win", "-xres", "800", "-yres", "600", "-nologo", "-noaudio", "-fixedTimeStep", "-quitAfter", "60", "-ignoreAsserts", "-mehrfach") + (BenutzerArgs $exe))
		$null = $p.Handle   # sonst liefert ExitCode unter PowerShell 5.1 nichts
		$starts += , @{ ordner = $ordner; datei = $datei; p = $p; t = (Get-Date) }
	}
	$startEnde = (Get-Date).AddSeconds($StartTimeout)
	while ((@($starts | Where-Object { -not $_.p.HasExited }).Count -gt 0) -and ((Get-Date) -le $startEnde)) {
		ReplayPump
		Start-Sleep -Seconds 2
	}
	foreach ($s in $starts) {
		$p = $s.p; $ordner = $s.ordner; $datei = $s.datei; $t = $s.t
		if (-not $p.HasExited) {
			$p.Kill(); $p.WaitForExit()
			$kurz = @("Zeitlimit $StartTimeout s, beendet")
			$ok = $false
		} else {
			$kurz = @("Exit $($p.ExitCode)")
			$ok = ($p.ExitCode -eq 0)
		}
		# -mehrfach: own log m<PID>_DebugLogFileD.txt, as Debug replays in the same folder write DebugLogFileD.txt
		$logDatei = Join-Path $ordner ("m{0}_DebugLogFileD.txt" -f $p.Id)
		if (Test-Path $logDatei) {
			Move-Item $logDatei (Join-Path $Out "start_$datei.DebugLogFileD.txt") -Force
			$logDatei = Join-Path $Out "start_$datei.DebugLogFileD.txt"
			foreach ($k in $muster.Keys) {
				$treffer = @(Select-String -Path $logDatei -Pattern $muster[$k] | ForEach-Object { $_.Line.Trim() })
				if ($treffer.Count -gt 0) {
					$kurz += "$k $($treffer.Count)"
					if ($k -ne "Lecksumme") { $ok = $false }
					Block "$datei - $k" ($treffer | Select-Object -Unique) 40
				}
			}
		} else {
			$kurz += "kein m$($p.Id)_DebugLogFileD.txt"
			$ok = $false
		}
		# further per-instance files (EngineLog) into the report folder, so none pile up next to the exe
		Get-ChildItem $ordner -Filter ("m{0}_*" -f $p.Id) -ErrorAction SilentlyContinue | Move-Item -Destination $Out -Force -ErrorAction SilentlyContinue
		Ergebnis "Start $datei" $ok (($kurz -join ", ") + (", {0:N0} s" -f ($p.ExitTime - $t).TotalSeconds))
	}

	# ------------------------------------------------------------------------------------------
	# optional: Bildvergleich, Lecksuche, Tastatur
	Phase "Optionale Tests"
	if ($Bildvergleich -ne "") {
		Schreibe ""
		Schreibe "## Bildvergleich gegen render-out/$Bildvergleich-*"
		$faelle = @(
			@("g-shell-d3d11", $ExeG, @("--scene", "shellmap", "--frames", "300,900")),
			@("zh-shell-d3d11", $ExeZ, @("--scene", "shellmap", "--frames", "300,900")),
			@("g-water-d3d11", $ExeG, @("--scene", "skirmish", "--map", "Tournament Lake", "--ai", "2", "--seed", "1", "--frames", "300,900"))
		)
		foreach ($f in $faelle) {
			$t = Get-Date
			WarteAufSlot
			$ziel = "render-out\pruefung\$Stempel\bild_$($f[0])"
			$r = LaufNebenbei "bild_$($f[0])" $Python (@("tools\render\run_render_test.py", "run", "--exe", $f[1], "--out", $ziel,
				"--reference", "render-out\$Bildvergleich-$($f[0])") + $f[2] + @("--extra", "-ignoreAsserts"))
			$ab = @(Select-String -Path $r.log -Pattern "^ABWEICHT|^FEHLER" | ForEach-Object Line)
			Ergebnis "Bild $($f[0])" (($r.rc -eq 0) -and ($ab.Count -eq 0)) "$($ab.Count) abweichend, Exit $($r.rc), $(Dauer $t)"
			if ($ab.Count -gt 0) { Block "Bild $($f[0])" $ab 30 }
		}
	}
	if ($Editor -or -not $OhneEditor) {
		Schreibe ""
		Schreibe "## Karteneditor (Preset x64-editor, Qt 6)"
		$t = Get-Date
		# own build folder (build-x64-editor); the replays keep running while it builds and tests
		$r = LaufNebenbei "karteneditor" "powershell.exe" @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "$Root\tools\mapeditor\bauen_windows.ps1", "-Config", "Release")
		$fehl = @(Select-String -Path $r.log -Pattern "^\[FEHLER\]" | ForEach-Object Line)
		Ergebnis "Karteneditor bauen und Selbsttest (Release)" (($r.rc -eq 0) -and ($fehl.Count -eq 0)) "Exit $($r.rc), $($fehl.Count) Fehler, $(Dauer $t)"
		if ($fehl.Count -gt 0) { Block "Karteneditor Fehler" $fehl 30 }
		Block "Karteneditor (Ausgabe)" (Get-Content $r.log -Tail 60) 60
	}
	if ($Lecksuche) {
		Schreibe ""
		Schreibe "## Lecksuche"
		$t = Get-Date
		$r = LaufNebenbei "lecksuche" $Python (@("tools\lecksuche\lecksuche.py", "--auswahl", "abnahme", "--out", "render-out\pruefung\$Stempel\lecksuche",
			"--warteschlange", "$Root\tools\abnahme\slot.py") + $(if ($Benutzerdaten) { @("--benutzerdaten", $Benutzerdaten) } else { @() }))
		Ergebnis "Lecksuche" ($r.rc -eq 0) "Exit $($r.rc), $(Dauer $t)"
		Block "Lecksuche" (Get-Content $r.log -Tail 60) 60
	}
	if ($Tastatur) {
		Schreibe ""
		Schreibe "## Tastaturtest"
		foreach ($exe in @($ExeG, $ExeZ)) {
			$t = Get-Date
			$r = LaufNebenbei "tastatur_$(Split-Path $exe -Leaf)" $Python @("tools\abnahme\tastatur_test.py", "--exe", $exe, "--out", "render-out\pruefung\$Stempel\tastatur_$(Split-Path $exe -Leaf)")
			Ergebnis "Tastatur $(Split-Path $exe -Leaf)" ($r.rc -eq 0) "Exit $($r.rc), $(Dauer $t)"
			Block "Tastatur $(Split-Path $exe -Leaf)" (Get-Content $r.log -Tail 40) 40
		}
	}

	# ------------------------------------------------------------------------------------------
	# Replays abwarten und berichten
	Phase "Karten- und Archivpruefung einsammeln"
	KartenBigErgebnis
	Phase "Replays abwarten (Rest)"
	# Wait visibly: every 30 s the state; runs beyond the replay time limit plus 10 min are given up
	# (runner.py ends its own game after the time limit).
	$warteBis = $replayStart.AddSeconds($ReplayTimeout + 600)
	$naechsteMeldung = Get-Date
	while ((ReplaysOffen) -gt 0) {
		ReplayPump
		if ((Get-Date) -ge $naechsteMeldung) {
			Write-Host ("  Replays: {0} von {1} fertig, {2} laufen, {3}" -f ($jobs.Count - (ReplaysOffen)), $jobs.Count,
				($script:ReplayNext - ($jobs.Count - (ReplaysOffen))), (Dauer $replayStart))
			$naechsteMeldung = (Get-Date).AddSeconds(30)
		}
		if ((Get-Date) -gt $warteBis) {
			Write-Host "  Replays: Zeitlimit ueberschritten, offene Laeufe gelten als fehlgeschlagen"
			break
		}
		Start-Sleep -Seconds 2
	}
	foreach ($j in $jobs) { if ($null -eq $j.rc) { $j.rc = -1 } }
	Write-Host ("Replays: {0} Laeufe, {1} gleichzeitig, {2}" -f $jobs.Count, $ReplayParallel, (Dauer $replayStart))
	Schreibe ""
	Schreibe "## Replays"
	foreach ($s in $saetze) {
		$meine = @($jobs | Where-Object { $_.exe -eq $s[0] -and $_.satz -eq $s[1] })
		if ($meine.Count -eq 0) { continue }
		$zeilen = @()
		foreach ($j in $meine) {
			$inhalt = @(Get-Content $j.log -ErrorAction SilentlyContinue) + @(Get-Content ($j.log + ".err") -ErrorAction SilentlyContinue)
			$treffer = @($inhalt | Where-Object { $_ -match "OK|FEHLER|ABWEICHUNG|Absturzbericht|Traceback|Error" })
			if ($treffer.Count -eq 0) { $treffer = @("$($j.name): keine Ausgabe, Exit $($j.rc)") }
			$zeilen += $treffer
		}
		$fehl = @($meine | Where-Object { $_.rc -ne 0 })
		$okN = $meine.Count - $fehl.Count
		Ergebnis "Replays $($s[1]) mit $(Split-Path $s[0] -Leaf) ($($meine[0].konfig))" ($fehl.Count -eq 0) "$okN von $($meine.Count) OK"
		Block "Replays $($s[1]) $($s[0])" $zeilen 60
	}
	Schreibe ""
	Schreibe "Replays gesamt: $($jobs.Count) Laeufe, $ReplayParallel gleichzeitig, $(Dauer $replayStart) (parallel zu den Starttests)."
	if ($uebersprungen.Count -gt 0) {
		Schreibe ""
		Schreibe "Nur in Release geprueft (lang, im Debug-Build ohne -Voll ausgelassen): $($uebersprungen -join ', ')."
	}
	foreach ($j in $jobs) { if ($j.rc -eq 0 -and $j.sek -gt 0) { $alteDauer[$j.key] = [Math]::Round($j.sek, 1) } }
	try { $alteDauer | ConvertTo-Json | Set-Content -Path $dauerDatei -Encoding UTF8 } catch { }

	# Absturzberichte, die waehrend der Pruefung entstanden sind
	Phase "Abschluss"
	$neu = @()
	foreach ($d in $crashOrdner) {
		if (Test-Path $d) {
			foreach ($n in (Get-ChildItem $d -Directory | ForEach-Object Name)) {
				if ($crashVorher[$d] -notcontains $n) { $neu += (Join-Path $d $n) }
			}
		}
	}
	Ergebnis "Absturzberichte" ($neu.Count -eq 0) "$($neu.Count) neu"
	foreach ($n in $neu) {
		$info = Join-Path $n "Info.txt"
		if (Test-Path $info) { Block "Absturzbericht $n" (Get-Content $info) 40 }
	}
}

# ----------------------------------------------------------------------------------------------
# Bericht schreiben
$kopf = New-Object System.Collections.Generic.List[string]
$kopf.Add("# Pruefbericht Windows $Stempel")
$kopf.Add("")
$gesamt = if ($script:Fehler -eq 0) { "**bestanden**" } else { "**$($script:Fehler) Schritt(e) fehlgeschlagen**" }
$kopf.Add("Stand: Branch ``$branch``, Commit ``$commitVoll``, $($offen.Count) ungesicherte Aenderungen")
$kopf.Add("")
$kopf.Add("Ergebnis: $gesamt, Dauer $(Dauer $Start). Einzelprotokolle: ``$Out``")
$kopf.Add("")
$kopf.Add("| Schritt | Ergebnis | Kurz |")
$kopf.Add("|---|---|---|")
foreach ($z in $script:Uebersicht) { $kopf.Add($z) }
Phase ""
$kopf.Add("")
$kopf.Add("## Zeitverteilung")
$kopf.Add("")
$kopf.Add("| Abschnitt | Dauer |")
$kopf.Add("|---|---|")
foreach ($ph in $script:Phasen) { $kopf.Add(("| {0} | {1:N0} s |" -f $ph[0], $ph[1])) }
$kopf.Add(("| davon Warten auf die Warteschlange (slot.py) | {0:N0} s |" -f $script:SlotWarten))
$alles = @($kopf) + @($script:BerichtZeilen)
[System.IO.File]::WriteAllLines($Bericht, [string[]]$alles, (New-Object System.Text.UTF8Encoding($false)))
Write-Host ""
Write-Host "Bericht: $Bericht"
if ($script:Fehler -eq 0) { exit 0 } else { exit 1 }
