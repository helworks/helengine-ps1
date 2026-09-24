[CmdletBinding()]
param(
    # Root of the csharpcodegen checkout whose cs2.cpp\.net.cpp runtime headers the embedded
    # asset stream test compiles against. The stream test is skipped, and recorded as skipped,
    # when this is omitted.
    [string] $CodegenRuntimeRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$sourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$codegenRuntimeHeaders = ''
if (-not [string]::IsNullOrWhiteSpace($CodegenRuntimeRoot)) {
    $codegenRuntimeHeaders = Join-Path (Resolve-Path -LiteralPath $CodegenRuntimeRoot).Path 'cs2.cpp\.net.cpp'
    if (-not (Test-Path -LiteralPath (Join-Path $codegenRuntimeHeaders 'system\io\stream.hpp') -PathType Leaf)) {
        throw "CodegenRuntimeRoot does not contain cs2.cpp\.net.cpp\system\io\stream.hpp: $CodegenRuntimeRoot"
    }
}
$buildRoot = Join-Path (Split-Path $sourceRoot -Parent) 'builds\helengine-ps1'
$runName = 'host-tests-{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'), ([guid]::NewGuid().ToString('N'))
$runRoot = Join-Path $buildRoot $runName
$toolchainLockPath = Join-Path $sourceRoot 'toolchain.lock.json'
if (-not (Test-Path -LiteralPath $toolchainLockPath -PathType Leaf)) {
    throw "Toolchain lock is missing: $toolchainLockPath"
}
try {
    $toolchainLock = Get-Content -LiteralPath $toolchainLockPath -Raw | ConvertFrom-Json
    $dockerImage = [string]$toolchainLock.image.reference
} catch {
    throw "Toolchain lock is not valid JSON or is missing image.reference: $toolchainLockPath"
}
if ($dockerImage -notmatch '^.+@sha256:[0-9a-fA-F]{64}$') {
    throw "Toolchain image reference must be digest-pinned: $dockerImage"
}

function Quote-ProcessArgument {
    param([Parameter(Mandatory = $true)][string] $Value)
    if ($Value -notmatch '[\s"]') { return $Value }
    return '"' + $Value.Replace('"', '\"') + '"'
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

function Add-ExitMarker {
    param(
        [Parameter(Mandatory = $true)][string] $LogPath,
        [Parameter(Mandatory = $true)][string] $Name,
        [Parameter(Mandatory = $true)][int] $ExitCode
    )

    Add-Content -LiteralPath $LogPath -Value ('{0}_EXIT_CODE={1}' -f $Name, $ExitCode)
}

New-Item -ItemType Directory -Force -Path (Join-Path $runRoot 'scripts') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $runRoot 'tests\host') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $runRoot 'src') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $runRoot 'third_party\EASTL\include') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $runRoot 'third_party\EABase\include\Common') | Out-Null
Copy-Item -LiteralPath (Join-Path $sourceRoot 'scripts\check-retail-memory.py') -Destination (Join-Path $runRoot 'scripts\check-retail-memory.py')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'tests\host\test_retail_memory.py') -Destination (Join-Path $runRoot 'tests\host\test_retail_memory.py')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'tests\host\host_smoke.cpp') -Destination (Join-Path $runRoot 'host_smoke.cpp')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'tests\host\Ps1RenderPolicyTests.cpp') -Destination (Join-Path $runRoot 'tests\host\Ps1RenderPolicyTests.cpp')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'tests\host\Ps1SharedPtrTests.cpp') -Destination (Join-Path $runRoot 'tests\host\Ps1SharedPtrTests.cpp')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'tests\host\Ps1RuntimeClockTests.cpp') -Destination (Join-Path $runRoot 'tests\host\Ps1RuntimeClockTests.cpp')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'tests\host\Ps1CpuProfileWindowTests.cpp') -Destination (Join-Path $runRoot 'tests\host\Ps1CpuProfileWindowTests.cpp')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'tests\host\Ps1EmbeddedArchiveTests.cpp') -Destination (Join-Path $runRoot 'tests\host\Ps1EmbeddedArchiveTests.cpp')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'tests\host\Ps1AuthoredProjectionTests.cpp') -Destination (Join-Path $runRoot 'tests\host\Ps1AuthoredProjectionTests.cpp')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'tests\host\Ps1AuthoredLightingTests.cpp') -Destination (Join-Path $runRoot 'tests\host\Ps1AuthoredLightingTests.cpp')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'tests\host\Ps1AuthoredUiGeometryTests.cpp') -Destination (Join-Path $runRoot 'tests\host\Ps1AuthoredUiGeometryTests.cpp')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'tests\host\Ps1EmbeddedAssetStreamTests.cpp') -Destination (Join-Path $runRoot 'tests\host\Ps1EmbeddedAssetStreamTests.cpp')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'tests\host\stream-config') -Destination (Join-Path $runRoot 'tests\host\stream-config') -Recurse
Copy-Item -LiteralPath (Join-Path $sourceRoot 'tests\host\test_embed_authored_assets.py') -Destination (Join-Path $runRoot 'tests\host\test_embed_authored_assets.py')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'scripts\embed-authored-assets.py') -Destination (Join-Path $runRoot 'scripts\embed-authored-assets.py')
if ($codegenRuntimeHeaders -ne '') {
    New-Item -ItemType Directory -Force -Path (Join-Path $runRoot 'codegen-runtime') | Out-Null
    Copy-Item -Path (Join-Path $codegenRuntimeHeaders '*') -Destination (Join-Path $runRoot 'codegen-runtime') -Recurse
}
Copy-Item -LiteralPath (Join-Path $sourceRoot 'tests\host\Ps1HashTests.cpp') -Destination (Join-Path $runRoot 'tests\host\Ps1HashTests.cpp')
Copy-Item -Path (Join-Path $sourceRoot 'third_party\nugget\third_party\EASTL\include\*') -Destination (Join-Path $runRoot 'third_party\EASTL\include') -Recurse
Copy-Item -Path (Join-Path $sourceRoot 'third_party\nugget\third_party\EABase\include\Common\*') -Destination (Join-Path $runRoot 'third_party\EABase\include\Common') -Recurse
Copy-Item -LiteralPath (Join-Path $sourceRoot 'src\platform') -Destination (Join-Path $runRoot 'src\platform') -Recurse

