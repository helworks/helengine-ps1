<#
.SYNOPSIS
Selects the authored online demo-menu scene closure in the PS1 project stage.
.DESCRIPTION
Uses the editor's typed scene catalog and batched stable references. Only the PS1
scene selection in user_settings/build_config.json changes; approved asset bytes,
font/texture settings, loading policy and original project remain untouched.
The original enabled menu targets are intersected with the physical scene catalog.
Startup/loading and the canonical Tilt level flow are included; the menu itself is
never filtered or rewritten. Runtime module manifests are validated without rewriting them.
#>
param(
    [Parameter(Mandatory = $true)][string]$StagedProjectRoot,
    [Parameter(Mandatory = $true)][string]$SourceProjectRoot,
    [Parameter(Mandatory = $true)][string]$PublisherRoot,
    [Parameter(Mandatory = $true)][string]$ReceiptPath,
    [Parameter(Mandatory = $true)][string]$SelectorAssemblyRoot,
    [ValidateRange(1, 10000)][int]$ExpectedSceneCount = 26,
    [switch]$ValidateOnly
)
$ErrorActionPreference = 'Stop'
$BuildRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../builds/helengine-ps1'))
$Stage = [IO.Path]::GetFullPath($StagedProjectRoot)
$Source = [IO.Path]::GetFullPath($SourceProjectRoot)
. (Join-Path $PSScriptRoot 'online-scene-selection.ps1')
$null = Get-SafePath $BuildRoot ([IO.Path]::GetRelativePath($BuildRoot, [IO.Path]::GetFullPath($SelectorAssemblyRoot)))

# Capture approved assets, settings and project metadata, rejecting linked descendants before any catalog access.
function Get-ProtectedHashes([string]$ProjectRoot) {
    $Hashes = [ordered]@{}
    foreach ($Relative in @('assets', 'settings')) {
        $Directory = Get-SafePath $ProjectRoot $Relative
        foreach ($Item in @(Get-ChildItem -LiteralPath $Directory -Recurse -Force)) {
            if ($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Protected input contains a reparse point: $($Item.FullName)"
            }
            if (!$Item.PSIsContainer) {
                $Hashes[$Item.FullName] = (Get-FileHash -LiteralPath $Item.FullName -Algorithm SHA256).Hash
            }
        }
    }
    $ProjectPath = Get-SafePath $ProjectRoot 'project.heproj'
    $Hashes[$ProjectPath] = (Get-FileHash -LiteralPath $ProjectPath -Algorithm SHA256).Hash
    return $Hashes
}

# Require exact membership and bytes; neither the catalog nor this selection helper may rewrite authored inputs.
function Assert-ProtectedHashes([string]$ProjectRoot, $Before) {
    $After = Get-ProtectedHashes $ProjectRoot
    if ($After.Count -ne $Before.Count) {
        throw "Protected input membership changed: $ProjectRoot"
    }
    foreach ($Path in $Before.Keys) {
        if ($After[$Path] -ne $Before[$Path]) {
            throw "Protected input changed: $Path"
        }
    }
}

# Recompute the editor's native-asset content identity from current bytes, excluding embedded authoring identities.
function Get-FreshSceneContentHash([string]$Path) {
    $InputStream = [IO.File]::OpenRead($Path)
    try { $Asset = [helengine.files.AssetSerializer]::Deserialize($InputStream) } finally { $InputStream.Dispose() }
    $Asset.AuthoringAssetId = [string]::Empty
    $Asset.FormerAuthoringAssetIds = [string[]]@()
    $CanonicalStream = [IO.MemoryStream]::new()
    try {
        [helengine.files.AssetSerializer]::Serialize($CanonicalStream, $Asset)
        return 'sha256:' + [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($CanonicalStream.ToArray())).ToLowerInvariant()
    } finally {
        $CanonicalStream.Dispose()
    }
}

