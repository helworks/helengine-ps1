[CmdletBinding()]
param([string] $OutputRoot = 'C:/dev/helworks/builds/helengine-ps1/menu-20261007/scene-modules/native')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output = [IO.Path]::GetFullPath($OutputRoot)
$allowed = [IO.Path]::GetFullPath('C:/dev/helworks/builds/helengine-ps1').TrimEnd('\') + '\'
if (-not $output.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) { throw 'Output must be in PS1 build workspace.' }
New-Item -ItemType Directory -Path $output -Force | Out-Null
$image = (Get-Content -LiteralPath (Join-Path $repoRoot 'toolchain.lock.json') -Raw | ConvertFrom-Json).image.reference
$common = @('run','--rm','--mount',"type=bind,source=$repoRoot,target=/repo,readonly",'--mount',"type=bind,source=$output,target=/proof",$image)
& rtk proxy docker @common g++ -std=c++20 -Wall -Wextra -Werror '-fsanitize=address,undefined' -fno-omit-frame-pointer -I/repo/src /repo/tests/host_scene_module_transfer.cpp -o /proof/host_scene_module_transfer *> (Join-Path $output 'host-compile.log')
if ($LASTEXITCODE -ne 0) { throw 'Module transfer host compile failed.' }
& rtk proxy docker @common /proof/host_scene_module_transfer *> (Join-Path $output 'host-run.log')
if ($LASTEXITCODE -ne 0) { throw 'Module transfer host execution failed.' }
& rtk proxy docker @common g++ -std=c++20 -DPS1_PC_PORT '-fsanitize=address,undefined' -fno-omit-frame-pointer -I/repo/tests/host/loading-policy-spy -I/repo/src /repo/tests/host_scene_module_frames.cpp /repo/src/platform/ps1/rendering/Ps1FrameContext.cpp -o /proof/host_scene_module_frames *> (Join-Path $output 'frame-compile.log')
if ($LASTEXITCODE -ne 0) { throw 'Module fence frame host compile failed.' }
& rtk proxy docker @common /proof/host_scene_module_frames *> (Join-Path $output 'frame-run.log')
if ($LASTEXITCODE -ne 0) { throw 'Module fence frame host execution failed.' }
& rtk proxy docker @common python3 -B /repo/scripts/test-scene-module-sdk.py --repo /repo --output /proof/sdk *> (Join-Path $output 'sdk-command.log')
if ($LASTEXITCODE -ne 0) { throw 'Real SDK module fixtures failed.' }
[ordered]@{ passed=$true; hostSanitizers=@('AddressSanitizer','UndefinedBehaviorSanitizer'); testSourceSha256=(Get-FileHash -LiteralPath (Join-Path $repoRoot 'tests/host_scene_module_transfer.cpp')).Hash; toolchain=$image } |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'transfer-test-receipt.json') -Encoding utf8
