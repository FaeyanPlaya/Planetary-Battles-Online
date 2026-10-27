# MatrixGame - licensed under GPLv2 or any later version.
[CmdletBinding()]
param(
    [string]$ArchivePath = '',
    [string]$CMakeArchivePath = '',
    [switch]$SkipCMake
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'download-verified.ps1')
$repoRoot = Split-Path -Parent $PSScriptRoot
$toolsRoot = Join-Path $repoRoot '.tools'
$installRoot = Join-Path $toolsRoot 'winlibs-13.2.0'
$compilerRoot = Join-Path $installRoot 'mingw32'
$archiveName = 'winlibs-i686-mcf-dwarf-gcc-13.2.0-mingw-w64ucrt-11.0.1-r3.7z'
$archiveUrl = "https://github.com/brechtsanders/winlibs_mingw/releases/download/13.2.0mcf-11.0.1-ucrt-r3/$archiveName"
$archiveHash = 'D4D50D6F1BFAF3309007F197DBBB82E3F968F6208363C1BE2A180E9C914A6511'

if (-not $SkipCMake) {
    & (Join-Path $PSScriptRoot 'setup-cmake.ps1') -ArchivePath $CMakeArchivePath
}

if (Test-Path -LiteralPath (Join-Path $compilerRoot 'bin/g++.exe')) {
    $installedVersion = & (Join-Path $compilerRoot 'bin/g++.exe') -dumpfullversion
    if ($LASTEXITCODE -ne 0 -or $installedVersion -ne '13.2.0') {
        throw 'The installed toolchain does not match the pinned GCC version.'
    }
    Write-Output "Toolchain already installed: $compilerRoot"
    exit 0
}

$extractor = Get-Command 7z -ErrorAction SilentlyContinue
if (-not $extractor) {
    throw '7-Zip is required. Install it and add 7z.exe to PATH.'
}

if (-not $ArchivePath) {
    $ArchivePath = Join-Path $toolsRoot "downloads/$archiveName"
    Write-Output 'Preparing the pinned Windows x86 toolchain archive...'
    Save-VerifiedDownload -Uri $archiveUrl -Path $ArchivePath -Sha256 $archiveHash
}

$ArchivePath = (Resolve-Path -LiteralPath $ArchivePath).Path
if ((Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash -ne $archiveHash) {
    throw 'Toolchain archive checksum mismatch. The archive was not extracted.'
}

New-Item -ItemType Directory -Path $installRoot -Force | Out-Null
& $extractor.Source x $ArchivePath "-o$installRoot" -y -bso0 -bsp0
if ($LASTEXITCODE -ne 0) { throw 'Toolchain extraction failed.' }
foreach ($program in @('gcc.exe', 'g++.exe', 'cmake.exe', 'ctest.exe', 'ninja.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $compilerRoot "bin/$program"))) {
        throw "The downloaded toolchain is missing $program."
    }
}
Write-Output "Toolchain installed: $compilerRoot"
