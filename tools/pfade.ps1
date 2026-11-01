# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Machine paths for the PowerShell tools: sets the GZH_* variables of this process that are still unset,
# from the machine file %LOCALAPPDATA%\GZHNeo\pfade.json (same file as tools/pfade.py). Without that file the
# asset folders are taken from the Data junction of an existing game build folder.
# Use:  . (Join-Path $PSScriptRoot 'pfade.ps1')   # after dot-sourcing, $env:GZH_ASSETS_GENERALS etc. are set if known
$script:GzhNamen = 'GZH_ASSETS_GENERALS', 'GZH_ASSETS_ZH', 'GZH_EA_GENERALS', 'GZH_EA_ZH', 'GZH_WERKZEUGE'
$script:GzhDatei = Join-Path $(if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { Join-Path $HOME 'AppData\Local' }) 'GZHNeo\pfade.json'
if (Test-Path -LiteralPath $script:GzhDatei) {
	try {
		$gzhDaten = Get-Content -LiteralPath $script:GzhDatei -Raw -Encoding UTF8 | ConvertFrom-Json
		foreach ($n in $script:GzhNamen) {
			$w = $gzhDaten.$n
			if ($w -and -not [Environment]::GetEnvironmentVariable($n)) {
				[Environment]::SetEnvironmentVariable($n, ([string]$w).Trim(), 'Process')
			}
		}
	} catch { Write-Warning "pfade.ps1: $($script:GzhDatei) nicht lesbar: $_" }
}
$gzhRepo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
foreach ($gzhP in @(@('GZH_ASSETS_GENERALS', 'Generals'), @('GZH_ASSETS_ZH', 'GeneralsMD'))) {
	if ([Environment]::GetEnvironmentVariable($gzhP[0])) { continue }
	foreach ($cfg in 'Debug', 'Release') {
		$link = Get-Item -LiteralPath (Join-Path $gzhRepo "build-x64\$($gzhP[1])\Code\Main\$cfg\Data") -ErrorAction SilentlyContinue
		if ($link -and $link.LinkType -eq 'Junction' -and $link.Target) {
			[Environment]::SetEnvironmentVariable($gzhP[0], (Split-Path -Parent ([string]$link.Target[0])), 'Process')
			break
		}
	}
}