$compileLog = Join-Path $runRoot 'host-cpp-compile.log'
$runLog = Join-Path $runRoot 'host-cpp-run.log'
$policyCompileLog = Join-Path $runRoot 'policy-cpp-compile.log'
$policyRunLog = Join-Path $runRoot 'policy-cpp-run.log'
$uiGeometryCompileLog = Join-Path $runRoot 'ui-geometry-cpp-compile.log'
$uiGeometryRunLog = Join-Path $runRoot 'ui-geometry-cpp-run.log'
$sharedPtrCompileLog = Join-Path $runRoot 'shared-ptr-cpp-compile.log'
$sharedPtrRunLog = Join-Path $runRoot 'shared-ptr-cpp-run.log'
$ps1HashCompileLog = Join-Path $runRoot 'ps1-hash-cpp-compile.log'
$ps1HashRunLog = Join-Path $runRoot 'ps1-hash-cpp-run.log'
$runtimeClockCompileLog = Join-Path $runRoot 'runtime-clock-cpp-compile.log'
$runtimeClockRunLog = Join-Path $runRoot 'runtime-clock-cpp-run.log'
$cpuProfileWindowCompileLog = Join-Path $runRoot 'cpu-profile-window-cpp-compile.log'
$cpuProfileWindowRunLog = Join-Path $runRoot 'cpu-profile-window-cpp-run.log'
$archiveCompileLog = Join-Path $runRoot 'embedded-archive-cpp-compile.log'
$archiveRunLog = Join-Path $runRoot 'embedded-archive-cpp-run.log'
$projectionCompileLog = Join-Path $runRoot 'authored-projection-cpp-compile.log'
$projectionRunLog = Join-Path $runRoot 'authored-projection-cpp-run.log'
$lightingCompileLog = Join-Path $runRoot 'authored-lighting-cpp-compile.log'
$lightingRunLog = Join-Path $runRoot 'authored-lighting-cpp-run.log'
$streamCompileLog = Join-Path $runRoot 'embedded-stream-cpp-compile.log'
$streamRunLog = Join-Path $runRoot 'embedded-stream-cpp-run.log'
$streamFailLog = Join-Path $runRoot 'embedded-stream-cpp-fail.log'
$pythonLog = Join-Path $runRoot 'python-tests.log'
$compileStdout = Join-Path $runRoot 'host-cpp-compile.stdout.log'
$compileStderr = Join-Path $runRoot 'host-cpp-compile.stderr.log'
$runStdout = Join-Path $runRoot 'host-cpp-run.stdout.log'
$runStderr = Join-Path $runRoot 'host-cpp-run.stderr.log'
$policyCompileStdout = Join-Path $runRoot 'policy-cpp-compile.stdout.log'
$policyCompileStderr = Join-Path $runRoot 'policy-cpp-compile.stderr.log'
$policyRunStdout = Join-Path $runRoot 'policy-cpp-run.stdout.log'
$policyRunStderr = Join-Path $runRoot 'policy-cpp-run.stderr.log'
$uiGeometryCompileStdout = Join-Path $runRoot 'ui-geometry-cpp-compile.stdout.log'
$uiGeometryCompileStderr = Join-Path $runRoot 'ui-geometry-cpp-compile.stderr.log'
$uiGeometryRunStdout = Join-Path $runRoot 'ui-geometry-cpp-run.stdout.log'
$uiGeometryRunStderr = Join-Path $runRoot 'ui-geometry-cpp-run.stderr.log'
$sharedPtrCompileStdout = Join-Path $runRoot 'shared-ptr-cpp-compile.stdout.log'
$sharedPtrCompileStderr = Join-Path $runRoot 'shared-ptr-cpp-compile.stderr.log'
$sharedPtrRunStdout = Join-Path $runRoot 'shared-ptr-cpp-run.stdout.log'
$sharedPtrRunStderr = Join-Path $runRoot 'shared-ptr-cpp-run.stderr.log'
$ps1HashCompileStdout = Join-Path $runRoot 'ps1-hash-cpp-compile.stdout.log'
$ps1HashCompileStderr = Join-Path $runRoot 'ps1-hash-cpp-compile.stderr.log'
$ps1HashRunStdout = Join-Path $runRoot 'ps1-hash-cpp-run.stdout.log'
$ps1HashRunStderr = Join-Path $runRoot 'ps1-hash-cpp-run.stderr.log'
$runtimeClockCompileStdout = Join-Path $runRoot 'runtime-clock-cpp-compile.stdout.log'
$runtimeClockCompileStderr = Join-Path $runRoot 'runtime-clock-cpp-compile.stderr.log'
$runtimeClockRunStdout = Join-Path $runRoot 'runtime-clock-cpp-run.stdout.log'
$runtimeClockRunStderr = Join-Path $runRoot 'runtime-clock-cpp-run.stderr.log'
$cpuProfileWindowCompileStdout = Join-Path $runRoot 'cpu-profile-window-cpp-compile.stdout.log'
$cpuProfileWindowCompileStderr = Join-Path $runRoot 'cpu-profile-window-cpp-compile.stderr.log'
$cpuProfileWindowRunStdout = Join-Path $runRoot 'cpu-profile-window-cpp-run.stdout.log'
$cpuProfileWindowRunStderr = Join-Path $runRoot 'cpu-profile-window-cpp-run.stderr.log'
$projectionCompileStdout = Join-Path $runRoot 'authored-projection-cpp-compile.stdout.log'
$projectionCompileStderr = Join-Path $runRoot 'authored-projection-cpp-compile.stderr.log'
$projectionRunStdout = Join-Path $runRoot 'authored-projection-cpp-run.stdout.log'
$projectionRunStderr = Join-Path $runRoot 'authored-projection-cpp-run.stderr.log'
$lightingCompileStdout = Join-Path $runRoot 'authored-lighting-cpp-compile.stdout.log'
$lightingCompileStderr = Join-Path $runRoot 'authored-lighting-cpp-compile.stderr.log'
$lightingRunStdout = Join-Path $runRoot 'authored-lighting-cpp-run.stdout.log'
$lightingRunStderr = Join-Path $runRoot 'authored-lighting-cpp-run.stderr.log'
$archiveCompileStdout = Join-Path $runRoot 'embedded-archive-cpp-compile.stdout.log'
$archiveCompileStderr = Join-Path $runRoot 'embedded-archive-cpp-compile.stderr.log'
$archiveRunStdout = Join-Path $runRoot 'embedded-archive-cpp-run.stdout.log'
$archiveRunStderr = Join-Path $runRoot 'embedded-archive-cpp-run.stderr.log'
$streamCompileStdout = Join-Path $runRoot 'embedded-stream-cpp-compile.stdout.log'
$streamCompileStderr = Join-Path $runRoot 'embedded-stream-cpp-compile.stderr.log'
$streamRunStdout = Join-Path $runRoot 'embedded-stream-cpp-run.stdout.log'
$streamRunStderr = Join-Path $runRoot 'embedded-stream-cpp-run.stderr.log'
$streamFailStdout = Join-Path $runRoot 'embedded-stream-cpp-fail.stdout.log'
$streamFailStderr = Join-Path $runRoot 'embedded-stream-cpp-fail.stderr.log'
$pythonStdout = Join-Path $runRoot 'python-tests.stdout.log'
$pythonStderr = Join-Path $runRoot 'python-tests.stderr.log'

$compileArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    'g++', '-std=c++20', '-Wall', '-Wextra', '/work/host_smoke.cpp', '-o', '/work/host_smoke'
)
$compileResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $compileArgs -WorkingDirectory $runRoot -StdoutPath $compileStdout -StderrPath $compileStderr
Merge-ProcessLogs -StdoutPath $compileResult.StdoutPath -StderrPath $compileResult.StderrPath -CombinedPath $compileLog
Add-ExitMarker -LogPath $compileLog -Name 'HOST_CPP_COMPILE' -ExitCode $compileResult.ExitCode
if ($compileResult.ExitCode -ne 0) { throw ("Host C++ compile failed with exit code {0}{1}{2}" -f $compileResult.ExitCode, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $compileResult.StdoutPath -StderrPath $compileResult.StderrPath)) }

$runArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    '/work/host_smoke'
)
$runResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $runArgs -WorkingDirectory $runRoot -StdoutPath $runStdout -StderrPath $runStderr
Merge-ProcessLogs -StdoutPath $runResult.StdoutPath -StderrPath $runResult.StderrPath -CombinedPath $runLog
Add-ExitMarker -LogPath $runLog -Name 'HOST_CPP_RUN' -ExitCode $runResult.ExitCode
if ($runResult.ExitCode -ne 0) { throw ("Host C++ executable failed with exit code {0}{1}{2}" -f $runResult.ExitCode, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $runResult.StdoutPath -StderrPath $runResult.StderrPath)) }

$uiGeometryCompileArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    'g++', '-std=c++20', '-Wall', '-Wextra', '-Isrc',
    '/work/tests/host/Ps1AuthoredUiGeometryTests.cpp', '-o', '/work/ps1_ui_geometry_tests'
)
$uiGeometryCompileResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $uiGeometryCompileArgs -WorkingDirectory $runRoot -StdoutPath $uiGeometryCompileStdout -StderrPath $uiGeometryCompileStderr
Merge-ProcessLogs -StdoutPath $uiGeometryCompileResult.StdoutPath -StderrPath $uiGeometryCompileResult.StderrPath -CombinedPath $uiGeometryCompileLog
Add-ExitMarker -LogPath $uiGeometryCompileLog -Name 'UI_GEOMETRY_CPP_COMPILE' -ExitCode $uiGeometryCompileResult.ExitCode
if ($uiGeometryCompileResult.ExitCode -ne 0) { throw ("PS1 UI geometry compile failed with exit code {0}{1}{2}" -f $uiGeometryCompileResult.ExitCode, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $uiGeometryCompileStdout -StderrPath $uiGeometryCompileStderr)) }
$uiGeometryRunArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    '/work/ps1_ui_geometry_tests'
)
$uiGeometryRunResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $uiGeometryRunArgs -WorkingDirectory $runRoot -StdoutPath $uiGeometryRunStdout -StderrPath $uiGeometryRunStderr
Merge-ProcessLogs -StdoutPath $uiGeometryRunResult.StdoutPath -StderrPath $uiGeometryRunResult.StderrPath -CombinedPath $uiGeometryRunLog
Add-ExitMarker -LogPath $uiGeometryRunLog -Name 'UI_GEOMETRY_CPP_RUN' -ExitCode $uiGeometryRunResult.ExitCode
if ($uiGeometryRunResult.ExitCode -ne 0) { throw ("PS1 UI geometry tests failed with exit code {0}{1}{2}" -f $uiGeometryRunResult.ExitCode, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $uiGeometryRunStdout -StderrPath $uiGeometryRunStderr)) }

$policyCompileArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    'g++', '-std=c++20', '-Wall', '-Wextra', '-Isrc',
    '/work/tests/host/Ps1RenderPolicyTests.cpp',
    '/work/src/platform/ps1/rendering/Ps1CubeMesh.cpp',
    '/work/src/platform/ps1/rendering/Ps1RotationClock.cpp',
    '-o', '/work/ps1_render_policy_tests'
)
$policyCompileResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $policyCompileArgs -WorkingDirectory $runRoot -StdoutPath $policyCompileStdout -StderrPath $policyCompileStderr
Merge-ProcessLogs -StdoutPath $policyCompileResult.StdoutPath -StderrPath $policyCompileResult.StderrPath -CombinedPath $policyCompileLog
Add-ExitMarker -LogPath $policyCompileLog -Name 'POLICY_CPP_COMPILE' -ExitCode $policyCompileResult.ExitCode
if ($policyCompileResult.ExitCode -ne 0) { throw ("PS1 render policy compile failed with exit code {0}{1}{2}" -f $policyCompileResult.ExitCode, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $policyCompileResult.StdoutPath -StderrPath $policyCompileResult.StderrPath)) }

$policyRunArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    '/work/ps1_render_policy_tests'
)
$policyRunResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $policyRunArgs -WorkingDirectory $runRoot -StdoutPath $policyRunStdout -StderrPath $policyRunStderr
Merge-ProcessLogs -StdoutPath $policyRunResult.StdoutPath -StderrPath $policyRunResult.StderrPath -CombinedPath $policyRunLog
Add-ExitMarker -LogPath $policyRunLog -Name 'POLICY_CPP_RUN' -ExitCode $policyRunResult.ExitCode
if ($policyRunResult.ExitCode -ne 0) { throw ("PS1 render policy executable failed with exit code {0}{1}{2}" -f $policyRunResult.ExitCode, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $policyRunResult.StdoutPath -StderrPath $policyRunResult.StderrPath)) }

$sharedPtrCompileArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    'g++', '-std=c++20', '-Wall', '-Wextra', '-Isrc',
    '/work/tests/host/Ps1SharedPtrTests.cpp', '-o', '/work/ps1_shared_ptr_tests'
)
$sharedPtrCompileResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $sharedPtrCompileArgs -WorkingDirectory $runRoot -StdoutPath $sharedPtrCompileStdout -StderrPath $sharedPtrCompileStderr
Merge-ProcessLogs -StdoutPath $sharedPtrCompileResult.StdoutPath -StderrPath $sharedPtrCompileResult.StderrPath -CombinedPath $sharedPtrCompileLog
Add-ExitMarker -LogPath $sharedPtrCompileLog -Name 'SHARED_PTR_CPP_COMPILE' -ExitCode $sharedPtrCompileResult.ExitCode
if ($sharedPtrCompileResult.ExitCode -ne 0) { throw ("PS1 shared ownership compile failed with exit code {0}{1}{2}" -f $sharedPtrCompileResult.ExitCode, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $sharedPtrCompileResult.StdoutPath -StderrPath $sharedPtrCompileResult.StderrPath)) }

$sharedPtrRunArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    '/work/ps1_shared_ptr_tests'
)
$sharedPtrRunResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $sharedPtrRunArgs -WorkingDirectory $runRoot -StdoutPath $sharedPtrRunStdout -StderrPath $sharedPtrRunStderr
Merge-ProcessLogs -StdoutPath $sharedPtrRunResult.StdoutPath -StderrPath $sharedPtrRunResult.StderrPath -CombinedPath $sharedPtrRunLog
Add-ExitMarker -LogPath $sharedPtrRunLog -Name 'SHARED_PTR_CPP_RUN' -ExitCode $sharedPtrRunResult.ExitCode
if ($sharedPtrRunResult.ExitCode -ne 0) { throw ("PS1 shared ownership executable failed with exit code {0}{1}{2}" -f $sharedPtrRunResult.ExitCode, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $sharedPtrRunResult.StdoutPath -StderrPath $sharedPtrRunResult.StderrPath)) }

$ps1HashCompileArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    'g++', '-std=c++20', '-Wall', '-Wextra', '-fsanitize=undefined,float-cast-overflow', '-DEA_COMPILER_HAS_INTTYPES', '-Isrc', '-I/work/third_party/EASTL/include', '-I/work/third_party/EABase/include/Common',
    '/work/tests/host/Ps1HashTests.cpp', '-o', '/work/ps1_hash_tests'
)
$ps1HashCompileResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $ps1HashCompileArgs -WorkingDirectory $runRoot -StdoutPath $ps1HashCompileStdout -StderrPath $ps1HashCompileStderr
Merge-ProcessLogs -StdoutPath $ps1HashCompileResult.StdoutPath -StderrPath $ps1HashCompileResult.StderrPath -CombinedPath $ps1HashCompileLog
Add-ExitMarker -LogPath $ps1HashCompileLog -Name 'PS1_HASH_CPP_COMPILE' -ExitCode $ps1HashCompileResult.ExitCode
if ($ps1HashCompileResult.ExitCode -ne 0) { throw ("PS1 hash compile failed with exit code {0}{1}{2}" -f $ps1HashCompileResult.ExitCode, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $ps1HashCompileResult.StdoutPath -StderrPath $ps1HashCompileResult.StderrPath)) }

