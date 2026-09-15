[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$sourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildRoot = Join-Path (Split-Path $sourceRoot -Parent) 'builds\helengine-ps1'
$runName = 'host-tests-{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'), ([guid]::NewGuid().ToString('N'))
$runRoot = Join-Path $buildRoot $runName
$dockerImage = 'ghcr.io/grumpycoders/pcsx-redux-build@sha256:4f99ff0dd972096280cd6faf27c4255cc2bcf932397f8f9276ee22b6b1a67555'

New-Item -ItemType Directory -Force -Path (Join-Path $runRoot 'scripts') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $runRoot 'tests\host') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $runRoot 'src') | Out-Null
Copy-Item -LiteralPath (Join-Path $sourceRoot 'scripts\check-retail-memory.py') -Destination (Join-Path $runRoot 'scripts\check-retail-memory.py')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'tests\host\test_retail_memory.py') -Destination (Join-Path $runRoot 'tests\host\test_retail_memory.py')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'tests\host\host_smoke.cpp') -Destination (Join-Path $runRoot 'host_smoke.cpp')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'tests\host\Ps1RenderPolicyTests.cpp') -Destination (Join-Path $runRoot 'tests\host\Ps1RenderPolicyTests.cpp')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'src\platform') -Destination (Join-Path $runRoot 'src\platform') -Recurse

$compileLog = Join-Path $runRoot 'host-cpp-compile.log'
$runLog = Join-Path $runRoot 'host-cpp-run.log'
$policyCompileLog = Join-Path $runRoot 'policy-cpp-compile.log'
$policyRunLog = Join-Path $runRoot 'policy-cpp-run.log'
$pythonLog = Join-Path $runRoot 'python-tests.log'
$binary = Join-Path $runRoot 'host_smoke'

$compileArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    'g++', '-std=c++20', '-Wall', '-Wextra', '/work/host_smoke.cpp', '-o', '/work/host_smoke'
)
& docker @compileArgs 2>&1 | Tee-Object -FilePath $compileLog | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Host C++ compile failed with exit code $LASTEXITCODE" }

$runArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    '/work/host_smoke'
)
& docker @runArgs 2>&1 | Tee-Object -FilePath $runLog | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Host C++ executable failed with exit code $LASTEXITCODE" }

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
& docker @policyCompileArgs 2>&1 | Tee-Object -FilePath $policyCompileLog | Out-Null
if ($LASTEXITCODE -ne 0) { throw "PS1 render policy compile failed with exit code $LASTEXITCODE" }

$policyRunArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    '/work/ps1_render_policy_tests'
)
& docker @policyRunArgs 2>&1 | Tee-Object -FilePath $policyRunLog | Out-Null
if ($LASTEXITCODE -ne 0) { throw "PS1 render policy executable failed with exit code $LASTEXITCODE" }

$pythonArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$runRoot,target=/work",
    '--workdir', '/work',
    $dockerImage,
    'python3', '-B', '-m', 'unittest', 'discover', '-s', '/work/tests/host', '-p', 'test_*.py', '-v'
)
# Capture both streams directly so PowerShell does not promote Docker's
# unittest diagnostics to a terminating NativeCommandError.
$pythonErrorLog = Join-Path $runRoot 'python-tests.stderr.log'
$pythonProcess = Start-Process -FilePath 'docker.exe' -ArgumentList $pythonArgs -Wait -NoNewWindow -RedirectStandardOutput $pythonLog -RedirectStandardError $pythonErrorLog -PassThru
$pythonExitCode = $pythonProcess.ExitCode
if (Test-Path -LiteralPath $pythonErrorLog) {
    Get-Content -LiteralPath $pythonErrorLog | Add-Content -LiteralPath $pythonLog
}
if ($pythonExitCode -ne 0) { throw "Python unit tests failed with exit code $pythonExitCode" }

$summary = [ordered]@{
    schema = 1
    run_directory = (Resolve-Path -LiteralPath $runRoot).Path
    host_cpp_compile = $compileLog
    host_cpp_run = $runLog
    policy_cpp_compile = $policyCompileLog
    policy_cpp_run = $policyRunLog
    python_tests = $pythonLog
    python_runtime = "docker image $dockerImage"
}
$summary | ConvertTo-Json -Depth 4 | Out-File -LiteralPath (Join-Path $runRoot 'summary.json') -Encoding utf8
Write-Output (Resolve-Path -LiteralPath (Join-Path $runRoot 'summary.json')).Path
