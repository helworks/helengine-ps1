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
Copy-Item -LiteralPath (Join-Path $sourceRoot 'scripts\check-retail-memory.py') -Destination (Join-Path $runRoot 'scripts\check-retail-memory.py')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'tests\host\test_retail_memory.py') -Destination (Join-Path $runRoot 'tests\host\test_retail_memory.py')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'tests\host\host_smoke.cpp') -Destination (Join-Path $runRoot 'host_smoke.cpp')

$compileLog = Join-Path $runRoot 'host-cpp-compile.log'
$runLog = Join-Path $runRoot 'host-cpp-run.log'
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

$pythonArgs = @('-B', '-m', 'unittest', 'discover', '-s', (Join-Path $runRoot 'tests\host'), '-p', 'test_*.py', '-v')
& python @pythonArgs 2>&1 | Tee-Object -FilePath $pythonLog
if ($LASTEXITCODE -ne 0) { throw "Python unit tests failed with exit code $LASTEXITCODE" }

$summary = [ordered]@{
    schema = 1
    run_directory = (Resolve-Path -LiteralPath $runRoot).Path
    host_cpp_compile = $compileLog
    host_cpp_run = $runLog
    python_tests = $pythonLog
}
$summary | ConvertTo-Json -Depth 4 | Out-File -LiteralPath (Join-Path $runRoot 'summary.json') -Encoding utf8
Write-Output (Resolve-Path -LiteralPath (Join-Path $runRoot 'summary.json')).Path