$ps1HashRunArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    '/work/ps1_hash_tests'
)
$ps1HashRunResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $ps1HashRunArgs -WorkingDirectory $runRoot -StdoutPath $ps1HashRunStdout -StderrPath $ps1HashRunStderr
Merge-ProcessLogs -StdoutPath $ps1HashRunResult.StdoutPath -StderrPath $ps1HashRunResult.StderrPath -CombinedPath $ps1HashRunLog
Add-ExitMarker -LogPath $ps1HashRunLog -Name 'PS1_HASH_CPP_RUN' -ExitCode $ps1HashRunResult.ExitCode
if ($ps1HashRunResult.ExitCode -ne 0) { throw ("PS1 hash executable failed with exit code {0}{1}{2}" -f $ps1HashRunResult.ExitCode, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $ps1HashRunResult.StdoutPath -StderrPath $ps1HashRunResult.StderrPath)) }

$runtimeClockCompileArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    'g++', '-std=c++20', '-Wall', '-Wextra', '-Isrc',
    '/work/tests/host/Ps1RuntimeClockTests.cpp', '-o', '/work/ps1_runtime_clock_tests'
)
$runtimeClockCompileResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $runtimeClockCompileArgs -WorkingDirectory $runRoot -StdoutPath $runtimeClockCompileStdout -StderrPath $runtimeClockCompileStderr
Merge-ProcessLogs -StdoutPath $runtimeClockCompileResult.StdoutPath -StderrPath $runtimeClockCompileResult.StderrPath -CombinedPath $runtimeClockCompileLog
Add-ExitMarker -LogPath $runtimeClockCompileLog -Name 'RUNTIME_CLOCK_CPP_COMPILE' -ExitCode $runtimeClockCompileResult.ExitCode
if ($runtimeClockCompileResult.ExitCode -ne 0) { throw ("PS1 runtime clock compile failed with exit code {0}{1}{2}" -f $runtimeClockCompileResult.ExitCode, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $runtimeClockCompileResult.StdoutPath -StderrPath $runtimeClockCompileResult.StderrPath)) }

$runtimeClockRunArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    '/work/ps1_runtime_clock_tests'
)
$runtimeClockRunResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $runtimeClockRunArgs -WorkingDirectory $runRoot -StdoutPath $runtimeClockRunStdout -StderrPath $runtimeClockRunStderr
Merge-ProcessLogs -StdoutPath $runtimeClockRunResult.StdoutPath -StderrPath $runtimeClockRunResult.StderrPath -CombinedPath $runtimeClockRunLog
Add-ExitMarker -LogPath $runtimeClockRunLog -Name 'RUNTIME_CLOCK_CPP_RUN' -ExitCode $runtimeClockRunResult.ExitCode
if ($runtimeClockRunResult.ExitCode -ne 0) { throw ("PS1 runtime clock executable failed with exit code {0}{1}{2}" -f $runtimeClockRunResult.ExitCode, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $runtimeClockRunResult.StdoutPath -StderrPath $runtimeClockRunResult.StderrPath)) }


$cpuProfileWindowCompileArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    'g++', '-std=c++20', '-Wall', '-Wextra', '-Isrc',
    '/work/tests/host/Ps1CpuProfileWindowTests.cpp', '/work/src/platform/ps1/profiling/Ps1CpuProfileWindow.cpp',
    '-o', '/work/ps1_cpu_profile_window_tests'
)
$cpuProfileWindowCompileResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $cpuProfileWindowCompileArgs -WorkingDirectory $runRoot -StdoutPath $cpuProfileWindowCompileStdout -StderrPath $cpuProfileWindowCompileStderr
Merge-ProcessLogs -StdoutPath $cpuProfileWindowCompileResult.StdoutPath -StderrPath $cpuProfileWindowCompileResult.StderrPath -CombinedPath $cpuProfileWindowCompileLog
Add-ExitMarker -LogPath $cpuProfileWindowCompileLog -Name 'CPU_PROFILE_WINDOW_CPP_COMPILE' -ExitCode $cpuProfileWindowCompileResult.ExitCode
if ($cpuProfileWindowCompileResult.ExitCode -ne 0) { throw ("PS1 CPU profile window compile failed with exit code {0}{1}{2}" -f $cpuProfileWindowCompileResult.ExitCode, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $cpuProfileWindowCompileResult.StdoutPath -StderrPath $cpuProfileWindowCompileResult.StderrPath)) }

$cpuProfileWindowRunArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    '/work/ps1_cpu_profile_window_tests'
)
$cpuProfileWindowRunResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $cpuProfileWindowRunArgs -WorkingDirectory $runRoot -StdoutPath $cpuProfileWindowRunStdout -StderrPath $cpuProfileWindowRunStderr
Merge-ProcessLogs -StdoutPath $cpuProfileWindowRunResult.StdoutPath -StderrPath $cpuProfileWindowRunResult.StderrPath -CombinedPath $cpuProfileWindowRunLog
Add-ExitMarker -LogPath $cpuProfileWindowRunLog -Name 'CPU_PROFILE_WINDOW_CPP_RUN' -ExitCode $cpuProfileWindowRunResult.ExitCode
if ($cpuProfileWindowRunResult.ExitCode -ne 0) { throw ("PS1 CPU profile window executable failed with exit code {0}{1}{2}" -f $cpuProfileWindowRunResult.ExitCode, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $cpuProfileWindowRunResult.StdoutPath -StderrPath $cpuProfileWindowRunResult.StderrPath)) }
$projectionCompileArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    'g++', '-std=c++20', '-Wall', '-Wextra', '-Isrc',
    '/work/tests/host/Ps1AuthoredProjectionTests.cpp', '-o', '/work/ps1_authored_projection_tests'
)
$projectionCompileResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $projectionCompileArgs -WorkingDirectory $runRoot -StdoutPath $projectionCompileStdout -StderrPath $projectionCompileStderr
Merge-ProcessLogs -StdoutPath $projectionCompileResult.StdoutPath -StderrPath $projectionCompileResult.StderrPath -CombinedPath $projectionCompileLog
Add-ExitMarker -LogPath $projectionCompileLog -Name 'AUTHORED_PROJECTION_CPP_COMPILE' -ExitCode $projectionCompileResult.ExitCode
if ($projectionCompileResult.ExitCode -ne 0) { throw ("PS1 authored projection compile failed with exit code {0}{1}{2}" -f $projectionCompileResult.ExitCode, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $projectionCompileResult.StdoutPath -StderrPath $projectionCompileResult.StderrPath)) }

$projectionRunArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    '/work/ps1_authored_projection_tests'
)
$projectionRunResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $projectionRunArgs -WorkingDirectory $runRoot -StdoutPath $projectionRunStdout -StderrPath $projectionRunStderr
Merge-ProcessLogs -StdoutPath $projectionRunResult.StdoutPath -StderrPath $projectionRunResult.StderrPath -CombinedPath $projectionRunLog
Add-ExitMarker -LogPath $projectionRunLog -Name 'AUTHORED_PROJECTION_CPP_RUN' -ExitCode $projectionRunResult.ExitCode
if ($projectionRunResult.ExitCode -ne 0) { throw ("PS1 authored projection executable failed with exit code {0}{1}{2}" -f $projectionRunResult.ExitCode, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $projectionRunResult.StdoutPath -StderrPath $projectionRunResult.StderrPath)) }

$lightingCompileArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    'g++', '-std=c++20', '-Wall', '-Wextra', '-Isrc',
    '/work/tests/host/Ps1AuthoredLightingTests.cpp', '-o', '/work/ps1_authored_lighting_tests'
)
$lightingCompileResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $lightingCompileArgs -WorkingDirectory $runRoot -StdoutPath $lightingCompileStdout -StderrPath $lightingCompileStderr
Merge-ProcessLogs -StdoutPath $lightingCompileResult.StdoutPath -StderrPath $lightingCompileResult.StderrPath -CombinedPath $lightingCompileLog
Add-ExitMarker -LogPath $lightingCompileLog -Name 'AUTHORED_LIGHTING_CPP_COMPILE' -ExitCode $lightingCompileResult.ExitCode
if ($lightingCompileResult.ExitCode -ne 0) { throw ("PS1 authored lighting compile failed with exit code {0}{1}{2}" -f $lightingCompileResult.ExitCode, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $lightingCompileResult.StdoutPath -StderrPath $lightingCompileResult.StderrPath)) }

$lightingRunArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    '/work/ps1_authored_lighting_tests'
)
$lightingRunResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $lightingRunArgs -WorkingDirectory $runRoot -StdoutPath $lightingRunStdout -StderrPath $lightingRunStderr
Merge-ProcessLogs -StdoutPath $lightingRunResult.StdoutPath -StderrPath $lightingRunResult.StderrPath -CombinedPath $lightingRunLog
Add-ExitMarker -LogPath $lightingRunLog -Name 'AUTHORED_LIGHTING_CPP_RUN' -ExitCode $lightingRunResult.ExitCode
if ($lightingRunResult.ExitCode -ne 0) { throw ("PS1 authored lighting executable failed with exit code {0}{1}{2}" -f $lightingRunResult.ExitCode, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $lightingRunResult.StdoutPath -StderrPath $lightingRunResult.StderrPath)) }

$archiveCompileArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    'g++', '-std=c++20', '-Wall', '-Wextra', '-Isrc',
    '/work/tests/host/Ps1EmbeddedArchiveTests.cpp', '-o', '/work/ps1_embedded_archive_tests'
)
$archiveCompileResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $archiveCompileArgs -WorkingDirectory $runRoot -StdoutPath $archiveCompileStdout -StderrPath $archiveCompileStderr
Merge-ProcessLogs -StdoutPath $archiveCompileResult.StdoutPath -StderrPath $archiveCompileResult.StderrPath -CombinedPath $archiveCompileLog
Add-ExitMarker -LogPath $archiveCompileLog -Name 'EMBEDDED_ARCHIVE_CPP_COMPILE' -ExitCode $archiveCompileResult.ExitCode
if ($archiveCompileResult.ExitCode -ne 0) { throw ("PS1 embedded archive compile failed with exit code {0}{1}{2}" -f $archiveCompileResult.ExitCode, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $archiveCompileResult.StdoutPath -StderrPath $archiveCompileResult.StderrPath)) }

$archiveRunArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    '/work/ps1_embedded_archive_tests'
)
$archiveRunResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $archiveRunArgs -WorkingDirectory $runRoot -StdoutPath $archiveRunStdout -StderrPath $archiveRunStderr
Merge-ProcessLogs -StdoutPath $archiveRunResult.StdoutPath -StderrPath $archiveRunResult.StderrPath -CombinedPath $archiveRunLog
Add-ExitMarker -LogPath $archiveRunLog -Name 'EMBEDDED_ARCHIVE_CPP_RUN' -ExitCode $archiveRunResult.ExitCode
if ($archiveRunResult.ExitCode -ne 0) { throw ("PS1 embedded archive executable failed with exit code {0}{1}{2}" -f $archiveRunResult.ExitCode, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $archiveRunResult.StdoutPath -StderrPath $archiveRunResult.StderrPath)) }

