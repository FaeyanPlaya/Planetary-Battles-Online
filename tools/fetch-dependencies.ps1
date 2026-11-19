# MatrixGame - licensed under GPLv2 or any later version.
# Prepares .tools/downloads with the pinned zlib, libpng, libogg, libvorbis, dr_mp3 and thread-runtime sources
# that CMake and the package helper read, verifying each one before it is kept.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'download-verified.ps1')
$repoRoot = Split-Path -Parent $PSScriptRoot
$downloadRoot = Join-Path $repoRoot '.tools/downloads'

# CMake's ExternalProject reuses a DOWNLOAD_DIR file named after the URL when its hash matches.
$manifest = Get-Content -LiteralPath (Join-Path $repoRoot 'ThirdParty/dependencies.json') -Raw | ConvertFrom-Json
foreach ($name in @('zlib', 'libpng', 'libogg', 'libvorbis', 'dr_mp3')) {
    $dependency = $manifest.$name
    # (a single file is kept under its versioned name: CMake's file(DOWNLOAD) reads it there)
    $fileName = if ($dependency.file) { $dependency.file } else { Split-Path -Leaf ([Uri]$dependency.url).AbsolutePath }
    Save-VerifiedDownload -Uri $dependency.url -Path (Join-Path $downloadRoot $fileName) -Sha256 $dependency.sha256
}

$runtime = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'package-runtime-source.json') -Raw | ConvertFrom-Json
Save-VerifiedDownload -Uri $runtime.url -Path (Join-Path $downloadRoot "mcfgthread-$($runtime.commit).tar.gz") -Sha256 $runtime.sha256
Write-Output "Verified dependency archives ready: $downloadRoot"
