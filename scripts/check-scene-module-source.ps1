[CmdletBinding()]
param([string] $OutputRoot = 'C:/dev/helworks/builds/helengine-ps1/menu-20261007/scene-modules/native', [switch] $Freeze)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$records = @()
foreach ($name in @('build.ps1','test-scene-modules.ps1','test-scene-module-adapters.ps1')) {
    $path = Join-Path $PSScriptRoot $name
    $tokens = $null
    $parseErrors = $null
    [Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$parseErrors) | Out-Null
    if ($parseErrors.Count -ne 0) { throw ($parseErrors | Out-String) }
    $records += [ordered]@{ path = $path; sha256 = (Get-FileHash -LiteralPath $path).Hash }
}
$prefix = [IO.Path]::GetFullPath('C:/dev/helworks/builds/helengine-ps1').TrimEnd('\') + '\'
$output = [IO.Path]::GetFullPath($OutputRoot)
if (-not $output.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Receipt outputs must be within PS1 build workspace.' }
$sourcePaths = @('Makefile','linker/scene-module.ld','scripts/build-scene-modules.py','scripts/compile-scene-module-candidates.py',
    'scripts/pack-scene-module.py','scripts/pack-scene-module-disc.py','scripts/write-scene-module-metadata.py',
    'src/platform/ps1/Ps1AuthoredScene.cpp','src/platform/ps1/Ps1AuthoredScene.hpp','src/platform/ps1/Ps1SceneBootstrap.cpp',
    'src/platform/ps1/rendering/Ps1RasterRenderer.cpp','src/platform/ps1/rendering/Ps1RasterRenderer.hpp',
    'tests/host_scene_module_transfer.cpp','tests/host_scene_module_frames.cpp','scripts/test-scene-module-plans.py',
    'scripts/test-scene-module-sdk.py','scripts/check-scene-module-source.ps1')
foreach ($relative in $sourcePaths) {
    $path = Join-Path $repo $relative
    $records += [ordered]@{ path = $path; sha256 = (Get-FileHash -LiteralPath $path).Hash }
}
foreach ($path in @(Get-ChildItem -LiteralPath (Join-Path $repo 'src/platform/ps1/modules') -File)) {
    $records += [ordered]@{ path = $path.FullName; sha256 = (Get-FileHash -LiteralPath $path.FullName).Hash }
}
New-Item -ItemType Directory -Path $output -Force | Out-Null
$adapterReceiptPath = Join-Path $output 'adapters/receipt.json'
$adaptersPassed = $false
if (Test-Path -LiteralPath $adapterReceiptPath -PathType Leaf) {
    $adapterReceipt = Get-Content -LiteralPath $adapterReceiptPath -Raw | ConvertFrom-Json
    $adaptersPassed = [bool]$adapterReceipt.passed
    foreach ($unit in @($adapterReceipt.units)) {
        if ((Get-FileHash -LiteralPath $unit.source).Hash -ne $unit.sourceSha256) { $adaptersPassed = $false }
    }
}
$proofsPassed = $adaptersPassed
foreach ($relative in @('transfer-test-receipt.json','sdk/sdk-fixtures-receipt.json','plans/receipt.json')) {
    $proofPath = Join-Path $output $relative
    if (-not (Test-Path -LiteralPath $proofPath -PathType Leaf)) { $proofsPassed = $false; continue }
    $proof = Get-Content -LiteralPath $proofPath -Raw | ConvertFrom-Json
    if ($relative -eq 'sdk/sdk-fixtures-receipt.json') {
        if (-not $proof.passed) { $proofsPassed = $false }
    } elseif (-not $proof.passed) { $proofsPassed = $false }
}
if ($Freeze -and -not $proofsPassed) { throw 'Native source cannot freeze before current SDK and host proof gates pass.' }
[ordered]@{ schemaVersion = 1; sourceHashes = $records; powershellParserPassed = $true;
    passed = [bool]($Freeze -and $proofsPassed); sourceFrozen = [bool]($Freeze -and $proofsPassed);
    fullBuildPassed = $false; sdkAdaptersPassed = $adaptersPassed;
    actualRegistryOwnershipVerified = [bool]($adaptersPassed -and $adapterReceipt.registryOwnershipEmissionVerified);
    runtimeUnloadVerified = $false;
    hostTransportReceipt = (Join-Path $output 'transfer-test-receipt.json');
    sdkOverlayReceipt = (Join-Path $output 'sdk/sdk-fixtures-receipt.json');
    metadataReceipt = (Join-Path $output 'plans/receipt.json');
    emulatorLaunched = $false } | ConvertTo-Json -Depth 12 |
    Set-Content -LiteralPath (Join-Path $output 'source-receipt.json') -Encoding utf8