# The stream test needs the shared runtime headers; without them it is recorded as skipped, never as passed.
$streamStatus = 'skipped'
$streamCompileExit = $null
$streamRunExit = $null
$streamFailExit = $null
if ($codegenRuntimeHeaders -ne '') {
    $streamCompileArgs = @(
        'run', '--rm',
        '--mount', "type=bind,source=$runRoot,target=/work",
        '--workdir', '/work',
        $dockerImage,
        'g++', '-std=c++20', '-fno-exceptions', '-fno-rtti', '-Wall', '-Wextra', '-DHE_CPP_TEST_HOST',
        '-Isrc', '-I/work/tests/host/stream-config', '-I/work/codegen-runtime',
        '/work/tests/host/Ps1EmbeddedAssetStreamTests.cpp', '-o', '/work/ps1_embedded_stream_tests'
    )
    $streamCompileResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $streamCompileArgs -WorkingDirectory $runRoot -StdoutPath $streamCompileStdout -StderrPath $streamCompileStderr
    Merge-ProcessLogs -StdoutPath $streamCompileResult.StdoutPath -StderrPath $streamCompileResult.StderrPath -CombinedPath $streamCompileLog
    Add-ExitMarker -LogPath $streamCompileLog -Name 'EMBEDDED_STREAM_CPP_COMPILE' -ExitCode $streamCompileResult.ExitCode
    $streamCompileExit = $streamCompileResult.ExitCode
    if ($streamCompileResult.ExitCode -ne 0) { throw ("PS1 embedded stream compile failed with exit code {0}{1}{2}" -f $streamCompileResult.ExitCode, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $streamCompileResult.StdoutPath -StderrPath $streamCompileResult.StderrPath)) }

    $streamRunArgs = @(
        'run', '--rm',
        '--mount', "type=bind,source=$runRoot,target=/work",
        '--workdir', '/work',
        $dockerImage,
        '/work/ps1_embedded_stream_tests'
    )
    $streamRunResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $streamRunArgs -WorkingDirectory $runRoot -StdoutPath $streamRunStdout -StderrPath $streamRunStderr
    Merge-ProcessLogs -StdoutPath $streamRunResult.StdoutPath -StderrPath $streamRunResult.StderrPath -CombinedPath $streamRunLog
    Add-ExitMarker -LogPath $streamRunLog -Name 'EMBEDDED_STREAM_CPP_RUN' -ExitCode $streamRunResult.ExitCode
    $streamRunExit = $streamRunResult.ExitCode
    if ($streamRunResult.ExitCode -ne 0) { throw ("PS1 embedded stream executable failed with exit code {0}{1}{2}" -f $streamRunResult.ExitCode, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $streamRunResult.StdoutPath -StderrPath $streamRunResult.StderrPath)) }

    # Seeking outside the asset must route to the runtime failure hook, which exits with 73.
    $streamFailArgs = $streamRunArgs + @('fail')
    $streamFailResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $streamFailArgs -WorkingDirectory $runRoot -StdoutPath $streamFailStdout -StderrPath $streamFailStderr
    Merge-ProcessLogs -StdoutPath $streamFailResult.StdoutPath -StderrPath $streamFailResult.StderrPath -CombinedPath $streamFailLog
    Add-ExitMarker -LogPath $streamFailLog -Name 'EMBEDDED_STREAM_CPP_FAIL' -ExitCode $streamFailResult.ExitCode
    $streamFailExit = $streamFailResult.ExitCode
    if ($streamFailResult.ExitCode -ne 73) { throw ("PS1 embedded stream failure path exited with {0} instead of 73" -f $streamFailResult.ExitCode) }
    $streamStatus = 'passed'
}

$pythonArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    'python3', '-B', '-m', 'unittest', 'discover', '-s', '/work/tests/host', '-p', 'test_*.py', '-v'
)
$pythonResult = Invoke-CapturedProcess -FilePath 'docker.exe' -ArgumentList $pythonArgs -WorkingDirectory $runRoot -StdoutPath $pythonStdout -StderrPath $pythonStderr
Merge-ProcessLogs -StdoutPath $pythonResult.StdoutPath -StderrPath $pythonResult.StderrPath -CombinedPath $pythonLog
Add-ExitMarker -LogPath $pythonLog -Name 'PYTHON_TESTS' -ExitCode $pythonResult.ExitCode
if ($pythonResult.ExitCode -ne 0) { throw ("Python unit tests failed with exit code {0}{1}{2}" -f $pythonResult.ExitCode, [Environment]::NewLine, (Get-ProcessFailureDetail -StdoutPath $pythonResult.StdoutPath -StderrPath $pythonResult.StderrPath)) }

