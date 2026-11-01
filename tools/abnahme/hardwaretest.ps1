<#
  Copyright (C) 2026 GZH Neo
  Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
  (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.

  Hardwaretest fuer einen anderen Rechner (Windows 10 oder 11, ohne Python, ohne Bau):
  sammelt Systemdaten und misst das Spiel in mehreren Detailstufen. Alles in EINEM Ordner, den man
  zurueckgeben kann; die Einstellungen des Spielers bleiben unberuehrt (Startschalter statt Options.ini,
  -staticLOD und -quitAfter).

  Ablauf:
    1. Systemdaten: Prozessor (Kerne, Threads, Takt), Arbeitsspeicher, Grafikkarte (Treiber, Speicher),
       Windows-Version, Energieplan, Bildschirm.
    2. Starttest: das Spiel startet im Fenster, 60 Logik-Frames, Protokoll mit Texturstufe, Grafikspeicher,
       Arbeiterzahl und gewaehlten Effekten.
    3. Je Detailstufe (Vorgabe Low, Medium) zwei Laeufe:
       - "echtzeit": Wiederholung einer Partie mit normaler Geschwindigkeit, 60 Sekunden, ohne VSync
         (was die Grafik ohne Bremse schafft; 30 Logik-Ticks je Sekunde bleiben).
       - "gefecht": derselbe Gefechtsabschnitt mit festem Zeitschritt und Kamerafahrt (Rechenlast von
         Logik und Grafik zusammen); Zeit je Bild als Mittelwert und Perzentile.
    4. Arbeiterzahl: bei einer Detailstufe (Vorgabe Low) derselbe Lauf mit weniger Job-Arbeitern als die
       Automatik waehlt (-Arbeiter, Vorgabe 0,1,3; 0 = serieller Referenzmodus). Zeigt, wie viel die
       Mehrfaedigkeit auf diesem Rechner bringt, etwa bei 4 Kernen ohne SMT.
    5. Effektlauf (nur mit -Effekte): bei Low derselbe halbe Gefechtsabschnitt mit jeweils einem Effekt weniger
       (und ganz ohne Effekte), um zu sehen, was die Grafik kostet; ersetzt dann die Teile 3 und 4.
    6. Schalterlauf (nur mit -Schalter): bei Low derselbe halbe Gefechtsabschnitt, jeweils mit einer Einstellung der Stufe
       geaendert (Wolkenschatten, Lichtkarte, Partikelzahl, Schattendecals, Anisotropie, Baumbewegung, weiche
       Wasserkanten, Panzerspuren, alle zusammen). Dazu wird ProjectData\INI\GameLODOverride.ini im Spielordner kurz
       ersetzt und danach (auch bei Abbruch) wiederhergestellt; ersetzt die Teile 3 und 4.
    7. Aufloesungslauf (nur mit -Aufloesung): bei Low derselbe halbe Gefechtsabschnitt in 1280x720, 1600x900 und
       1920x1080 (Fenster); zeigt, ob die Pixelzahl der Engpass ist; ersetzt die Teile 3 und 4.
	7b. Renderskalierungslauf (nur mit -Renderskalierung): bei Low derselbe halbe Gefechtsabschnitt in 1280x720, 1600x900
	   und 1920x1080, je mit der internen Renderaufloesung -Anteile (Vorgabe 1, 0.75, 0.67, 0.5; -renderScale), dazu bei
	   1920x1080 und dem kleinsten Anteil unter 1 die Filter bilinear und cas zum Vergleich mit bicubic (Vorgabe). Mit -gpuTiming:
	   GPU-Zeit der 3D-Szene, des Hochskalierens und des ganzen Bildes je Lauf (GPUEFFEKTE); zwei Bilder je Lauf
	   (bilder-rs-<Lauf>) zur Sichtung; ersetzt die Teile 3 und 4.
    8. Auslastung: waehrend jedes Laufs alle 3 s CPU gesamt, CPU des Spiels (in Prozent aller logischen
       Prozessoren) und GPU (3D-Engine des Spielprozesses) ueber WMI; Mittelwert und p95 je Lauf in summary.md
       (die ersten 15 s eines Laufs zaehlen nicht). Ohne Zaehler steht dort "-". Die Abfrage kostet etwas CPU.
    9. summary.md: Tabelle aller Laeufe, Rechner und Auffaelligkeiten.

  Aufruf (PowerShell, im Ordner des Skripts; die Spielordner liegen daneben, siehe -Spielordner):
    powershell -ExecutionPolicy Bypass -File hardwaretest.ps1
    powershell -ExecutionPolicy Bypass -File hardwaretest.ps1 -Spiel generals -Stufen Low,High
    powershell -ExecutionPolicy Bypass -File hardwaretest.ps1 -Schnell        (nur Starttest und Echtzeit, rund 5 Minuten)

  Schalter:
    -Spiel zh|generals      welches Spiel (Vorgabe zh)
    -Spielordner <Pfad>     Ordner mit der Exe (Vorgabe: ..\ZeroHour bzw. ..\Generals neben dem Skriptordner)
    -Stufen <Liste>         Detailstufen (Vorgabe Low,Medium; High und VeryHigh sind auf schwachen Rechnern nicht aussagekraeftig)
    -Breite/-Hoehe          Fenstergroesse (Vorgabe 1920 x 1080, bei kleinerem Bildschirm 1280 x 720)
    -Schnell                kein Gefechtslauf (die Arbeiterlaeufe nehmen dann den Echtzeitlauf)
    -Aufloesung             nur der Aufloesungslauf (Teil 7) statt Detailstufen und Arbeiterzahl; Stufe aus -ArbeiterStufe (Vorgabe Low)
	-Renderskalierung       nur der Renderskalierungslauf (Teil 7b) statt Detailstufen und Arbeiterzahl; Stufe aus -ArbeiterStufe
	-Anteile <Liste>        Anteile fuer Teil 7b (Vorgabe 1,0.75,0.67,0.5)
    -Schalter               nur der Schalterlauf (Teil 6) statt Detailstufen und Arbeiterzahl; Stufe aus -ArbeiterStufe (Vorgabe Low)
    -Effekte                nur der Effektlauf (Teil 5) statt Detailstufen und Arbeiterzahl; Stufe aus -ArbeiterStufe (Vorgabe Low)
    -Arbeiter <Liste>       Arbeiterzahlen fuer Teil 4 (Vorgabe 0,1,3; leer = ohne Teil 4)
    -ArbeiterStufe <Stufe>  Detailstufe fuer Teil 4 (Vorgabe Low)
    -Ausgabe <Ordner>       Zielordner (Vorgabe hardwaretest-<Rechner>-<Zeit> im Skriptordner)
#>
[CmdletBinding()]
param(
    [ValidateSet("zh", "generals")][string]$Spiel = "zh",
    [string]$Spielordner,
    [string[]]$Stufen = @("Low", "Medium"),
    [int]$Breite = 0,
    [int]$Hoehe = 0,
    [switch]$Schnell,
    [switch]$Effekte,
    [switch]$Schalter,
    [switch]$Aufloesung,
	[switch]$Renderskalierung,
	[double[]]$Anteile = @(1.0, 0.75, 0.67, 0.5),
    [int[]]$Arbeiter = @(0, 1, 3),
    [string]$ArbeiterStufe = "Low",
    [string]$Ausgabe
)

$ErrorActionPreference = "Stop"
$hier = $PSScriptRoot
$daten = Join-Path $hier "daten"
if (-not $Spielordner) {
    $name = if ($Spiel -eq "zh") { "ZeroHour" } else { "Generals" }
    $Spielordner = Join-Path (Split-Path $hier -Parent) $name
}
$exeName = if ($Spiel -eq "zh") { "GZHNeoZH.exe" } else { "GZHNeo.exe" }
$exe = Join-Path $Spielordner $exeName
if (-not (Test-Path $exe)) { throw "Exe nicht gefunden: $exe (Schalter -Spielordner)" }
$replay = Join-Path $daten ($(if ($Spiel -eq "zh") { "zh-tournament-lake.rep" } else { "generals-tournament-lake.rep" }))
$fahrt = Join-Path $daten ($(if ($Spiel -eq "zh") { "gefecht-zh.txt" } else { "gefecht-generals.txt" }))
foreach ($f in @($replay, $fahrt)) { if (-not (Test-Path $f)) { throw "Messdaten fehlen: $f" } }
$gefechtFrames = if ($Spiel -eq "zh") { 16230 } else { 7230 }

Add-Type -AssemblyName System.Windows.Forms
$schirm = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
if ($Breite -le 0 -or $Hoehe -le 0) {
    if ($schirm.Height -ge 1100) { $Breite = 1920; $Hoehe = 1080 } else { $Breite = 1280; $Hoehe = 720 }
}
if (-not $Ausgabe) { $Ausgabe = Join-Path $hier ("hardwaretest-{0}-{1}" -f $env:COMPUTERNAME, (Get-Date -Format "yyyyMMdd-HHmm")) }
New-Item -ItemType Directory -Force $Ausgabe | Out-Null
$bericht = New-Object System.Collections.Generic.List[string]
function Zeile($t) { $bericht.Add($t); Write-Host $t }

# ---- 1. Systemdaten
$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
$os = Get-CimInstance Win32_OperatingSystem
$ram = [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB, 1)
$gpus = Get-CimInstance Win32_VideoController
$plan = (powercfg /getactivescheme) -join " "
$sys = @()
$sys += "Rechner: $env:COMPUTERNAME"
$sys += "Prozessor: $($cpu.Name.Trim()), $($cpu.NumberOfCores) Kerne, $($cpu.NumberOfLogicalProcessors) Threads, max. $($cpu.MaxClockSpeed) MHz"
$sys += "Arbeitsspeicher: $ram GB"
foreach ($g in $gpus) { $sys += "Grafik: $($g.Name), Treiber $($g.DriverVersion) ($($g.DriverDate)), Speicher laut Windows $([math]::Round($g.AdapterRAM / 1MB)) MB" }
$sys += "Windows: $($os.Caption) $($os.Version) (Build $($os.BuildNumber))"
$sys += "Energieplan: $plan"
$sys += "Bildschirm: $($schirm.Width) x $($schirm.Height), Messfenster $Breite x $Hoehe"
$sys += "Spiel: $Spiel, $exe"
$sys | Set-Content -Encoding UTF8 (Join-Path $Ausgabe "system.txt")
$sys | ForEach-Object { Write-Host $_ }

# ---- Hilfsfunktionen
$auslastung = New-Object System.Collections.Generic.List[object]
function Probe([int]$id, [string]$prozess, [int]$logisch) {
    $o = @{ cpu = $null; spiel = $null; gpu = $null }
    try { $o.cpu = [double](Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor -Filter "Name='_Total'").PercentProcessorTime } catch {}
    try {
        $pp = Get-CimInstance Win32_PerfFormattedData_PerfProc_Process -Filter "IDProcess=$id"
        if ($pp) { $o.spiel = [double]$pp.PercentProcessorTime / $logisch }
    } catch {}
    try {
        $g = Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine |
            Where-Object { $_.Name -like "pid_${id}_*" -and $_.Name -like "*engtype_3D*" }
        if ($g) { $o.gpu = [double](($g | Measure-Object UtilizationPercentage -Sum).Sum) }
    } catch {}
    return $o
}
function Mittel95($werte) {
    $w = @($werte | Where-Object { $null -ne $_ } | Sort-Object)
    if ($w.Count -eq 0) { return @{ m = $null; p95 = $null; n = 0 } }
    $m = ($w | Measure-Object -Average).Average
    $i = [math]::Min($w.Count - 1, [int][math]::Ceiling(0.95 * $w.Count) - 1)
    return @{ m = $m; p95 = $w[$i]; n = $w.Count }
}
function Lauf([string]$kennung, [string[]]$spielArgs, [int]$sekunden) {
    $log = Join-Path $Ausgabe "$kennung.crc"
    Remove-Item $log, "$log.frames.csv", "$log.ticks.csv" -ErrorAction SilentlyContinue
    $alle = @("-win", "-xres", $Breite, "-yres", $Hoehe, "-nologo", "-noaudio", "-ignoreAsserts", "-mehrfach",
              "-crcLog", $log) + $spielArgs
    $start = Get-Date
    $p = Start-Process -FilePath $exe -ArgumentList $alle -WorkingDirectory $Spielordner -PassThru
    $logisch = [math]::Max(1, [int]$cpu.NumberOfLogicalProcessors)
    $proben = @()
    $abgelaufen = $false
    while (-not $p.WaitForExit(3000)) {
        $t = ((Get-Date) - $start).TotalSeconds
        if ($t -gt $sekunden) { $abgelaufen = $true; break }
        if ($t -gt 15) { $proben += Probe $p.Id "" $logisch }
    }
    $c = Mittel95 ($proben | ForEach-Object { $_.cpu })
    $sp = Mittel95 ($proben | ForEach-Object { $_.spiel })
    $gp = Mittel95 ($proben | ForEach-Object { $_.gpu })
    $auslastung.Add([pscustomobject]@{ lauf = $kennung; cpu = $c; spiel = $sp; gpu = $gp })
    if ($abgelaufen) {
        try { $p.Kill() } catch {}
        return @{ ok = $false; grund = "Zeitgrenze von $sekunden s ueberschritten (eigener Prozess beendet)"; log = $log; dauer = ((Get-Date) - $start).TotalSeconds }
    }
    return @{ ok = ($p.ExitCode -eq 0); grund = "Exit $($p.ExitCode)"; log = $log; dauer = ((Get-Date) - $start).TotalSeconds }
}
function Werte([string]$log, [string]$marke) {
    if (-not (Test-Path $log)) { return $null }
    $z = Select-String -Path $log -Pattern "^$marke " -Encoding Default | Select-Object -Last 1
    if (-not $z) { return $null }
    if ($z.Line -match "mean ([\d.]+) p50 ([\d.]+) p95 ([\d.]+) p99 ([\d.]+) max ([\d.]+)") {
        return @{ mean = [double]$Matches[1]; p50 = [double]$Matches[2]; p95 = [double]$Matches[3]; p99 = [double]$Matches[4]; max = [double]$Matches[5] }
    }
    return $null
}
function Info([string]$log, [string]$marke) {
    if (-not (Test-Path $log)) { return "" }
    $z = Select-String -Path $log -Pattern "^$marke " -Encoding Default | Select-Object -First 1
    if ($z) { return $z.Line } else { return "" }
}
function GpuMs([string]$log, [string]$bereich) {
	if (-not (Test-Path $log)) { return $null }
	$z = Select-String -Path $log -Pattern "^GPUEFFEKTE " -Encoding Default | Select-Object -Last 1
	if ($z -and $z.Line -match " $bereich ([\d.]+) ms") { return [double]$Matches[1] }
	return $null
}
function Absturz($seit) {
    $d = Join-Path $Spielordner "Crashes"
    if (-not (Test-Path $d)) { return @() }
    return @(Get-ChildItem $d -Directory | Where-Object { $_.LastWriteTime -ge $seit })
}

$seit = Get-Date
$ergebnisse = @()
if ($Effekte -or $Schalter -or $Aufloesung -or $Renderskalierung) { $Stufen = @(); $Arbeiter = @() }

# ---- 2. Starttest
Zeile ""
Zeile "== Starttest"
$r = Lauf "start" @("-quitAfter", "60") 180
Zeile ("Start: {0}, {1:N0} s" -f $r.grund, $r.dauer)
foreach ($m in @("TEXTURESTUFE", "VIDEOMEM", "WORKERS", "D3D11EFFECTS")) { $i = Info $r.log $m; if ($i) { Zeile $i } }

# ---- 3. Laeufe je Stufe
foreach ($stufe in $Stufen) {
    Zeile ""
    Zeile "== Stufe $stufe"
    $echt = Lauf "echtzeit-$stufe" @("-replay", $replay, "-staticLOD", $stufe, "-noVSync", "-quitAfter", "1800") 300
    $e = Werte $echt.log "FRAMES"
    $fx = Info $echt.log "D3D11EFFECTS"
    if ($e) { Zeile ("echtzeit: mittel {0:N1} ms (= {1:N0} Bilder/s), p95 {2:N1} ms, p99 {3:N1} ms, max {4:N0} ms; {5}" -f $e.mean, (1000 / $e.mean), $e.p95, $e.p99, $e.max, $echt.grund) }
    else { Zeile "echtzeit: keine Messwerte ($($echt.grund))" }
    $ge = $null
    if (-not $Schnell) {
        $gef = Lauf "gefecht-$stufe" @("-replay", $replay, "-staticLOD", $stufe, "-fixedTimeStep", "-noVSync", "-cameraPath", $fahrt,
                                       "-shotDir", (Join-Path $Ausgabe "bilder-$stufe"), "-shotEvery", "300", "-quitAfter", "$gefechtFrames") 1800
        $ge = Werte $gef.log "FRAMES"
        if ($ge) { Zeile ("gefecht: mittel {0:N2} ms je Bild, p95 {1:N2} ms, p99 {2:N2} ms; {3:N0} s Laufzeit; {4}" -f $ge.mean, $ge.p95, $ge.p99, $gef.dauer, $gef.grund) }
        else { Zeile "gefecht: keine Messwerte ($($gef.grund))" }
    }
    $ergebnisse += [pscustomobject]@{ stufe = $stufe; echt = $e; gefecht = $ge; effekte = $fx }
}

# ---- 4. Arbeiterzahl
$arbeiterErgebnisse = @()
if ($Arbeiter.Count -gt 0) {
    Zeile ""
    Zeile "== Arbeiterzahl (Stufe $ArbeiterStufe)"
    foreach ($n in $Arbeiter) {
        if ($Schnell) {
            $a = Lauf "arbeiter-$n" @("-replay", $replay, "-staticLOD", $ArbeiterStufe, "-noVSync", "-workers", "$n", "-quitAfter", "1800") 300
        } else {
            $a = Lauf "arbeiter-$n" @("-replay", $replay, "-staticLOD", $ArbeiterStufe, "-fixedTimeStep", "-noVSync", "-workers", "$n",
                                      "-cameraPath", $fahrt, "-quitAfter", "$gefechtFrames") 1800
        }
        $aw = Werte $a.log "FRAMES"
        $wz = Info $a.log "WORKERS"
        if ($aw) { Zeile ("Arbeiter {0}: mittel {1:N2} ms je Bild, p95 {2:N2} ms, p99 {3:N2} ms; {4:N0} s Laufzeit; {5}; {6}" -f $n, $aw.mean, $aw.p95, $aw.p99, $a.dauer, $a.grund, $wz) }
        else { Zeile "Arbeiter ${n}: keine Messwerte ($($a.grund))" }
        $arbeiterErgebnisse += [pscustomobject]@{ n = $n; w = $aw }
    }
}

# ---- 4b. Effektlauf
$effektErgebnisse = @()
if ($Effekte) {
    Zeile ""
    Zeile "== Effektlauf (Stufe $ArbeiterStufe, halber Gefechtsabschnitt)"
    $basis = @("fxaa", "sharpen", "softparticles", "colorgrade", "particlelight", "particleground")
    $varianten = @(@{ n = "Stufe unveraendert"; a = @() }, @{ n = "ohne alle Effekte"; a = @("-d3d11Effects", "none") })
    foreach ($e in $basis) {
        $rest = ($basis | Where-Object { $_ -ne $e }) -join ","
        $varianten += @{ n = "ohne $e"; a = @("-d3d11Effects", $rest) }
    }
    $nr = 0
    foreach ($v in $varianten) {
        $nr++
        $a = Lauf "effekt-$nr" (@("-replay", $replay, "-staticLOD", $ArbeiterStufe, "-fixedTimeStep", "-noVSync", "-cameraPath", $fahrt,
                                  "-quitAfter", "$([int]($gefechtFrames / 2))") + $v.a) 1800
        $aw = Werte $a.log "FRAMES"
        if ($aw) { Zeile ("{0}: mittel {1:N2} ms je Bild, p95 {2:N2} ms; {3:N0} s; {4}" -f $v.n, $aw.mean, $aw.p95, $a.dauer, $a.grund) }
        else { Zeile "$($v.n): keine Messwerte ($($a.grund))" }
        $effektErgebnisse += [pscustomobject]@{ n = $v.n; w = $aw; lauf = "effekt-$nr" }
    }
}

# ---- 4c. Schalterlauf
$schalterErgebnisse = @()
if ($Schalter) {
    Zeile ""
    Zeile "== Schalterlauf (Stufe $ArbeiterStufe, halber Gefechtsabschnitt)"
    $iniPfad = Join-Path $Spielordner "ProjectData\INI\GameLODOverride.ini"
    if (-not (Test-Path $iniPfad)) { throw "GameLODOverride.ini nicht gefunden: $iniPfad" }
    $latin1 = [System.Text.Encoding]::GetEncoding(28591)
    $original = [System.IO.File]::ReadAllBytes($iniPfad)
    $text = $latin1.GetString($original)
    function IniVariante([string]$ini, [string]$stufe, [hashtable]$werte) {
        $m = [regex]::Match($ini, "(?ms)^StaticGameLOD\s*=\s*$stufe\b.*?(?=^StaticGameLOD\s*=|\z)")
        if (-not $m.Success) { throw "Stufe $stufe nicht in der INI" }
        $block = $m.Value
        foreach ($k in $werte.Keys) {
            $re = "(?m)^(\s*$k\s*=\s*).*?(\r?)$"
            if (-not [regex]::IsMatch($block, $re)) { throw "Eintrag $k fehlt in Stufe $stufe" }
            $block = [regex]::Replace($block, $re, { param($mm) $mm.Groups[1].Value + $werte[$k] + $mm.Groups[2].Value })
        }
        return $ini.Substring(0, $m.Index) + $block + $ini.Substring($m.Index + $m.Length)
    }
    $varianten = @(
        @{ n = "Stufe unveraendert"; w = @{} },
        @{ n = "ohne Wolkenschatten (UseCloudMap)"; w = @{ UseCloudMap = "No" } },
        @{ n = "ohne Lichtkarte (UseLightMap)"; w = @{ UseLightMap = "No" } },
        @{ n = "Partikel 1000 (MaxParticleCount)"; w = @{ MaxParticleCount = "1000" } },
        @{ n = "ohne Schattendecals (UseShadowDecals)"; w = @{ UseShadowDecals = "No" } },
        @{ n = "Anisotropie 1 (TerrainAnisotropy)"; w = @{ TerrainAnisotropy = "1" } },
        @{ n = "ohne Baumbewegung (UseTreeSway)"; w = @{ UseTreeSway = "No" } },
        @{ n = "ohne weiche Wasserkanten (ShowSoftWaterEdge)"; w = @{ ShowSoftWaterEdge = "No" } },
        @{ n = "Panzerspuren 20 (MaxTankTrackEdges)"; w = @{ MaxTankTrackEdges = "20" } },
        @{ n = "alle zusammen"; w = @{ UseCloudMap = "No"; UseLightMap = "No"; MaxParticleCount = "1000"; UseShadowDecals = "No";
                                     TerrainAnisotropy = "1"; UseTreeSway = "No"; ShowSoftWaterEdge = "No"; MaxTankTrackEdges = "20" } }
    )
    try {
        $nr = 0
        foreach ($v in $varianten) {
            $nr++
            [System.IO.File]::WriteAllBytes($iniPfad, $latin1.GetBytes((IniVariante $text $ArbeiterStufe $v.w)))
            $a = Lauf "schalter-$nr" @("-replay", $replay, "-staticLOD", $ArbeiterStufe, "-fixedTimeStep", "-noVSync", "-cameraPath", $fahrt,
                                       "-quitAfter", "$([int]($gefechtFrames / 2))") 1800
            $aw = Werte $a.log "FRAMES"
            if ($aw) { Zeile ("{0}: mittel {1:N2} ms je Bild, p95 {2:N2} ms; {3:N0} s; {4}" -f $v.n, $aw.mean, $aw.p95, $a.dauer, $a.grund) }
            else { Zeile "$($v.n): keine Messwerte ($($a.grund))" }
            $schalterErgebnisse += [pscustomobject]@{ n = $v.n; w = $aw; lauf = "schalter-$nr" }
        }
    } finally {
        [System.IO.File]::WriteAllBytes($iniPfad, $original)
        Zeile "GameLODOverride.ini wiederhergestellt."
    }
}

# ---- 4d. Aufloesungslauf
$aufErgebnisse = @()
if ($Aufloesung) {
    Zeile ""
    Zeile "== Aufloesungslauf (Stufe $ArbeiterStufe, halber Gefechtsabschnitt)"
    $merkB = $Breite; $merkH = $Hoehe
    try {
        $nr = 0
        foreach ($r in @(@(1280, 720), @(1600, 900), @(1920, 1080))) {
            $nr++
            $Breite = $r[0]; $Hoehe = $r[1]
            $a = Lauf "aufloesung-$nr" @("-replay", $replay, "-staticLOD", $ArbeiterStufe, "-fixedTimeStep", "-noVSync", "-cameraPath", $fahrt,
                                         "-quitAfter", "$([int]($gefechtFrames / 2))") 1800
            $aw = Werte $a.log "FRAMES"
            $name = "$($r[0]) x $($r[1])"
            if ($aw) { Zeile ("{0}: mittel {1:N2} ms je Bild, p95 {2:N2} ms; {3:N0} s; {4}" -f $name, $aw.mean, $aw.p95, $a.dauer, $a.grund) }
            else { Zeile "${name}: keine Messwerte ($($a.grund))" }
            $aufErgebnisse += [pscustomobject]@{ n = $name; w = $aw; lauf = "aufloesung-$nr" }
        }
    } finally { $Breite = $merkB; $Hoehe = $merkH }
}

# ---- 4e. Renderskalierungslauf
$rsErgebnisse = @()
if ($Renderskalierung) {
	Zeile ""
	Zeile "== Renderskalierungslauf (Stufe $ArbeiterStufe, halber Gefechtsabschnitt)"
	$merkB = $Breite; $merkH = $Hoehe
	$kleinster = ($Anteile | Where-Object { $_ -lt 0.999 } | Sort-Object | Select-Object -First 1)
	$plan = @()
	foreach ($r in @(@(1280, 720), @(1600, 900), @(1920, 1080))) {
		foreach ($s in $Anteile) { $plan += @{ b = $r[0]; h = $r[1]; s = $s; f = "bicubic" } }
	}
	if ($kleinster) {
		foreach ($f in @("bilinear", "cas")) { $plan += @{ b = 1920; h = 1080; s = $kleinster; f = $f } }
	}
	try {
		$nr = 0
		foreach ($p in $plan) {
			$nr++
			$Breite = $p.b; $Hoehe = $p.h
			$anteil = $p.s.ToString([System.Globalization.CultureInfo]::InvariantCulture)
			$a = Lauf "rs-$nr" @("-replay", $replay, "-staticLOD", $ArbeiterStufe, "-fixedTimeStep", "-noVSync", "-cameraPath", $fahrt,
								 "-renderScale", $anteil, "-renderScaleFilter", $p.f, "-gpuTiming",
								 "-shotDir", (Join-Path $Ausgabe "bilder-rs-$nr"), "-shotEvery", "$([int]($gefechtFrames / 4))",
								 "-quitAfter", "$([int]($gefechtFrames / 2))") 1800
			$aw = Werte $a.log "FRAMES"
			$g = @{ szene = (GpuMs $a.log "szene"); hoch = (GpuMs $a.log "hochskalieren"); bild = (GpuMs $a.log "bild") }
			$name = "$($p.b) x $($p.h), Anteil $anteil, $($p.f)"
			if ($aw) { Zeile ("{0}: mittel {1:N2} ms je Bild, p95 {2:N2} ms; GPU Szene {3} ms, Hochskalieren {4} ms, Bild {5} ms; {6:N0} s; {7}" -f $name, $aw.mean, $aw.p95, $g.szene, $g.hoch, $g.bild, $a.dauer, $a.grund) }
			else { Zeile "${name}: keine Messwerte ($($a.grund))" }
			$rsErgebnisse += [pscustomobject]@{ n = $name; w = $aw; g = $g; lauf = "rs-$nr" }
		}
	} finally { $Breite = $merkB; $Hoehe = $merkH }
}

# ---- 5. Absturzberichte und Zusammenfassung
$abst = Absturz $seit
Zeile ""
if ($abst.Count -gt 0) {
    Zeile "ACHTUNG: $($abst.Count) Absturzbericht(e) waehrend des Tests: $($abst.Name -join ', ')"
    foreach ($a in $abst) { Copy-Item $a.FullName (Join-Path $Ausgabe ("Absturz-" + $a.Name)) -Recurse -ErrorAction SilentlyContinue }
} else { Zeile "Keine Absturzberichte." }

$md = @("# Hardwaretest $env:COMPUTERNAME ($(Get-Date -Format 'yyyy-MM-dd HH:mm'))", "")
$md += $sys | ForEach-Object { "- $_" }
$md += ""
$md += "| Stufe | Echtzeit mittel (ms) | Bilder/s | p95 | p99 | Gefecht mittel (ms) | Gefecht p95 | Effekte |"
$md += "|---|---|---|---|---|---|---|---|"
foreach ($x in $ergebnisse) {
    $em = if ($x.echt) { "{0:N1}" -f $x.echt.mean } else { "-" }
    $fps = if ($x.echt) { "{0:N0}" -f (1000 / $x.echt.mean) } else { "-" }
    $e95 = if ($x.echt) { "{0:N1}" -f $x.echt.p95 } else { "-" }
    $e99 = if ($x.echt) { "{0:N1}" -f $x.echt.p99 } else { "-" }
    $gm = if ($x.gefecht) { "{0:N2}" -f $x.gefecht.mean } else { "-" }
    $g95 = if ($x.gefecht) { "{0:N2}" -f $x.gefecht.p95 } else { "-" }
    $fl = if ($x.effekte -match "flags (0x[0-9A-Fa-f]+)") { $Matches[1] } else { "-" }
    $md += "| $($x.stufe) | $em | $fps | $e95 | $e99 | $gm | $g95 | $fl |"
}
if ($arbeiterErgebnisse.Count -gt 0) {
    $md += ""
    $md += "Arbeiterzahl (Stufe $ArbeiterStufe; Automatik waehlt logische Prozessoren minus 2, mindestens 1; 0 = seriell):"
    $md += ""
    $md += "| Arbeiter | mittel (ms) | p95 | p99 |"
    $md += "|---|---|---|---|"
    foreach ($x in $arbeiterErgebnisse) {
        if ($x.w) { $md += ("| {0} | {1:N2} | {2:N2} | {3:N2} |" -f $x.n, $x.w.mean, $x.w.p95, $x.w.p99) }
        else { $md += "| $($x.n) | - | - | - |" }
    }
}
if ($effektErgebnisse.Count -gt 0) {
    $md += ""
    $md += "Effektlauf (Stufe $ArbeiterStufe, halber Gefechtsabschnitt, fester Zeitschritt):"
    $md += ""
    $md += "| Variante | mittel (ms) | p95 | Lauf |"
    $md += "|---|---|---|---|"
    foreach ($x in $effektErgebnisse) {
        if ($x.w) { $md += ("| {0} | {1:N2} | {2:N2} | {3} |" -f $x.n, $x.w.mean, $x.w.p95, $x.lauf) }
        else { $md += "| $($x.n) | - | - | $($x.lauf) |" }
    }
}
if ($schalterErgebnisse.Count -gt 0) {
    $md += ""
    $md += "Schalterlauf (Stufe $ArbeiterStufe, halber Gefechtsabschnitt, fester Zeitschritt):"
    $md += ""
    $md += "| Variante | mittel (ms) | p95 | Lauf |"
    $md += "|---|---|---|---|"
    foreach ($x in $schalterErgebnisse) {
        if ($x.w) { $md += ("| {0} | {1:N2} | {2:N2} | {3} |" -f $x.n, $x.w.mean, $x.w.p95, $x.lauf) }
        else { $md += "| $($x.n) | - | - | $($x.lauf) |" }
    }
}
if ($aufErgebnisse.Count -gt 0) {
    $md += ""
    $md += "Aufloesungslauf (Stufe $ArbeiterStufe, halber Gefechtsabschnitt, fester Zeitschritt):"
    $md += ""
    $md += "| Aufloesung | mittel (ms) | p95 | Lauf |"
    $md += "|---|---|---|---|"
    foreach ($x in $aufErgebnisse) {
        if ($x.w) { $md += ("| {0} | {1:N2} | {2:N2} | {3} |" -f $x.n, $x.w.mean, $x.w.p95, $x.lauf) }
        else { $md += "| $($x.n) | - | - | $($x.lauf) |" }
    }
}
$md += ""
if ($rsErgebnisse.Count -gt 0) {
	$md += ""
	$md += "Renderskalierungslauf (Stufe $ArbeiterStufe, halber Gefechtsabschnitt, fester Zeitschritt; GPU-Zeiten aus den Zeitmarken, Mittel je Bild):"
	$md += ""
	$md += "| Aufloesung, Anteil, Filter | mittel (ms) | p95 | GPU Szene (ms) | GPU Hochskalieren (ms) | GPU Bild (ms) | Lauf |"
	$md += "|---|---|---|---|---|---|---|"
	foreach ($x in $rsErgebnisse) {
		$gs = if ($null -ne $x.g.szene) { "{0:N2}" -f $x.g.szene } else { "-" }
		$gh = if ($null -ne $x.g.hoch) { "{0:N2}" -f $x.g.hoch } else { "-" }
		$gb = if ($null -ne $x.g.bild) { "{0:N2}" -f $x.g.bild } else { "-" }
		if ($x.w) { $md += ("| {0} | {1:N2} | {2:N2} | {3} | {4} | {5} | {6} |" -f $x.n, $x.w.mean, $x.w.p95, $gs, $gh, $gb, $x.lauf) }
		else { $md += "| $($x.n) | - | - | $gs | $gh | $gb | $($x.lauf) |" }
	}
}
$md += "Auslastung je Lauf (Mittel / p95 in Prozent, Proben alle 3 s ab Sekunde 15; Spiel-CPU in Prozent aller logischen Prozessoren, 25 % bei 4 Threads = ein Thread voll; GPU = 3D-Engine des Spielprozesses):"
$md += ""
$md += "| Lauf | CPU gesamt | Spiel-CPU | GPU | Proben |"
$md += "|---|---|---|---|---|"
function Zelle($x) { if ($null -eq $x.m) { return "-" } else { return ("{0:N0} / {1:N0}" -f $x.m, $x.p95) } }
foreach ($a in $auslastung) {
    $md += "| $($a.lauf) | $(Zelle $a.cpu) | $(Zelle $a.spiel) | $(Zelle $a.gpu) | $($a.cpu.n) |"
}
$md += ""
$md += "Ablauf im Einzelnen:"
$md += ""
$md += ($bericht | ForEach-Object { "    $_" })
$md | Set-Content -Encoding UTF8 (Join-Path $Ausgabe "summary.md")
Write-Host ""
Write-Host "Fertig. Ergebnis: $Ausgabe  (summary.md, system.txt, *.crc)"
