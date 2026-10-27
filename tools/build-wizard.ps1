# MatrixGame - licensed under GPLv2 or any later version.
# Guided Windows build started by build.bat: finds or prepares the tools, asks for the
# paths it cannot discover, imports local game resources and runs tools/build.ps1.
[CmdletBinding()]
param(
    [ValidateSet('', 'Debug', 'Release')][string]$Configuration = '',
    [ValidateSet('', 'MinGW', 'MSVC')][string]$Compiler = '',
    [switch]$NonInteractive,
    [switch]$Check,
    [switch]$Reconfigure,
    [switch]$Reset,
    [switch]$Help
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Get-Item -LiteralPath (Split-Path -Parent $PSScriptRoot)).FullName
$settingsPath = Join-Path $repoRoot '.local/build-wizard.env'
$pinnedToolchain = Join-Path $repoRoot '.tools/winlibs-13.2.0/mingw32'
$pinnedCMake = Join-Path $repoRoot '.tools/cmake-4.4.4-windows-i386'
$preparedSdk = Join-Path $repoRoot '.tools/directx-sdk/DXSDK'
$preparedRuntime = Join-Path $repoRoot '.tools/directx-x86/d3dx9_43.dll'
$script:Interactive = -not ($NonInteractive -or $Check)
$script:Missing = New-Object 'System.Collections.Generic.List[string]'

function Show-Usage {
    Write-Host @'
Usage: build.bat [-Configuration Debug|Release] [-Compiler MinGW|MSVC] [options]

Without options the wizard asks for the build options, finds or prepares the tools,
offers to import game resources from an installation and builds the game.

  -Reconfigure     Ask every question again, using the saved answers as defaults.
  -NonInteractive  Use saved, discovered or default answers without asking. Pinned
                   tools are downloaded when missing; system software is never installed.
  -Check           Report the environment and saved answers without changing anything.
                   Exits with status 1 when a required component is missing.
  -Reset           Remove the saved answers in .local/build-wizard.env.
  -Help            Show this text.

For scripted or advanced builds, use tools/build.ps1 directly; see docs/BUILD_WINDOWS.md.
'@
}

# Starting from Explorer opens a console that closes when the script ends.
function Test-StartedFromExplorer {
    try {
        $self = Get-CimInstance Win32_Process -Filter "ProcessId=$PID"
        $shell = Get-CimInstance Win32_Process -Filter "ProcessId=$($self.ParentProcessId)"
        if (-not $shell -or $shell.Name -ne 'cmd.exe') { return $false }
        $starter = Get-Process -Id $shell.ParentProcessId -ErrorAction Stop
        return $starter.ProcessName -eq 'explorer'
    } catch {
        return $false
    }
}

# Answers come from the console, or from redirected input when supplied; the end of
# that input turns the remaining questions into their defaults.
function Read-Answer([string]$Prompt, [string]$Default = '') {
    if (-not $script:Interactive) { return $Default }
    $suffix = if ($Default) { " [$Default]" } else { '' }
    Write-Host -NoNewline "$Prompt$suffix`: "
    $line = [Console]::In.ReadLine()
    if ($null -eq $line) {
        Write-Host ''
        $script:Interactive = $false
        return $Default
    }
    $line = $line.Trim()
    if ($line) { return $line }
    return $Default
}

function Read-Choice([string]$Prompt, [string[]]$Choices, [string]$Default) {
    while ($true) {
        $answer = Read-Answer $Prompt $Default
        $matching = @($Choices | Where-Object { $answer -and $_.StartsWith($answer, [StringComparison]::OrdinalIgnoreCase) })
        $exact = @($matching | Where-Object { $_ -eq $answer })
        if ($exact.Count) { return $exact[0] }
        if ($matching.Count -eq 1) { return $matching[0] }
        if (-not $script:Interactive) { return $Default }
        Write-Host "Choose one of: $($Choices -join ', ')."
    }
}

function Read-YesNo([string]$Prompt, [bool]$Default) {
    $answer = Read-Choice $Prompt @('yes', 'no') $(if ($Default) { 'yes' } else { 'no' })
    return $answer -eq 'yes'
}

function Read-Path([string]$Prompt) {
    $answer = Read-Answer $Prompt
    $answer = [Environment]::ExpandEnvironmentVariables($answer.Trim().Trim('"'))
    if (-not $answer) { return '' }
    return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($answer)
}

function Read-ArchivePath([string]$Prompt) {
    $path = Read-Path $Prompt
    if (-not $path -or -not (Test-Path -LiteralPath $path -PathType Leaf)) { throw 'No archive file was supplied.' }
    return $path
}

function Read-Settings {
    $values = @{}
    if (Test-Path -LiteralPath $settingsPath) {
        foreach ($line in [IO.File]::ReadAllLines($settingsPath)) {
            if ($line -match '^\s*([A-Za-z][A-Za-z0-9]*)=(.*)$') { $values[$Matches[1]] = $Matches[2] }
        }
    }
    return $values
}

