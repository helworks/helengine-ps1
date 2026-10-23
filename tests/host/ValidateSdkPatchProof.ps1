param([Parameter(Mandatory=$true)][string]$FixtureRoot)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
New-Item -ItemType Directory -Force -Path $FixtureRoot|Out-Null
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile("$repo/scripts/build.ps1",[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Wrapper did not parse.'}
$node=$ast.Find({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Confirm-PinnedSdkPatch'},$true)
. ([scriptblock]::Create($node.Extent.Text))
$sdk="$repo/third_party/nugget";$patch="$repo/patches/nugget/0001-frame-completion.patch"
$before=(@(& git -C $sdk diff --no-ext-diff --binary HEAD) -join "`n")
$indexBefore=(@(& git -C $sdk diff --cached --binary) -join "`n")
$hash1=Confirm-PinnedSdkPatch $sdk $patch $FixtureRoot
$hash2=Confirm-PinnedSdkPatch $sdk $patch $FixtureRoot
if($hash1 -cne $hash2 -or $hash1 -notmatch '^[a-f0-9]{64}$'){throw 'Patch proof was not stable/idempotent.'}
$mutant=[IO.File]::ReadAllText($patch).Replace('m_transferSequence = 0;','m_transferSequence = 1;')
if($mutant -ceq [IO.File]::ReadAllText($patch)){throw 'Mutation did not change the patch.'}
[IO.File]::WriteAllText("$FixtureRoot/mutant.patch",$mutant)
$rejected=$false
try {Confirm-PinnedSdkPatch $sdk "$FixtureRoot/mutant.patch" $FixtureRoot|Out-Null} catch {if($_.Exception.Message -notmatch 'SDK changes differ'){throw};$rejected=$true}
if(-not $rejected){throw 'A different valid patch was accepted.'}
if((@(& git -C $sdk diff --no-ext-diff --binary HEAD) -join "`n") -cne $before){throw 'Patch proof changed SDK sources.'}
if((@(& git -C $sdk diff --cached --binary) -join "`n") -cne $indexBefore){throw 'Patch proof changed the real SDK index.'}
foreach($case in @(
 @{Run='relative-output';Cache="$FixtureRoot/cache";Message='RunRoot must be absolute'},
 @{Run=$repo;Cache="$FixtureRoot/cache";Message='RunRoot must not overlap'},
 @{Run="$FixtureRoot/native";Cache=$repo;Message='NativeObjectCacheRoot must not overlap'},
 @{Run="$FixtureRoot/native";Cache='/';Message='NativeObjectCacheRoot cannot be a volume root'},
 @{Run="$FixtureRoot/native";Cache='relative-cache';Message='NativeObjectCacheRoot must be absolute'}
)){
 $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=(Get-Process -Id $PID).Path;$psi.UseShellExecute=$false;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
 foreach($arg in @('-NoProfile','-File',"$repo/scripts/build.ps1",'-Configuration','Release','-RunRoot',$case.Run,'-NativeObjectCacheRoot',$case.Cache)){$psi.ArgumentList.Add($arg)}
 $child=[Diagnostics.Process]::Start($psi);$stdout=$child.StandardOutput.ReadToEndAsync();$stderr=$child.StandardError.ReadToEndAsync();$child.WaitForExit()
 $text=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()
 if($child.ExitCode -eq 0 -or -not $text.Contains($case.Message)){throw "Expected path rejection '$($case.Message)', observed $($child.ExitCode): $text"}
}
Write-Output "PASS: patch idempotence/hash, valid-mutant rejection, worktree/index preservation and 5 output containment negatives; SDK patch diff SHA256=$hash1"
