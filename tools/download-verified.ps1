# MatrixGame - licensed under GPLv2 or any later version.
# Shared by the setup scripts: dot-source this file, then call Save-VerifiedDownload.

# Ensures that $Path holds the file with the pinned SHA256, downloading it if needed.
# A cached file with different content is discarded. Each attempt writes to a
# separate partial file that is moved into place only after verification, so an
# interrupted or mismatching download never remains at $Path.
function Save-VerifiedDownload {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Uri,
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Sha256,
        [ValidateRange(1, 10)][int]$Attempts = 3,
        [ValidateRange(0, 300)][int]$RetryDelaySeconds = 5
    )

    $expected = $Sha256.ToUpperInvariant()
    $name = Split-Path -Leaf $Path
    if (Test-Path -LiteralPath $Path) {
        if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $expected) { return }
        Write-Warning "Discarding cached ${name}: its SHA256 does not match the pinned value."
        Remove-Item -LiteralPath $Path -Force
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null

    $partial = "$Path.partial"
    $failure = ''
    $previousProgress = $ProgressPreference
    $ProgressPreference = 'SilentlyContinue'
    try {
        for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
            if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial -Force }
            try {
                # A browser-like agent receives SourceForge's HTML download page instead of the archive.
                Invoke-WebRequest -Uri $Uri -OutFile $partial -UseBasicParsing -UserAgent 'MatrixGame-setup'
                $actual = (Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash
                if ($actual -eq $expected) {
                    Move-Item -LiteralPath $partial -Destination $Path -Force
                    return
                }
                $failure = "SHA256 mismatch (received $actual)"
            }
            catch {
                $failure = $_.Exception.Message
            }
            if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial -Force }
            Write-Warning "Download attempt $attempt of $Attempts for $name failed: $failure"
            if ($attempt -lt $Attempts -and $RetryDelaySeconds -gt 0) {
                Start-Sleep -Seconds ($RetryDelaySeconds * [int][math]::Pow(2, $attempt - 1))
            }
        }
    }
    finally {
        $ProgressPreference = $previousProgress
        if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial -Force }
    }
    throw "Could not download a verified $name after $Attempts attempts: $failure"
}

# Returns a SHA256 over every file below $Root: one "<file SHA256> <relative/path>"
# line per file, sorted ordinally and joined with LF. Paths use forward slashes and
# keep their case, so the value is independent of enumeration order and location.
function Get-DirectoryHash {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$Root)

    # Enumerated FullName values are long paths, so 8.3 short names in $Root must not shift the prefix.
    $base = (Get-Item -LiteralPath $Root).FullName.TrimEnd('\', '/')
    [string[]]$entries = @(Get-ChildItem -LiteralPath $base -File -Recurse -Force | ForEach-Object {
        $relative = $_.FullName.Substring($base.Length + 1).Replace('\', '/')
        "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash) $relative"
    })
    [Array]::Sort($entries, [StringComparer]::Ordinal)
    $bytes = [Text.Encoding]::UTF8.GetBytes(($entries -join "`n"))
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '') }
    finally { $sha.Dispose() }
}
