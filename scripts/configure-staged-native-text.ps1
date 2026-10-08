<#
.SYNOPSIS
Authors PS1-only component/transform overrides and three native-size font assets.
.DESCRIPTION
Reads an immutable authored snapshot and preserves its common payloads and other
platform scopes. All layout conversion occurs in project metadata; runtime text
uses FontScale=1 and a screen-bound viewport with scaling disabled.
ValidateOnly computes and round-trips the complete plan without writing files.
#>
param(
    [Parameter(Mandatory=$true)][string]$SourceProjectRoot,
    [Parameter(Mandatory=$true)][string]$StagedProjectRoot,
    [Parameter(Mandatory=$true)][string]$PublisherRoot,
    [Parameter(Mandatory=$true)][string]$ReceiptPath,
    [ValidateRange(1,256)][int]$TitlePixelSize=13,
    [ValidateRange(1,256)][int]$ButtonPixelSize=21,
    [ValidateRange(1,256)][int]$FooterPixelSize=11,
    [switch]$ValidateOnly
)
$ErrorActionPreference='Stop'
$sourceRoot=[IO.Path]::GetFullPath($SourceProjectRoot)
$stageRoot=[IO.Path]::GetFullPath($StagedProjectRoot)
if($sourceRoot.Equals($stageRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Source and staging roots must differ.'}
$buildRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../builds/helengine-ps1'))
if(!$stageRoot.StartsWith($buildRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){
    throw 'PS1 project staging must remain inside the workspace PS1 build root.'
}
if(!(Test-Path -LiteralPath (Join-Path $stageRoot 'project.heproj'))){throw 'This helper configures an existing project stage.'}
foreach($name in @('helengine.core.dll','helengine.files.dll','helengine.editor.dll')){
    [Reflection.Assembly]::LoadFrom((Join-Path $PublisherRoot $name))|Out-Null
}
$overrideResolver=[helengine.editor.EditorOverrideScopeResolver]::Load($stageRoot)
$wrappers=[helengine.editor.ComponentPlatformOverridePayloadService]::new()
$changes=[Collections.Generic.List[object]]::new()
$writes=[Collections.Generic.Dictionary[string,byte[]]]::new([StringComparer]::OrdinalIgnoreCase)

# Reject escaping paths and existing reparse ancestors before reading or writing.
function Get-ContainedPath([string]$root,[string]$relative){
    $prefix=$root.TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
    $path=[IO.Path]::GetFullPath((Join-Path $root $relative))
    if(!$path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw "Path escapes root: $relative"}
    $cursor=$path
    while($cursor.Length -ge $root.Length){
        if(Test-Path -LiteralPath $cursor){
            if(((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Reparse path: $cursor"}
        }
        if($cursor.Equals($root,[StringComparison]::OrdinalIgnoreCase)){break}
        $cursor=[IO.Path]::GetDirectoryName($cursor)
    }
    return $path
}
$receiptPathSafe=Get-ContainedPath $buildRoot ([IO.Path]::GetRelativePath($buildRoot,[IO.Path]::GetFullPath($ReceiptPath)))

# Decode tagged authored fields without changing their order or unknown bytes.
function Read-Fields([byte[]]$payload){
    if($payload.Length -lt 5 -or $payload[0] -ne 1){throw 'Unsupported tagged component payload.'}
    $fields=[Collections.Generic.Dictionary[string,byte[]]]::new([StringComparer]::Ordinal)
    $position=5
    $count=[BitConverter]::ToInt32($payload,1)
    if($count -lt 0){throw 'Negative field count.'}
    for($index=0;$index -lt $count;$index++){
        if($position+4 -gt $payload.Length){throw 'Truncated field name.'}
        $length=[BitConverter]::ToInt32($payload,$position);$position+=4
        if($length -lt 1 -or $position+$length+4 -gt $payload.Length){throw 'Invalid field name.'}
        $name=[Text.Encoding]::UTF8.GetString($payload,$position,$length);$position+=$length
        $length=[BitConverter]::ToInt32($payload,$position);$position+=4
        if($length -lt 0 -or $position+$length -gt $payload.Length){throw 'Invalid field length.'}
        $bytes=[byte[]]::new($length)
        [Array]::Copy($payload,$position,$bytes,0,$length);$position+=$length
        $fields.Add($name,$bytes)
    }
    if($position -ne $payload.Length){throw 'Unexpected tagged component tail.'}
    return ,$fields
}

# Encode the same tagged field structure after editing only the selected fields.
function Write-Fields($fields){
    $stream=[IO.MemoryStream]::new();$writer=[IO.BinaryWriter]::new($stream)
    try{
        $writer.Write([byte]1);$writer.Write([int]$fields.Count)
        foreach($field in $fields.GetEnumerator()){
            $name=[Text.Encoding]::UTF8.GetBytes($field.Key)
            $writer.Write([int]$name.Length);$writer.Write($name)
            $writer.Write([int]$field.Value.Length);$writer.Write([byte[]]$field.Value)
        }
        $writer.Flush();return ,$stream.ToArray()
    }finally{$writer.Dispose();$stream.Dispose()}
}

# Produce an integral native pixel coordinate using the authored axis scale.
function Native-Pixel([double]$value,[double]$scale){return [int][Math]::Round($value*$scale,0,[MidpointRounding]::AwayFromZero)}

# Scale a typed scalar/vector field and retain its original binary type.
function Scale-Field($fields,[string]$name,[double[]]$scales,[bool]$integer){
    if(!$fields.ContainsKey($name)){throw "Required layout field absent: $name"}
    $before=$fields[$name]
    if($before.Length -ne 4*$scales.Length){throw "Unexpected layout field size: $name"}
    $after=[byte[]]::new($before.Length)
    for($axis=0;$axis -lt $scales.Length;$axis++){
        $value=if($integer){[BitConverter]::ToInt32($before,4*$axis)}else{[BitConverter]::ToSingle($before,4*$axis)}
        $pixel=Native-Pixel $value $scales[$axis]
        if($integer -and $value -gt 0){$pixel=[Math]::Max(1,$pixel)}
        $bytes=if($integer){[BitConverter]::GetBytes([int]$pixel)}else{[BitConverter]::GetBytes([single]$pixel)}
        $bytes.CopyTo($after,4*$axis)
    }
    $fields[$name]=$after
}

# Serialize one typed optional reference with the existing engine encoding.
function Reference-Bytes($reference){
    $stream=[IO.MemoryStream]::new()
    $writer=[helengine.EngineBinaryWriter]::Create($stream,[helengine.EngineBinaryEndianness]::LittleEndian)
    try{[helengine.editor.SceneComponentBinaryFieldEncoding]::WriteOptionalReference($writer,$reference);return ,$stream.ToArray()}
    finally{$writer.Dispose();$stream.Dispose()}
}

# Add one PS1 scope while retaining the exact common payload and other scopes.
function Set-ComponentOverride($record,$fields,[string[]]$properties,$font,$scope){
    $base=$wrappers.UnwrapBaseRecord($record)
    $state=[helengine.EntityComponentSaveState]::new();$state.ComponentKey=$record.ComponentKey
    foreach($existing in $wrappers.ReadOverrideStates($record)){$state.SetScopedPlatformOverride($existing.Scope,$existing)}
    $override=$state.GetOrCreateScopedPlatformOverride($scope)
    $merged=if($override.Payload.Length -gt 0){Read-Fields $override.Payload}else{Read-Fields $base.Payload}
    foreach($property in $properties){
        $merged[$property]=$fields[$property]
        $override.SetPropertyOverride($property)
        $staleMembers=[Collections.Generic.List[string]]::new()
        foreach($member in $override.EnumerateMemberValues().GetEnumerator()){
            if($member.Key.Equals($property,[StringComparison]::Ordinal) -or $member.Key.StartsWith($property+'.',[StringComparison]::Ordinal)){
                $staleMembers.Add($member.Key)
            }
        }
        foreach($member in $staleMembers){$override.RemoveMemberValue($member)}
    }
    $override.Payload=Write-Fields $merged
    if($null -ne $font){$override.SetAssetReference('Font',$font)}
    $wrapped=$wrappers.Wrap($base,$state)
    if([Convert]::ToBase64String($wrappers.UnwrapBaseRecord($wrapped).Payload) -ne [Convert]::ToBase64String($base.Payload)){throw 'Common component payload changed.'}
    return $wrapped
}

# Plan dedicated TTF/settings/identity files; the same bytes use distinct assets.
$fontSource=Get-ContainedPath $sourceRoot 'assets/fonts/DemoDiscBody.ttf'
$fontBytes=[IO.File]::ReadAllBytes($fontSource)
$fontHash=(Get-FileHash -LiteralPath $fontSource).Hash.ToLowerInvariant()
$fontSettingsSource=Get-ContainedPath $sourceRoot 'assets/fonts/DemoDiscBody.ttf.hasset'
$fontPlans=@(
    @{Role='Title';Pixels=$TitlePixelSize;Id='51000000000000000000000000000001'},
    @{Role='Buttons';Pixels=$ButtonPixelSize;Id='51000000000000000000000000000002'},
    @{Role='Footer';Pixels=$FooterPixelSize;Id='51000000000000000000000000000003'}
)
$references=@{}
foreach($plan in $fontPlans){
    $relative="fonts/DemoDiscPs1$($plan.Role).ttf"
    $path=Get-ContainedPath $stageRoot ('assets/'+$relative)
    $stream=[IO.File]::OpenRead($fontSettingsSource)
    try{$settings=[helengine.editor.SectionedAssetImportSettingsBinarySerializer]::Deserialize($stream)}finally{$stream.Dispose()}
    if(!$settings.Processor.Platforms.ContainsKey('ps1')){throw 'Authored font lacks PS1 settings.'}
    $settings.Importer.AssetId=$plan.Id
    $settings.Processor.Platforms['ps1'].Font.PixelSize=$plan.Pixels
    $settings.Processor.Platforms['ps1'].FontAtlasTexture.MaxResolution=256
    $stream=[IO.MemoryStream]::new()
    try{[helengine.editor.SectionedAssetImportSettingsBinarySerializer]::Serialize($stream,$settings);$settingsBytes=$stream.ToArray()}finally{$stream.Dispose()}
    $metadata=Get-ContainedPath $stageRoot ('assets/'+$relative+'.hmeta')
    if(Test-Path -LiteralPath $metadata){
        $existing=Get-Content -Raw -LiteralPath $metadata|ConvertFrom-Json
        if($existing.assetId -ne $plan.Id){throw "Dedicated font asset belongs to another owner: $relative"}
    }
    $writes.Add($path,$fontBytes)
    $writes.Add((Get-ContainedPath $stageRoot ('assets/'+$relative+'.hasset')),$settingsBytes)
    $writes.Add($metadata,[Text.UTF8Encoding]::new($false).GetBytes(([ordered]@{version=1;assetId=$plan.Id;formerAssetIds=@()}|ConvertTo-Json -Depth 3)+"`n"))
    $reference=[helengine.SceneAssetReferenceFactory]::CreateFileSystemReference($plan.Id,$relative,('sha256:'+$fontHash))
    $references[$plan.Role]=$reference
}
foreach($license in Get-ChildItem -LiteralPath ([IO.Path]::GetDirectoryName($fontSource)) -File){
    if($license.Name -match '(?i)license|licence|copying|ofl'){
        $licenseSource=Get-ContainedPath $sourceRoot ('assets/fonts/'+$license.Name)
        $writes.Add((Get-ContainedPath $stageRoot ('assets/fonts/'+$license.Name)),[IO.File]::ReadAllBytes($licenseSource))
    }
}

# NoScaling is a valid project mode; keep marquee units native in that mode.
# Read only the baseline source and require both known guards before planning a write.
$marqueeRelative='assets/codebase/menu/FooterIdentityMarqueeComponent.cs'
$marqueeSource=Get-ContainedPath $sourceRoot $marqueeRelative
if((Get-FileHash -LiteralPath $marqueeSource).Hash -ne '2C3CFC33E92A3484D4FC316E7A658A42990FC3D159210DFFE0FC5CB32DA5833D'){
    throw 'Baseline marquee source changed; review the native-pixel adaptation before staging.'
}
$marqueeText=[IO.File]::ReadAllText($marqueeSource)
$marqueeGuard='&& (viewport.ScalingMode == ViewportComponent.ReferenceCanvasScalingMode'
if(([regex]::Matches($marqueeText,[regex]::Escape($marqueeGuard))).Count -ne 2){throw 'Unexpected baseline marquee viewport guards.'}
$marqueeText=$marqueeText.Replace($marqueeGuard,"&& (viewport.ScalingMode == ViewportComponent.NoScalingMode`n                                || viewport.ScalingMode == ViewportComponent.ReferenceCanvasScalingMode")
$scaleReturn='AnchorSpace anchorSpace = viewport.AnchorSpace;'
if(([regex]::Matches($marqueeText,[regex]::Escape($scaleReturn))).Count -ne 1){throw 'Unexpected marquee scale implementation.'}
$marqueeText=$marqueeText.Replace($scaleReturn,"if (viewport.ScalingMode == ViewportComponent.NoScalingMode) {`n                                return new float2(1f, 1f);`n                            }`n                            "+$scaleReturn)
$writes.Add((Get-ContainedPath $stageRoot $marqueeRelative),[Text.UTF8Encoding]::new($false).GetBytes($marqueeText))

# Read the unchanged baseline instead of cumulatively transforming staged data.
$sceneRelative='assets/scenes/DemoDiscMainMenu.helen'
$sceneSource=Get-ContainedPath $sourceRoot $sceneRelative
$sourceHash=(Get-FileHash -LiteralPath $sceneSource).Hash
$stream=[IO.File]::OpenRead($sceneSource)
try{$scene=[helengine.files.AssetSerializer]::Deserialize($stream)}finally{$stream.Dispose()}
if($scene.SceneSettings.CanvasProfile.Width -ne 1280 -or $scene.SceneSettings.CanvasProfile.Height -ne 720){throw 'Unexpected authored reference canvas.'}
$menuRoots=@($scene.RootEntities|Where-Object Name -eq 'DemoDiscMenuRoot')
if($menuRoots.Count -ne 1){throw 'Expected one authored menu root.'}

# Store absolute, native-size entity positions exclusively on the PS1 scope.
function Resolve-EntityLevelOrder($entity,$inheritedLevelOrder){
    if($entity.HasOverrideLevelOrder){return ,[helengine.SceneOverrideScopeStepKind[]]$overrideResolver.ResolveLevelOrder($entity.OverrideLevelOrder)}
    return ,[helengine.SceneOverrideScopeStepKind[]]$overrideResolver.ResolveLevelOrder($inheritedLevelOrder)
}

# Resolve the project's group chain rather than authoring an unreachable platform-only scope.
# A config-first order cannot express a config-independent PS1 prefix; reject it.
function Resolve-NativeScope($levelOrder){
    $scope=$overrideResolver.BuildTargetPath($levelOrder,'ps1',$null)
    $platform=$null
    if(!$scope.TryGetStepId([helengine.SceneOverrideScopeStepKind]::Platform,[ref]$platform) -or $platform -ne 'ps1'){
        throw 'Native PS1 text requires Platform before BuildConfig in the effective level order; a group-only prefix would affect other platforms.'
    }
    foreach($configuration in @('release','debug')){
        $target=$overrideResolver.BuildTargetPath($levelOrder,'ps1',$configuration)
        if(!$scope.IsPrefixOf($target)){throw "Native PS1 scope does not select for $configuration."}
    }
    return $scope
}

function Configure-Entity($entity,$inheritedLevelOrder){
    $levelOrder=Resolve-EntityLevelOrder $entity $inheritedLevelOrder
    $scope=Resolve-NativeScope $levelOrder
    $scopeSteps=$scope.ToSteps()
    $transforms=[Collections.Generic.List[helengine.SceneEntityPlatformTransformOverrideAsset]]::new()
    foreach($existing in $entity.PlatformTransformOverrides){$transforms.Add($existing)}
    $transform=$null
    foreach($existing in $transforms){if([helengine.SceneOverrideScopePath]::AreEqual($existing.Scope,$scopeSteps)){$transform=$existing}}
    if($null -eq $transform){$transform=[helengine.SceneEntityPlatformTransformOverrideAsset]::new();$transform.Scope=$scopeSteps;$transforms.Add($transform)}
    $transform.HasLocalPositionOverride=$true
    $transform.LocalPosition=[helengine.float3]::new((Native-Pixel $entity.LocalPosition.X 0.25),(Native-Pixel $entity.LocalPosition.Y (1.0/3.0)),$entity.LocalPosition.Z)
    $entity.PlatformTransformOverrides=$transforms.ToArray()
    for($index=0;$index -lt $entity.Components.Length;$index++){
        $record=$entity.Components[$index]
        $fields=Read-Fields ($wrappers.UnwrapBaseRecord($record).Payload)
        $properties=[Collections.Generic.List[string]]::new();$font=$null
        switch($record.ComponentTypeId){
            'helengine.ViewportComponent' {
                $fields['ScalingMode']=[byte[]]@([helengine.ViewportComponent]::NoScalingMode)
                $fields['ReferenceWidth']=[BitConverter]::GetBytes([int]320)
                $fields['ReferenceHeight']=[BitConverter]::GetBytes([int]240)
                foreach($name in @('ScalingMode','ReferenceWidth','ReferenceHeight')){$properties.Add($name)}
            }
            'helengine.LayoutComponent' {Scale-Field $fields 'AnchorDistances' @(0.25,0.25,(1.0/3.0),(1.0/3.0)) $false;$properties.Add('AnchorDistances')}
            'helengine.TextComponent' {
                Scale-Field $fields 'Size' @(0.25,(1.0/3.0)) $true
                $role=if($entity.Name -match '^DemoDiscTitle'){ 'Title' }elseif($entity.Name -eq 'DemoDiscFooterIdentityText'){ 'Footer' }else{ 'Buttons' }
                if($role -eq 'Title'){
                    $size=[byte[]]$fields['Size'].Clone();[BitConverter]::GetBytes([int]16).CopyTo($size,4);$fields['Size']=$size
                }elseif($role -eq 'Footer'){
                    $size=[byte[]]$fields['Size'].Clone();[BitConverter]::GetBytes([int]13).CopyTo($size,4);$fields['Size']=$size
                }
                $fields['FontScale']=[BitConverter]::GetBytes([single]1)
                Scale-Field $fields 'OutlineScale' @((1.0/3.0)) $false
                Scale-Field $fields 'ShadowOffset' @((1.0/3.0),(1.0/3.0)) $false
                $font=$references[$role];$fields['Font']=Reference-Bytes $font
                foreach($name in @('Size','FontScale','OutlineScale','ShadowOffset','Font')){$properties.Add($name)}
                $changes.Add([ordered]@{entity=$entity.Name;role=$role;font=$font.RelativePath;fontScale=1;sizeX=[BitConverter]::ToInt32($fields['Size'],0);sizeY=[BitConverter]::ToInt32($fields['Size'],4);outline=[BitConverter]::ToSingle($fields['OutlineScale'])})
            }
            'helengine.RoundedRectComponent' {
                Scale-Field $fields 'Size' @(0.25,(1.0/3.0)) $true
                foreach($name in @('Radius','BorderThickness')){Scale-Field $fields $name @((1.0/3.0)) $false;$properties.Add($name)}
                $properties.Add('Size')
            }
            {$_ -in @('helengine.SpriteComponent','helengine.ClipRectComponent','helengine.InteractableComponent')} {
                Scale-Field $fields 'Size' @(0.25,(1.0/3.0)) $true;$properties.Add('Size')
            }
            'helengine.ScrollComponent' {
                Scale-Field $fields 'Size' @(0.25,(1.0/3.0)) $true
                Scale-Field $fields 'ItemExtent' @((1.0/3.0)) $true
                foreach($name in @('Size','ItemExtent')){$properties.Add($name)}
            }
            'DemoDisc.menu.FooterIdentityMarqueeComponent, DemoDisc' {
                Scale-Field $fields 'StripWidth' @(0.25) $false;$properties.Add('StripWidth')
                $speed=[BitConverter]::ToSingle($fields['PixelsPerSecond'])
                $fields['PixelsPerSecond']=[BitConverter]::GetBytes([single]($speed*0.25));$properties.Add('PixelsPerSecond')
            }
        }
        if($properties.Count -gt 0){$entity.Components[$index]=Set-ComponentOverride $record $fields $properties.ToArray() $font $scope}
    }
    foreach($child in $entity.Children){Configure-Entity $child $levelOrder}
}
Configure-Entity $menuRoots[0] $null
$assetReferences=[Collections.Generic.List[helengine.SceneAssetReference]]::new()
foreach($reference in $scene.AssetReferences){$assetReferences.Add($reference)}
foreach($role in @('Title','Buttons','Footer')){$assetReferences.Add($references[$role])}
$scene.AssetReferences=$assetReferences.ToArray()
$stream=[IO.MemoryStream]::new()
try{[helengine.files.AssetSerializer]::Serialize($stream,$scene);$sceneBytes=$stream.ToArray()}finally{$stream.Dispose()}
# A full serializer round-trip validates the wrapped records before any writes.
$stream=[IO.MemoryStream]::new($sceneBytes,$false)
try{$roundTrip=[helengine.files.AssetSerializer]::Deserialize($stream)}finally{$stream.Dispose()}
if($roundTrip.SceneSettings.CanvasProfile.Width -ne 1280 -or $roundTrip.SceneSettings.CanvasProfile.Height -ne 720){throw 'Common canvas changed.'}

# Canonically serialize only the untouched scopes for a byte-for-byte comparison.
function Unchanged-ScopeBytes($record,$scope){
    $state=[helengine.EntityComponentSaveState]::new()
    foreach($override in $wrappers.ReadOverrideStates($record)){
        if(!$override.Scope.Equals($scope)){$state.SetScopedPlatformOverride($override.Scope,$override)}
    }
    return ,$wrappers.Wrap($wrappers.UnwrapBaseRecord($record),$state).Payload
}

# Compare the actual serialized result, not the planning dictionary values.
function Validate-RoundTrip($authoredEntities,$stagedEntities,$inheritedLevelOrder,$insideMenu){
    if($authoredEntities.Length -ne $stagedEntities.Length){throw 'Authored entity count changed.'}
    for($entityIndex=0;$entityIndex -lt $authoredEntities.Length;$entityIndex++){
        $before=$authoredEntities[$entityIndex];$after=$stagedEntities[$entityIndex]
        $levelOrder=Resolve-EntityLevelOrder $before $inheritedLevelOrder
        $inMenu=$insideMenu -or $before.Name -eq 'DemoDiscMenuRoot'
        $scope=if($inMenu){Resolve-NativeScope $levelOrder}else{[helengine.EditorOverrideScope]::Common}
        $scopeSteps=$scope.ToSteps()
        if($before.Name -ne $after.Name -or $before.LocalPosition -ne $after.LocalPosition -or $before.LocalScale -ne $after.LocalScale -or $before.LocalOrientation -ne $after.LocalOrientation){throw 'Common entity transform changed.'}
        if($before.HasOverrideLevelOrder -ne $after.HasOverrideLevelOrder -or [string]::Join(',', $before.OverrideLevelOrder) -ne [string]::Join(',', $after.OverrideLevelOrder)){throw 'Authored level order changed.'}
        if($inMenu){
            foreach($configuration in @('release','debug')){
                $target=$overrideResolver.BuildTargetPath($levelOrder,'ps1',$configuration)
                $selectedTransform=$null;$selectedDepth=-1
                foreach($candidate in $after.PlatformTransformOverrides){
                    $candidateScope=[helengine.EditorOverrideScope]::FromSteps($candidate.Scope)
                    if($candidateScope.IsPrefixOf($target) -and $candidateScope.Depth -gt $selectedDepth){$selectedTransform=$candidate;$selectedDepth=$candidateScope.Depth}
                }
                $expectedPosition=[helengine.float3]::new((Native-Pixel $before.LocalPosition.X 0.25),(Native-Pixel $before.LocalPosition.Y (1.0/3.0)),$before.LocalPosition.Z)
                if($null -eq $selectedTransform -or !$selectedTransform.HasLocalPositionOverride -or $selectedTransform.LocalPosition -ne $expectedPosition){throw "Native entity transform is not selected for $configuration."}
            }
        }
        if($before.Components.Length -ne $after.Components.Length){throw 'Authored component count changed.'}
        for($componentIndex=0;$componentIndex -lt $before.Components.Length;$componentIndex++){
            $original=$before.Components[$componentIndex];$record=$after.Components[$componentIndex]
            if($original.ComponentTypeId -ne $record.ComponentTypeId -or $original.ComponentKey -ne $record.ComponentKey){throw 'Common component identity changed.'}
            if([Convert]::ToBase64String((Unchanged-ScopeBytes $original $scope)) -ne [Convert]::ToBase64String((Unchanged-ScopeBytes $record $scope))){throw 'Common payload or another platform override changed.'}
            foreach($configuration in @('release','debug')){
                $target=$overrideResolver.BuildTargetPath($levelOrder,'ps1',$configuration)
                $override=$null
                foreach($candidate in $wrappers.ReadOverrideStates($record)){
                    if($candidate.Scope.IsPrefixOf($target) -and ($null -eq $override -or $candidate.Scope.Depth -gt $override.Scope.Depth)){$override=$candidate}
                }
                if($null -eq $override){
                    if($inMenu -and $record.ComponentTypeId -in @('helengine.TextComponent','helengine.ViewportComponent')){throw "Native text/viewport has no selected PS1 override for $configuration."}
                    continue
                }
                if($inMenu -and !$override.Scope.Equals($scope)){throw "Unexpected deeper PS1 override selects for $configuration."}
                $effective=Read-Fields $override.Payload
                if($record.ComponentTypeId -eq 'helengine.TextComponent'){
                    if([BitConverter]::ToSingle($effective['FontScale']) -ne 1){throw 'Serialized PS1 FontScale is not one.'}
                    if(!$override.HasPropertyOverride('FontScale') -or !$override.HasPropertyOverride('Font')){throw 'PS1 native text properties were not explicitly overridden.'}
                    foreach($member in $override.EnumerateMemberValues().GetEnumerator()){
                        if($member.Key -eq 'FontScale'){throw 'Stale synthetic FontScale member overrides native text.'}
                    }
                    $referenceStream=[IO.MemoryStream]::new($effective['Font'],$false)
                    $reader=[helengine.EngineBinaryReader]::Create($referenceStream,[helengine.EngineBinaryEndianness]::LittleEndian)
                    try{$effectiveFont=[helengine.SceneAssetReferenceFactory]::ReadOptionalReference($reader)}finally{$reader.Dispose();$referenceStream.Dispose()}
                    $role=if($before.Name -match '^DemoDiscTitle'){'Title'}elseif($before.Name -eq 'DemoDiscFooterIdentityText'){'Footer'}else{'Buttons'}
                    if($effectiveFont.AssetId -ne $references[$role].AssetId -or $effectiveFont.RelativePath -ne $references[$role].RelativePath){throw 'PS1 native font reference does not match its role.'}
                }elseif($record.ComponentTypeId -eq 'helengine.ViewportComponent'){
                    if($effective['ScalingMode'][0] -ne [helengine.ViewportComponent]::NoScalingMode){throw 'PS1 viewport still scales text.'}
                }
            }
        }
        $untouchedBefore=@($before.PlatformTransformOverrides|Where-Object {![helengine.SceneOverrideScopePath]::AreEqual($_.Scope,$scopeSteps)})|ConvertTo-Json -Depth 12 -Compress
        $untouchedAfter=@($after.PlatformTransformOverrides|Where-Object {![helengine.SceneOverrideScopePath]::AreEqual($_.Scope,$scopeSteps)})|ConvertTo-Json -Depth 12 -Compress
        if($untouchedBefore -ne $untouchedAfter){throw 'Another platform transform override changed.'}
        Validate-RoundTrip $before.Children $after.Children $levelOrder $inMenu
    }
}
$stream=[IO.File]::OpenRead($sceneSource)
try{$unchangedScene=[helengine.files.AssetSerializer]::Deserialize($stream)}finally{$stream.Dispose()}
Validate-RoundTrip $unchangedScene.RootEntities $roundTrip.RootEntities $null $false
foreach($plan in $fontPlans){
    $path=Get-ContainedPath $stageRoot ("assets/fonts/DemoDiscPs1$($plan.Role).ttf.hasset")
    $stream=[IO.MemoryStream]::new($writes[$path],$false)
    try{$actualSettings=[helengine.editor.SectionedAssetImportSettingsBinarySerializer]::Deserialize($stream)}finally{$stream.Dispose()}
    if($actualSettings.Importer.AssetId -ne $plan.Id -or $actualSettings.Processor.Platforms['ps1'].Font.PixelSize -ne $plan.Pixels -or $actualSettings.Processor.Platforms['ps1'].FontAtlasTexture.MaxResolution -ne 256){throw 'Font identity/PS1 settings did not round-trip.'}
}
$writes.Add((Get-ContainedPath $stageRoot $sceneRelative),$sceneBytes)
$receipt=[ordered]@{platform='ps1';sourceRoot=$sourceRoot;stagedRoot=$stageRoot;authoredSceneSha256=$sourceHash;marqueeSourceSha256=(Get-FileHash -LiteralPath $marqueeSource).Hash;fontSourceSha256=$fontHash;canvas=@(320,240);viewportScalingMode='NoScaling';fontScale=1;fontPlans=$fontPlans;textCount=$changes.Count;texts=$changes;commonAndOtherScopesPreserved=$true;effectiveSerializedOverridesVerified=$true;selectedForReleaseAndDebug=$true;nativeProjectScope=(Resolve-NativeScope (Resolve-EntityLevelOrder $menuRoots[0] $null)).ToString();fontSettingsRoundTripPassed=$true;roundTripPassed=$true;validateOnly=[bool]$ValidateOnly}
if(!$ValidateOnly){
    foreach($write in $writes.GetEnumerator()){
        if((Test-Path -LiteralPath $write.Key) -and [Convert]::ToBase64String([IO.File]::ReadAllBytes($write.Key)) -eq [Convert]::ToBase64String($write.Value)){continue}
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($write.Key))|Out-Null
        [IO.File]::WriteAllBytes($write.Key,$write.Value)
    }
    if((Get-FileHash -LiteralPath $sceneSource).Hash -ne $sourceHash){throw 'Immutable baseline changed.'}
    $receipt.stagedSceneSha256=(Get-FileHash -LiteralPath (Get-ContainedPath $stageRoot $sceneRelative)).Hash
    $receipt|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $receiptPathSafe
}
$receipt|ConvertTo-Json -Depth 8
