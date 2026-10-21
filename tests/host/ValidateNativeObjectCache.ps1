param([Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
$helper=Join-Path $PSScriptRoot '../../scripts/native-object-cache.ps1'
if(!(Test-Path -LiteralPath $helper)){throw 'Fixed native output must discard leftover compiler outputs before restoring a matching cache.'}
. $helper
$fixture=[IO.Path]::GetFullPath($Output)
$run=Join-Path $fixture 'run'
$cache=Join-Path $fixture 'cache'
$nested=Join-Path $run 'generated-core'
New-Item -ItemType Directory -Force -Path $nested,$cache | Out-Null
foreach($name in @('old.o','old.dep','old.a','keep.cpp','keep.hpp','keep.bin')){[IO.File]::WriteAllText((Join-Path $nested $name),'fixture')}
[IO.File]::WriteAllText((Join-Path $cache 'matching.o'),'matching cache')
Reset-NativeCompilerOutputs -Root $run
foreach($name in @('old.o','old.dep','old.a')){if(Test-Path -LiteralPath (Join-Path $nested $name)){throw "Stale compiler output survived: $name"}}
foreach($name in @('keep.cpp','keep.hpp','keep.bin')){if(!(Test-Path -LiteralPath (Join-Path $nested $name))){throw "Input removed: $name"}}
if(!(Test-Path -LiteralPath (Join-Path $cache 'matching.o'))){throw 'Matching cache outside the run root was modified.'}
Write-Output 'PASS fixed native output reset: stale objects/dependencies/archives removed; sources, content and sibling cache preserved.'
