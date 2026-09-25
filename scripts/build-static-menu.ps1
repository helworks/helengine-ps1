[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $ProjectRoot,
    [Parameter(Mandatory = $true)][string] $EngineSourceRoot,
    [string] $EditorAppPath,
    [Parameter(Mandatory = $true)][string] $OutputRoot,
    [switch] $ExportOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$projectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$engineRoot = (Resolve-Path -LiteralPath $EngineSourceRoot).Path
$buildsRoot = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $repoRoot) 'builds\helengine-ps1')).TrimEnd('\')
$output = [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\')
if (-not $output.StartsWith($buildsRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputRoot must be a fresh directory beneath $buildsRoot."
}
if (Test-Path -LiteralPath $output) { throw "Use a fresh static-menu output directory: $output" }
if (-not $ExportOnly -and [string]::IsNullOrWhiteSpace($EditorAppPath)) {
    throw '-EditorAppPath is required unless -ExportOnly is specified.'
}

$editorApp = $null
if ($ExportOnly) {
    $engineBinaryRoot = Join-Path $engineRoot 'helengine.ui\helengine.editor.app\bin\vs\Debug\net9.0-windows'
} else {
    $editorApp = (Resolve-Path -LiteralPath $EditorAppPath).Path
    if (-not $editorApp.StartsWith($buildsRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "EditorAppPath must be beneath $buildsRoot because the authored build publishes codegen beside the editor assembly."
    }
    $engineBinaryRoot = Split-Path -Parent $editorApp
}

$pinnedRevision = '5cc124eec06b8db729b2f9b15d99d26cb1ca8cc3'
$pinnedBlob = '7bdb310b5169ebf2c68a394a850f9eacf13cda6a'
$sourceRelativePath = 'assets/scenes/DemoDiscMainMenu.helen'
$sceneRelativePath = 'scenes/DemoDiscMainMenu.helen'
$expectedSceneId = 'Scenes/DemoDiscMainMenu.helen'
$blobSpec = "$pinnedRevision`:$sourceRelativePath"
$resolvedBlob = (& git -c "safe.directory=$projectRoot" -C $projectRoot rev-parse $blobSpec 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $resolvedBlob -ne $pinnedBlob) {
    throw "Pinned menu scene blob mismatch: expected $pinnedBlob at $blobSpec, got '$resolvedBlob'."
}

$sourceRoot = Join-Path $output 'source'
$exportRoot = Join-Path $output 'export'
New-Item -ItemType Directory -Path $sourceRoot -Force | Out-Null
New-Item -ItemType Directory -Path $exportRoot -Force | Out-Null
$sourcePath = Join-Path $sourceRoot 'DemoDiscMainMenu.helen'

# Stream Git's raw blob bytes directly to disk. PowerShell's text redirection would corrupt HELE.
$startInfo = New-Object System.Diagnostics.ProcessStartInfo
$startInfo.FileName = 'git'
$startInfo.Arguments = "-c safe.directory=`"$projectRoot`" -C `"$projectRoot`" cat-file blob `"$blobSpec`""
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$gitProcess = New-Object System.Diagnostics.Process
$gitProcess.StartInfo = $startInfo
if (-not $gitProcess.Start()) { throw 'Could not start Git to extract the pinned menu blob.' }
$stderrTask = $gitProcess.StandardError.ReadToEndAsync()
$destination = [IO.File]::Create($sourcePath)
try {
    $gitProcess.StandardOutput.BaseStream.CopyTo($destination)
} finally {
    $destination.Dispose()
}
$gitProcess.WaitForExit()
$gitError = $stderrTask.Result
if ($gitProcess.ExitCode -ne 0) { throw "Pinned menu blob extraction failed: $gitError" }
$extractedBlob = (& git -c "safe.directory=$projectRoot" -C $projectRoot hash-object $sourcePath | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $extractedBlob -ne $pinnedBlob) {
    throw "Extracted menu scene hash mismatch: expected $pinnedBlob, got '$extractedBlob'."
}
$sourceBytes = [IO.File]::ReadAllBytes($sourcePath)
if ($sourceBytes.Length -lt 4 -or [Text.Encoding]::ASCII.GetString($sourceBytes, 0, 4) -ne 'HELE') {
    throw "Extracted menu scene does not begin with HELE: $sourcePath"
}

foreach ($name in @('helengine.editor.dll', 'helengine.files.dll', 'helengine.core.dll', 'helengine.baseplatform.dll', 'helengine.shader.dll', 'helengine.shader.compilation.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $engineBinaryRoot $name) -PathType Leaf)) {
        throw "Current engine binary is missing beside the selected engine binary root: $(Join-Path $engineBinaryRoot $name)"
    }
}

$samplegenProject = Join-Path $repoRoot 'tools\helengine.ps1.samplegen\helengine.ps1.samplegen.csproj'
$samplegenBuildRoot = Join-Path $output 'samplegen-build'
New-Item -ItemType Directory -Path $samplegenBuildRoot | Out-Null
$samplegenArguments = @(
    'build', $samplegenProject,
    '--artifacts-path', $samplegenBuildRoot,
    ('-p:HelengineRoot=' + $engineRoot)
)
if ($engineBinaryRoot) { $samplegenArguments += '-p:EngineBinaryRoot=' + $engineBinaryRoot }
& dotnet @samplegenArguments -v:q --nologo
if ($LASTEXITCODE -ne 0) { throw "Static-menu exporter build failed with exit code $LASTEXITCODE." }
$samplegenAssembly = Join-Path $samplegenBuildRoot 'bin\helengine.ps1.samplegen\debug\helengine.ps1.samplegen.dll'
if (-not (Test-Path -LiteralPath $samplegenAssembly -PathType Leaf)) {
    throw "Static-menu exporter assembly was not produced: $samplegenAssembly"
}
& dotnet $samplegenAssembly --static-menu $sourcePath $exportRoot $pinnedRevision $pinnedBlob
if ($LASTEXITCODE -ne 0) { throw "Static-menu export failed with exit code $LASTEXITCODE." }

$exportedScenePath = Join-Path $exportRoot 'scene.helen'
$exportReportPath = Join-Path $exportRoot 'report.json'
if (-not (Test-Path -LiteralPath $exportedScenePath -PathType Leaf) -or -not (Test-Path -LiteralPath $exportReportPath -PathType Leaf)) {
    throw 'Static-menu exporter did not produce scene.helen and report.json.'
}
$report = Get-Content -LiteralPath $exportReportPath -Raw | ConvertFrom-Json
$sceneId = [string]$report.sceneId
if ($sceneId -ne $expectedSceneId) { throw "Exporter report scene ID '$sceneId' does not equal '$expectedSceneId'." }
if ([string]$report.sourceRevision -ne $pinnedRevision -or [string]$report.sourceBlobId -ne $pinnedBlob) {
    throw 'Exporter report does not match the pinned source revision and blob.'
}
if ([string]$report.authoringAssetId -notmatch '^[0-9a-f]{32}$') { throw 'Exporter report has no valid authoring asset ID.' }

if ($ExportOnly) {
    Write-Output $exportedScenePath
    return
}

$authoredRoot = Join-Path $output 'authored'
& (Join-Path $PSScriptRoot 'build-authored.ps1') `
    -ProjectRoot $projectRoot `
    -SubsetScenePath $exportedScenePath `
    -EngineSourceRoot $engineRoot `
    -EditorAppPath $editorApp `
    -OutputRoot $authoredRoot `
    -SceneRelativePath $sceneRelativePath `
    -SceneId $sceneId `
    -AllowKnownBatch2DCodegenErrors
if ($LASTEXITCODE -ne 0) { throw "Authored static-menu build failed with exit code $LASTEXITCODE." }
Write-Output (Join-Path $authoredRoot 'authored-build.json')
