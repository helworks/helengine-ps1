[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $ProjectRoot,

    [Parameter(Mandatory = $true)]
    [string] $OutputRoot,

    [Parameter(Mandatory = $true)]
    [string] $EngineBinaryRoot,

    [string] $SourceScenePath
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$samplegenProject = Join-Path $repoRoot 'tools\helengine.ps1.samplegen\helengine.ps1.samplegen.csproj'
$engineBinaryRootPath = (Resolve-Path -LiteralPath $EngineBinaryRoot).Path
$buildsRoot = Join-Path (Split-Path -Parent $repoRoot) 'builds\helengine-ps1'
$dotnetBuildRoot = Join-Path $buildsRoot ('samplegen-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $dotnetBuildRoot | Out-Null
$msbuildProperties = @("-p:EngineBinaryRoot=$engineBinaryRootPath")
& dotnet restore --artifacts-path $dotnetBuildRoot $samplegenProject @msbuildProperties
if ($LASTEXITCODE -ne 0) {
    throw "Authored fixture subset restore failed with exit code $LASTEXITCODE."
}
$buildArguments = @('build', '--artifacts-path', $dotnetBuildRoot, $samplegenProject, '--no-restore', '-v:minimal')
$buildArguments += $msbuildProperties
& dotnet @buildArguments
if ($LASTEXITCODE -ne 0) {
    throw "Authored fixture subset build failed with exit code $LASTEXITCODE."
}
$samplegenAssembly = Join-Path $dotnetBuildRoot 'bin\helengine.ps1.samplegen\debug\helengine.ps1.samplegen.dll'
if (-not (Test-Path -LiteralPath $samplegenAssembly)) {
    throw "Authored fixture subset build did not produce the expected assembly: '$samplegenAssembly'."
}
$arguments = @(
    $samplegenAssembly,
    (Resolve-Path -LiteralPath $ProjectRoot).Path,
    [System.IO.Path]::GetFullPath($OutputRoot),
    $engineBinaryRootPath
)
if (-not [string]::IsNullOrWhiteSpace($SourceScenePath)) {
    $arguments += [System.IO.Path]::GetFullPath($SourceScenePath)
}
& dotnet @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Authored fixture subset export failed with exit code $LASTEXITCODE."
}
