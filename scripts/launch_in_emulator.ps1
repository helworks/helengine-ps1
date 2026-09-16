[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidateScript({ [IO.Path]::IsPathRooted($_) })][string] $Manifest,
    [Parameter(Mandatory = $true)][ValidateScript({ [IO.Path]::IsPathRooted($_) })][string] $EmulatorPath,
    # Shows the emulator window for a visual check; unattended evidence runs keep it hidden.
    [switch] $ShowWindow
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Quote-ProcessArgument {
    param([Parameter(Mandatory = $true)][string] $Value)
    if ($Value -notmatch '[\s"]') { return $Value }
    # Start-Process joins ArgumentList elements into one command line. Paths
    # with spaces therefore need their own Windows command-line quotes.
    return '"' + $Value + '"'
}

function Read-DuckStationSettings {
    param([Parameter(Mandatory = $true)][string] $Path)

    $values = @{}
    $section = $null
    foreach ($line in Get-Content -LiteralPath $Path) {
        $trimmed = $line.Trim()
        if ([string]::IsNullOrWhiteSpace($trimmed) -or $trimmed.StartsWith(';') -or $trimmed.StartsWith('#')) {
            continue
        }
        if ($trimmed -match '^\[(?<section>[^\]]+)\]$') {
            $section = $Matches.section.Trim()
            continue
        }
        if ($trimmed -notmatch '^(?<key>[^=]+)=(?<value>.*)$' -or [string]::IsNullOrWhiteSpace($section)) {
            throw "Invalid DuckStation settings line in $Path`: $line"
        }
        $key = $Matches.key.Trim()
        $value = $Matches.value.Trim()
        $compoundKey = '{0}/{1}' -f $section, $key
        if ($values.ContainsKey($compoundKey)) {
            throw "Duplicate DuckStation setting: $compoundKey"
        }
        $values[$compoundKey] = $value
    }
    return $values
}

if (-not (Test-Path -LiteralPath $Manifest -PathType Leaf)) {
    throw "Manifest does not exist: $Manifest"
}
$manifestPath = (Resolve-Path -LiteralPath $Manifest -ErrorAction Stop).Path

try {
    $manifestObject = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
} catch {
    throw "Manifest is not valid JSON: $manifestPath"
}

$manifestDirectory = Split-Path -Parent $manifestPath
$artifacts = @($manifestObject.artifacts)
if ($artifacts.Count -eq 0) { throw 'Manifest must contain at least one artifact' }

foreach ($artifact in $artifacts) {
    $artifactPath = [string]$artifact.path
    $expectedHash = [string]$artifact.sha256
    if ([string]::IsNullOrWhiteSpace($artifactPath) -or -not [IO.Path]::IsPathRooted($artifactPath)) {
        throw "Manifest artifact path must be absolute: $artifactPath"
    }
    if ($expectedHash -notmatch '^[0-9a-fA-F]{64}$') {
        throw "Manifest artifact has an invalid SHA-256: $artifactPath"
    }
    if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
        throw "Manifest artifact is missing: $artifactPath"
    }
    $actualHash = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $expectedHash.ToLowerInvariant()) {
        throw "Artifact hash mismatch for $artifactPath`: expected $expectedHash, got $actualHash"
    }
}

$exeArtifacts = @($artifacts | Where-Object { ([string]$_.path) -match '(?i)\.ps-exe$' })
if ($exeArtifacts.Count -ne 1) { throw 'Manifest must contain exactly one PS-X EXE artifact' }
$exePath = (Resolve-Path -LiteralPath ([string]$exeArtifacts[0].path)).Path

if (-not (Test-Path -LiteralPath $EmulatorPath -PathType Leaf)) {
    throw "DuckStation executable does not exist: $EmulatorPath"
}
$emulatorPath = (Resolve-Path -LiteralPath $EmulatorPath -ErrorAction Stop).Path
$portableDirectory = Split-Path -Parent $emulatorPath
$portableMarker = Join-Path $portableDirectory 'portable.txt'
$settingsPath = Join-Path $portableDirectory 'settings.ini'
$firmwarePath = Join-Path (Join-Path $portableDirectory 'bios') 'openbios.bin'
$expectedFirmwareSha256 = '713ea2aed58606282b3ff5e91d77f8c5892ea36f5adcd279d2c7dd26604a625d'