function Save-Settings([hashtable]$Values) {
    $lines = @('# Saved by build.bat. Run build.bat -Reset to remove these answers.')
    foreach ($key in ($Values.Keys | Sort-Object)) {
        if ($null -ne $Values[$key] -and "$($Values[$key])" -ne '') { $lines += "$key=$($Values[$key])" }
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $settingsPath) -Force | Out-Null
    [IO.File]::WriteAllLines($settingsPath, [string[]]$lines, (New-Object Text.UTF8Encoding $false))
}

# Captures a program's combined output without turning its error stream into PowerShell errors.
function Invoke-Capture([string]$FilePath, [string[]]$Arguments = @()) {
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $FilePath
    $info.Arguments = ($Arguments | ForEach-Object { if ($_ -match '[\s"]') { '"' + $_.Replace('"', '\"') + '"' } else { $_ } }) -join ' '
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    try {
        $process = [Diagnostics.Process]::Start($info)
        $errorTask = $process.StandardError.ReadToEndAsync()
        $output = $process.StandardOutput.ReadToEnd()
        $process.WaitForExit()
        return [pscustomobject]@{ ExitCode = $process.ExitCode; Output = ($output + $errorTask.Result).Trim() }
    } catch {
        return [pscustomobject]@{ ExitCode = -1; Output = $_.Exception.Message }
    }
}

function New-Probe([bool]$Ok, [string]$Value = '', [string]$Summary = '', [string]$Warning = '') {
    return [pscustomobject]@{ Ok = $Ok; Value = $Value; Summary = $Summary; Warning = $Warning }
}

function Show-Probe([string]$Label, $Probe) {
    if ($Probe.Ok -and $Probe.Warning) {
        Write-Host "[WARN] ${Label}: $($Probe.Summary)"
        Write-Host "       $($Probe.Warning)"
    } elseif ($Probe.Ok) {
        Write-Host "[OK]   ${Label}: $($Probe.Summary)"
    } else {
        Write-Host "[FAIL] ${Label}: $($Probe.Summary)"
    }
}

