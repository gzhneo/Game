# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
#
# Legt den Quelltext der FFmpeg-DLLs fuer eine Veroeffentlichung ab (LGPL 2.1: der Quelltext gehoert an
# denselben Ort wie das Setup):
#   ffmpeg-<ref>-source.tar.gz         das FFmpeg-Release, das vcpkg gebaut hat
#   ffmpeg-vcpkg-port-<version>.zip    der vcpkg-Port mit seinen Patches und Bauoptionen
# Version und Quelle stehen in vcpkgs SPDX-Datei des Builds; der Port kommt aus dem vcpkg-Stand der
# builtin-baseline in vcpkg.json. Das Skript prueft, dass der Port dort genau die gebaute Version ist.
#
# Aufruf: tools\release\ffmpeg_quelle.ps1 -Ziel <Ordner> [-Installiert build-x64\vcpkg_installed\x64-windows]

param(
	[Parameter(Mandatory = $true)][string]$Ziel,
	[string]$Installiert = "build-x64\vcpkg_installed\x64-windows"
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

$spdxDatei = Join-Path $Installiert "share\ffmpeg\vcpkg.spdx.json"
if (-not (Test-Path $spdxDatei)) { throw "SPDX-Datei fehlt: $spdxDatei (erst bauen)" }
$spdx = Get-Content $spdxDatei -Raw | ConvertFrom-Json
$portVersion = ($spdx.packages | Where-Object { $_.name -eq "ffmpeg" }).versionInfo          # etwa 9.0.1#1
$quelle = ($spdx.packages | Where-Object { $_.name -eq "ffmpeg/ffmpeg" }).downloadLocation   # git+https://github.com/ffmpeg/ffmpeg@n9.0.1
if (-not $portVersion -or $quelle -notmatch "github\.com/ffmpeg/ffmpeg@(.+)$") { throw "unerwartete SPDX-Angaben: $portVersion / $quelle" }
$ref = $Matches[1]
$version, $portRevision = $portVersion.Split("#")
if (-not $portRevision) { $portRevision = "0" }

New-Item -ItemType Directory -Force $Ziel | Out-Null
$quellArchiv = Join-Path $Ziel "ffmpeg-$ref-source.tar.gz"
# Direkt von codeload.github.com: github.com/.../archive leitet nur dorthin weiter und antwortet Build-Rechnern zeitweise mit 504.
Invoke-WebRequest -UseBasicParsing "https://codeload.github.com/FFmpeg/FFmpeg/tar.gz/refs/tags/$ref" -OutFile $quellArchiv
"FFmpeg-Quelltext: $quellArchiv ($([int]((Get-Item $quellArchiv).Length / 1MB)) MB)"

# Port aus dem vcpkg-Stand der Baseline: nur dieser eine Commit, flach geholt.
$baseline = (Get-Content (Join-Path $repo "vcpkg.json") -Raw | ConvertFrom-Json).'builtin-baseline'
$arbeit = Join-Path ([IO.Path]::GetTempPath()) "gzh-vcpkg-$baseline"
if (-not (Test-Path (Join-Path $arbeit ".git"))) {
	git init -q $arbeit
	git -C $arbeit remote add origin https://github.com/microsoft/vcpkg.git
}
git -C $arbeit fetch -q --depth 1 origin $baseline
if ($LASTEXITCODE -ne 0) { throw "vcpkg-Commit $baseline nicht abrufbar" }
$eintraege = (git -C $arbeit show "FETCH_HEAD:versions/f-/ffmpeg.json" | Out-String | ConvertFrom-Json).versions
$eintrag = $eintraege | Where-Object {
	$v = if ($_.version) { $_.version } elseif ($_.'version-semver') { $_.'version-semver' } elseif ($_.'version-string') { $_.'version-string' } else { $_.'version-date' }
	$v -eq $version -and [int]$_.'port-version' -eq [int]$portRevision
} | Select-Object -First 1
if (-not $eintrag) { throw "ffmpeg $portVersion steht nicht in der Versionsliste der Baseline $baseline" }
$baum = git -C $arbeit rev-parse "FETCH_HEAD:ports/ffmpeg"
if ($baum -ne $eintrag.'git-tree') {
	throw "ports/ffmpeg der Baseline ($baum) ist nicht der Stand von ffmpeg $portVersion ($($eintrag.'git-tree')); Ueberschreibung im Manifest?"
}
$portArchiv = Join-Path $Ziel "ffmpeg-vcpkg-port-$version-$portRevision.zip"
git -C $arbeit archive --format=zip --prefix "ffmpeg-vcpkg-port/" -o $portArchiv "FETCH_HEAD:ports/ffmpeg"
if ($LASTEXITCODE -ne 0) { throw "Port-Archiv nicht erzeugt" }
"vcpkg-Port: $portArchiv (vcpkg $baseline, ports/ffmpeg $baum)"
