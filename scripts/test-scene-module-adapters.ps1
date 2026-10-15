[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $GeneratedCoreRoot,
    [string] $OutputRoot = 'C:/dev/helworks/builds/helengine-ps1/menu-20261007/scene-modules/native/adapters'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$generated = (Resolve-Path -LiteralPath $GeneratedCoreRoot).Path
$output = [IO.Path]::GetFullPath($OutputRoot)
$prefix = [IO.Path]::GetFullPath('C:/dev/helworks/builds/helengine-ps1').TrimEnd('\') + '\'
if (-not $output.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Adapter outputs must be in the PS1 workspace.' }
foreach ($name in @('IRuntimeCodeModuleProvider.hpp','IRuntimeSceneContentProvider.hpp','RuntimeSceneCatalogEntry.hpp','RuntimeSceneCatalog.hpp','helcpp_config.hpp','runtime/feature_config.hpp')) {
    if (-not (Test-Path -LiteralPath (Join-Path $generated $name) -PathType Leaf)) { throw "Missing current Core ABI header: $name" }
}
New-Item -ItemType Directory -Path $output -Force | Out-Null
$hostHeaders = Join-Path $output 'host-headers'
New-Item -ItemType Directory -Path (Join-Path $hostHeaders 'runtime') -Force | Out-Null
$officialGenerated = 'C:/dev/helworks/builds/helengine-ps1/menu-20261007/style-final/staged-project/cache/build/v2/b2e9a8af1309e9acf12da508093ca468/b/ps1/Release/release/generated-core'
$headerRecords = @()
foreach ($name in @('GeneratedRuntimeModuleRegistration.hpp','ps1_runtime_scene_catalog.hpp','runtime/runtime_startup_manifest.hpp')) {
    $inputHeader = Join-Path $officialGenerated $name
    Copy-Item -LiteralPath $inputHeader -Destination (Join-Path $hostHeaders $name) -Force
    $headerRecords += [ordered]@{ source = $inputHeader; sha256 = (Get-FileHash -LiteralPath $inputHeader).Hash }
}
$metadata = 'C:/dev/helworks/builds/helengine-ps1/menu-20261007/scene-modules/native/plans/metadata'
$image = (Get-Content -LiteralPath (Join-Path $repo 'toolchain.lock.json') -Raw | ConvertFrom-Json).image.reference
$flags = @('-std=c++20','-march=mips1','-mabi=32','-EL','-fno-pic','-mno-shared','-mno-abicalls','-mfp32','-mno-llsc',
    '-msoft-float','-fno-stack-protector','-nostdlib','-ffreestanding','-mno-gpopt','-fno-builtin','-fno-exceptions',
    '-frtti','-Os','-DHELENGINE_CODE_MODULE_ID=""','-DPS1_SCENE_MODULES=1','-I/core','-I/host-headers','-I/metadata','-I/repo/src','-I/repo/src/platform/ps1',
    '-I/repo/third_party/nugget','-I/repo/third_party/nugget/third_party/EASTL/include',
    '-I/repo/third_party/nugget/third_party/EABase/include/Common')
$units = @('modules/Ps1CodeModuleProvider.cpp','modules/Ps1DiscContent.cpp','modules/Ps1CodeModuleLifetime.cpp','Ps1AuthoredScene.cpp','Ps1SceneBootstrap.cpp','metadata/ps1_scene_module_metadata.cpp')
$records = @()
foreach ($name in $units) {
    $source = Join-Path $repo ('src/platform/ps1/' + $name)
    $containerSource = '/repo/src/platform/ps1/' + $name
    if ($name.StartsWith('metadata/', [StringComparison]::Ordinal)) {
        $source = Join-Path $metadata ([IO.Path]::GetFileName($name))
        $containerSource = '/metadata/' + [IO.Path]::GetFileName($name)
    }
    $outputName = [IO.Path]::GetFileName($name)
    $arguments = @('run','--rm','--mount',"type=bind,source=$repo,target=/repo,readonly",
        '--mount',"type=bind,source=$generated,target=/core,readonly",'--mount',"type=bind,source=$output,target=/out",
        '--mount',"type=bind,source=$hostHeaders,target=/host-headers,readonly",'--mount',"type=bind,source=$metadata,target=/metadata,readonly",
        $image,'mipsel-none-elf-g++') + $flags + @('-c',$containerSource,'-o',('/out/' + $outputName + '.o'))
    & rtk proxy docker @arguments *> (Join-Path $output ($outputName + '.log'))
    if ($LASTEXITCODE -ne 0) { throw "Actual Core ABI adapter compile failed: $name" }
    $records += [ordered]@{ source = $source; sourceSha256 = (Get-FileHash -LiteralPath $source).Hash; objectSha256 = (Get-FileHash -LiteralPath (Join-Path $output ($outputName + '.o'))).Hash }
}
$coreReceipt = Join-Path (Split-Path -Parent $generated) 'receipt.json'
$registry = Get-Content -LiteralPath (Join-Path $generated 'RuntimeComponentRegistry.cpp') -Raw
$deserializer = Get-Content -LiteralPath (Join-Path $generated 'IRuntimeComponentDeserializer.hpp') -Raw
if (-not $registry.Contains('DeserializersByTypeId->AddOwned(') -or -not $deserializer.Contains('virtual ~IRuntimeComponentDeserializer()')) {
    throw 'Fresh Core factory ownership/destructor emission is required for the final native source gate.'
}
[ordered]@{ passed = $true; units = $records; generatedCoreRoot = $generated;
    runtimeConfigSha256 = (Get-FileHash -LiteralPath (Join-Path $generated 'helcpp_config.hpp')).Hash;
    runtimeFeatureConfigSha256 = (Get-FileHash -LiteralPath (Join-Path $generated 'runtime/feature_config.hpp')).Hash;
    coreApiReceiptSha256 = (Get-FileHash -LiteralPath $coreReceipt).Hash;
    registryOwnershipEmissionVerified = $true; runtimeUnloadVerified = $false;
    supplementalHeaderHashes = $headerRecords; image = $image; flags = $flags } |
    ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $output 'receipt.json') -Encoding utf8