$null = Get-SafePath $BuildRoot ([IO.Path]::GetRelativePath($BuildRoot, $Stage))
$Receipt = Get-SafePath $BuildRoot ([IO.Path]::GetRelativePath($BuildRoot, [IO.Path]::GetFullPath($ReceiptPath)))
if ($Stage -eq $Source) { throw 'The original project cannot be used as the writable stage.' }
$ConfigPath = Get-SafePath $Stage 'user_settings/build_config.json'
$ConfigBeforeHash = (Get-FileHash -LiteralPath $ConfigPath -Algorithm SHA256).Hash
$ProtectedStage = Get-ProtectedHashes $Stage
$ProtectedSource = Get-ProtectedHashes $Source
foreach ($Name in @('helengine.core.dll', 'helengine.files.dll', 'helengine.editor.dll', 'AssimpNetter.dll')) {
    [Reflection.Assembly]::LoadFrom((Join-Path $PublisherRoot $Name)) | Out-Null
}
$env:PATH = (Join-Path $PublisherRoot 'runtimes/win-x64/native') + [IO.Path]::PathSeparator + $env:PATH
$Catalog = [helengine.editor.EditorProjectSceneCatalogService]::new($Stage)
$SourceCatalog = [helengine.editor.EditorProjectSceneCatalogService]::new($Source)
$Discovered = [Collections.Generic.List[string]]::new()
$Discovered.AddRange([string[]]$Catalog.GetSceneIds())
$Discovered.Sort([StringComparer]::Ordinal)
$OriginalIds = @($SourceCatalog.GetSceneIds())
if (@(Compare-Object -ReferenceObject $OriginalIds -DifferenceObject $Discovered.ToArray() -CaseSensitive).Count -ne 0) {
    throw 'The stage does not contain the complete original authored scene id set.'
}
$StartupId = 'DemoDiscMainMenu'
$LoadingId = 'SceneLoadingScreen'
if (!$Discovered.Contains($StartupId) -or !$Discovered.Contains($LoadingId)) {
    throw 'The original menu and persistent loading scene must both be authored files in the stage.'
}
$MenuSources = @('assets/codebase/menu.authoring/DemoDiscSceneCatalog.cs', 'assets/codebase/menu.authoring/DemoDiscMenuDefinitionProvider.cs',
    'assets/codebase/menu.authoring/DemoDiscMenuTheme.cs', 'assets/codebase/menu.authoring/MenuDefinition.cs',
    'assets/codebase/menu.authoring/MenuPanelDefinition.cs', 'assets/codebase/menu.authoring/MenuItemDefinition.cs',
    'assets/codebase/menu.authoring/MenuActionDefinition.cs', 'assets/codebase/menu/MenuActionKind.cs')
$MenuAssembly = Get-VerifiedProjectAssembly $Stage $Source 'DemoDisc' $MenuSources $PublisherRoot $SelectorAssemblyRoot
$TiltAssembly = Get-VerifiedProjectAssembly $Stage $Source 'TiltPlay' @('assets/codebase/TiltPlay/TiltTrialLevelCatalog.cs',
    'assets/codebase/TiltPlay/TiltTrialLevelCatalogEntry.cs', 'assets/codebase/TiltPlay/TiltTrialSceneIds.cs') $PublisherRoot $SelectorAssemblyRoot