function Get-UniquePaths([string[]]$Paths) {
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    $result = @()
    foreach ($path in $Paths) {
        if (-not $path) { continue }
        try { $full = [IO.Path]::GetFullPath($path).TrimEnd('\') } catch { continue }
        if ($seen.Add($full)) { $result += $full }
    }
    return $result
}

# Accepts an installation root, its bin directory or the named program inside it.
function Resolve-ToolRoot([string]$Candidate, [string]$Program) {
    if (-not $Candidate) { return '' }
    if (Test-Path -LiteralPath $Candidate -PathType Leaf) {
        if ((Split-Path -Leaf $Candidate) -ne $Program) { return '' }
        $Candidate = Split-Path -Parent $Candidate
    }
    if ((Split-Path -Leaf $Candidate) -eq 'bin' -and (Test-Path -LiteralPath (Join-Path $Candidate $Program))) {
        return Split-Path -Parent $Candidate
    }
    return $Candidate
}

function Test-SevenZip([string]$Candidate) {
    if (-not $Candidate) { return New-Probe $false -Summary 'no path supplied' }
    if (Test-Path -LiteralPath $Candidate -PathType Container) { $Candidate = Join-Path $Candidate '7z.exe' }
    if (-not (Test-Path -LiteralPath $Candidate -PathType Leaf)) { return New-Probe $false -Summary "7z.exe was not found at $Candidate" }
    $result = Invoke-Capture $Candidate
    $banner = @($result.Output -split "`r?`n" | Where-Object { $_ -match '7-Zip' } | Select-Object -First 1)
    if (-not $banner) { return New-Probe $false -Summary "$Candidate is not a 7-Zip console program" }
    return New-Probe $true $Candidate "$($banner[0].Trim()) at $Candidate"
}

function Test-MinGW([string]$Candidate) {
    $root = Resolve-ToolRoot $Candidate 'gcc.exe'
    if (-not $root) { return New-Probe $false -Summary 'no compiler path supplied' }
    $bin = Join-Path $root 'bin'
    foreach ($program in @('gcc.exe', 'g++.exe', 'ninja.exe', 'objdump.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $bin $program))) { return New-Probe $false -Summary "$program is missing from $bin" }
    }
    $target = (Invoke-Capture (Join-Path $bin 'gcc.exe') @('-dumpmachine')).Output
    if ($target -ne 'i686-w64-mingw32') { return New-Probe $false -Summary "the compiler targets '$target'; an i686-w64-mingw32 (x86) compiler is required" }
    $version = (Invoke-Capture (Join-Path $bin 'g++.exe') @('-dumpfullversion')).Output
    $warning = if ($version -ne '13.2.0') { 'The pinned toolchain is GCC 13.2.0; other versions are not validated.' } else { '' }
    return New-Probe $true $root "GCC $version (x86) at $root" $warning
}

function Test-CMake([string]$Candidate) {
    $root = Resolve-ToolRoot $Candidate 'cmake.exe'
    if (-not $root) { return New-Probe $false -Summary 'no CMake path supplied' }
    $cmake = Join-Path $root 'bin/cmake.exe'
    if (-not (Test-Path -LiteralPath $cmake)) { return New-Probe $false -Summary "bin/cmake.exe was not found below $root" }
    if (-not (Test-Path -LiteralPath (Join-Path $root 'bin/ctest.exe'))) { return New-Probe $false -Summary "bin/ctest.exe is missing below $root" }
    $text = (Invoke-Capture $cmake @('--version')).Output
    if ($text -notmatch 'cmake version (\d+)\.(\d+)\.(\d+)') { return New-Probe $false -Summary "cannot read the CMake version at $root" }
    $version = [version]"$($Matches[1]).$($Matches[2]).$($Matches[3])"
    if ($version -lt [version]'3.27') { return New-Probe $false -Summary "CMake $version is older than the required 3.27" }
    $warning = if ($version -ne [version]'4.4.4') { 'The pinned CMake is 4.4.4; normal work uses that version.' } else { '' }
    return New-Probe $true $root "CMake $version at $root" $warning
}

function Test-DirectXSdk([string]$Candidate) {
    if (-not $Candidate) { return New-Probe $false -Summary 'no SDK path supplied' }
    foreach ($file in @('Include/d3dx9.h', 'Lib/x86/d3d9.lib', 'Lib/x86/d3dx9.lib', 'Lib/x86/DxErr.lib')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Candidate $file))) { return New-Probe $false -Summary "$file is missing below $Candidate" }
    }
    return New-Probe $true ([IO.Path]::GetFullPath($Candidate).TrimEnd('\')) "DirectX SDK at $Candidate"
}

function Test-Installation([string]$Candidate) {
    if (-not $Candidate) { return New-Probe $false -Summary 'no installation path supplied' }
    if (-not (Test-Path -LiteralPath (Join-Path $Candidate 'DATA/robots.pkg'))) {
        return New-Probe $false -Summary "DATA/robots.pkg was not found below $Candidate"
    }
    return New-Probe $true ([IO.Path]::GetFullPath($Candidate).TrimEnd('\')) "game installation at $Candidate"
}

function Find-SevenZip {
    $candidates = @()
    $command = Get-Command 7z -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { $candidates += $command.Source }
    foreach ($key in @('HKLM:\SOFTWARE\7-Zip', 'HKCU:\SOFTWARE\7-Zip', 'HKLM:\SOFTWARE\WOW6432Node\7-Zip')) {
        $entry = Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
        foreach ($name in @('Path64', 'Path')) {
            if ($entry -and $entry.$name) { $candidates += Join-Path $entry.$name '7z.exe' }
        }
    }
    foreach ($base in @($env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:ProgramW6432)) {
        if ($base) { $candidates += Join-Path $base '7-Zip/7z.exe' }
    }
    return $candidates
}

function Find-Installations {
    $roots = @()
    $steam = Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue
    if ($steam -and $steam.SteamPath) {
        $libraries = @($steam.SteamPath)
        $folders = Join-Path $steam.SteamPath 'steamapps/libraryfolders.vdf'
        if (Test-Path -LiteralPath $folders) {
            foreach ($match in [regex]::Matches([IO.File]::ReadAllText($folders), '"path"\s+"([^"]+)"')) {
                $libraries += $match.Groups[1].Value.Replace('\\', '\')
            }
        }
        foreach ($library in $libraries) {
            $common = Join-Path $library 'steamapps/common'
            if (Test-Path -LiteralPath $common) { $roots += @(Get-ChildItem -LiteralPath $common -Directory -ErrorAction SilentlyContinue | ForEach-Object FullName) }
        }
    }
    foreach ($key in @('HKLM:\SOFTWARE\WOW6432Node\GOG.com\Games', 'HKLM:\SOFTWARE\GOG.com\Games')) {
        foreach ($game in @(Get-ChildItem -LiteralPath $key -ErrorAction SilentlyContinue)) {
            $entry = Get-ItemProperty -LiteralPath $game.PSPath -ErrorAction SilentlyContinue
            if ($entry -and $entry.path) { $roots += $entry.path }
        }
    }
    foreach ($base in @($env:ProgramFiles, ${env:ProgramFiles(x86)}, 'C:\Games', 'C:\GOG Games')) {
        if ($base -and (Test-Path -LiteralPath $base)) {
            $roots += @(Get-ChildItem -LiteralPath $base -Directory -Filter '*Rangers*' -ErrorAction SilentlyContinue | ForEach-Object FullName)
        }
    }
    return @(Get-UniquePaths $roots | Where-Object { (Test-Installation $_).Ok })
}

function Find-VisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { return @() }
    $result = Invoke-Capture $vswhere @('-products', '*', '-format', 'json', '-utf8')
    if ($result.ExitCode -ne 0 -or -not $result.Output) { return @() }
    $parsed = $result.Output | ConvertFrom-Json
    $instances = @($parsed | ForEach-Object { $_ })
    $tools = Invoke-Capture $vswhere @('-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'instanceId')
    $withTools = @($tools.Output -split "`r?`n" | Where-Object { $_ })
    return @($instances | ForEach-Object {
        [pscustomobject]@{
            Name = $_.displayName
            Path = $_.installationPath
            Version = [version]($_.installationVersion -replace '^(\d+\.\d+(\.\d+)?).*$', '$1')
            CppTools = $withTools -contains $_.instanceId
        }
    })
}

# Runs a setup script, keeping its messages off the caller's return values.
function Invoke-Setup([string]$Name, [hashtable]$Arguments = @{}) {
    & (Join-Path $PSScriptRoot $Name) @Arguments | Out-Host
}

function Invoke-Winget([string]$Package, [string]$Override = '') {
    $winget = Get-Command winget -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $winget) { throw 'winget is not available. Install the program manually and supply its path.' }
    $arguments = @('install', '--exact', '--id', $Package, '--accept-source-agreements', '--accept-package-agreements')
    if ($Override) { $arguments += @('--override', $Override) }
    & $winget.Source @arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "winget could not install $Package (exit code $LASTEXITCODE)." }
}

# Resolves one component: a configured or discovered candidate that passes its probe is
# used directly unless the user is reconfiguring; otherwise the user chooses among the
# found candidate, a supplied path and the component's preparation actions. Each action
# returns the candidates to probe afterwards.
function Resolve-Component {
    param(
        [string]$Label,
        [string]$Configured,
        [string[]]$Discovered,
        [scriptblock]$Probe,
        [System.Collections.Specialized.OrderedDictionary]$Actions = $null,
        [string]$PathPrompt = 'Path',
        [switch]$Optional,
        [switch]$Ask
    )
    $chosen = $null
    foreach ($candidate in @(Get-UniquePaths (@($Configured) + $Discovered))) {
        $result = & $Probe $candidate
        if ($result.Ok) { $chosen = $result; break }
        if ($candidate -eq $Configured) { Show-Probe $Label $result }
    }
    if ($chosen -and (-not $Ask -or -not $script:Interactive)) {
        Show-Probe $Label $chosen
        return $chosen
    }
    if (-not $Actions) { $Actions = [ordered]@{} }
    while ($script:Interactive) {
        $options = @()
        if ($chosen) {
            Show-Probe $Label $chosen
            $options += 'use'
        } else {
            Write-Host "[----] ${Label}: not found"
        }
        $options += 'path'
        $options += @($Actions.Keys)
        $options += $(if ($Optional) { 'skip' } else { 'quit' })
        $labels = $options | ForEach-Object { '[' + $_.Substring(0, 1).ToUpperInvariant() + ']' + $_.Substring(1) }
        $default = if ($chosen) { 'use' } elseif ($Actions.Count) { @($Actions.Keys)[0] } else { $options[-1] }
        $action = Read-Choice "  $($labels -join ', ')?" $options $default
        if ($action -eq 'use' -and $chosen) { return $chosen }
        if ($action -eq 'skip') { return New-Probe $false -Summary 'skipped' }
        if ($action -eq 'quit') { throw "Stopped while resolving $Label." }
        $candidates = @()
        if ($action -eq 'path') {
            $candidates = @(Read-Path "  $PathPrompt")
        } else {
            try {
                $candidates = @(& $Actions[$action])
            } catch {
                Write-Host "[FAIL] ${Label}: $($_.Exception.Message)"
                continue
            }
        }
        $found = $null
        foreach ($candidate in @(Get-UniquePaths $candidates)) {
            $result = & $Probe $candidate
            if ($result.Ok) { $found = $result; break }
            Show-Probe $Label $result
        }
        if ($found) {
            Show-Probe $Label $found
            return $found
        }
    }
    if ($chosen) {
        Show-Probe $Label $chosen
        return $chosen
    }
    if (-not $Optional) { $script:Missing.Add($Label) }
    Write-Host "[----] ${Label}: not found"
    return New-Probe $false -Summary 'not found'
}

# 7-Zip is only needed to extract downloaded archives, so it is resolved on first use and
# placed first on PATH, where the setup scripts look for it.
$script:SevenZip = $null
function Use-SevenZip {
    if ($script:SevenZip) { return }
    $actions = [ordered]@{ install = { Invoke-Winget '7zip.7zip'; Find-SevenZip } }
    $result = Resolve-Component '7-Zip' $script:Settings['SevenZip'] (Find-SevenZip) ${function:Test-SevenZip} $actions 'Path to 7z.exe'
    if (-not $result.Ok) { throw '7-Zip is required to extract the downloaded archives.' }
    $script:SevenZip = $result.Value
    $script:Settings['SevenZip'] = $result.Value
    $env:PATH = (Split-Path -Parent $result.Value) + ';' + $env:PATH
}

# The wizard resolves CMake before the compiler, so the toolchain setup must not prepare,
# and possibly download, its own pinned CMake.
function Install-PinnedToolchain([string]$ArchivePath = '') {
    Use-SevenZip
    $arguments = @{ SkipCMake = $true }
    if ($ArchivePath) { $arguments['ArchivePath'] = $ArchivePath }
    Invoke-Setup 'setup-toolchain.ps1' $arguments
}

# The setup script also checks an already prepared DLL against the pinned hash, so it runs
# before every copy; 7-Zip is needed only when the DLL has to be extracted.
function Copy-DirectXRuntime([string]$GameRoot) {
    $cached = Test-Path -LiteralPath $preparedRuntime
    if (-not $cached) { Use-SevenZip }
    try {
        Invoke-Setup 'setup-directx-runtime.ps1'
    } catch {
        if (-not $cached) { throw }
        throw "$($_.Exception.Message) Delete $preparedRuntime and run build.bat again to prepare it from the verified download."
    }
    Copy-Item -LiteralPath $preparedRuntime -Destination (Join-Path $GameRoot 'd3dx9_43.dll') -Force
    Write-Host '[COPY] d3dx9_43.dll beside the game'
}

function Get-StagedResource([string]$Name) {
    $path = Join-Path $repoRoot $Name
    if (Test-Path -LiteralPath $path -PathType Leaf) { return $path }
    return ''
}

# Copies one resource from the installation to its canonical repository-root name. Matching
# files are left alone; a different file is replaced only after confirmation.
function Import-Resource([string]$Source, [string]$Name) {
    $target = Join-Path $repoRoot $Name
    if (Test-Path -LiteralPath $target -PathType Leaf) {
        $sameSize = (Get-Item -LiteralPath $target).Length -eq (Get-Item -LiteralPath $Source).Length
        if ($sameSize -and (Get-FileHash -LiteralPath $target).Hash -eq (Get-FileHash -LiteralPath $Source).Hash) {
            Write-Host "[OK]   $Name is already up to date."
            return
        }
        if (-not (Read-YesNo "  $Name differs from the installation's copy. Replace it?" $false)) {
            Write-Host "[KEEP] $Name keeps its existing content."
            return
        }
    }
    $partial = "$target.partial"
    try {
        Copy-Item -LiteralPath $Source -Destination $partial -Force
        Move-Item -LiteralPath $partial -Destination $target -Force
    } finally {
        if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial -Force }
    }
    Write-Host "[COPY] $Name"
}

function Import-Resources([string]$Installation) {
    $languages = @(Get-ChildItem -LiteralPath (Join-Path $Installation 'CFG') -Directory -ErrorAction SilentlyContinue |
        Where-Object { (Test-Path -LiteralPath (Join-Path $_.FullName 'robots.dat')) -or (Test-Path -LiteralPath (Join-Path $_.FullName 'Lang.dat')) } |
        ForEach-Object Name)
    $speech = @(Get-ChildItem -LiteralPath (Join-Path $Installation 'DATA') -File -Filter 'voices*.pkg' -ErrorAction SilentlyContinue |
        ForEach-Object { $_.BaseName.Substring(6) } | Where-Object { $_ })
    $available = @($languages + $speech | Sort-Object -Unique)
    $language = ''
    if ($available.Count) {
        $default = $script:Settings['Language']
        if (-not $default -or $available -notcontains $default) { $default = $(if ($available -contains 'Rus') { 'Rus' } else { $available[0] }) }
        $language = Read-Choice "  Language for configuration, speech and map names ($($available -join ', '))" $available $default
        $script:Settings['Language'] = $language
    }
    $plan = @(
        @{ Name = 'robots.pkg'; Sources = @('DATA/robots.pkg') },
        @{ Name = 'robots.dat'; Sources = @('CFG/robots.dat', "CFG/$language/robots.dat") },
        @{ Name = 'sound.pkg'; Sources = @('DATA/sound.pkg') },
        @{ Name = 'voices.pkg'; Sources = @("DATA/voices$language.pkg") },
        @{ Name = 'music.pkg'; Sources = @('DATA/music.pkg') },
        @{ Name = 'Lang.dat'; Sources = @("CFG/$language/Lang.dat") }
    )
    foreach ($entry in $plan) {
        $source = @($entry.Sources | Where-Object { $_ -notmatch '//' } | ForEach-Object { Join-Path $Installation $_ } |
            Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1)
        if ($source -and $entry.Name -eq 'robots.dat') {
            $packed = Read-YesNo '  Import the packed configuration robots.dat? Without it the game uses the repository text configuration' ($script:Settings['PackedConfiguration'] -eq 'yes')
            $script:Settings['PackedConfiguration'] = $(if ($packed) { 'yes' } else { 'no' })
            if (-not $packed) { continue }
        }
        if ($source) {
            Import-Resource $source[0] $entry.Name
        } else {
            Write-Host "[----] $($entry.Name): not present in the installation"
        }
    }
}

function Get-RuntimeInstalled {
    $system = if ([Environment]::Is64BitOperatingSystem) { 'SysWOW64' } else { 'System32' }
    return Test-Path -LiteralPath (Join-Path $env:WINDIR "$system/d3dx9_43.dll")
}

if ($Help) { Show-Usage; exit 0 }
if ($Reset) {
    if (Test-Path -LiteralPath $settingsPath) { Remove-Item -LiteralPath $settingsPath -Force }
    Write-Host 'Saved build answers removed.'
    exit 0
}

function Invoke-Wizard {
    # Windows PowerShell 5.1 may otherwise refuse the TLS versions download hosts require.
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    $script:Settings = Read-Settings
    $saved = $script:Settings.Count -gt 0
    Write-Host 'MatrixGame build wizard'
    Write-Host "Repository: $repoRoot"
    if ($Check) { Write-Host 'Check only: nothing is downloaded, installed, copied or saved.' }

    $defaults = @{
        Configuration = 'Release'; Compiler = 'MinGW'; Output = 'EXE'; Cheats = 'no'; Music = 'yes'
        Tests = 'no'; Jobs = [string][Environment]::ProcessorCount; DirectXRuntime = 'yes'; PackedConfiguration = 'yes'
    }
    foreach ($key in $defaults.Keys) { if (-not $script:Settings[$key]) { $script:Settings[$key] = $defaults[$key] } }
    if ($Configuration) { $script:Settings['Configuration'] = $Configuration }
    if ($Compiler) { $script:Settings['Compiler'] = $Compiler }

    $ask = $script:Interactive -and ($Reconfigure -or -not $saved)
    if ($script:Interactive -and -not $ask) {
        Write-Host ''
        Write-Host "Saved answers: $($script:Settings['Configuration']) $($script:Settings['Compiler']) $($script:Settings['Output']), cheats $($script:Settings['Cheats']), music $($script:Settings['Music']), tests $($script:Settings['Tests']), $($script:Settings['Jobs']) jobs."
        $next = Read-Choice '[B]uild with them, [C]hange them or [Q]uit?' @('build', 'change', 'quit') 'build'
        if ($next -eq 'quit') { return 0 }
        $ask = $next -eq 'change'
    }

    Write-Host ''
    Write-Host 'Build options'
    if ($ask) {
        if (-not $Configuration) { $script:Settings['Configuration'] = Read-Choice '  Configuration (Debug, Release)' @('Debug', 'Release') $script:Settings['Configuration'] }
        if (-not $Compiler) { $script:Settings['Compiler'] = Read-Choice '  Compiler (MinGW, MSVC)' @('MinGW', 'MSVC') $script:Settings['Compiler'] }
        $script:Settings['Output'] = Read-Choice '  Output (EXE for the standalone game, DLL for the Space Rangers 2 host)' @('EXE', 'DLL') $script:Settings['Output']
        $script:Settings['Cheats'] = $(if (Read-YesNo '  Enable developer cheats?' ($script:Settings['Cheats'] -eq 'yes')) { 'yes' } else { 'no' })
        if ($script:Settings['Output'] -eq 'EXE') {
            $script:Settings['Music'] = $(if (Read-YesNo '  Include the battle music decoder?' ($script:Settings['Music'] -eq 'yes')) { 'yes' } else { 'no' })
        }
        $script:Settings['Tests'] = $(if (Read-YesNo '  Also build the engine tests?' ($script:Settings['Tests'] -eq 'yes')) { 'yes' } else { 'no' })
        while ($true) {
            $jobs = Read-Answer '  Parallel compile jobs' $script:Settings['Jobs']
            if ($jobs -match '^\d+$' -and [int]$jobs -ge 1) { $script:Settings['Jobs'] = $jobs; break }
            Write-Host '  Enter a positive number.'
            if (-not $script:Interactive) { break }
        }
    }
    $isDll = $script:Settings['Output'] -eq 'DLL'
    $useMsvc = $script:Settings['Compiler'] -eq 'MSVC'
    Write-Host "  $($script:Settings['Configuration']) $($script:Settings['Compiler']) $($script:Settings['Output']); cheats $($script:Settings['Cheats']); music $(if ($isDll) { 'owned by the host' } else { $script:Settings['Music'] }); tests $($script:Settings['Tests']); $($script:Settings['Jobs']) jobs"

    Write-Host ''
    Write-Host 'Tools'
    $prepare = -not $Check
    if ($useMsvc) {
        $studios = @(Find-VisualStudio)
        $suitable = @($studios | Where-Object { $_.Version.Major -eq 17 -and $_.CppTools } | Select-Object -First 1)
        foreach ($studio in $studios) {
            if (-not $suitable -or $studio.Path -ne $suitable[0].Path) {
                $reason = if ($studio.Version.Major -ne 17) { 'the build uses the Visual Studio 2022 generator' } else { 'its x86/x64 C++ tools are not installed' }
                Write-Host "[INFO] $($studio.Name) $($studio.Version) is not used: $reason."
            }
        }
        if ($suitable) {
            Write-Host "[OK]   Visual Studio 2022: $($suitable[0].Name) $($suitable[0].Version) at $($suitable[0].Path)"
        } else {
            Write-Host '[----] Visual Studio 2022 with the x86/x64 C++ tools: not found'
            $install = $prepare -and (Read-YesNo '  Install the Visual Studio 2022 Build Tools with the C++ workload through winget (several GB)?' $false)
            if ($install) {
                Invoke-Winget 'Microsoft.VisualStudio.2022.BuildTools' '--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
                $suitable = @(Find-VisualStudio | Where-Object { $_.Version.Major -eq 17 -and $_.CppTools } | Select-Object -First 1)
            }
            if ($suitable) {
                Write-Host "[OK]   Visual Studio 2022: $($suitable[0].Name) at $($suitable[0].Path)"
            } else {
                $script:Missing.Add('Visual Studio 2022 C++ tools')
                # Nothing else is prepared for a build that cannot run.
                if (-not $Check) { throw 'MSVC builds need Visual Studio 2022 with the x86/x64 C++ tools.' }
            }
        }
    }
    # CMake comes before the compiler, so preparing the compiler never has to fetch it.
    $cmakeActions = $null
    if ($prepare) {
        $cmakeActions = [ordered]@{
            download = { Invoke-Setup 'setup-cmake.ps1'; $pinnedCMake }
            archive = { $archive = Read-ArchivePath '  Path to cmake-4.4.4-windows-i386.zip'; Invoke-Setup 'setup-cmake.ps1' @{ ArchivePath = $archive }; $pinnedCMake }
        }
    }
    $cmakeCommand = Get-Command cmake -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    $cmakeCandidates = @($pinnedCMake)
    if ($cmakeCommand) { $cmakeCandidates += Split-Path -Parent (Split-Path -Parent $cmakeCommand.Source) }
    if (-not $script:Interactive -and $prepare -and -not (@($script:Settings['CMakeRoot']) + $cmakeCandidates | Where-Object { $_ -and (Test-CMake $_).Ok })) {
        Invoke-Setup 'setup-cmake.ps1'
    }
    $cmake = Resolve-Component 'CMake' $script:Settings['CMakeRoot'] $cmakeCandidates ${function:Test-CMake} $cmakeActions 'CMake installation directory (contains bin\cmake.exe)' -Ask:$ask
    if ($cmake.Ok) { $script:Settings['CMakeRoot'] = $cmake.Value }
    if ($useMsvc) {
        $sdkActions = $null
        if ($prepare) {
            $sdkActions = [ordered]@{
                download = { Use-SevenZip; Invoke-Setup 'setup-directx-sdk.ps1'; $preparedSdk }
                archive = { $archive = Read-ArchivePath '  Path to DXSDK_Jun10.exe'; Use-SevenZip; Invoke-Setup 'setup-directx-sdk.ps1' @{ ArchivePath = $archive }; $preparedSdk }
            }
        }
        $sdkCandidates = @($env:DXSDK_DIR, $preparedSdk)
        foreach ($base in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) { if ($base) { $sdkCandidates += Join-Path $base 'Microsoft DirectX SDK (June 2010)' } }
        if (-not $script:Interactive -and $prepare -and -not (@($script:Settings['DirectXSdk']) + $sdkCandidates | Where-Object { $_ -and (Test-DirectXSdk $_).Ok })) {
            Write-Host 'Preparing the pinned DirectX SDK subset (about 570 MB to download)...'
            Use-SevenZip
            Invoke-Setup 'setup-directx-sdk.ps1'
        }
        $sdk = Resolve-Component 'DirectX SDK (June 2010)' $script:Settings['DirectXSdk'] $sdkCandidates ${function:Test-DirectXSdk} $sdkActions 'DirectX SDK directory (contains Include and Lib)' -Ask:$ask
        if ($sdk.Ok) { $script:Settings['DirectXSdk'] = $sdk.Value }
    } else {
        $toolchainActions = $null
        if ($prepare) {
            $toolchainActions = [ordered]@{
                download = { Install-PinnedToolchain; $pinnedToolchain }
                archive = { Install-PinnedToolchain (Read-ArchivePath '  Path to the WinLibs archive'); $pinnedToolchain }
            }
        }
        $gcc = Get-Command gcc -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        $toolchainCandidates = @($pinnedToolchain)
        if ($gcc) { $toolchainCandidates += Split-Path -Parent (Split-Path -Parent $gcc.Source) }
        if (-not $script:Interactive -and $prepare -and -not (@($script:Settings['ToolchainRoot']) + $toolchainCandidates | Where-Object { $_ -and (Test-MinGW $_).Ok })) {
            Write-Host 'Preparing the pinned MinGW toolchain (about 95 MB to download)...'
            Install-PinnedToolchain
        }
        $toolchain = Resolve-Component 'MinGW x86 compiler' $script:Settings['ToolchainRoot'] $toolchainCandidates ${function:Test-MinGW} $toolchainActions 'Compiler installation directory (contains bin\gcc.exe)' -Ask:$ask
        if ($toolchain.Ok) { $script:Settings['ToolchainRoot'] = $toolchain.Value }
    }

    $runtimeNeeded = -not $isDll -and -not (Get-RuntimeInstalled)
    if ($isDll) {
        Write-Host '[OK]   DirectX runtime: supplied by the host game'
    } elseif (-not $runtimeNeeded) {
        Write-Host '[OK]   DirectX runtime: the x86 d3dx9_43.dll is installed'
    } else {
        Write-Host '[----] DirectX runtime: the x86 d3dx9_43.dll is not installed; the game cannot start without it.'
        if ($ask) {
            $script:Settings['DirectXRuntime'] = $(if (Read-YesNo '  Prepare the pinned d3dx9_43.dll (about 100 MB to download) and place it beside the game?' ($script:Settings['DirectXRuntime'] -eq 'yes')) { 'yes' } else { 'no' })
        }
    }

    Write-Host ''
    Write-Host 'Game resources'
    $resources = @('robots.pkg', 'robots.dat', 'sound.pkg', 'voices.pkg', 'music.pkg', 'Lang.dat')
    $present = @($resources | Where-Object { Get-StagedResource $_ })
    if ($isDll) {
        Write-Host '[OK]   The DLL uses the host game resources.'
    } else {
        if ($present) { Write-Host "[OK]   In the repository: $($present -join ', ')" }
        if ($present -notcontains 'robots.pkg') { Write-Host '[----] robots.pkg is missing; it is required to play.' }
        $import = $false
        if ($script:Interactive -and ($ask -or $present -notcontains 'robots.pkg')) {
            $import = Read-YesNo '  Import resources from an owned Space Rangers 2 installation?' ($present -notcontains 'robots.pkg')
        } elseif (-not $script:Interactive -and -not $Check -and $present -notcontains 'robots.pkg' -and $script:Settings['GameInstallation']) {
            $import = $true
        }
        if ($import) {
            $installation = Resolve-Component 'Game installation' $script:Settings['GameInstallation'] (Find-Installations) ${function:Test-Installation} $null 'Installation directory (contains DATA\robots.pkg)' -Optional -Ask:$ask
            if ($installation.Ok) {
                $script:Settings['GameInstallation'] = $installation.Value
                Import-Resources $installation.Value
            }
        } elseif ($Check -and $script:Settings['GameInstallation']) {
            Show-Probe 'Game installation' (Test-Installation $script:Settings['GameInstallation'])
        }
    }

    if ($Check) {
        Write-Host ''
        if ($script:Missing.Count) {
            Write-Host "Missing: $($script:Missing -join ', ')."
            return 1
        }
        Write-Host 'Everything required for this build is available.'
        return 0
    }
    if ($script:Missing.Count) { throw "Cannot build without: $($script:Missing -join ', ')." }
    Save-Settings $script:Settings
    Write-Host ''
    Write-Host "Saved answers to $settingsPath."

    Write-Host ''
    Write-Host 'Dependency sources'
    try {
        Invoke-Setup 'fetch-dependencies.ps1'
    } catch {
        Write-Host "[WARN] $($_.Exception.Message)"
        Write-Host '       CMake downloads and verifies the missing archives during the build.'
    }

    Write-Host ''
    Write-Host 'Building'
    $buildArguments = @{
        Configuration = $script:Settings['Configuration']
        Compiler = $script:Settings['Compiler']
        CMakeRoot = $script:Settings['CMakeRoot']
        Jobs = [int]$script:Settings['Jobs']
    }
    if (-not $useMsvc) { $buildArguments['ToolchainRoot'] = $script:Settings['ToolchainRoot'] }
    if ($isDll) { $buildArguments['DLL'] = $true; $buildArguments['WithoutResources'] = $true }
    if ($script:Settings['Cheats'] -eq 'yes') { $buildArguments['Cheats'] = $true }
    if (-not $isDll -and $script:Settings['Music'] -ne 'yes') { $buildArguments['WithoutMusic'] = $true }
    if ($script:Settings['Tests'] -ne 'yes') { $buildArguments['WithoutTests'] = $true }
    if ($useMsvc) { $env:DXSDK_DIR = $script:Settings['DirectXSdk'] }
    Invoke-Setup 'build.ps1' $buildArguments

    $kind = if ($isDll) { 'dll' } else { 'exe' }
    $gameRoot = Join-Path $repoRoot ("build/{0}-{1}-{2}/MatrixGame" -f $script:Settings['Compiler'].ToLowerInvariant(), $script:Settings['Configuration'].ToLowerInvariant(), $kind)
    if ($useMsvc) { $gameRoot = Join-Path $gameRoot $script:Settings['Configuration'] }
    if ($runtimeNeeded -and $script:Settings['DirectXRuntime'] -eq 'yes') {
        Write-Host ''
        Write-Host 'DirectX runtime'
        Copy-DirectXRuntime $gameRoot
    }

    Write-Host ''
    if ($isDll) {
        Write-Host "Built $(Join-Path $gameRoot 'MatrixGame.dll'). See docs/ORIGINAL_HOST.md for installing it into the host game."
    } else {
        Write-Host "Built $(Join-Path $gameRoot 'MatrixGame.exe')."
        if (-not (Test-Path -LiteralPath (Join-Path $gameRoot 'DATA/robots.pkg'))) {
            Write-Host 'Game resources are missing: run build.bat again to import robots.pkg, or place it at DATA\robots.pkg beside the game.'
        } elseif ($runtimeNeeded -and $script:Settings['DirectXRuntime'] -ne 'yes') {
            Write-Host 'Install the x86 DirectX runtime (d3dx9_43.dll) before starting the game.'
        } else {
            Write-Host 'Start MatrixGame.exe from that directory to play.'
        }
    }
    return 0
}

# Dot-sourcing defines the functions without running the wizard, for the helper tests.
if ($MyInvocation.InvocationName -eq '.') { return }

$pauseAtEnd = $script:Interactive -and (Test-StartedFromExplorer)
$status = 1
try {
    # Only the final value is the status; progress goes to the host.
    $status = [int](Invoke-Wizard | Select-Object -Last 1)
} catch {
    Write-Host ''
    Write-Host "Build wizard stopped: $($_.Exception.Message)"
    $status = 1
} finally {
    if ($pauseAtEnd) {
        Write-Host 'Press Enter to close this window.'
        [void][Console]::In.ReadLine()
    }
}
exit $status
