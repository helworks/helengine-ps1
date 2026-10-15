param(
    [Parameter(Mandatory=$true)][string]$SourceProjectRoot,
    [Parameter(Mandatory=$true)][string]$StagedProjectRoot,
    [Parameter(Mandatory=$true)][string]$PublisherRoot,
    [string]$PlatformId='ps1',
    [string[]]$SceneRelativePaths=@('assets/scenes/DemoDiscMainMenu.helen'),
    [Parameter(Mandatory=$true)][string]$ReceiptPath
)
$ErrorActionPreference='Stop'
[System.Reflection.Assembly]::LoadFrom((Join-Path $PublisherRoot 'helengine.core.dll'))|Out-Null
[System.Reflection.Assembly]::LoadFrom((Join-Path $PublisherRoot 'helengine.files.dll'))|Out-Null
[System.Reflection.Assembly]::LoadFrom((Join-Path $PublisherRoot 'helengine.editor.dll'))|Out-Null
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Text;
public static class StagedFontFields {
    public static Dictionary<string,int[]> Read(byte[] payload) {
        if(payload==null || payload.Length<5 || payload[0]!=1) throw new InvalidOperationException("Unsupported component payload.");
        var result=new Dictionary<string,int[]>(); int p=5;
        int count=BitConverter.ToInt32(payload,1);
        for(int i=0;i<count;i++) {
            int n=BitConverter.ToInt32(payload,p); p+=4;
            string name=Encoding.UTF8.GetString(payload,p,n); p+=n;
            int size=BitConverter.ToInt32(payload,p); p+=4;
            if(size<0 || p+size>payload.Length) throw new InvalidOperationException("Invalid field bounds.");
            result.Add(name,new[]{p,size}); p+=size;
        }
        if(p!=payload.Length) throw new InvalidOperationException("Unexpected component payload tail.");
        return result;
    }
    public static string FontPath(byte[] payload,int[] field) {
        int p=field[0];
        if(field[1]<9 || payload[p]!=1) throw new InvalidOperationException("Unsupported font reference.");
        if(BitConverter.ToInt32(payload,p+1)!=1) return null;
        int n=BitConverter.ToInt32(payload,p+5);
        if(n<0 || n>field[1]-9) throw new InvalidOperationException("Invalid font reference path.");
        return Encoding.UTF8.GetString(payload,p+9,n);
    }
}
'@
$sourceRoot=[IO.Path]::GetFullPath($SourceProjectRoot)
$stageRoot=[IO.Path]::GetFullPath($StagedProjectRoot)
if($sourceRoot -eq $stageRoot){throw 'Staging normalization requires a separate immutable baseline.'}
function Get-ContainedPath([string]$root,[string]$relativePath) {
    $prefix=$root.TrimEnd([IO.Path]::DirectorySeparatorChar,[IO.Path]::AltDirectorySeparatorChar)+[IO.Path]::DirectorySeparatorChar
    $path=[IO.Path]::GetFullPath((Join-Path $root $relativePath))
    if(!$path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Scene or font path escapes its expected root.'}
    $cursor=$path
    while($cursor.Length -ge $root.Length){
        if(Test-Path -LiteralPath $cursor){
            $item=Get-Item -LiteralPath $cursor -Force
            if(($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'Scene or font path traverses a reparse point.'}
        }
        if($cursor.Equals($root,[StringComparison]::OrdinalIgnoreCase)){break}
        $cursor=[IO.Path]::GetDirectoryName($cursor)
    }
    return $path
}
$changes=[Collections.Generic.List[object]]::new()
$fontRatios=@{}
$fontWrappers=[helengine.editor.ComponentPlatformOverridePayloadService]::new()
# A baseline rewrite must not replace a newer native-pixel scene already staged.
# Inspect common and scoped text policies without changing any target record.
function Test-NativeTextScene($entities) {
    foreach($entity in $entities){
        foreach($component in $entity.Components){
            if($component.ComponentTypeId -ne 'helengine.TextComponent'){continue}
            $payloads=[Collections.Generic.List[byte[]]]::new()
            $payloads.Add($fontWrappers.UnwrapBaseRecord($component).Payload)
            foreach($override in $fontWrappers.ReadOverrideStates($component)){
                if($override.Payload.Length -gt 0){$payloads.Add($override.Payload)}
            }
            foreach($payload in $payloads){
                $fields=[StagedFontFields]::Read($payload)
                if($fields.ContainsKey('UseNativeFontPixels')){
                    $field=$fields['UseNativeFontPixels']
                    if($field[1] -ne 1){throw 'UseNativeFontPixels must be one boolean byte.'}
                    if($payload[$field[0]] -eq 1){return $true}
                }
            }
        }
        if(Test-NativeTextScene $entity.Children){return $true}
    }
    return $false
}
function Get-FontRatio([string]$relativePath) {
    if($fontRatios.ContainsKey($relativePath)){return $fontRatios[$relativePath]}
    $sidecar=Get-ContainedPath $sourceRoot ('assets/'+$relativePath+'.hasset')
    if(!(Test-Path -LiteralPath $sidecar)){return $null}
    $stream=[IO.File]::OpenRead($sidecar)
    try {$settings=[helengine.editor.SectionedAssetImportSettingsBinarySerializer]::Deserialize($stream)} finally {$stream.Dispose()}
    $baseline=[helengine.editor.FontAssetProcessorSettings]::new().PixelSize
    if($settings.Processor.Platforms.ContainsKey('default') -and $settings.Processor.Platforms['default'].Sections.ContainsKey('font')){
        $baseline=$settings.Processor.Platforms['default'].Font.PixelSize
    }
    $raster=$baseline
    if($settings.Processor.Platforms.ContainsKey($PlatformId) -and $settings.Processor.Platforms[$PlatformId].Sections.ContainsKey('font')){
        $raster=$settings.Processor.Platforms[$PlatformId].Font.PixelSize
    }
    if($baseline -lt 1 -or $raster -lt 1){throw 'Font pixel size must be positive.'}
    $value=[pscustomobject]@{Path=$relativePath;BaselinePixelSize=$baseline;RasterPixelSize=$raster;Ratio=[double]$baseline/$raster;SettingsSha256=(Get-FileHash -LiteralPath $sidecar).Hash}
    $fontRatios[$relativePath]=$value
    return $value
}
function Update-Entities($entities,[string]$sceneRelativePath) {
    foreach($entity in $entities){
        foreach($component in $entity.Components){
            if($component.ComponentTypeId -ne 'helengine.TextComponent'){continue}
            try {$fields=[StagedFontFields]::Read($component.Payload)} catch {throw "Unsupported text payload at $sceneRelativePath / $($entity.Name): $([BitConverter]::ToString($component.Payload,0,[Math]::Min(12,$component.Payload.Length)))"}
            if(!$fields.ContainsKey('Font') -or !$fields.ContainsKey('FontScale')){continue}
            $fontPath=[StagedFontFields]::FontPath($component.Payload,$fields['Font'])
            if(!$fontPath){continue}
            $ratio=Get-FontRatio $fontPath
            if(!$ratio -or $ratio.Ratio -eq 1){continue}
            $scaleField=$fields['FontScale']
            if($scaleField[1] -ne 4){throw 'FontScale must be a single float.'}
            $before=[BitConverter]::ToSingle($component.Payload,$scaleField[0])
            $after=[single]($before*$ratio.Ratio)
            $updated=[byte[]]$component.Payload.Clone()
            [BitConverter]::GetBytes($after).CopyTo($updated,$scaleField[0])
            for($byteIndex=0;$byteIndex -lt $updated.Length;$byteIndex++){
                if($byteIndex -ge $scaleField[0] -and $byteIndex -lt $scaleField[0]+4){continue}
                if($updated[$byteIndex] -ne $component.Payload[$byteIndex]){throw 'A field other than FontScale changed.'}
            }
            $component.Payload=$updated
            $changes.Add([ordered]@{scene=$sceneRelativePath;entity=$entity.Name;font=$fontPath;baselinePixelSize=$ratio.BaselinePixelSize;rasterPixelSize=$ratio.RasterPixelSize;ratio=$ratio.Ratio;authoredFontScale=$before;stagedFontScale=$after;settingsSha256=$ratio.SettingsSha256})
        }
        Update-Entities $entity.Children $sceneRelativePath
    }
}
$sceneHashes=[Collections.Generic.List[object]]::new()
foreach($scenePath in $SceneRelativePaths){
    $sourcePath=Get-Item -LiteralPath (Get-ContainedPath $sourceRoot $scenePath)
    $relative=[IO.Path]::GetRelativePath($sourceRoot,$sourcePath.FullName)
    $targetPath=Get-ContainedPath $stageRoot $relative
    if(!(Test-Path -LiteralPath $targetPath)){continue}
    $sourceHash=(Get-FileHash -LiteralPath $sourcePath.FullName).Hash
    $targetStream=[IO.File]::OpenRead($targetPath)
    try {$currentStageScene=[helengine.files.AssetSerializer]::Deserialize($targetStream)} finally {$targetStream.Dispose()}
    if(Test-NativeTextScene $currentStageScene.RootEntities){
        $sceneHashes.Add([ordered]@{path=$relative;sourceSha256=$sourceHash;stagedSha256=(Get-FileHash -LiteralPath $targetPath).Hash;skippedNativePixelScene=$true})
        continue
    }
    $stream=[IO.File]::OpenRead($sourcePath.FullName)
    try {$scene=[helengine.files.AssetSerializer]::Deserialize($stream)} finally {$stream.Dispose()}
    $beforeCount=$changes.Count
    Update-Entities $scene.RootEntities $relative
    if($changes.Count -gt $beforeCount){
        $stream=[IO.File]::Open($targetPath,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try {[helengine.files.AssetSerializer]::Serialize($stream,$scene)} finally {$stream.Dispose()}
    }
    if((Get-FileHash -LiteralPath $sourcePath.FullName).Hash -ne $sourceHash){throw 'Immutable scene baseline changed.'}
    $sceneHashes.Add([ordered]@{path=$relative;sourceSha256=$sourceHash;stagedSha256=(Get-FileHash -LiteralPath $targetPath).Hash})
}
[ordered]@{platform=$PlatformId;sourceRoot=$sourceRoot;stagedRoot=$stageRoot;scenes=$sceneHashes;changes=$changes;onlyFontScaleChanged=$true;nativePixelScenesPreserved=$true}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $ReceiptPath
Write-Output "Normalized $($changes.Count) text components from immutable baseline ($PlatformId)."
