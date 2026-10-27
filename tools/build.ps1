# MatrixGame - licensed under GPLv2 or any later version.
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [ValidateSet('MinGW', 'MSVC')][string]$Compiler = 'MinGW',
    [switch]$DLL,
    [switch]$Cheats,
    [switch]$Trace,
    [switch]$WithoutResources,
    [switch]$WithoutTests,
    [switch]$TestsOnly,
    [switch]$Coverage,
    [switch]$WithoutMusic,
    [string]$ToolchainRoot = '',
    [string]$CMakeRoot = '',
    [int]$Jobs = 4
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $ToolchainRoot) { $ToolchainRoot = Join-Path $repoRoot '.tools/winlibs-13.2.0/mingw32' }
$ToolchainRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ToolchainRoot)
$toolBin = Join-Path $ToolchainRoot 'bin'
if (-not $CMakeRoot) { $CMakeRoot = Join-Path $repoRoot '.tools/cmake-4.4.4-windows-i386' }
$CMakeRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($CMakeRoot)
$cmake = Join-Path $CMakeRoot 'bin/cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) {
    throw 'Pinned tools are missing. Run tools/setup-toolchain.ps1 first.'
}
if ($Jobs -lt 1) { throw 'Jobs must be a positive number.' }
if ($TestsOnly -and $WithoutTests) { throw 'TestsOnly and WithoutTests cannot be combined.' }
if ($Coverage -and $Compiler -ne 'MinGW') { throw 'Coverage requires the pinned MinGW toolchain and its gcov.' }

$kind = if ($DLL) { 'dll' } else { 'exe' }
$buildName = "{0}-{1}-{2}" -f $Compiler.ToLowerInvariant(), $Configuration.ToLowerInvariant(), $kind
# Instrumented objects stay in their own directory so ordinary builds are unaffected.
if ($Coverage) { $buildName += '-coverage' }
if ($Trace) { $buildName += '-trace' }
$buildRoot = Join-Path $repoRoot "build/$buildName"
$dllOption = if ($DLL) { 'ON' } else { 'OFF' }
$cheatsOption = if ($Cheats) { 'ON' } else { 'OFF' }
$testingOption = if ($WithoutTests) { 'OFF' } else { 'ON' }
# The DLL's host owns music; a standalone build can also leave out libogg and libvorbis.
$musicOption = if ($DLL -or $WithoutMusic) { 'OFF' } else { 'ON' }
$configureArgs = @(
    '-S', $repoRoot, '-B', $buildRoot,
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DMATRIXGAME_BUILD_DLL=$dllOption",
    "-DMATRIXGAME_CHEATS=$cheatsOption",
    "-DMATRIXGAME_TRACE=$(if ($Trace) { 'ON' } else { 'OFF' })",
    "-DMATRIXGAME_MUSIC=$musicOption",
    "-DBUILD_TESTING=$testingOption",
    "-DMATRIXGAME_COVERAGE=$(if ($Coverage) { 'ON' } else { 'OFF' })"
)
if (-not $DLL) { $configureArgs += '-DMATRIXGAME_PKG_BRING_FROM_GAME=OFF' }

