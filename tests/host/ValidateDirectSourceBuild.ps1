param([Parameter(Mandatory=$true)][string]$FixtureRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if(-not [IO.Path]::IsPathRooted($FixtureRoot)){throw 'FixtureRoot must be absolute.'}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$lock=Get-Content (Join-Path $repo 'toolchain.lock.json') -Raw|ConvertFrom-Json
$passed=0
function Check([bool]$Value,[string]$Message){if(-not $Value){throw $Message};$script:passed++}
New-Item -ItemType Directory -Force -Path "$FixtureRoot/work","$FixtureRoot/objects"|Out-Null
$before=@{}
foreach($path in @('Makefile','src/main.cpp','third_party/nugget/psyqo/Makefile','third_party/nugget/common.mk')){$before[$path]=(Get-FileHash (Join-Path $repo $path) -Algorithm SHA256).Hash}
$probe='ps1-layout-probe:;@printf "%s\n" "SOURCES=$(foreach s,$(SRCS),$(abspath $(s)))" "OBJECTS=$(OBJS)" "ALLDEPS=$(PS1_ALL_DEPS)" "LIBS=$(LIBRARIES)" "FLAGS=$(CPPFLAGS) $(CXXFLAGS)"'
function Invoke-MakePlan([bool]$Sdk,[string[]]$Extra){
 $workdir=if($Sdk){'/src/third_party/nugget/psyqo'}else{'/src'}
 $arguments=@('run','--rm','--platform','linux/amd64','--memory','128m','--memory-swap','128m','--cpus','0.5','--network','none','--mount',"type=bind,source=$repo,target=/src,readonly",'--mount',"type=bind,source=$FixtureRoot/work,target=/work",'--mount',"type=bind,source=$FixtureRoot/objects,target=/nativecache/objects",'--workdir',$workdir,'--entrypoint','make',$lock.image.reference,'-f','/src/Makefile','NOFUSEVOLATILE=','PS1_SOURCE_ROOT=/src','NATIVE_OBJECT_ROOT=/nativecache/objects')
 if($Sdk){$arguments+=@('PS1_BUILD_SDK=1','BINDIR=/nativecache/objects/third_party/nugget/psyqo/')}else{$arguments+='BINDIR=/work/out/'}
 $output=@(& docker @arguments @Extra 2>&1)
 if($LASTEXITCODE -ne 0){throw "make query failed: $($output -join [Environment]::NewLine)"}
 return $output
}
function Read-Plan($Output){
 $values=@{}
 foreach($line in $Output){$text=[string]$line;$at=$text.IndexOf('=');if($at -gt 0){$values[$text.Substring(0,$at)]=$text.Substring($at+1)}}
 return $values
}
$mainOutput=Invoke-MakePlan $false @('--eval',$probe,'ps1-layout-probe','--no-print-directory')
$mainOutput|Set-Content "$FixtureRoot/platform-plan.log"
$main=Read-Plan $mainOutput
$sources=@($main.SOURCES.Split(' ',[StringSplitOptions]::RemoveEmptyEntries))
$objects=@($main.OBJECTS.Split(' ',[StringSplitOptions]::RemoveEmptyEntries))
$deps=@($main.ALLDEPS.Split(' ',[StringSplitOptions]::RemoveEmptyEntries))
Check ($sources.Count -eq 8 -and $objects.Count -eq 8 -and $deps.Count -eq 8) 'Expected eight platform sources, external objects and dependency outputs.'
foreach($source in $sources){Check ($source.StartsWith('/src/src/',[StringComparison]::Ordinal)) 'Platform source must be read directly from the real checkout.'}
foreach($path in @($objects+$deps)){Check ($path.StartsWith('/nativecache/objects/',[StringComparison]::Ordinal)) 'Compiler output must remain outside read-only sources.'}
Check ($main.LIBS -ceq '/nativecache/objects/third_party/nugget/psyqo/libpsyqo.a') 'SDK archive must be external.'
$dry=Invoke-MakePlan $false @('-n',$objects[0],'--no-print-directory')
$dry|Set-Content "$FixtureRoot/platform-dry-run.log"
Check (($dry -join "`n").Contains("'/src/src/main.cpp'")) 'Compiler plan lost the real source path.'
Check (($dry -join "`n").Contains("'/nativecache/objects/src/main.o'")) 'Compiler plan lost the external object.'
Check (($dry -join "`n").Contains("'/nativecache/objects/src/main.dep'")) 'Compiler plan lost the external dependency output.'
$sdkOutput=Invoke-MakePlan $true @('--eval',$probe,'ps1-layout-probe','--no-print-directory')
$sdkOutput|Set-Content "$FixtureRoot/sdk-plan.log"
$sdk=Read-Plan $sdkOutput
$sdkSources=@($sdk.SOURCES.Split(' ',[StringSplitOptions]::RemoveEmptyEntries))
$sdkObjects=@($sdk.OBJECTS.Split(' ',[StringSplitOptions]::RemoveEmptyEntries))
$sdkDeps=@($sdk.ALLDEPS.Split(' ',[StringSplitOptions]::RemoveEmptyEntries))
Check ($sdkSources.Count -gt 20 -and $sdkSources.Count -eq $sdkObjects.Count -and $sdkSources.Count -eq $sdkDeps.Count) 'SDK source/object/dependency counts differ.'
foreach($source in $sdkSources){Check ($source.StartsWith('/src/third_party/nugget/',[StringComparison]::Ordinal)) 'SDK source escaped its pinned actual checkout.'}
foreach($path in @($sdkObjects+$sdkDeps)){Check ($path.StartsWith('/nativecache/objects/third_party/nugget/',[StringComparison]::Ordinal)) 'SDK compiler outputs must remain external.'}
foreach($path in $before.Keys){Check ((Get-FileHash (Join-Path $repo $path) -Algorithm SHA256).Hash -ceq $before[$path]) "Source changed during print/dry-run: $path"}
Write-Output "PASS: $script:passed direct-source PS1 layout assertions; no compiler or SDK build executed."
