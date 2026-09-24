[CmdletBinding()]
param(
    # Authoring project that owns the cube_test scene (never modified; a staged copy is built).
    [Parameter(Mandatory = $true)][string] $ProjectRoot,
    # Camera-plus-cube subset written by tools/helengine.ps1.samplegen (scripts/generate-authored-fixture.ps1).
    [Parameter(Mandatory = $true)][string] $SubsetScenePath,
    # PS1 engine checkout that owns the editor build graph and generated Core sources.
    [Parameter(Mandatory = $true)][string] $EngineSourceRoot,
    # helengine.editor.app.dll built from that checkout.
    [Parameter(Mandatory = $true)][string] $EditorAppPath,
    # csharpcodegen checkout whose codegen/codegen.csproj transpiles Core and gameplay code. Defaults
    # to the engine's own engine/vendor/csharpcodegen submodule, so PS1 uses the generator the engine
    # commit pins rather than one chosen here. Pass this only to deliberately override that pin.
    [string] $CodegenRoot = '',
    # Fresh output root below C:\dev\helworks\builds\helengine-ps1.
    [Parameter(Mandatory = $true)][string] $OutputRoot,
    [string] $SceneRelativePath = 'scenes/rendering/cube_test.helen',
    [string] $SceneId = 'cube_test',
    # Keeps every runtime script instead of pruning to the classes the scene's components name.
    # Pruning exists because the subset milestone only needed one component and the rest of the
    # project may use constructs the PS1 runtime rejects. A whole authored scene needs the whole
    # runtime codebase, because its components reference helpers no component names directly.
    [switch] $RetainAllRuntimeScripts,
    # Class names to drop even when retaining everything else, for project scripts the PS1 runtime
    # profile cannot transpile. Each entry is a deliberate, recorded exclusion rather than a silent
    # one: the build fails if a named class is not found.
    [string[]] $ExcludeScriptClasses = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$projectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$subsetScene = (Resolve-Path -LiteralPath $SubsetScenePath).Path
$engineRoot = (Resolve-Path -LiteralPath $EngineSourceRoot).Path
$editorApp = (Resolve-Path -LiteralPath $EditorAppPath).Path
$pinnedCodegenRoot = Join-Path $engineRoot 'engine\vendor\csharpcodegen'
$codegenIsPinned = [string]::IsNullOrWhiteSpace($CodegenRoot)
$codegenRoot = if ($codegenIsPinned) { $pinnedCodegenRoot } else { (Resolve-Path -LiteralPath $CodegenRoot).Path }
if (-not (Test-Path -LiteralPath $codegenRoot -PathType Container)) {
    throw "csharpcodegen is missing at ${codegenRoot}; run: git -C $engineRoot submodule update --init --recursive"
}
$codegenProject = Join-Path $codegenRoot 'codegen\codegen.csproj'
if (-not (Test-Path -LiteralPath $codegenProject -PathType Leaf)) { throw "codegen project is missing: $codegenProject" }
$output = [IO.Path]::GetFullPath($OutputRoot)
$buildsRoot = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $repoRoot) 'builds\helengine-ps1')).TrimEnd('\')
if (-not $editorApp.StartsWith($buildsRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "EditorAppPath must be beneath $buildsRoot because the authored build publishes codegen beside the editor assembly."
}
if (Test-Path -LiteralPath $output) { throw "Use a fresh authored build output directory: $output" }
$sceneBytes = [IO.File]::ReadAllBytes($subsetScene)
if ($sceneBytes.Length -lt 32 -or [Text.Encoding]::ASCII.GetString($sceneBytes, 0, 4) -ne 'HELE') { throw "Subset scene is not a serialized Helengine asset: $subsetScene" }

New-Item -ItemType Directory -Path $output | Out-Null
$logsRoot = Join-Path $output 'logs'
New-Item -ItemType Directory -Path $logsRoot | Out-Null

# 1. Build the PS1 platform builder against the same engine checkout the editor was built from.
$builderArtifacts = Join-Path $output 'builder'
& dotnet build (Join-Path $repoRoot 'builder\helengine.ps1.builder.csproj') -c Debug --artifacts-path $builderArtifacts -p:HelengineRoot=$engineRoot -v:q -nologo *> (Join-Path $logsRoot 'builder-build.log')
if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $logsRoot 'builder-build.log') -Tail 20; throw 'PS1 platform builder build failed.' }
$builderAssembly = Join-Path $builderArtifacts 'bin\helengine.ps1.builder\debug\helengine.ps1.builder.dll'
if (-not (Test-Path -LiteralPath $builderAssembly -PathType Leaf)) { throw "Builder assembly was not produced: $builderAssembly" }

