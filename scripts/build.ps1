[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('SmallDebug', 'Release')]
    [string] $Configuration,

    [Parameter(Mandatory = $true)]
    [ValidateSet('Clear', 'Static', 'Rotate', 'DepthOrder', 'InvalidPose', 'Capacity', 'CompletionStress')]
    [string] $DemoMode,

    [string[]] $SdkDefine = @(),

    [switch] $CoreBoot,

    [string] $GeneratedCoreRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($CoreBoot) {
    if ([string]::IsNullOrWhiteSpace($GeneratedCoreRoot)) { throw 'CoreBoot requires GeneratedCoreRoot.' }
    $GeneratedCoreRoot = (Resolve-Path -LiteralPath $GeneratedCoreRoot).Path
    foreach ($requiredCoreFile in @('helengine_core_unity.cpp', 'Core.cpp', 'Core.hpp', 'helcpp_config.hpp')) {
        if (-not (Test-Path -LiteralPath (Join-Path $GeneratedCoreRoot $requiredCoreFile) -PathType Leaf)) {
            throw "CoreBoot input is missing: $requiredCoreFile"
        }
    }
    $coreConfig = Get-Content -LiteralPath (Join-Path $GeneratedCoreRoot 'helcpp_config.hpp') -Raw
    foreach ($requiredDefine in @('HE_CPP_PLATFORM_PS1 1', 'HE_CPP_PLATFORM_IS_LITTLE_ENDIAN 1', 'HE_CPP_USE_EXCEPTIONS 0', 'HE_CPP_USE_RTTI 1')) {
        if ($coreConfig -notmatch ('(?m)^#define ' + [regex]::Escape($requiredDefine) + '\s*$')) {
            throw "CoreBoot configuration must define $requiredDefine"
        }
    }
} elseif (-not [string]::IsNullOrWhiteSpace($GeneratedCoreRoot)) {
    throw 'GeneratedCoreRoot must be paired with CoreBoot.'
}

$sourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildRoot = Join-Path (Split-Path $sourceRoot -Parent) 'builds\helengine-ps1'
$runName = '{0}-{1}-{2}' -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'), $Configuration, ([guid]::NewGuid().ToString('N'))
$runRoot = Join-Path $buildRoot $runName
$sdkPatchRelative = 'patches\nugget\0001-frame-completion.patch'
$toolchainLockRelative = 'toolchain.lock.json'

function Quote-ProcessArgument {
    param([Parameter(Mandatory = $true)][string] $Value)
    if ($Value -notmatch '[\s"]') { return $Value }
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Read-ToolchainLock {
    param([Parameter(Mandatory = $true)][string] $Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Toolchain lock is missing: $Path"
    }
    try {
        $lock = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
        $imageReference = [string]$lock.image.reference
        $sdkCommit = [string]$lock.sdk.commit
    } catch {
        throw "Toolchain lock is not valid JSON or is missing required fields: $Path"
    }
    if ($imageReference -notmatch '^.+@sha256:[0-9a-fA-F]{64}$') {
        throw "Toolchain image reference must be digest-pinned: $imageReference"
    }
    if ($sdkCommit -notmatch '^[0-9a-fA-F]{40}$') {
        throw "Toolchain SDK commit must be a full revision: $sdkCommit"
    }
    return $lock
}

function Get-GitRevision {
    param([Parameter(Mandatory = $true)][string] $RepositoryPath)

    if (-not (Test-Path -LiteralPath $RepositoryPath -PathType Container)) {
        throw "SDK checkout path is missing: $RepositoryPath"
    }
    $revision = (& git -c "safe.directory=$RepositoryPath" -C $RepositoryPath rev-parse HEAD 2>$null | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $revision -notmatch '^[0-9a-fA-F]{40}$') {
        throw "Unable to resolve SDK revision at $RepositoryPath"
    }
    return $revision
}

function Invoke-CapturedProcess {
    param(
        [Parameter(Mandatory = $true)][string] $FilePath,
        [Parameter(Mandatory = $true)][string[]] $ArgumentList,
        [Parameter(Mandatory = $true)][string] $WorkingDirectory,
        [Parameter(Mandatory = $true)][string] $StdoutPath,
        [Parameter(Mandatory = $true)][string] $StderrPath
    )

    $quotedArguments = @($ArgumentList | ForEach-Object { Quote-ProcessArgument -Value ([string]$_) })
    try {
        $process = Start-Process -FilePath $FilePath -ArgumentList $quotedArguments -WorkingDirectory $WorkingDirectory -RedirectStandardOutput $StdoutPath -RedirectStandardError $StderrPath -NoNewWindow -Wait -PassThru
    } catch {
        throw ("Failed to start {0}: {1}" -f $FilePath, $_.Exception.Message)
    }
    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        StdoutPath = $StdoutPath
        StderrPath = $StderrPath
    }
}

