# MatrixGame - licensed under GPLv2 or any later version.
[CmdletBinding()]
param([string]$ArchivePath = '')

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'download-verified.ps1')
$repoRoot = Split-Path -Parent $PSScriptRoot
$toolsRoot = Join-Path $repoRoot '.tools'
$archiveName = 'cmake-4.4.4-windows-i386.zip'
$installRoot = Join-Path $toolsRoot 'cmake-4.4.4-windows-i386'
$archiveUrl = "https://github.com/Kitware/CMake/releases/download/v4.4.4/$archiveName"
$archiveHash = '1A791472619AE85ABF95E3F52838B746A47542EC731A288ABAC2FEE8154D5664'

if (-not (Test-Path -LiteralPath (Join-Path $installRoot 'bin/cmake.exe'))) {
    if (-not $ArchivePath) {
        $ArchivePath = Join-Path $toolsRoot "downloads/$archiveName"
        Save-VerifiedDownload -Uri $archiveUrl -Path $ArchivePath -Sha256 $archiveHash
    }
    $ArchivePath = (Resolve-Path -LiteralPath $ArchivePath).Path
    if ((Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash -ne $archiveHash) {
        throw 'CMake archive checksum mismatch. The archive was not extracted.'
    }
    New-Item -ItemType Directory -Path $toolsRoot -Force | Out-Null
    Expand-Archive -LiteralPath $ArchivePath -DestinationPath $toolsRoot -Force
}

foreach ($program in @('cmake', 'ctest')) {
    $executable = Join-Path $installRoot "bin/$program.exe"
    if (-not (Test-Path -LiteralPath $executable)) { throw "The CMake installation is missing $program." }
    $version = & $executable --version
    if ($LASTEXITCODE -ne 0 -or $version[0] -ne "$program version 4.4.4") {
        throw "The installed $program does not match the pinned version."
    }
}
Write-Output "CMake and CTest ready: $installRoot"