$ProviderType = $MenuAssembly.assembly.GetType('DemoDisc.menu.DemoDiscMenuDefinitionProvider', $true)
$Definition = [Activator]::CreateInstance($ProviderType).CreateMenuDefinition()
$TiltSelector = $TiltAssembly.assembly.GetType('DemoDisc.TiltPlay.TiltTrialSceneIds', $true).GetField('LevelSelectSceneId').GetRawConstantValue()
$TiltEntries = $TiltAssembly.assembly.GetType('DemoDisc.TiltPlay.TiltTrialLevelCatalog', $true).GetMethod('CreateEntries').Invoke($null, $null)
$Selection = Get-OnlineSceneClosure $Definition $Discovered.ToArray() $StartupId $LoadingId $TiltSelector ([string[]]@($TiltEntries.SceneId))
if ($Selection.selectedSceneIds.Count -ne $ExpectedSceneCount) { throw "Expected $ExpectedSceneCount online-closure scenes; resolved=$($Selection.selectedSceneIds.Count)." }
$OrderedIds = [Collections.Generic.List[string]]::new()
$OrderedIds.AddRange($Selection.selectedSceneIds)
$References = $Catalog.CreateSceneReferences($OrderedIds)
if ($References.Count -ne $OrderedIds.Count) { throw 'The typed catalog returned an incomplete reference batch.' }
$ModuleDocument = [helengine.editor.EditorCodeModuleManifestService]::new($Stage).Load()
$RuntimeModules = @($ModuleDocument.Modules | Where-Object ModuleKind -eq ([helengine.editor.EditorCodeModuleKind]::Runtime))
$ExpectedModules = @('DemoDisc', 'Raytracing', 'TiltPlay', 'Zombislayer')
if (@(Compare-Object -ReferenceObject $ExpectedModules -DifferenceObject @($RuntimeModules.ModuleId) -CaseSensitive).Count -ne 0) {
    throw 'The full authored project requires exactly its four runtime code modules.'
}
$ModuleProof = @()
foreach ($Module in $RuntimeModules) {
    if ($Module.LoadScopes -notcontains 'always-loaded') { throw "Unexpected runtime residency: $($Module.ModuleId)" }
    $ModulePath = Get-SafePath $Stage ($Module.FolderPath + '/code.module.json')
    $OriginalModulePath = Get-SafePath $Source ($Module.FolderPath + '/code.module.json')
    $Hash = (Get-FileHash -LiteralPath $ModulePath -Algorithm SHA256).Hash
    if ($Hash -ne (Get-FileHash -LiteralPath $OriginalModulePath -Algorithm SHA256).Hash) {
        throw "A runtime module manifest differs from the original: $($Module.ModuleId)"
    }
    $ModuleProof += [ordered]@{ moduleId = $Module.ModuleId; folderPath = $Module.FolderPath; dependencies = @($Module.DependencyModuleIds); loadScopes = @($Module.LoadScopes); manifestSha256 = $Hash }
}
$Config = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json -AsHashtable
$Platforms = @($Config.platforms | Where-Object platformId -eq 'ps1')
if ($Platforms.Count -ne 1) { throw 'Expected exactly one existing PS1 user build configuration.' }
$Platform = $Platforms[0]
$OriginalConfigWithoutSelection = $Config | ConvertTo-Json -Depth 50 -Compress
$BeforePlatform = @{}
foreach ($Key in $Platform.Keys) {
    if ($Key -notin @('overridesProjectScenes', 'selectedSceneReferences', 'selectedSceneIds', 'sceneOrders')) {
        $BeforePlatform[$Key] = $Platform[$Key]
    }
}
$ReferenceRecords = @()
$Orders = @()
$SceneProof = @()
for ($Index = 0; $Index -lt $References.Count; $Index++) {
    $Reference = $References[$Index]
    if ($Reference.SourceKind -ne [helengine.SceneAssetReferenceSourceKind]::FileSystem) {
        throw "An authored scene was represented by a generated reference: $($OrderedIds[$Index])"
    }
    $Path = Get-SafePath $Stage ('assets/' + $Reference.RelativePath)
    if (!$Path.EndsWith('.helen', [StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw 'A scene reference does not identify an existing authored scene file.'
    }
    $ActualHash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    $FreshContentHash = Get-FreshSceneContentHash $Path
    if ($Reference.ContentHash -cne $FreshContentHash) { throw "Typed reference content hash is stale for $($OrderedIds[$Index])." }
    if ($Catalog.ResolveSceneId($Path) -cne $OrderedIds[$Index]) { throw 'Typed reference resolves to the wrong operational scene id.' }
    $Record = [ordered]@{ sourceKind = [int]$Reference.SourceKind; relativePath = $Reference.RelativePath; providerId = $Reference.ProviderId; assetId = $Reference.AssetId; contentHash = $Reference.ContentHash }
    $ReferenceRecords += $Record
    $Orders += [ordered]@{ sceneReference = $Record; sceneId = $OrderedIds[$Index]; orderNumber = $Index + 1 }
    $SceneProof += [ordered]@{ sceneId = $OrderedIds[$Index]; orderNumber = $Index + 1; authoredPath = $Path; sha256 = $ActualHash; canonicalContentHash = $FreshContentHash; reference = $Record }
}
$Platform.overridesProjectScenes = $true
$Platform.selectedSceneReferences = $ReferenceRecords
$Platform.selectedSceneIds = $OrderedIds.ToArray()
$Platform.sceneOrders = $Orders
$FinalJson = $Config | ConvertTo-Json -Depth 50
$RoundTrip = $FinalJson | ConvertFrom-Json -AsHashtable
$RoundTripPlatform = @($RoundTrip.platforms | Where-Object platformId -eq 'ps1')[0]
if ($RoundTripPlatform.selectedSceneReferences.Count -ne $ExpectedSceneCount -or $RoundTripPlatform.sceneOrders.Count -ne $ExpectedSceneCount) {
    throw 'The complete selection did not survive JSON serialization.'
}
# Restore the old selection in a comparison copy so every unrelated setting must remain equal.
$Comparison = $FinalJson | ConvertFrom-Json -AsHashtable
$ComparisonPlatform = @($Comparison.platforms | Where-Object platformId -eq 'ps1')[0]
$OldConfig = $OriginalConfigWithoutSelection | ConvertFrom-Json -AsHashtable
$OldPlatform = @($OldConfig.platforms | Where-Object platformId -eq 'ps1')[0]
foreach ($Key in @('overridesProjectScenes', 'selectedSceneReferences', 'selectedSceneIds', 'sceneOrders')) {
    if ($OldPlatform.ContainsKey($Key)) { $ComparisonPlatform[$Key] = $OldPlatform[$Key] } else { $ComparisonPlatform.Remove($Key) }
}
if (($Comparison | ConvertTo-Json -Depth 50 -Compress) -cne $OriginalConfigWithoutSelection) {
    throw 'The proposed selection changes an unrelated user or platform setting.'
}
Assert-ProtectedHashes $Stage $ProtectedStage
Assert-ProtectedHashes $Source $ProtectedSource
if ((Get-FileHash -LiteralPath $ConfigPath -Algorithm SHA256).Hash -ne $ConfigBeforeHash) { throw 'The user build configuration changed during preparation.' }
$Plan = [ordered]@{
    schemaVersion = 1; platform = 'ps1'; validateOnly = [bool]$ValidateOnly
    sceneCount = $ExpectedSceneCount; startupSceneId = $StartupId; loadingSceneId = $LoadingId
    selectedSceneIds = $OrderedIds.ToArray(); onlineSceneSelectionValidated = $true
    selectionPolicy = 'enabled-menu-physical-scene-closure-v1'; selectionPolicyVerified = $true; onlineSceneIds = $Selection.onlineSceneIds
    internalSceneIds = $Selection.internalSceneIds; missingOnlineSceneIds = $Selection.missingOnlineSceneIds
    excludedAuthoredSceneIds = $Selection.excludedAuthoredSceneIds; authoredSceneCount = $Discovered.Count
    menuRows = $Selection.menuRows; menuDefinitionUnchanged = $true
    selectorAssemblies = @(@{path=$MenuAssembly.path;sha256=$MenuAssembly.sha256;pdbPath=$MenuAssembly.pdbPath;pdbSha256=$MenuAssembly.pdbSha256;sources=$MenuAssembly.sources},
        @{path=$TiltAssembly.path;sha256=$TiltAssembly.sha256;pdbPath=$TiltAssembly.pdbPath;pdbSha256=$TiltAssembly.pdbSha256;sources=$TiltAssembly.sources})
    allReferencesAuthored = $true; scenes = $SceneProof; runtimeModules = $ModuleProof
    moduleSelectionPolicy = 'Authored runtime manifests remain unchanged; selected online scene closure supplies component roots to the existing physical code partition.'
    configPath = $ConfigPath; configBeforeSha256 = $ConfigBeforeHash
    protectedStageHashes = $ProtectedStage; protectedOriginalHashes = $ProtectedSource
    approvedAssetsAndSettingsUnchanged = $true; originalProjectUnchanged = $true; unrelatedUserSettingsUnchanged = $true
}
if (!$ValidateOnly) {
    $Bytes = [Text.UTF8Encoding]::new($false).GetBytes($FinalJson + [Environment]::NewLine)
    $FinalHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes))
    if ($FinalHash -ne $ConfigBeforeHash) { [IO.File]::WriteAllBytes($ConfigPath, $Bytes) }
    Assert-ProtectedHashes $Stage $ProtectedStage
    Assert-ProtectedHashes $Source $ProtectedSource
    $Plan.configAfterSha256 = (Get-FileHash -LiteralPath $ConfigPath -Algorithm SHA256).Hash
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Receipt)) | Out-Null
    $Plan | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath $Receipt -Encoding utf8
}
[ordered]@{ sceneCount = $ExpectedSceneCount; startupSceneId = $StartupId; loadingSceneId = $LoadingId; runtimeModules = @($RuntimeModules.ModuleId); validateOnly = [bool]$ValidateOnly; configPath = $ConfigPath; receiptPath = $Receipt } | ConvertTo-Json -Depth 5