function Merge-ProcessLogs {
    param(
        [Parameter(Mandatory = $true)][string] $StdoutPath,
        [Parameter(Mandatory = $true)][string] $StderrPath,
        [Parameter(Mandatory = $true)][string] $CombinedPath
    )

    $lines = @()
    if (Test-Path -LiteralPath $StdoutPath) { $lines += @(Get-Content -LiteralPath $StdoutPath) }
    if (Test-Path -LiteralPath $StderrPath) { $lines += @(Get-Content -LiteralPath $StderrPath) }
    $lines | Out-File -LiteralPath $CombinedPath -Encoding utf8
}

function Get-ProcessFailureDetail {
    param(
        [Parameter(Mandatory = $true)][string] $StdoutPath,
        [Parameter(Mandatory = $true)][string] $StderrPath
    )

    $lines = @()
    if (Test-Path -LiteralPath $StderrPath) { $lines += @(Get-Content -LiteralPath $StderrPath -Tail 80) }
    if ($lines.Count -eq 0 -and (Test-Path -LiteralPath $StdoutPath)) {
        $lines += @(Get-Content -LiteralPath $StdoutPath -Tail 80)
    }
    if ($lines.Count -eq 0) { return '<no native output captured>' }
    return ($lines -join [Environment]::NewLine)
}

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
    $ignoredGeneratedFiles = @(
        'patch-check.log', 'patch-check.stdout.log', 'patch-check.stderr.log',
        'patch-apply.log', 'patch-apply.stdout.log', 'patch-apply.stderr.log',
        'build.log', 'docker.stdout.log', 'docker.stderr.log',
        'retail-memory-check.log', 'retail-memory-check.stdout.log',
        'retail-memory-check.stderr.log', 'manifest.json'
    )
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
    $toolchainLockPath = Join-Path $sourceRoot $toolchainLockRelative
    $toolchainLock = Read-ToolchainLock -Path $toolchainLockPath
    $dockerImage = [string]$toolchainLock.image.reference
    $sdkSource = Join-Path $sourceRoot 'third_party\nugget'
    $sdkRevision = Get-GitRevision -RepositoryPath $sdkSource
    if ($sdkRevision.ToLowerInvariant() -ne ([string]$toolchainLock.sdk.commit).ToLowerInvariant()) {
        throw "SDK revision mismatch: expected $($toolchainLock.sdk.commit), got $sdkRevision"
    }
    $sdkStatus = @(& git -c "safe.directory=$sdkSource" -C $sdkSource status --porcelain --untracked-files=all 2>$null)
    if ($sdkStatus.Count -ne 0) {
        throw "SDK checkout is dirty at $sdkSource"
    }
    $sdkSubmoduleRevisions = [ordered]@{}
    foreach ($submodule in $toolchainLock.sdk.submodules.PSObject.Properties) {
        $submoduleRelative = ([string]$submodule.Name).Replace('/', '\')
        $submodulePath = Join-Path $sdkSource $submoduleRelative
        $submoduleRevision = Get-GitRevision -RepositoryPath $submodulePath
        if ($submoduleRevision.ToLowerInvariant() -ne ([string]$submodule.Value).ToLowerInvariant()) {
            throw "SDK submodule revision mismatch for $($submodule.Name): expected $($submodule.Value), got $submoduleRevision"
        }
        $sdkSubmoduleRevisions[$submodule.Name] = $submoduleRevision
    }
    $revision = Get-GitRevision -RepositoryPath $sourceRoot
    $status = @(& git -c "safe.directory=$sourceRoot" -C $sourceRoot status --porcelain 2>$null)
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
    if ($CoreBoot) {
        Copy-InputPath -SourcePath $GeneratedCoreRoot -DestinationRoot $runRoot -RelativeRoot 'generated-core'
    }

    $sdkCopy = Join-Path $runRoot 'third_party\nugget'
    $patchCopy = Join-Path $runRoot $sdkPatchRelative
    $patchCheck = Invoke-CapturedProcess -FilePath 'git.exe' -ArgumentList @('-C', $sdkCopy, 'apply', '--check', '--unsafe-paths', $patchCopy) -WorkingDirectory $runRoot -StdoutPath (Join-Path $runRoot 'patch-check.stdout.log') -StderrPath (Join-Path $runRoot 'patch-check.stderr.log')
    Merge-ProcessLogs -StdoutPath $patchCheck.StdoutPath -StderrPath $patchCheck.StderrPath -CombinedPath (Join-Path $runRoot 'patch-check.log')
    $patchCheckExit = $patchCheck.ExitCode
    if ($patchCheckExit -ne 0) {
        $failureExitCode = $patchCheckExit
        throw ("SDK frame-completion patch check failed with exit code {0}{1}{2}" -f $patchCheckExit, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $patchCheck.StdoutPath -StderrPath $patchCheck.StderrPath))
    }
    $patchApply = Invoke-CapturedProcess -FilePath 'git.exe' -ArgumentList @('-C', $sdkCopy, 'apply', '--unsafe-paths', $patchCopy) -WorkingDirectory $runRoot -StdoutPath (Join-Path $runRoot 'patch-apply.stdout.log') -StderrPath (Join-Path $runRoot 'patch-apply.stderr.log')
    Merge-ProcessLogs -StdoutPath $patchApply.StdoutPath -StderrPath $patchApply.StderrPath -CombinedPath (Join-Path $runRoot 'patch-apply.log')
    $patchApplyExit = $patchApply.ExitCode
    if ($patchApplyExit -ne 0) {
        $failureExitCode = $patchApplyExit
        throw ("SDK frame-completion patch application failed with exit code {0}{1}{2}" -f $patchApplyExit, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $patchApply.StdoutPath -StderrPath $patchApply.StderrPath))
    }

    $inputHashes = @(Get-InputHashes -Root $runRoot)
    $buildLog = Join-Path $runRoot 'build.log'
    $dockerStdout = Join-Path $runRoot 'docker.stdout.log'
    $dockerStderr = Join-Path $runRoot 'docker.stderr.log'
    $dockerArgs = @(
        'run', '--rm',
        '--mount', "type=bind,source=$runRoot,target=/work",
        '--workdir', '/work',
        $dockerImage,
        'make',
        "BUILD=$Configuration",
        "DEMO_MODE=$($DemoMode.ToUpperInvariant())",
        "CORE_BOOT=$([int][bool]$CoreBoot)",
        "SDK_DEFINES=$($SdkDefine -join ' ')",
        'all'
    )
    $dockerResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $dockerArgs -WorkingDirectory $runRoot -StdoutPath $dockerStdout -StderrPath $dockerStderr
    Merge-ProcessLogs -StdoutPath $dockerResult.StdoutPath -StderrPath $dockerResult.StderrPath -CombinedPath $buildLog
    $dockerExit = $dockerResult.ExitCode
    if ($dockerExit -ne 0) {
        $failureExitCode = $dockerExit
        throw ("Docker build failed with exit code {0}{1}{2}" -f $dockerExit, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $dockerResult.StdoutPath -StderrPath $dockerResult.StderrPath))
    }

    $artifactDirectory = Join-Path $runRoot 'out'
    $elf = Join-Path $artifactDirectory 'helengine_ps1.elf'
    $exe = Join-Path $artifactDirectory 'helengine_ps1.ps-exe'
    $map = Join-Path $artifactDirectory 'helengine_ps1.map'
    foreach ($artifact in @($elf, $exe, $map)) {
        if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) { throw "Expected build artifact is missing: $artifact" }
    }

    $validationStdout = Join-Path $runRoot 'retail-memory-check.stdout.log'
    $validationStderr = Join-Path $runRoot 'retail-memory-check.stderr.log'
    $validationArgs = @(
        'run', '--rm',
        '--mount', "type=bind,source=$runRoot,target=/work",
        '--workdir', '/work',
        $dockerImage,
        'python3', '-B', '/work/scripts/check-retail-memory.py',
        '--elf', '/work/out/helengine_ps1.elf',
        '--exe', '/work/out/helengine_ps1.ps-exe'
    )
    $validationResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $validationArgs -WorkingDirectory $runRoot -StdoutPath $validationStdout -StderrPath $validationStderr
    $validationLog = Join-Path $runRoot 'retail-memory-check.log'
    Merge-ProcessLogs -StdoutPath $validationResult.StdoutPath -StderrPath $validationResult.StderrPath -CombinedPath $validationLog
    $validationExit = $validationResult.ExitCode
    if ($validationExit -ne 0) {
        $failureExitCode = $validationExit
        throw ("Retail memory checker rejected the built ELF/PS-X EXE with exit code {0}{1}{2}" -f $validationExit, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $validationResult.StdoutPath -StderrPath $validationResult.StderrPath))
    }
    $validationOutput = @(Get-Content -LiteralPath $validationResult.StdoutPath)
    $validationSummary = ($validationOutput -join [Environment]::NewLine) | ConvertFrom-Json

    $toolchainLockCopy = Join-Path $runRoot 'toolchain.lock.json'
    $patchHash = (Get-FileHash -LiteralPath $patchCopy -Algorithm SHA256).Hash.ToLowerInvariant()
    $lockHash = (Get-FileHash -LiteralPath $toolchainLockCopy -Algorithm SHA256).Hash.ToLowerInvariant()
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
        core_boot = [bool]$CoreBoot
        generated_core_root = $GeneratedCoreRoot
        sdk_defines = @($SdkDefine)
        sdk = [ordered]@{
            revision = $sdkRevision
            submodules = $sdkSubmoduleRevisions
        }
        docker_image = $dockerImage
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