if (-not (Test-Path -LiteralPath $portableMarker -PathType Leaf)) {
    throw "DuckStation portable marker is missing: $portableMarker"
}
if (-not (Test-Path -LiteralPath $settingsPath -PathType Leaf)) {
    throw "DuckStation portable settings are missing: $settingsPath"
}
if (-not (Test-Path -LiteralPath $firmwarePath -PathType Leaf)) {
    throw "Verified OpenBIOS firmware is missing: $firmwarePath"
}
$firmwareSha256 = (Get-FileHash -LiteralPath $firmwarePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($firmwareSha256 -ne $expectedFirmwareSha256) {
    throw "OpenBIOS firmware hash mismatch for $firmwarePath`: expected $expectedFirmwareSha256, got $firmwareSha256"
}

$settings = Read-DuckStationSettings -Path $settingsPath
$requiredSettings = [ordered]@{
    'BIOS/PathNTSCU' = 'openbios.bin'
    'BIOS/PathNTSCJ' = 'openbios.bin'
    'BIOS/PathPAL' = 'openbios.bin'
    'GPU/Renderer' = 'Software'
    'GPU/ResolutionScale' = '1'
    'GPU/PGXPEnable' = 'false'
    'Console/Enable8MBRAM' = 'false'
    'CPU/OverclockEnable' = 'false'
}
foreach ($required in $requiredSettings.GetEnumerator()) {
    if (-not $settings.ContainsKey($required.Key)) {
        throw "Required DuckStation setting is missing: $($required.Key)"
    }
    if ($settings[$required.Key] -cne $required.Value) {
        throw "DuckStation setting $($required.Key) must be '$($required.Value)', got '$($settings[$required.Key])'"
    }
}

$launchStamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$launchLog = Join-Path $manifestDirectory ('duckstation-' + $launchStamp + '.log')
$errorLog = Join-Path $manifestDirectory ('duckstation-' + $launchStamp + '.err.log')
$quotedExecutablePath = Quote-ProcessArgument -Value $exePath
$arguments = @('-batch', '-nogui', '-earlyconsole', $quotedExecutablePath)
$windowStyle = if ($ShowWindow) { 'Normal' } else { 'Hidden' }
$process = Start-Process -FilePath $emulatorPath -ArgumentList $arguments -WorkingDirectory $portableDirectory -WindowStyle $windowStyle -RedirectStandardOutput $launchLog -RedirectStandardError $errorLog -PassThru

$launchRecord = [ordered]@{
    schema = 2
    started_utc = [DateTime]::UtcNow.ToString('o')
    process_id = $process.Id
    emulator = $emulatorPath
    emulator_sha256 = (Get-FileHash -LiteralPath $emulatorPath -Algorithm SHA256).Hash.ToLowerInvariant()
    portable_directory = $portableDirectory
    portable_marker = $portableMarker
    settings = $settingsPath
    settings_sha256 = (Get-FileHash -LiteralPath $settingsPath -Algorithm SHA256).Hash.ToLowerInvariant()
    firmware = $firmwarePath
    firmware_sha256 = $firmwareSha256
    manifest = $manifestPath
    manifest_sha256 = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
    executable = $exePath
    executable_sha256 = ([string]$exeArtifacts[0].sha256).ToLowerInvariant()
    stdout_log = $launchLog
    stderr_log = $errorLog
    arguments = $arguments
    window_shown = [bool]$ShowWindow
}
$launchRecord | ConvertTo-Json -Depth 8 | Out-File -LiteralPath (Join-Path $manifestDirectory 'last-launch.json') -Encoding utf8
$launchRecord | ConvertTo-Json -Depth 8
