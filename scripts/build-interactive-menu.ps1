[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $ProjectRoot,
    [Parameter(Mandatory = $true)][string] $EngineSourceRoot,
    [Parameter(Mandatory = $true)][string] $EditorAppPath,
    [Parameter(Mandatory = $true)][string] $OutputRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$dotnetExe = 'C:\Program Files\dotnet\dotnet.exe'
if (-not (Test-Path -LiteralPath $dotnetExe -PathType Leaf)) { throw "The configured .NET SDK was not found: $dotnetExe" }

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$projectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$engineRoot = (Resolve-Path -LiteralPath $EngineSourceRoot).Path
$editorApp = (Resolve-Path -LiteralPath $EditorAppPath).Path
$buildsRoot = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $repoRoot) 'builds\helengine-ps1')).TrimEnd('\')
$output = [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\')
if (-not $output.StartsWith($buildsRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputRoot must be a fresh directory beneath $buildsRoot."
}
if (Test-Path -LiteralPath $output) { throw "Use a fresh interactive-menu output directory: $output" }
if (-not $editorApp.StartsWith($buildsRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "EditorAppPath must be beneath $buildsRoot because the authored build publishes codegen beside the editor assembly."
}
function ConvertTo-CmdArgument([string] $value) {
    return '"' + $value.Replace('"', '\"') + '"'
}
function Invoke-DotnetCaptured([string] $label, [string[]] $dotnetArguments) {
    $logsRoot = Join-Path $output 'logs'
    New-Item -ItemType Directory -Path $logsRoot -Force | Out-Null
    $batchPath = Join-Path $logsRoot ($label + '.cmd')
    $stdoutPath = Join-Path $logsRoot ($label + '.stdout.log')
    $stderrPath = Join-Path $logsRoot ($label + '.stderr.log')
    $argumentLine = ($dotnetArguments | ForEach-Object { ConvertTo-CmdArgument ([string]$_) }) -join ' '
    $batch = @(
        '@echo off',
        ('{0} {1} 1> {2} 2> {3}' -f (ConvertTo-CmdArgument $dotnetExe), $argumentLine, (ConvertTo-CmdArgument $stdoutPath), (ConvertTo-CmdArgument $stderrPath)),
        'exit /b %ERRORLEVEL%'
    )
    [IO.File]::WriteAllLines($batchPath, $batch, [Text.Encoding]::ASCII)
    & $env:ComSpec /d /c $batchPath
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        Get-Content -LiteralPath $stderrPath -Tail 60 -ErrorAction SilentlyContinue
        Get-Content -LiteralPath $stdoutPath -Tail 60 -ErrorAction SilentlyContinue
        throw "dotnet $label failed with exit code $exitCode (see $stdoutPath and $stderrPath)."
    }
}

# Use the DemoDisc-authored scene and keep every platform's shared presentation in sync.
$menuSourceRelativePath = 'assets/scenes/DemoDiscMainMenu.helen'
$menuSceneRelativePath = 'scenes/DemoDiscMainMenu.helen'
$menuSceneId = 'Scenes/DemoDiscMainMenu.helen'
$exportRoot = Join-Path $output 'export'
New-Item -ItemType Directory -Path $exportRoot -Force | Out-Null
$menuSourcePath = Join-Path $projectRoot $menuSourceRelativePath
if (-not (Test-Path -LiteralPath $menuSourcePath -PathType Leaf)) { throw "DemoDisc main-menu source scene is missing: $menuSourcePath" }

$cubeTestSourcePath = Join-Path $projectRoot 'assets\scenes\rendering\cube_test.helen'
if (-not (Test-Path -LiteralPath $cubeTestSourcePath -PathType Leaf)) { throw "cube_test source scene is missing: $cubeTestSourcePath" }
$editorBinaryRoot = Split-Path -Parent $editorApp
$samplegenProject = Join-Path $repoRoot 'tools\helengine.ps1.samplegen\helengine.ps1.samplegen.csproj'
$samplegenBuildRoot = Join-Path $output 'samplegen-build'
$samplegenArguments = @(
    'build', $samplegenProject,
    '--artifacts-path', $samplegenBuildRoot,
    ('-p:HelengineRoot=' + $engineRoot),
    ('-p:EngineBinaryRoot=' + $editorBinaryRoot),
    '-v:q', '--nologo'
)
Invoke-DotnetCaptured 'samplegen-build' $samplegenArguments
$samplegenAssembly = Join-Path $samplegenBuildRoot 'bin\helengine.ps1.samplegen\debug\helengine.ps1.samplegen.dll'
if (-not (Test-Path -LiteralPath $samplegenAssembly -PathType Leaf)) { throw "Full-scene exporter was not produced: $samplegenAssembly" }

$menuExportRoot = Join-Path $exportRoot 'menu'
$cubeExportRoot = Join-Path $exportRoot 'cube_test'
$menuTexturePath = Join-Path $projectRoot 'assets\textures\menu\main_menu_background_ps1.png'
Invoke-DotnetCaptured 'menu-export' @($samplegenAssembly, '--ps1-menu-texture', $menuSourcePath, $menuExportRoot, $menuTexturePath)
Invoke-DotnetCaptured 'cube-test-export' @($samplegenAssembly, '--full', $cubeTestSourcePath, $cubeExportRoot)

$menuReport = Get-Content -LiteralPath (Join-Path $menuExportRoot 'report.json') -Raw | ConvertFrom-Json
if (-not [string]::Equals([string]$menuReport.sceneId, $menuSceneRelativePath, [StringComparison]::OrdinalIgnoreCase)) {
    throw "PS1 menu texture export has unexpected scene ID '$($menuReport.sceneId)'."
}
if ([int]$menuReport.backgroundTexture.outputQuads -ne 4 -or
    [int]$menuReport.backgroundTexture.replacedSharedTiles -ne 4 -or
    [string]$menuReport.backgroundTexture.rotationComponentType -ne 'DemoDisc.rendering.AxisRotationComponent, DemoDisc') {
    throw 'PS1 menu export report does not describe the expected rotating four-quad texture background.'
}
$cubeReport = Get-Content -LiteralPath (Join-Path $cubeExportRoot 'report.json') -Raw | ConvertFrom-Json
$cubeSceneRelativePath = 'scenes/rendering/cube_test.helen'
if (-not [string]::Equals([string]$cubeReport.sceneId, $cubeSceneRelativePath, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Full cube_test export has unexpected scene ID '$($cubeReport.sceneId)'."
}

$authoredRoot = Join-Path $output 'authored'
& (Join-Path $PSScriptRoot 'build-authored.ps1') `
    -ProjectRoot $projectRoot `
    -SubsetScenePath (Join-Path $menuExportRoot 'scene.helen') `
    -EngineSourceRoot $engineRoot `
    -EditorAppPath $editorApp `
    -OutputRoot $authoredRoot `
    -SceneRelativePath $menuSceneRelativePath `
    -SceneId $menuSceneId `
    -AdditionalSubsetScenePaths @((Join-Path $cubeExportRoot 'scene.helen')) `
    -AdditionalSceneRelativePaths @($cubeSceneRelativePath) `
    -AdditionalSceneIds @('cube_test') `
    -RetainAllRuntimeScripts
if ($LASTEXITCODE -ne 0) { throw "Interactive-menu authored build failed with exit code $LASTEXITCODE." }
Write-Output (Join-Path $authoredRoot 'authored-build.json')
