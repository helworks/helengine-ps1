[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$sourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
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
Copy-Item -LiteralPath (Join-Path $sourceRoot 'tests\host\Ps1HashTests.cpp') -Destination (Join-Path $runRoot 'tests\host\Ps1HashTests.cpp')
Copy-Item -Path (Join-Path $sourceRoot 'third_party\nugget\third_party\EASTL\include\*') -Destination (Join-Path $runRoot 'third_party\EASTL\include') -Recurse
Copy-Item -Path (Join-Path $sourceRoot 'third_party\nugget\third_party\EABase\include\Common\*') -Destination (Join-Path $runRoot 'third_party\EABase\include\Common') -Recurse
Copy-Item -LiteralPath (Join-Path $sourceRoot 'src\platform') -Destination (Join-Path $runRoot 'src\platform') -Recurse

$compileLog = Join-Path $runRoot 'host-cpp-compile.log'
$runLog = Join-Path $runRoot 'host-cpp-run.log'
$policyCompileLog = Join-Path $runRoot 'policy-cpp-compile.log'
$policyRunLog = Join-Path $runRoot 'policy-cpp-run.log'
$sharedPtrCompileLog = Join-Path $runRoot 'shared-ptr-cpp-compile.log'
$sharedPtrRunLog = Join-Path $runRoot 'shared-ptr-cpp-run.log'
$ps1HashCompileLog = Join-Path $runRoot 'ps1-hash-cpp-compile.log'
$ps1HashRunLog = Join-Path $runRoot 'ps1-hash-cpp-run.log'
$runtimeClockCompileLog = Join-Path $runRoot 'runtime-clock-cpp-compile.log'
$runtimeClockRunLog = Join-Path $runRoot 'runtime-clock-cpp-run.log'
$pythonLog = Join-Path $runRoot 'python-tests.log'
$compileStdout = Join-Path $runRoot 'host-cpp-compile.stdout.log'
$compileStderr = Join-Path $runRoot 'host-cpp-compile.stderr.log'
$runStdout = Join-Path $runRoot 'host-cpp-run.stdout.log'
$runStderr = Join-Path $runRoot 'host-cpp-run.stderr.log'
$policyCompileStdout = Join-Path $runRoot 'policy-cpp-compile.stdout.log'
$policyCompileStderr = Join-Path $runRoot 'policy-cpp-compile.stderr.log'
$policyRunStdout = Join-Path $runRoot 'policy-cpp-run.stdout.log'
$policyRunStderr = Join-Path $runRoot 'policy-cpp-run.stderr.log'
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
        python_stdout = $pythonStdout
        python_stderr = $pythonStderr
    }
    python_runtime = "docker image $dockerImage"
}
$summary | ConvertTo-Json -Depth 4 | Out-File -LiteralPath (Join-Path $runRoot 'summary.json') -Encoding utf8
Write-Output (Resolve-Path -LiteralPath (Join-Path $runRoot 'summary.json')).Path
