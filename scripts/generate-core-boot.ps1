[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $EngineSourceRoot,
    [Parameter(Mandatory = $true)][string] $EngineBinaryRoot,
    [Parameter(Mandatory = $true)][string] $CodegenRoot,
    [Parameter(Mandatory = $true)][string] $OutputRoot
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$engineSource = (Resolve-Path -LiteralPath $EngineSourceRoot).Path
$engineBinaries = (Resolve-Path -LiteralPath $EngineBinaryRoot).Path
$generatorRoot = (Resolve-Path -LiteralPath $CodegenRoot).Path
$output = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $output) { throw "Use a fresh Core generation output directory: $output" }
$repoRoot = Split-Path -Parent $PSScriptRoot
$generatedRoot = Join-Path $output 'generated'
New-Item -ItemType Directory -Path $output | Out-Null
$generatorProject = Join-Path $generatorRoot 'codegen/codegen.csproj'
& dotnet build $generatorProject --no-restore -v:q
if ($LASTEXITCODE -ne 0) { throw 'Code generator build failed.' }
$generator = Join-Path $generatorRoot 'codegen/bin/Debug/net9.0/codegen.dll'
$arguments = @(
    $generator, '--cpp', '--project', (Join-Path $engineSource 'engine/helengine.core/helengine.core.csproj'),
    '--output', $generatedRoot, '--runtime-root', (Join-Path $generatorRoot 'cs2.cpp'),
    '--compiler', 'gcc', '--platform', 'ps1', '--endianness', 'little', '--runtime', 'custom-retro',
    '--feature-catalog', (Join-Path $engineSource 'engine/helengine.editor/codegen/features/helengine-feature-catalog.json'),
    '--set', 'generated-math-convention=engine-row-vector', '--set', 'pointer-size-bytes=4',
    '--set', 'include-project-defined-preprocessor-symbols=false',
    '--set', 'additional-preprocessor-symbols=HELENGINE_CODEGEN_DISABLE_MENU_REFLECTION;HELENGINE_CODEGEN_DISABLE_RUNTIME_SCRIPT_REFLECTION',
    '--set', 'codegen-forced-disabled-features=shaders;debug_overlay',
    '--set', 'load-native-runtime-metadata=false', '--set', 'codegen-use-std-string=false',
    '--set', 'codegen-use-std-vector=false', '--set', 'codegen-use-std-unordered-map=false',
    '--set', 'codegen-use-std-unordered-set=false',
    '--set', 'codegen-use-std-shared-ptr=false', '--set', 'codegen-use-std-function=false', '--set', 'codegen-use-std-chrono=false',
    '--set', 'codegen-use-rtti=true', '--set', 'codegen-use-exceptions=false',
    '--set', 'codegen-use-std-math=false', '--set', 'codegen-runtime-math-header=Ps1Math.hpp',
    '--set', 'codegen-use-hosted-file-system=false',
    '--set', 'codegen-runtime-provider-header=Ps1RuntimeProvider.hpp',
    '--set', 'codegen-compact-native-exception-messages=true'
)
& dotnet @arguments *> (Join-Path $output 'generation.log')
if ($LASTEXITCODE -ne 0) {
    Get-Content (Join-Path $output 'generation.log') -Tail 12
    throw 'Core conversion failed; no runnable artifact was produced.'
}
$finalizerProject = Join-Path $repoRoot 'tools/helengine.ps1.coregen/helengine.ps1.coregen.csproj'
$finalizerArtifacts = Join-Path $output 'finalizer'
& dotnet build $finalizerProject --artifacts-path $finalizerArtifacts -v:q
if ($LASTEXITCODE -ne 0) { throw 'Core finalizer build failed.' }
& dotnet (Join-Path $finalizerArtifacts 'bin/helengine.ps1.coregen/debug/helengine.ps1.coregen.dll') $generatedRoot $engineBinaries
if ($LASTEXITCODE -ne 0) { throw 'Core translation-unit finalization failed.' }
Write-Output $generatedRoot

