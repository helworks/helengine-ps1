[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('SmallDebug', 'Release')]
    [string] $Configuration,

    [Parameter(Mandatory = $true)]
    [ValidateSet('Clear', 'Static', 'Rotate', 'DepthOrder', 'InvalidPose', 'Capacity', 'CompletionStress')]
    [string] $DemoMode
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$sourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildRoot = Join-Path (Split-Path $sourceRoot -Parent) 'builds\helengine-ps1'
$runName = '{0}-{1}-{2}' -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'), $Configuration, ([guid]::NewGuid().ToString('N'))
$runRoot = Join-Path $buildRoot $runName
$dockerImage = 'ghcr.io/grumpycoders/pcsx-redux-build@sha256:4f99ff0dd972096280cd6faf27c4255cc2bcf932397f8f9276ee22b6b1a67555'
$sdkPatchRelative = 'patches\nugget\0001-frame-completion.patch'

function Get-RelativeInputPath {
    param(
        [Parameter(Mandatory = $true)][string] $BasePath,
        [Parameter(Mandatory = $true)][string] $FullPath
    )
    $baseUri = New-Object System.Uri(($BasePath.TrimEnd('\') + '\'))
    $fullUri = New-Object System.Uri($FullPath)
    return [Uri]::UnescapeDataString($baseUri.MakeRelativeUri($fullUri).ToString()).Replace('/', '\')
}

function Copy-InputPath {
    param(
        [Parameter(Mandatory = $true)][string] $SourcePath,
        [Parameter(Mandatory = $true)][string] $DestinationRoot,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string] $RelativeRoot
    )

    $resolved = (Resolve-Path -LiteralPath $SourcePath).Path
    if ((Get-Item -LiteralPath $resolved).PSIsContainer) {
        $base = $resolved
        $files = Get-ChildItem -LiteralPath $base -Recurse -File -Force
    } else {
        $base = Split-Path -Parent $resolved
        $files = @(Get-Item -LiteralPath $resolved -Force)
    }

    foreach ($file in $files) {
        $relative = Get-RelativeInputPath -BasePath $base -FullPath $file.FullName
        $segments = $relative -split '[\\/]'
        if ($segments -contains '.git' -or $segments -contains 'out' -or $segments -contains 'bin' -or $segments -contains 'obj') { continue }
        if ($file.Extension -in @('.o', '.dep', '.a', '.elf', '.map', '.ps-exe', '.cpe')) { continue }
        $destination = Join-Path (Join-Path $DestinationRoot $RelativeRoot) $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
    }
}

function Get-InputHashes {
    param([Parameter(Mandatory = $true)][string] $Root)
    $ignoredExtensions = @('.o', '.dep', '.a', '.elf', '.map', '.ps-exe', '.cpe')
    $ignoredGeneratedFiles = @('patch-check.log', 'patch-apply.log', 'build.log', 'retail-memory-check.log', 'manifest.json')
    $items = @()
    foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse -File -Force) {
        $relative = Get-RelativeInputPath -BasePath $Root -FullPath $file.FullName
        $segments = $relative -split '[\\/]'
        if ($segments -contains '.git' -or $segments -contains 'out' -or $file.Name -in $ignoredGeneratedFiles -or $file.Extension -in $ignoredExtensions) { continue }
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $items += [ordered]@{ path = $relative.Replace('\', '/'); sha256 = $hash }
    }
    return $items
}

New-Item -ItemType Directory -Force -Path $runRoot | Out-Null
$failureExitCode = 1
try {
    $revision = ((& git -c "safe.directory=$sourceRoot" rev-parse HEAD) | Out-String).Trim()
    $status = @(& git -c "safe.directory=$sourceRoot" status --porcelain)
    $dirty = $status.Count -gt 0

    $allowFiles = @(
        'Makefile',
        'toolchain.lock.json',
        'scripts\check-retail-memory.py',
        $sdkPatchRelative
    )
    $allowDirectories = @(
        'linker',
        'src',
        'third_party\nugget'
    )
    foreach ($relative in $allowFiles) {
        Copy-InputPath -SourcePath (Join-Path $sourceRoot $relative) -DestinationRoot $runRoot -RelativeRoot (Split-Path -Parent $relative)
    }
    foreach ($relative in $allowDirectories) {
        Copy-InputPath -SourcePath (Join-Path $sourceRoot $relative) -DestinationRoot $runRoot -RelativeRoot $relative
    }

    $sdkCopy = Join-Path $runRoot 'third_party\nugget'
    $patchCopy = Join-Path $runRoot $sdkPatchRelative
    $checkArgs = @('apply', '--check', '--unsafe-paths', $patchCopy)
    & git -C $sdkCopy @checkArgs 2>&1 | Out-File -FilePath (Join-Path $runRoot 'patch-check.log') -Encoding utf8
    $patchCheckExit = $LASTEXITCODE
    if ($patchCheckExit -ne 0) {
        $failureExitCode = $patchCheckExit
        throw "SDK frame-completion patch check failed with exit code $patchCheckExit"
    }
    & git -C $sdkCopy apply --unsafe-paths $patchCopy 2>&1 | Out-File -FilePath (Join-Path $runRoot 'patch-apply.log') -Encoding utf8
    $patchApplyExit = $LASTEXITCODE
    if ($patchApplyExit -ne 0) {
        $failureExitCode = $patchApplyExit
        throw "SDK frame-completion patch application failed with exit code $patchApplyExit"
    }

    $inputHashes = @(Get-InputHashes -Root $runRoot)
    $buildLog = Join-Path $runRoot 'build.log'
    $dockerArgs = @(
        'run', '--rm',
        '--mount', "type=bind,source=$runRoot,target=/work",
        '--workdir', '/work',
        $dockerImage,
        'make',
        "BUILD=$Configuration",
        "DEMO_MODE=$($DemoMode.ToUpperInvariant())",
        'all'
    )
    & docker @dockerArgs 2>&1 | Tee-Object -FilePath $buildLog | Out-Null
    $dockerExit = $LASTEXITCODE
    if ($dockerExit -ne 0) {
        $failureExitCode = $dockerExit
        throw "Docker build failed with exit code $dockerExit"
    }

    $artifactDirectory = Join-Path $runRoot 'out'
    $elf = Join-Path $artifactDirectory 'helengine_ps1.elf'
    $exe = Join-Path $artifactDirectory 'helengine_ps1.ps-exe'
    $map = Join-Path $artifactDirectory 'helengine_ps1.map'
    foreach ($artifact in @($elf, $exe, $map)) {
        if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) { throw "Expected build artifact is missing: $artifact" }
    }

    $validationArgs = @('-B', (Join-Path $runRoot 'scripts\check-retail-memory.py'), '--elf', $elf, '--exe', $exe)
    $validationOutput = @(& python @validationArgs 2>&1)
    $validationExit = $LASTEXITCODE
    $validationOutput | Out-File -FilePath (Join-Path $runRoot 'retail-memory-check.log') -Encoding utf8
    if ($validationExit -ne 0) {
        $failureExitCode = $validationExit
        throw "Retail memory checker rejected the built ELF/PS-X EXE with exit code $validationExit"
    }
    $validationSummary = ($validationOutput -join "`n") | ConvertFrom-Json

    $toolchainLock = Join-Path $runRoot 'toolchain.lock.json'
    $patchHash = (Get-FileHash -LiteralPath $patchCopy -Algorithm SHA256).Hash.ToLowerInvariant()
    $lockHash = (Get-FileHash -LiteralPath $toolchainLock -Algorithm SHA256).Hash.ToLowerInvariant()
    $artifactRecords = @()
    foreach ($artifact in @($elf, $exe, $map)) {
        $artifactRecords += [ordered]@{
            path = (Resolve-Path -LiteralPath $artifact).Path
            sha256 = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
            bytes = (Get-Item -LiteralPath $artifact).Length
        }
    }
    $manifest = [ordered]@{
        schema = 1
        created_utc = [DateTime]::UtcNow.ToString('o')
        configuration = $Configuration
        demo_mode = $DemoMode
        source = [ordered]@{ revision = $revision; dirty = $dirty }
        toolchain_lock_sha256 = $lockHash
        sdk_patch_sha256 = $patchHash
        copied_inputs = $inputHashes
        artifacts = $artifactRecords
        retail_memory = $validationSummary
        build_directory = (Resolve-Path -LiteralPath $runRoot).Path
    }
    $manifestPath = Join-Path $runRoot 'manifest.json'
    $manifest | ConvertTo-Json -Depth 12 | Out-File -LiteralPath $manifestPath -Encoding utf8
    Write-Output (Resolve-Path -LiteralPath $manifestPath).Path
} catch {
    # Write directly to stderr: Windows PowerShell can replace an explicit
    # `exit <code>` with 1 when a terminating Write-Error is left pending.
    [Console]::Error.WriteLine($_.Exception.Message)
    exit $failureExitCode
}