$savedPath = $env:PATH
try {
    if ($Compiler -eq 'MinGW') {
        $env:PATH = "$toolBin;$savedPath"
        $target = & (Join-Path $toolBin 'gcc.exe') -dumpmachine
        if ($LASTEXITCODE -ne 0 -or $target -ne 'i686-w64-mingw32') {
            throw "Expected an x86 MinGW compiler, received: $target"
        }
        $configureArgs += @('-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$toolBin/ninja.exe")
        $compilerBin = $toolBin.Replace('\', '/')
        $cachePath = Join-Path $buildRoot 'CMakeCache.txt'
        $storedCompiler = if (Test-Path -LiteralPath $cachePath) {
            Select-String -LiteralPath $cachePath -Pattern '^CMAKE_CXX_COMPILER:[^=]+=(.*)$'
        }
        if ($storedCompiler -and $storedCompiler.Matches[0].Groups[1].Value.Replace('\', '/') -ne "$compilerBin/g++.exe") {
            # CMake's automatic compiler change discards options and dependency install prefixes.
            $allowedRoot = [IO.Path]::GetFullPath($buildRoot) + [IO.Path]::DirectorySeparatorChar
            foreach ($relative in @('', 'zlib/src/zlib-external-build', 'libpng/src/libpng-external-build', 'libogg/src/libogg-external-build', 'libvorbis/src/libvorbis-external-build')) {
                $configurationRoot = [IO.Path]::GetFullPath((Join-Path $buildRoot $relative))
                $targetCache = Join-Path $configurationRoot 'CMakeCache.txt'
                if (-not $targetCache.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Compiler cache migration left the selected build directory.' }
                if (Test-Path -LiteralPath $targetCache) { Remove-Item -LiteralPath $targetCache }
                $informationRoot = Join-Path $configurationRoot 'CMakeFiles'
                if (Test-Path -LiteralPath $informationRoot) {
                    foreach ($version in Get-ChildItem -LiteralPath $informationRoot -Directory | Where-Object Name -Match '^\d+\.\d+') {
                        foreach ($information in @('CMakeCCompiler.cmake', 'CMakeCXXCompiler.cmake', 'CMakeASMCompiler.cmake')) {
                            $target = Join-Path $version.FullName $information
                            if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target }
                        }
                    }
                }
            }
        }
        $configureArgs += @(
            "-DCMAKE_C_COMPILER=$compilerBin/gcc.exe",
            "-DCMAKE_CXX_COMPILER=$compilerBin/g++.exe"
        )
    } else {
        $configureArgs += @('-G', 'Visual Studio 17 2022', '-A', 'Win32')
    }

    & $cmake @configureArgs
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    $buildArgs = @('--build', $buildRoot, '--config', $Configuration, '--parallel', $Jobs)
    if ($TestsOnly) { $buildArgs += @('--target', 'matrixgame_tests') }
    & $cmake @buildArgs
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
} finally {
    $env:PATH = $savedPath
}

$gameRoot = Join-Path $buildRoot 'MatrixGame'
$binaryRoot = if ($Compiler -eq 'MSVC') { Join-Path $gameRoot $Configuration } else { $gameRoot }
if ($Compiler -eq 'MinGW' -and (-not $TestsOnly -or $DLL)) {
    # Debug builds import compiler runtimes. Stage their transitive dependencies
    # so starting from Explorer does not accidentally load an installed x64 DLL.
    $pendingBinaries = New-Object 'System.Collections.Generic.Queue[string]'
    $seenBinaries = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    $extension = if ($DLL) { 'dll' } else { 'exe' }
    $pendingBinaries.Enqueue((Join-Path $binaryRoot "MatrixGame.$extension"))
    while ($pendingBinaries.Count) {
        $binary = $pendingBinaries.Dequeue()
        if (-not $seenBinaries.Add($binary)) { continue }
        $imports = & (Join-Path $toolBin 'objdump.exe') -p $binary
        if ($LASTEXITCODE -ne 0) { throw "Cannot inspect runtime imports: $binary" }
        foreach ($import in $imports) {
            if ($import -match 'DLL Name:\s+([A-Za-z0-9_+.-]+)') {
                $runtimeName = $Matches[1]
                $runtimeSource = Join-Path $toolBin $runtimeName
                if (Test-Path -LiteralPath $runtimeSource) {
                    $runtimeTarget = Join-Path $binaryRoot $runtimeName
                    Copy-Item -LiteralPath $runtimeSource -Destination $runtimeTarget -Force
                    $pendingBinaries.Enqueue($runtimeTarget)
                }
            }
        }
    }
}
if ($TestsOnly) {
    # DLL host tests load the actual library and need its imported runtimes.
    Write-Output "Test build output: $buildRoot/tests"
    return
}
if (-not $DLL) {
    # Refresh tracked configuration, including for existing build directories.
    # Multi-configuration generators place the EXE below the CMake CFG directory.
    Copy-Item -LiteralPath (Join-Path $repoRoot 'MatrixGame/CFG') -Destination $binaryRoot -Recurse -Force
    $dataRoot = Join-Path $binaryRoot 'DATA'
    $configRoot = Join-Path $binaryRoot 'CFG'
    New-Item -ItemType Directory -Path $dataRoot -Force | Out-Null
    if (-not $WithoutResources) {
        foreach ($resource in @(
            @{Name = 'robots.pkg'; Destination = $dataRoot},
            @{Name = 'robots.dat'; Destination = $configRoot},
            @{Name = 'sound.pkg'; Destination = $dataRoot},
            @{Name = 'voices.pkg'; Destination = $dataRoot},
            @{Name = 'music.pkg'; Destination = $dataRoot},
            @{Name = 'sounds.txt'; Destination = $configRoot},
            @{Name = 'Lang.dat'; Destination = $configRoot}
        )) {
            $source = Join-Path $repoRoot $resource.Name
            if (Test-Path -LiteralPath $source) {
                Copy-Item -LiteralPath $source -Destination (Join-Path $resource.Destination $resource.Name) -Force
                Write-Output "Local $($resource.Name) staged for playtesting."
            }
        }
    }
    if ($WithoutResources) {
        Write-Output 'Local resource staging skipped; any existing output resources are retained.'
    } elseif (-not (Test-Path -LiteralPath (Join-Path $dataRoot 'robots.pkg'))) {
        Write-Output 'Game resources are missing; provide robots.pkg locally to play.'
    }
}
Write-Output "Build output: $binaryRoot"