# 2. Stage the project: assets, settings and the project file only, with the subset replacing the scene.
$engineVersion = '1.0.0+' + ((& git -c "safe.directory=$engineRoot" -C $engineRoot rev-parse HEAD | Out-String).Trim())
if ($engineVersion -notmatch '^1\.0\.0\+[0-9a-f]{40}$') { throw "Could not resolve the PS1 engine revision from $engineRoot" }
$stagedProject = Join-Path $output 'staged-project'
New-Item -ItemType Directory -Path $stagedProject | Out-Null
foreach ($relative in @('assets', 'settings')) {
    $sourcePath = Join-Path $projectRoot $relative
    if (-not (Test-Path -LiteralPath $sourcePath)) { throw "Project is missing ${relative}: $sourcePath" }
    & robocopy $sourcePath (Join-Path $stagedProject $relative) /E /NFL /NDL /NJH /NJS /NP /XD bin obj .generated-build | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "Staging $relative failed with robocopy exit code $LASTEXITCODE" }
}
$stagedScene = Join-Path $stagedProject ('assets\' + $SceneRelativePath.Replace('/', '\'))
if (-not (Test-Path -LiteralPath $stagedScene -PathType Leaf)) { throw "Staged project lacks the scene to replace: $stagedScene" }
$originalSceneHash = (Get-FileHash -LiteralPath $stagedScene -Algorithm SHA256).Hash.ToLowerInvariant()
Copy-Item -LiteralPath $subsetScene -Destination $stagedScene -Force
$subsetHash = (Get-FileHash -LiteralPath $stagedScene -Algorithm SHA256).Hash.ToLowerInvariant()

# Prune project code to the scripts the retained components need. Every other script (menus,
# overlays, path tracer, editor tooling) is outside the milestone and may use constructs the
# fatal-only PS1 runtime rejects. The report records exactly what was removed.
$subsetReportPath = Join-Path (Split-Path -Parent $subsetScene) 'report.json'
if (-not (Test-Path -LiteralPath $subsetReportPath -PathType Leaf)) { throw "Subset report is missing beside the subset scene: $subsetReportPath" }
$subsetReport = Get-Content -LiteralPath $subsetReportPath -Raw | ConvertFrom-Json
$retainedScriptClasses = @()
foreach ($component in @($subsetReport.retainedComponents)) {
    $typeId = [string]$component.componentTypeId
    if ($typeId.StartsWith('helengine.')) { continue }
    $typeName = ($typeId -split ',')[0].Trim()
    $retainedScriptClasses += $typeName.Substring($typeName.LastIndexOf('.') + 1)
}
$retainedScriptClasses = @($retainedScriptClasses | Sort-Object -Unique)
$codebaseRoot = Join-Path $stagedProject 'assets\codebase'
if (-not (Test-Path -LiteralPath $codebaseRoot)) { throw "Staged project has no codebase: $codebaseRoot" }
$removedModules = @()
foreach ($moduleFile in Get-ChildItem -LiteralPath $codebaseRoot -Recurse -Filter 'code.module.json') {
    $module = Get-Content -LiteralPath $moduleFile.FullName -Raw | ConvertFrom-Json
    if ([string]$module.moduleKind -ne 'runtime') {
        $removedModules += [string]$module.moduleId
        Remove-Item -LiteralPath $moduleFile.DirectoryName -Recurse -Force
    }
}
$retainedScripts = @()
$removedScripts = @()
foreach ($script in Get-ChildItem -LiteralPath $codebaseRoot -Recurse -Filter '*.cs') {
    $className = [IO.Path]::GetFileNameWithoutExtension($script.Name)
    if ($ExcludeScriptClasses -contains $className) {
        $removedScripts += $script.FullName.Substring($stagedProject.Length + 1).Replace('\', '/')
        Remove-Item -LiteralPath $script.FullName -Force
        $sidecarPath = $script.FullName + '.hmeta'
        if (Test-Path -LiteralPath $sidecarPath) { Remove-Item -LiteralPath $sidecarPath -Force }
        continue
    }
    if ($RetainAllRuntimeScripts -or $retainedScriptClasses -contains $className) {
        $retainedScripts += $script.FullName.Substring($stagedProject.Length + 1).Replace('\', '/')
        continue
    }
    $removedScripts += $script.FullName.Substring($stagedProject.Length + 1).Replace('\', '/')
    Remove-Item -LiteralPath $script.FullName -Force
    $sidecar = $script.FullName + '.hmeta'
    if (Test-Path -LiteralPath $sidecar) { Remove-Item -LiteralPath $sidecar -Force }
}
foreach ($className in $retainedScriptClasses) {
    if (-not ($retainedScripts | Where-Object { $_.EndsWith('/' + $className + '.cs') })) { throw "Retained component script was not found in the project codebase: $className.cs" }
}
foreach ($excluded in $ExcludeScriptClasses) {
    if (-not ($removedScripts | Where-Object { $_.EndsWith('/' + $excluded + '.cs') })) { throw "Excluded script class was not found in the project codebase: $excluded.cs" }
}
if ($RetainAllRuntimeScripts -and $removedScripts.Count -ne $ExcludeScriptClasses.Count) {
    throw "RetainAllRuntimeScripts removed $($removedScripts.Count) scripts but only $($ExcludeScriptClasses.Count) were excluded."
}
[ordered]@{
    retainedScriptClasses = $retainedScriptClasses
    retainedScripts = $retainedScripts
    removedScripts = $removedScripts
    removedModules = $removedModules
} | ConvertTo-Json -Depth 4 | Out-File -LiteralPath (Join-Path $output 'code-prune-report.json') -Encoding utf8

$projectDocument = Get-Content -LiteralPath (Join-Path $projectRoot 'project.heproj') -Raw | ConvertFrom-Json
$projectDocument.requiredEngineVersion = $engineVersion
$supported = @($projectDocument.supportedPlatforms | Where-Object { $_ -ne 'ps1' }) + @('ps1')
$projectDocument.supportedPlatforms = $supported
$projectDocument | ConvertTo-Json -Depth 8 | Out-File -LiteralPath (Join-Path $stagedProject 'project.heproj') -Encoding utf8
$platformsSettingsPath = Join-Path $stagedProject 'settings\platforms.json'
$platformsSettings = if (Test-Path -LiteralPath $platformsSettingsPath) { Get-Content -LiteralPath $platformsSettingsPath -Raw | ConvertFrom-Json } else { [pscustomobject]@{ supportedPlatforms = @() } }
$platformsSettings.supportedPlatforms = @($platformsSettings.supportedPlatforms | Where-Object { $_ -ne 'ps1' }) + @('ps1')
$platformsSettings | ConvertTo-Json -Depth 4 | Out-File -LiteralPath $platformsSettingsPath -Encoding utf8

# Hash the final staged project inputs after scene replacement, script pruning and settings edits.
$stagedInputs = @(
    Get-ChildItem -LiteralPath (Join-Path $stagedProject 'assets') -Recurse -File
) + @(
    Get-ChildItem -LiteralPath (Join-Path $stagedProject 'settings') -Recurse -File
)
$stagedInputHashesPath = Join-Path $output 'staged-input-hashes.json'
$hashRows = @($stagedInputs | Sort-Object FullName | ForEach-Object {
    [ordered]@{
        path = $_.FullName.Substring($stagedProject.Length + 1).Replace('\', '/')
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
})
$hashRows | ConvertTo-Json -Depth 4 | Out-File -LiteralPath $stagedInputHashesPath -Encoding utf8

# The editor's headless build reads its selection from the project-local build configuration.
# The subset report written beside the subset scene records the original scene identity.
$subsetReportPath = Join-Path (Split-Path -Parent $subsetScene) 'report.json'
if (-not (Test-Path -LiteralPath $subsetReportPath -PathType Leaf)) { throw "Subset report is missing beside the subset scene: $subsetReportPath" }
$subsetReport = Get-Content -LiteralPath $subsetReportPath -Raw | ConvertFrom-Json
$authoringAssetId = [string]$subsetReport.authoringAssetId
if ($authoringAssetId -notmatch '^[0-9a-f]{32}$') { throw "Subset report has no authoring asset id: $subsetReportPath" }
if ([string]$subsetReport.sceneId -ne $SceneRelativePath) { throw "Subset report scene id '$($subsetReport.sceneId)' does not match '$SceneRelativePath'." }
$editorOutput = Join-Path $output 'editor-output'
$buildConfig = [ordered]@{
    platforms = @(
        [ordered]@{
            platformId = 'ps1'
            overridesProjectScenes = $true
            selectedSceneReferences = @([ordered]@{
                sourceKind = 1
                relativePath = $SceneRelativePath
                providerId = ''
                assetId = $authoringAssetId
                contentHash = 'sha256:' + $subsetHash
            })
            selectedSceneIds = @($SceneId)
            sceneOrders = @([ordered]@{ sceneId = $SceneId; orderNumber = 1 })
            outputDirectoryPath = $editorOutput
            debugBuild = $false
            selectedEnvironmentId = 'release'
            selectedBuildProfileId = 'release'
            editorPrebuildCommandIdsByBuildProfileId = [ordered]@{}
            selectedGraphicsProfileId = 'ps1-opaque'
            selectedBuildOptionValues = [ordered]@{}
            selectedGraphicsOptionValues = [ordered]@{}
            selectedCodegenProfileId = 'default'
            selectedStorageProfileId = 'embedded-archive'
            selectedMediaProfileId = 'ps1-exe'
            selectedCodegenOptionValues = [ordered]@{}
        }
    )
    queueItems = @()
}
New-Item -ItemType Directory -Path (Join-Path $stagedProject 'user_settings') | Out-Null
$buildConfig | ConvertTo-Json -Depth 8 | Out-File -LiteralPath (Join-Path $stagedProject 'user_settings\build_config.json') -Encoding utf8

# 3. Register the ps1 platform in a build-local engine user-settings root.
$generatedCoreRoot = Join-Path $output 'generated-core'
New-Item -ItemType Directory -Path $generatedCoreRoot | Out-Null
$engineUserSettings = Join-Path $output 'engine-user-settings'
New-Item -ItemType Directory -Path $engineUserSettings | Out-Null
$registry = [ordered]@{
    platforms = @(
        [ordered]@{
            engineVersion = $engineVersion
            platformId = 'ps1'
            displayName = 'PlayStation'
            builderAssemblyPath = $builderAssembly
            playerSourceRootPath = $repoRoot
            generatedCoreCppRootPath = $generatedCoreRoot
            pluginManifestPath = ''
        }
    )
}
$registry | ConvertTo-Json -Depth 8 | Out-File -LiteralPath (Join-Path $engineUserSettings 'platforms.json') -Encoding utf8

# 4. Publish codegen beside the editor assembly, which is the first place the editor looks for it.
# Doing this ourselves keeps the generator explicit and skips the editor's on-demand fallback, which
# publishes into HELENGINE_BUILD_WORKSPACE_ROOT. That root is fresh per build here, so the fallback's
# commit-keyed cache would never hit and would republish about 37 MB on every run.
$publishedCodegen = Join-Path (Split-Path -Parent $editorApp) 'codegen'
# Clear it first. dotnet publish leaves files it considers up to date, so publishing a different
# csharpcodegen over an earlier one yields a directory holding one commit's tool and another
# commit's bundled .net.cpp headers. That mix compiles a Core whose calls the headers do not declare.
if (Test-Path -LiteralPath $publishedCodegen) { Remove-Item -LiteralPath $publishedCodegen -Recurse -Force }
$codegenPublishLog = Join-Path $logsRoot 'codegen-publish.log'
& dotnet publish $codegenProject -c Release -o $publishedCodegen --artifacts-path (Join-Path $output 'codegen-artifacts') -v:q --nologo *> $codegenPublishLog
if ($LASTEXITCODE -ne 0) { Get-Content $codegenPublishLog -Tail 20; throw "Publishing codegen beside the editor failed with exit code $LASTEXITCODE." }
$codegenTool = Join-Path $publishedCodegen 'codegen.exe'
if (-not (Test-Path -LiteralPath $codegenTool -PathType Leaf)) { throw "codegen publish did not produce the tool: $codegenTool" }
if (-not (Test-Path -LiteralPath (Join-Path $publishedCodegen '.net.cpp\system\io\stream.hpp') -PathType Leaf)) {
    throw "Published codegen directory does not carry the bundled .net.cpp runtime: $publishedCodegen"
}
$codegenCommit = (& git -c "safe.directory=$codegenRoot" -C $codegenRoot rev-parse HEAD 2>$null | Out-String).Trim()

# 5. Run the editor's headless build graph for the staged project.
$workspaceRoot = Join-Path $output 'workspace'
New-Item -ItemType Directory -Path $workspaceRoot | Out-Null
$env:HELENGINE_SOURCE_ROOT = $engineRoot
$env:HELENGINE_ENGINE_USER_SETTINGS_ROOT = $engineUserSettings
$env:HELENGINE_BUILD_WORKSPACE_ROOT = $workspaceRoot
$env:HELENGINE_PS1_REPOSITORY_ROOT = $repoRoot
$editorLog = Join-Path $logsRoot 'editor-build.log'
$editorErrorLog = Join-Path $logsRoot 'editor-build.stderr.log'
$editorProjectPath = Join-Path $stagedProject 'project.heproj'
$editorArguments = '"{0}" --project "{1}" --build ps1 --build-profile release --output "{2}"' -f $editorApp, $editorProjectPath, $editorOutput
$editorProcess = Start-Process `
    -FilePath (Get-Command dotnet -ErrorAction Stop).Source `
    -ArgumentList $editorArguments `
    -Wait `
    -PassThru `
    -WindowStyle Hidden `
    -RedirectStandardOutput $editorLog `
    -RedirectStandardError $editorErrorLog
$editorExit = $editorProcess.ExitCode

[ordered]@{
    schema = 1
    created_utc = [DateTime]::UtcNow.ToString('o')
    engine_version = $engineVersion
    engine_source_root = $engineRoot
    editor_app = $editorApp
    codegen_root = $codegenRoot
    codegen_is_engine_pinned = $codegenIsPinned
    codegen_commit = $codegenCommit
    codegen_tool = $codegenTool
    builder_assembly = $builderAssembly
    project_root = $projectRoot
    subset_scene = $subsetScene
    subset_scene_sha256 = $subsetHash
    staged_input_hashes = $stagedInputHashesPath
    replaced_scene_sha256 = $originalSceneHash
    staged_project = $stagedProject
    generated_core_root = $generatedCoreRoot
    workspace_root = $workspaceRoot
    editor_output = $editorOutput
    editor_exit_code = $editorExit
} | ConvertTo-Json -Depth 4 | Out-File -LiteralPath (Join-Path $output 'authored-build.json') -Encoding utf8

if ($editorExit -ne 0) {
    Get-Content -LiteralPath $editorLog -Tail 40
    Get-Content -LiteralPath $editorErrorLog -Tail 40
    throw "Editor headless build for ps1 failed with exit code $editorExit (see $editorLog)."
}
Write-Output (Join-Path $output 'authored-build.json')