$summary = [ordered]@{
    schema = 2
    run_directory = (Resolve-Path -LiteralPath $runRoot).Path
    host_cpp_compile = $compileLog
    host_cpp_run = $runLog
    policy_cpp_compile = $policyCompileLog
    policy_cpp_run = $policyRunLog
    shared_ptr_cpp_compile = $sharedPtrCompileLog
    shared_ptr_cpp_run = $sharedPtrRunLog
    ps1_hash_cpp_compile = $ps1HashCompileLog
    ps1_hash_cpp_run = $ps1HashRunLog
    runtime_clock_cpp_compile = $runtimeClockCompileLog
    runtime_clock_cpp_run = $runtimeClockRunLog
    cpu_profile_window_cpp_compile = $cpuProfileWindowCompileLog
    cpu_profile_window_cpp_run = $cpuProfileWindowRunLog
    embedded_archive_cpp_compile = $archiveCompileLog
    embedded_archive_cpp_run = $archiveRunLog
    authored_projection_cpp_compile = $projectionCompileLog
    authored_projection_cpp_run = $projectionRunLog
    authored_lighting_cpp_compile = $lightingCompileLog
    authored_lighting_cpp_run = $lightingRunLog
    embedded_stream_status = $streamStatus
    embedded_stream_cpp_compile = $streamCompileLog
    embedded_stream_cpp_run = $streamRunLog
    embedded_stream_cpp_fail = $streamFailLog
    python_tests = $pythonLog
    exit_codes = [ordered]@{
        host_cpp_compile = $compileResult.ExitCode
        host_cpp_run = $runResult.ExitCode
        policy_cpp_compile = $policyCompileResult.ExitCode
        policy_cpp_run = $policyRunResult.ExitCode
        shared_ptr_cpp_compile = $sharedPtrCompileResult.ExitCode
        shared_ptr_cpp_run = $sharedPtrRunResult.ExitCode
        ps1_hash_cpp_compile = $ps1HashCompileResult.ExitCode
        ps1_hash_cpp_run = $ps1HashRunResult.ExitCode
        runtime_clock_cpp_compile = $runtimeClockCompileResult.ExitCode
        runtime_clock_cpp_run = $runtimeClockRunResult.ExitCode
        cpu_profile_window_cpp_compile = $cpuProfileWindowCompileResult.ExitCode
        cpu_profile_window_cpp_run = $cpuProfileWindowRunResult.ExitCode
        embedded_archive_cpp_compile = $archiveCompileResult.ExitCode
        embedded_archive_cpp_run = $archiveRunResult.ExitCode
        authored_projection_cpp_compile = $projectionCompileResult.ExitCode
        authored_projection_cpp_run = $projectionRunResult.ExitCode
        authored_lighting_cpp_compile = $lightingCompileResult.ExitCode
        authored_lighting_cpp_run = $lightingRunResult.ExitCode
        embedded_stream_cpp_compile = $streamCompileExit
        embedded_stream_cpp_run = $streamRunExit
        embedded_stream_cpp_fail = $streamFailExit
        python_tests = $pythonResult.ExitCode
    }
    native_streams = [ordered]@{
        host_cpp_compile_stdout = $compileStdout
        host_cpp_compile_stderr = $compileStderr
        host_cpp_run_stdout = $runStdout
        host_cpp_run_stderr = $runStderr
        policy_cpp_compile_stdout = $policyCompileStdout
        policy_cpp_compile_stderr = $policyCompileStderr
        policy_cpp_run_stdout = $policyRunStdout
        policy_cpp_run_stderr = $policyRunStderr
        shared_ptr_cpp_compile_stdout = $sharedPtrCompileStdout
        shared_ptr_cpp_compile_stderr = $sharedPtrCompileStderr
        shared_ptr_cpp_run_stdout = $sharedPtrRunStdout
        shared_ptr_cpp_run_stderr = $sharedPtrRunStderr
        ps1_hash_cpp_compile_stdout = $ps1HashCompileStdout
        ps1_hash_cpp_compile_stderr = $ps1HashCompileStderr
        ps1_hash_cpp_run_stdout = $ps1HashRunStdout
        ps1_hash_cpp_run_stderr = $ps1HashRunStderr
        runtime_clock_cpp_compile_stdout = $runtimeClockCompileStdout
        runtime_clock_cpp_compile_stderr = $runtimeClockCompileStderr
        runtime_clock_cpp_run_stdout = $runtimeClockRunStdout
        runtime_clock_cpp_run_stderr = $runtimeClockRunStderr
        cpu_profile_window_cpp_compile_stdout = $cpuProfileWindowCompileStdout
        cpu_profile_window_cpp_compile_stderr = $cpuProfileWindowCompileStderr
        cpu_profile_window_cpp_run_stdout = $cpuProfileWindowRunStdout
        cpu_profile_window_cpp_run_stderr = $cpuProfileWindowRunStderr
        authored_projection_cpp_compile_stdout = $projectionCompileStdout
        authored_projection_cpp_compile_stderr = $projectionCompileStderr
        authored_projection_cpp_run_stdout = $projectionRunStdout
        authored_projection_cpp_run_stderr = $projectionRunStderr
        authored_lighting_cpp_compile_stdout = $lightingCompileStdout
        authored_lighting_cpp_compile_stderr = $lightingCompileStderr
        authored_lighting_cpp_run_stdout = $lightingRunStdout
        authored_lighting_cpp_run_stderr = $lightingRunStderr
        embedded_archive_cpp_compile_stdout = $archiveCompileStdout
        embedded_archive_cpp_compile_stderr = $archiveCompileStderr
        embedded_archive_cpp_run_stdout = $archiveRunStdout
        embedded_archive_cpp_run_stderr = $archiveRunStderr
        embedded_stream_cpp_compile_stdout = $streamCompileStdout
        embedded_stream_cpp_compile_stderr = $streamCompileStderr
        embedded_stream_cpp_run_stdout = $streamRunStdout
        embedded_stream_cpp_run_stderr = $streamRunStderr
        embedded_stream_cpp_fail_stdout = $streamFailStdout
        embedded_stream_cpp_fail_stderr = $streamFailStderr
        python_stdout = $pythonStdout
        python_stderr = $pythonStderr
    }
    python_runtime = "docker image $dockerImage"
}
$summary | ConvertTo-Json -Depth 4 | Out-File -LiteralPath (Join-Path $runRoot 'summary.json') -Encoding utf8
Write-Output (Resolve-Path -LiteralPath (Join-Path $runRoot 'summary.json')).Path
