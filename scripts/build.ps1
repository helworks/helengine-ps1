[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('SmallDebug', 'Release')]
    [string] $Configuration,

    [ValidateSet('Clear', 'Static', 'Rotate', 'DepthOrder', 'InvalidPose', 'Capacity', 'CompletionStress')]
    [string] $DemoMode = 'Clear',

    [string[]] $SdkDefine = @(),

    [switch] $CoreBoot,

    [string] $GeneratedCoreRoot,

    [string] $NativeObjectCacheRoot,

    [switch] $Authored,

    [switch] $CpuProfile,

    [string] $AuthoredContentRoot,

    [string] $AuthoredDeclarations,

    [string] $RunRoot,

    [string] $SceneModulePlan
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$sceneModules = -not [string]::IsNullOrWhiteSpace($SceneModulePlan)
if ($sceneModules) {
    if (-not $Authored -or [string]::IsNullOrWhiteSpace($RunRoot)) { throw 'SceneModulePlan requires Authored and an explicit fixed RunRoot.' }
    $SceneModulePlan = (Resolve-Path -LiteralPath $SceneModulePlan).Path
}

if ($CpuProfile -and (-not $Authored -or $Configuration -ne 'Release')) {
    throw 'CpuProfile requires authored Release.'
}

if ($Authored) {
    # Authored mode links generated Core exactly like the boot checkpoint and adds the
    # embedded content archive plus the generated scene catalog.
    if ($CoreBoot) { throw 'Authored and CoreBoot are mutually exclusive; Authored implies the Core link.' }
    if ([string]::IsNullOrWhiteSpace($GeneratedCoreRoot)) { throw 'Authored requires GeneratedCoreRoot.' }
    if ([string]::IsNullOrWhiteSpace($AuthoredContentRoot)) { throw 'Authored requires AuthoredContentRoot.' }
    if ([string]::IsNullOrWhiteSpace($AuthoredDeclarations)) { throw 'Authored requires AuthoredDeclarations.' }
    $AuthoredContentRoot = (Resolve-Path -LiteralPath $AuthoredContentRoot).Path
    $AuthoredDeclarations = (Resolve-Path -LiteralPath $AuthoredDeclarations).Path
    if (-not (Test-Path -LiteralPath (Join-Path $GeneratedCoreRoot 'ps1_runtime_scene_catalog.cpp') -PathType Leaf)) {
        throw 'Authored input is missing: ps1_runtime_scene_catalog.cpp (run the PS1 platform builder first).'
    }
    $CoreBoot = $true
} elseif (-not [string]::IsNullOrWhiteSpace($AuthoredContentRoot) -or -not [string]::IsNullOrWhiteSpace($AuthoredDeclarations)) {
    throw 'AuthoredContentRoot and AuthoredDeclarations must be paired with Authored.'
}

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
if ([string]::IsNullOrWhiteSpace($RunRoot)) { $RunRoot = $env:HELENGINE_PS1_BUILD_RUN_ROOT }
if ([string]::IsNullOrWhiteSpace($RunRoot)) { throw 'RunRoot must name an explicit fixed native build directory.' }
if (-not [IO.Path]::IsPathRooted($RunRoot) -or -not [IO.Path]::GetPathRoot($RunRoot).EndsWith('\')) {
    throw 'RunRoot must be absolute.'
}
$runRoot = [IO.Path]::GetFullPath($RunRoot).TrimEnd('\', '/')
if ($runRoot -eq [IO.Path]::GetPathRoot($runRoot).TrimEnd('\', '/')) { throw 'RunRoot cannot be a volume root.' }
foreach ($inputRoot in @($sourceRoot, $GeneratedCoreRoot, $AuthoredContentRoot)) {
    if ([string]::IsNullOrWhiteSpace($inputRoot)) { continue }
    $inputPath = [IO.Path]::GetFullPath($inputRoot).TrimEnd('\', '/')
    if ($runRoot -eq $inputPath -or $runRoot.StartsWith($inputPath + '\', [StringComparison]::OrdinalIgnoreCase) -or $inputPath.StartsWith($runRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "RunRoot must not overlap source inputs: $inputPath"
    }
}
$sdkPatchRelative = 'patches\nugget\0001-frame-completion.patch'
$toolchainLockRelative = 'toolchain.lock.json'

function Quote-ProcessArgument {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string] $Value)
    # Windows CRT escaping: double backslashes before quotes and the closing quote.
    $escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
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
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][AllowEmptyString()][string[]] $ArgumentList,
        [Parameter(Mandatory = $true)][string] $WorkingDirectory,
        [Parameter(Mandatory = $true)][string] $StdoutPath,
        [Parameter(Mandatory = $true)][string] $StderrPath
    )

    # Preserve each argument, including empty values and paths with quotes or spaces.
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FilePath
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    if ($startInfo.PSObject.Properties.Name -contains 'ArgumentList') {
        foreach ($argument in $ArgumentList) { $startInfo.ArgumentList.Add($argument) }
    } else {
        # The platform builder supports Windows PowerShell 5 as its fallback host.
        $startInfo.Arguments = (@($ArgumentList | ForEach-Object { Quote-ProcessArgument -Value $_ }) -join ' ')
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) { throw 'Process did not start.' }
        # Read both pipes concurrently so a verbose child cannot block on a full pipe.
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        [IO.File]::WriteAllText($StdoutPath, $stdoutTask.GetAwaiter().GetResult())
        [IO.File]::WriteAllText($StderrPath, $stderrTask.GetAwaiter().GetResult())
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            StdoutPath = $StdoutPath
            StderrPath = $StderrPath
        }
    } catch {
        throw ("Failed to start {0}: {1}" -f $FilePath, $_.Exception.Message)
    } finally {
        $process.Dispose()
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

function Sync-NativeBuildFiles {
    param(
        [Parameter(Mandatory = $true)][string] $SourceRoot,
        [Parameter(Mandatory = $true)][string] $DestinationRoot
    )

    New-Item -ItemType Directory -Force -Path $DestinationRoot | Out-Null
    $patchedSdkInputs = @(
        'third_party/nugget/psyqo/gpu.hh',
        'third_party/nugget/psyqo/psyqo.mk',
        'third_party/nugget/psyqo/src/gpu.cpp',
        'third_party/nugget/psyqo/src/ordering-table.cpp'
    )
    foreach ($file in Get-ChildItem -LiteralPath $SourceRoot -Recurse -File -Force) {
        $relative = Get-RelativeInputPath -BasePath $SourceRoot -FullPath $file.FullName
        if ($file.Extension -notin @('.o', '.dep', '.a') -and $relative.Replace('\', '/') -notin $patchedSdkInputs) { continue }
        $destination = Join-Path $DestinationRoot $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
        (Get-Item -LiteralPath $destination).LastWriteTimeUtc = $file.LastWriteTimeUtc
    }
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
        'retail-memory-check.stderr.log', 'manifest.json', 'last-launch.json'
    )
    $items = @()
    foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse -File -Force) {
        if ($file.Name -match '^duckstation(?:[-.].*)?\.(?:log|json)$') { continue }
        $relative = Get-RelativeInputPath -BasePath $Root -FullPath $file.FullName
        $segments = $relative -split '[\\/]'
        if ($segments -contains '.git' -or $segments -contains 'out' -or $file.Name -in $ignoredGeneratedFiles -or $file.Extension -in $ignoredExtensions) { continue }
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $items += [ordered]@{ path = $relative.Replace('\', '/'); sha256 = $hash }
    }
    return $items
}

New-Item -ItemType Directory -Force -Path $runRoot | Out-Null
if ($Authored -and $env:HELENGINE_MENU_GENERATED_AUDIT_SCRIPT) {
    & $env:HELENGINE_MENU_GENERATED_AUDIT_SCRIPT -GeneratedCoreRoot $GeneratedCoreRoot
}
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
    if ([string]::IsNullOrWhiteSpace($NativeObjectCacheRoot)) {
        $NativeObjectCacheRoot = Join-Path (Split-Path -Parent $runRoot) 'native-objects'
    }
    $NativeObjectCacheRoot = [IO.Path]::GetFullPath($NativeObjectCacheRoot)
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
        'third_party\nugget',
        'third_party\newlib-float'
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
    if ($Authored) {
        Copy-InputPath -SourcePath $AuthoredContentRoot -DestinationRoot $runRoot -RelativeRoot 'authored-content'
        Copy-InputPath -SourcePath (Join-Path $sourceRoot 'scripts\embed-authored-assets.py') -DestinationRoot $runRoot -RelativeRoot 'scripts'
        # The packer runs inside the container from /work, so declared sources are rewritten to the copied tree.
        $declared = Get-Content -LiteralPath $AuthoredDeclarations -Raw | ConvertFrom-Json
        $containerFiles = @()
        foreach ($entry in @($declared.files)) {
            $relativePath = [string]$entry.relativePath
            if (-not (Test-Path -LiteralPath (Join-Path (Join-Path $runRoot 'authored-content') ($relativePath.Replace('/', '\'))) -PathType Leaf)) {
                throw "Declared authored asset is missing from the staged content: $relativePath"
            }
            $containerFiles += [ordered]@{ relativePath = $relativePath; sourcePath = ('authored-content/' + $relativePath) }
        }
        if ($containerFiles.Count -eq 0) { throw 'Authored declarations list no files.' }
        [IO.File]::WriteAllText((Join-Path $runRoot 'authored-declarations.json'), ([ordered]@{ files = $containerFiles } | ConvertTo-Json -Depth 4), (New-Object System.Text.UTF8Encoding($false)))
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
    if ($sceneModules) {
        foreach ($relativeHeader in @('helcpp_config.hpp', 'runtime/feature_config.hpp')) {
            if (-not (Test-Path -LiteralPath (Join-Path $runRoot ('generated-core/' + $relativeHeader)) -PathType Leaf)) {
                throw "Scene modules require the composed native configuration header: $relativeHeader"
            }
        }
        foreach ($scriptName in @('build-scene-modules.py', 'pack-scene-module.py', 'pack-scene-module-disc.py', 'write-scene-module-metadata.py')) {
            Copy-InputPath -SourcePath (Join-Path $sourceRoot ('scripts\' + $scriptName)) -DestinationRoot $runRoot -RelativeRoot 'scripts'
        }
        $moduleTask = Join-Path $runRoot 'scene-modules'
        New-Item -ItemType Directory -Path $moduleTask -Force | Out-Null
        Copy-Item -LiteralPath $SceneModulePlan -Destination (Join-Path $moduleTask 'partition-plan.json') -Force
    }

    # The PSYQo SDK rules emit .o/.dep files beside sources and libpsyqo.a beside
    # the SDK makefile. Keep those files in the disposable run tree during the build,
    # then mirror only those compiler outputs into a separate profile/flags cache.
    # Source files and final artifacts remain isolated in their immutable run folder.
    $nativeFingerprintInput = @(
        $dockerImage,
        [string]$toolchainLock.sdk.commit,
        $sdkRevision,
        (Get-FileHash -LiteralPath $patchCopy -Algorithm SHA256).Hash.ToLowerInvariant(),
        $Configuration,
        $DemoMode,
        [string][bool]$CoreBoot,
        [string][bool]$Authored,
        [string][bool]$CpuProfile,
        [string][bool]$sceneModules,
        (Get-FileHash -LiteralPath (Join-Path $runRoot 'Makefile') -Algorithm SHA256).Hash.ToLowerInvariant(),
        $(if ($sceneModules) { (Get-FileHash -LiteralPath $SceneModulePlan -Algorithm SHA256).Hash.ToLowerInvariant() } else { '' }),
        $(if ($sceneModules) { (Get-FileHash -LiteralPath (Join-Path $runRoot 'generated-core/helcpp_config.hpp') -Algorithm SHA256).Hash.ToLowerInvariant() } else { '' }),
        $(if ($sceneModules) { (Get-FileHash -LiteralPath (Join-Path $runRoot 'generated-core/runtime/feature_config.hpp') -Algorithm SHA256).Hash.ToLowerInvariant() } else { '' }),
        ($SdkDefine -join ' ')
    ) -join "`n"
    $fingerprintBytes = [Text.Encoding]::UTF8.GetBytes($nativeFingerprintInput)
    $fingerprintHasher = [Security.Cryptography.SHA256]::Create()
    try {
        $nativeFingerprint = ([BitConverter]::ToString($fingerprintHasher.ComputeHash($fingerprintBytes))).Replace('-', '').ToLowerInvariant()
    } finally {
        $fingerprintHasher.Dispose()
    }
    $nativeCachePath = Join-Path $NativeObjectCacheRoot ('objects-' + $nativeFingerprint)
    if (Test-Path -LiteralPath $nativeCachePath -PathType Container) {
        Sync-NativeBuildFiles -SourceRoot $nativeCachePath -DestinationRoot $runRoot
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
        "AUTHORED=$([int][bool]$Authored)",
        "CPU_PROFILE=$([int][bool]$CpuProfile)",
        "SDK_DEFINES=$($SdkDefine -join ' ')",
        'all'
    )
    if ($sceneModules) {
        $dockerArgs = @('run', '--rm', '--mount', "type=bind,source=$runRoot,target=/work", '--workdir', '/work',
            $dockerImage, 'python3', '-B', '/work/scripts/build-scene-modules.py',
            '--plan', '/work/scene-modules/partition-plan.json', '--configuration', $Configuration,
            '--sdk-defines', ($SdkDefine -join ' '))
        if ($CpuProfile) { $dockerArgs += '--cpu-profile' }
    }
    $dockerResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $dockerArgs -WorkingDirectory $runRoot -StdoutPath $dockerStdout -StderrPath $dockerStderr
    Merge-ProcessLogs -StdoutPath $dockerResult.StdoutPath -StderrPath $dockerResult.StderrPath -CombinedPath $buildLog
    $dockerExit = $dockerResult.ExitCode
    if ($dockerExit -ne 0) {
        $failureExitCode = $dockerExit
        throw ("Docker build failed with exit code {0}{1}{2}" -f $dockerExit, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $dockerResult.StdoutPath -StderrPath $dockerResult.StderrPath))
    }
    Sync-NativeBuildFiles -SourceRoot $runRoot -DestinationRoot $nativeCachePath

    if ($sceneModules) {
        $moduleReceipt = Get-Content -LiteralPath (Join-Path $runRoot 'out\scene-modules-receipt.json') -Raw | ConvertFrom-Json
        if (-not $moduleReceipt.passed) { throw 'Scene-module artifact receipt did not pass.' }
        $moduleArtifactRecords = @()
        foreach ($name in @('helengine_ps1.bin', 'helengine_ps1.cue', 'helengine_ps1.json', 'helengine_ps1.elf', 'helengine_ps1.ps-exe', 'helengine_ps1.map', 'scene-modules-receipt.json')) {
            $artifact = Join-Path $runRoot ('out\' + $name)
            if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) { throw "Expected scene-module artifact is missing: $artifact" }
            $moduleArtifactRecords += [ordered]@{ path = $artifact; sha256 = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant(); bytes = (Get-Item -LiteralPath $artifact).Length }
        }
        $moduleManifest = [ordered]@{
            schema = 1; configuration = $Configuration; core_boot = $true; authored = $true; scene_modules = $true
            cpu_profile = [bool]$CpuProfile
            entry_point = (Join-Path $runRoot 'out\helengine_ps1.cue')
            scene_module_plan = $SceneModulePlan; scene_module_validation = $moduleReceipt
            source = [ordered]@{ revision = $revision; dirty = $dirty }; docker_image = $dockerImage
            sdk = [ordered]@{ revision = $sdkRevision; submodules = $sdkSubmoduleRevisions }
            generated_core_root = $GeneratedCoreRoot; authored_content_root = $AuthoredContentRoot
            copied_inputs = $inputHashes; artifacts = $moduleArtifactRecords; build_directory = $runRoot
        }
        $moduleManifestPath = Join-Path $runRoot 'manifest.json'
        $moduleManifest | ConvertTo-Json -Depth 20 | Out-File -LiteralPath $moduleManifestPath -Encoding utf8
        Write-Output $moduleManifestPath
        return
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
        authored = [bool]$Authored
        cpu_profile = [bool]$CpuProfile
        authored_content_root = $AuthoredContentRoot
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

