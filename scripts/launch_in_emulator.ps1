[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidateScript({ [IO.Path]::IsPathRooted($_) })][string] $Manifest,
    [Parameter(Mandatory = $true)][ValidateScript({ [IO.Path]::IsPathRooted($_) })][string] $EmulatorPath,
    [string] $FirmwarePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Manifest -PathType Leaf)) { throw "Manifest does not exist: $Manifest" }
if (-not (Test-Path -LiteralPath $EmulatorPath -PathType Leaf)) { throw "Emulator does not exist: $EmulatorPath" }
$manifestObject = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
$manifestDirectory = Split-Path -Parent (Resolve-Path -LiteralPath $Manifest).Path

foreach ($artifact in @($manifestObject.artifacts)) {
    if (-not (Test-Path -LiteralPath $artifact.path -PathType Leaf)) { throw "Manifest artifact is missing: $($artifact.path)" }
    $actualHash = (Get-FileHash -LiteralPath $artifact.path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $artifact.sha256.ToLowerInvariant()) {
        throw "Artifact hash mismatch for $($artifact.path): expected $($artifact.sha256), got $actualHash"
    }
}
$exeArtifact = @($manifestObject.artifacts | Where-Object { $_.path -match '\.ps-exe$' })
if ($exeArtifact.Count -ne 1) { throw 'Manifest must contain exactly one PS-X EXE artifact' }

$emulatorDirectory = Split-Path -Parent (Resolve-Path -LiteralPath $EmulatorPath).Path
if ([string]::IsNullOrWhiteSpace($FirmwarePath)) {
    $FirmwarePath = Join-Path $emulatorDirectory 'openbios.bin'
}
if (-not (Test-Path -LiteralPath $FirmwarePath -PathType Leaf)) { throw "Verified OpenBIOS firmware is missing: $FirmwarePath" }
if ((Split-Path -Parent (Resolve-Path -LiteralPath $FirmwarePath).Path) -ne $emulatorDirectory) {
    throw 'Firmware must be the OpenBIOS file shipped beside the verified emulator executable'
}
if ((Split-Path -Leaf $FirmwarePath) -notmatch '^openbios(-fastboot|-unirom)?\.bin$') {
    throw "Firmware does not identify as an OpenBIOS image: $FirmwarePath"
}

$launchLog = Join-Path $manifestDirectory ('emulator-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.log')
$errorLog = Join-Path $manifestDirectory ('emulator-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.err.log')
$emulatorLog = Join-Path $manifestDirectory ('pcsx-redux-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.log')
$arguments = @(
    '-portable',
    '-safe',
    '-stdout',
    '-no-gui-log',
    '-logfile', $emulatorLog,
    '-bios', $FirmwarePath,
    '-exe', $exeArtifact[0].path,
    '-run'
)
$process = Start-Process -FilePath $EmulatorPath -ArgumentList $arguments -WorkingDirectory $emulatorDirectory -WindowStyle Hidden -RedirectStandardOutput $launchLog -RedirectStandardError $errorLog -PassThru
$launchRecord = [ordered]@{
    started_utc = [DateTime]::UtcNow.ToString('o')
    process_id = $process.Id
    emulator = (Resolve-Path -LiteralPath $EmulatorPath).Path
    firmware = (Resolve-Path -LiteralPath $FirmwarePath).Path
    firmware_sha256 = (Get-FileHash -LiteralPath $FirmwarePath -Algorithm SHA256).Hash.ToLowerInvariant()
    executable = $exeArtifact[0].path
    executable_sha256 = $exeArtifact[0].sha256
    stdout_log = $launchLog
    stderr_log = $errorLog
    emulator_log = $emulatorLog
    arguments = $arguments
}
$launchRecord | ConvertTo-Json -Depth 8 | Out-File -LiteralPath (Join-Path $manifestDirectory 'last-launch.json') -Encoding utf8
$launchRecord | ConvertTo-Json -Depth 8
