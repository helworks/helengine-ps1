[CmdletBinding()]
param(
    # Folder holding generated-core\, content\ and declarations\ copied from a finished monolith run root.
    [Parameter(Mandatory = $true)]
    [string] $InputsRoot,

    # Fixed absolute native run root; the object cache lives beside it in native-objects (build.ps1 default).
    [Parameter(Mandatory = $true)]
    [string] $RunRoot,

    # Optional path relative to the generated-core copy whose mtime is bumped for a third, warm run.
    [string] $TouchHeader
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Verifies that a clean build, a warm-cache rebuild and (optionally) a rebuild after an
# mtime-only header change all produce byte-identical ps-exe and elf artifacts.

$buildScript = Join-Path $PSScriptRoot 'build.ps1'

if (-not (Test-Path -LiteralPath $InputsRoot -PathType Container)) {
    [Console]::Error.WriteLine("InputsRoot folder is missing: $InputsRoot")
    exit 2
}
$InputsRoot = (Resolve-Path -LiteralPath $InputsRoot).Path
foreach ($requiredFolder in @('generated-core', 'content', 'declarations')) {
    $requiredPath = Join-Path $InputsRoot $requiredFolder
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Container)) {
        [Console]::Error.WriteLine("InputsRoot is missing the folder: $requiredPath")
        exit 2
    }
}
if (-not [IO.Path]::IsPathRooted($RunRoot)) {
    [Console]::Error.WriteLine("RunRoot must be absolute: $RunRoot")
    exit 2
}
$RunRoot = [IO.Path]::GetFullPath($RunRoot).TrimEnd('\', '/')
$cacheRoot = Join-Path (Split-Path -Parent $RunRoot) 'native-objects'

# Hosts that can run this script (pwsh 7 or Windows PowerShell 5.1) can also run build.ps1.
$hostExecutable = (Get-Process -Id $PID).Path

function Get-DeclarationsFile {
    param([Parameter(Mandatory = $true)][string] $Root)
    $declarations = Join-Path $Root 'declarations'
    $files = @(Get-ChildItem -LiteralPath $declarations -File -Filter '*.json')
    if ($files.Count -ne 1) {
        throw "Expected exactly one declarations .json file in $declarations, found $($files.Count)."
    }
    return $files[0].FullName
}

function Invoke-VerifiedBuild {
    param([Parameter(Mandatory = $true)][string] $Inputs)
    $declarationsFile = Get-DeclarationsFile -Root $Inputs
    # Remove previous outputs so a skipped relink cannot make two runs trivially equal.
    $outDirectory = Join-Path $RunRoot 'out'
    if (Test-Path -LiteralPath $outDirectory) { Remove-Item -LiteralPath $outDirectory -Recurse -Force }
    & $hostExecutable -NoProfile -File $buildScript -Configuration Release -Authored `
        -GeneratedCoreRoot (Join-Path $Inputs 'generated-core') `
        -AuthoredContentRoot (Join-Path $Inputs 'content') `
        -AuthoredDeclarations $declarationsFile `
        -RunRoot $RunRoot | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "build.ps1 failed with exit code $LASTEXITCODE." }
    $result = [ordered]@{}
    foreach ($name in @('ps-exe', 'elf')) {
        $artifact = Join-Path $outDirectory "helengine_ps1.$name"
        if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) { throw "Expected build artifact is missing: $artifact" }
        $result[$name] = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    return $result
}

$touchedInputs = $InputsRoot.TrimEnd('\', '/') + '.touched'
try {
    $runs = @()

    # Run 1: clean object cache.
    if (Test-Path -LiteralPath $cacheRoot) { Remove-Item -LiteralPath $cacheRoot -Recurse -Force }
    $runs += , (Invoke-VerifiedBuild -Inputs $InputsRoot)

    # Run 2: warm object cache.
    $runs += , (Invoke-VerifiedBuild -Inputs $InputsRoot)

    # Run 3: warm cache, one header's mtime bumped, content unchanged.
    if (-not [string]::IsNullOrWhiteSpace($TouchHeader)) {
        if (Test-Path -LiteralPath $touchedInputs) { Remove-Item -LiteralPath $touchedInputs -Recurse -Force }
        Copy-Item -LiteralPath $InputsRoot -Destination $touchedInputs -Recurse -Force
        $headerPath = Join-Path (Join-Path $touchedInputs 'generated-core') $TouchHeader
        if (-not (Test-Path -LiteralPath $headerPath -PathType Leaf)) { throw "TouchHeader is missing from the generated-core copy: $TouchHeader" }
        (Get-Item -LiteralPath $headerPath).LastWriteTimeUtc = [DateTime]::UtcNow.AddMinutes(5)
        $runs += , (Invoke-VerifiedBuild -Inputs $touchedInputs)
    }
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
} finally {
    if (Test-Path -LiteralPath $touchedInputs) { Remove-Item -LiteralPath $touchedInputs -Recurse -Force }
}

for ($i = 0; $i -lt $runs.Count; $i++) {
    Write-Output ('run={0} ps-exe={1} elf={2}' -f ($i + 1), $runs[$i]['ps-exe'], $runs[$i]['elf'])
}
for ($i = 1; $i -lt $runs.Count; $i++) {
    foreach ($name in @('ps-exe', 'elf')) {
        if ($runs[$i][$name] -cne $runs[0][$name]) {
            Write-Output ('NOT DETERMINISTIC: helengine_ps1.{0} differs between run 1 and {1}' -f $name, ($i + 1))
            exit 1
        }
    }
}
Write-Output 'DETERMINISTIC'
exit 0
