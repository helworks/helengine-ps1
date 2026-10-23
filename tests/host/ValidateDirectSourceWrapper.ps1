param([Parameter(Mandatory=$true)][string]$FixtureRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if([IO.Path]::DirectorySeparatorChar -ne '/'){throw 'This process integration fixture requires a Unix host.'}
New-Item -ItemType Directory -Force -Path "$FixtureRoot/tools","$FixtureRoot/native","$FixtureRoot/cache"|Out-Null
# A prior real Docker mount leaves only empty mountpoint placeholders. Remove
# those owned empty nodes so this repeatable fixture still detects source copies.
foreach($relative in @('Makefile','toolchain.lock.json','src','scripts','linker','third_party')){
 $placeholder=Join-Path "$FixtureRoot/native" $relative
 if(Test-Path -LiteralPath $placeholder){
   $entry=Get-Item -LiteralPath $placeholder -Force
   if(($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'Fixture mountpoint became a link.'}
   if($entry.PSIsContainer){
     if(@(Get-ChildItem -LiteralPath $placeholder -Force).Count -ne 0){throw "Nonempty source fixture mountpoint: $placeholder"}
   } elseif($entry.Length -ne 0){throw "Nonempty source fixture mountpoint: $placeholder"}
   Remove-Item -LiteralPath $placeholder -Force
 }
}
$beforeIndex=(& git -C "$repo/third_party/nugget" diff --cached --binary) -join "`n"
$fake=@'
#!/bin/sh
printf '%s\n' "$@" > "$HELENGINE_PS1_FIXTURE_ARGS"
printf 'expected fixture stdout\n'
printf 'expected fixture stderr\n' >&2
exit 17
'@
[IO.File]::WriteAllText("$FixtureRoot/tools/docker",$fake)
& chmod +x "$FixtureRoot/tools/docker"
$previousPath=$env:PATH
$env:PATH="$FixtureRoot/tools"+[IO.Path]::PathSeparator+$env:PATH
$env:HELENGINE_PS1_FIXTURE_ARGS="$FixtureRoot/docker-args.txt"
try {
 $psi=[Diagnostics.ProcessStartInfo]::new()
 $psi.FileName=(Get-Process -Id $PID).Path
 $psi.UseShellExecute=$false;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
 foreach($arg in @('-NoProfile','-File',"$repo/scripts/build.ps1",'-Configuration','Release','-RunRoot',"$FixtureRoot/native",'-NativeObjectCacheRoot',"$FixtureRoot/cache")){$psi.ArgumentList.Add($arg)}
 $child=[Diagnostics.Process]::Start($psi)
 $out=$child.StandardOutput.ReadToEndAsync();$err=$child.StandardError.ReadToEndAsync()
 $child.WaitForExit()
 [IO.File]::WriteAllText("$FixtureRoot/wrapper.stdout.log",$out.GetAwaiter().GetResult())
 [IO.File]::WriteAllText("$FixtureRoot/wrapper.stderr.log",$err.GetAwaiter().GetResult())
 if($child.ExitCode -ne 17){throw "Child exit propagation failed: $($child.ExitCode); see wrapper.stderr.log"}
 $argsRead=[IO.File]::ReadAllLines("$FixtureRoot/docker-args.txt")
 foreach($sourceEntry in @('Makefile','toolchain.lock.json','src','scripts','linker','third_party')){
   if($argsRead -cnotcontains "type=bind,source=$repo/$sourceEntry,target=/work/$sourceEntry,readonly"){throw "Real read-only source mount missing: $sourceEntry"}
 }
 if($argsRead -cnotcontains "type=bind,source=$FixtureRoot/cache,target=/nativecache"){throw 'External compiler cache mount missing.'}
 if($argsRead -cnotcontains 'NATIVE_OBJECT_ROOT=/nativecache/objects'){throw 'External make object root was not propagated.'}
 foreach($path in @('Makefile','src','scripts','linker','third_party')){
   if(Test-Path -LiteralPath "$FixtureRoot/native/$path"){throw "Wrapper copied a product source before Docker: $path"}
 }
 if(-not([IO.File]::ReadAllText("$FixtureRoot/native/build.log").Contains('expected fixture stderr'))){throw 'Native stderr was lost.'}
 if(-not([IO.File]::ReadAllText("$FixtureRoot/native/build.log").Contains('expected fixture stdout'))){throw 'Native stdout was lost.'}
 $afterIndex=(& git -C "$repo/third_party/nugget" diff --cached --binary) -join "`n"
 if($afterIndex -cne $beforeIndex){throw 'SDK real index changed while validating the patch.'}
 if(Test-Path "$FixtureRoot/native/manifest.json"){throw 'Failed tool must not publish a successful manifest.'}
 Write-Output 'PASS: real wrapper uses readonly checkout mounts, external cache, exact SDK patch, original Git index, stdout/stderr and exit17; no compiler executed.'
} finally {$env:PATH=$previousPath;Remove-Item Env:HELENGINE_PS1_FIXTURE_ARGS -ErrorAction SilentlyContinue}
