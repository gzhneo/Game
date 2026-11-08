# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
#
# Download-Skript fuer vcpkg (X_VCPKG_ASSET_SOURCES=x-script,...) im Release-Workflow. Auf den Build-Rechnern von
# GitHub Actions antwortet github.com zeitweise mit 504, waehrend die API erreichbar ist. Das Skript laedt die Datei
# zuerst ueber die angegebene Adresse (mit Wiederholungen) und weicht bei github.com aus:
#   .../releases/download/<tag>/<datei>  -> Asset ueber api.github.com (mit GH_TOKEN, falls gesetzt)
#   .../archive/<ref>.tar.gz bzw. .zip   -> codeload.github.com (dorthin leitet github.com ohnehin weiter)
# Die SHA-512 wird geprueft; Rueckgabewert 0 nur bei passender Datei.
#
# Aufruf: tools\release\vcpkg_download.ps1 <url> <sha512> <ziel>

param(
	[Parameter(Mandatory = $true)][string]$Url,
	[Parameter(Mandatory = $true)][string]$Sha512,
	[Parameter(Mandatory = $true)][string]$Ziel
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

function Kopfzeilen([bool]$api)
{
	$h = @{ "User-Agent" = "gzhneo-release" }
	if ($api) {
		$h["Accept"] = "application/octet-stream"
		if ($env:GH_TOKEN) { $h["Authorization"] = "Bearer $env:GH_TOKEN" }
	}
	return $h
}

function Laden([string]$adresse, [bool]$api)
{
	for ($versuch = 1; $versuch -le 3; $versuch++) {
		try {
			Invoke-WebRequest -Uri $adresse -Headers (Kopfzeilen $api) -OutFile $Ziel -MaximumRedirection 10 -UseBasicParsing
			$ist = (Get-FileHash -Algorithm SHA512 $Ziel).Hash.ToLower()
			if ($ist -eq $Sha512.ToLower()) { return $true }
			Write-Host "vcpkg_download: falsche SHA-512 von $adresse ($ist)"
			return $false
		} catch {
			Write-Host "vcpkg_download: Versuch $versuch fuer $adresse gescheitert: $($_.Exception.Message)"
			Start-Sleep -Seconds (5 * $versuch)
		}
	}
	return $false
}

if (Laden $Url $false) { exit 0 }

if ($Url -match '^https://github\.com/([^/]+)/([^/]+)/releases/download/([^/]+)/([^/?]+)$') {
	$besitzer, $repo, $tag, $datei = $Matches[1], $Matches[2], $Matches[3], $Matches[4]
	$kopf = @{ "User-Agent" = "gzhneo-release"; "Accept" = "application/vnd.github+json" }
	if ($env:GH_TOKEN) { $kopf["Authorization"] = "Bearer $env:GH_TOKEN" }
	try {
		$release = Invoke-RestMethod -Uri "https://api.github.com/repos/$besitzer/$repo/releases/tags/$tag" -Headers $kopf
		$asset = $release.assets | Where-Object name -eq $datei | Select-Object -First 1
		if ($asset -and (Laden $asset.url $true)) { exit 0 }
	} catch {
		Write-Host "vcpkg_download: API fuer $besitzer/$repo $tag gescheitert: $($_.Exception.Message)"
	}
} elseif ($Url -match '^https://github\.com/([^/]+)/([^/]+)/archive/(.+)\.(tar\.gz|zip)$') {
	$art = if ($Matches[4] -eq "zip") { "zip" } else { "tar.gz" }
	if (Laden "https://codeload.github.com/$($Matches[1])/$($Matches[2])/$art/$($Matches[3])" $false) { exit 0 }
}

Remove-Item -Force -ErrorAction SilentlyContinue $Ziel
exit 1
