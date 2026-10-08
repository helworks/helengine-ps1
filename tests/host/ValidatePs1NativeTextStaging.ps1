<#
.SYNOPSIS
Checks project override merging in memory against the existing authored PS1 stage.
.DESCRIPTION
The preparation helper runs in ValidateOnly mode. Fixtures add existing PS1 colour
and synthetic values, plus an unrelated platform scope, without writing a project.
#>
param(
    [Parameter(Mandatory=$true)][string]$SourceProjectRoot,
    [Parameter(Mandatory=$true)][string]$StagedProjectRoot,
    [Parameter(Mandatory=$true)][string]$PublisherRoot,
    [Parameter(Mandatory=$true)][string]$ReceiptPath
)
$ErrorActionPreference='Stop'
$helper=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../scripts/configure-staged-native-text.ps1'))
$null=. $helper -SourceProjectRoot $SourceProjectRoot -StagedProjectRoot $StagedProjectRoot -PublisherRoot $PublisherRoot -ReceiptPath $ReceiptPath -ValidateOnly

# Find a genuine persisted text payload instead of duplicating its field schema.
function Find-TextRecord($entities){
    foreach($entity in $entities){
        foreach($component in $entity.Components){if($component.ComponentTypeId -eq 'helengine.TextComponent'){return $component}}
        $child=Find-TextRecord $entity.Children
        if($null -ne $child){return $child}
    }
    return $null
}
$baseRecord=$wrappers.UnwrapBaseRecord((Find-TextRecord $unchangedScene.RootEntities))
if($null -eq $baseRecord){throw 'Authored menu has no text fixture.'}
$menuOrder=Resolve-EntityLevelOrder $menuRoots[0] $null
$scope=Resolve-NativeScope $menuOrder
$releaseTarget=$overrideResolver.BuildTargetPath($menuOrder,'ps1','release')
$debugTarget=$overrideResolver.BuildTargetPath($menuOrder,'ps1','debug')
if(!$scope.IsPrefixOf($releaseTarget) -or !$scope.IsPrefixOf($debugTarget)){throw 'Project native scope is not selected for both build configurations.'}
if([helengine.EditorOverrideScope]::ForPlatform('ps1').IsPrefixOf($releaseTarget)){throw 'Group-first fixture did not expose the old platform-only scope error.'}
$inheritedChild=[helengine.SceneEntityAsset]::new()
$inheritedChild.HasOverrideLevelOrder=$false
$inheritedChild.OverrideLevelOrder=[helengine.SceneOverrideScopeStepKind[]]@([helengine.SceneOverrideScopeStepKind]2,[helengine.SceneOverrideScopeStepKind]3)
$inheritedOrder=Resolve-EntityLevelOrder $inheritedChild $menuOrder
if(!(Resolve-NativeScope $inheritedOrder).Equals($scope)){throw 'Child without authored order ignored its parent order.'}
$inheritedChild.HasOverrideLevelOrder=$true
$authoredOrder=Resolve-EntityLevelOrder $inheritedChild $menuOrder
if(!(Resolve-NativeScope $authoredOrder).Equals([helengine.EditorOverrideScope]::ForPlatform('ps1'))){throw 'Explicit child level order was not honored.'}
$groupEndOrder=[helengine.SceneOverrideScopeStepKind[]]@([helengine.SceneOverrideScopeStepKind]2,[helengine.SceneOverrideScopeStepKind]1,[helengine.SceneOverrideScopeStepKind]3)
$groupEndScope=Resolve-NativeScope $groupEndOrder
if(!$groupEndScope.IsPrefixOf($overrideResolver.BuildTargetPath($groupEndOrder,'ps1','release'))){throw 'Platform/group/config order did not select.'}
$groupAfterConfig=[helengine.SceneOverrideScopeStepKind[]]@([helengine.SceneOverrideScopeStepKind]2,[helengine.SceneOverrideScopeStepKind]3,[helengine.SceneOverrideScopeStepKind]1)
$groupAfterConfigScope=Resolve-NativeScope $groupAfterConfig
foreach($configuration in @('release','debug')){
    if(!$groupAfterConfigScope.IsPrefixOf($overrideResolver.BuildTargetPath($groupAfterConfig,'ps1',$configuration))){throw 'Group after config order did not select for both configurations.'}
}
$configFirst=[helengine.SceneOverrideScopeStepKind[]]@([helengine.SceneOverrideScopeStepKind]1,[helengine.SceneOverrideScopeStepKind]3,[helengine.SceneOverrideScopeStepKind]2)
$rejected=$false
try{$null=Resolve-NativeScope $configFirst}catch{if($_.Exception.Message -notmatch 'requires Platform before BuildConfig'){throw};$rejected=$true}
if(!$rejected){throw 'Config-first order would author an unsafe group-only scope.'}
$state=[helengine.EntityComponentSaveState]::new()
$existing=$state.GetOrCreateScopedPlatformOverride($scope)
$fields=Read-Fields $baseRecord.Payload
$fields['Color']=[byte[]]@(5,6,7,8)
$fields['FontScale']=[BitConverter]::GetBytes([single]2)
$existing.Payload=Write-Fields $fields
$existing.SetPropertyOverride('Color')
$existing.SetPropertyOverride('FontScale')
$existing.SetMemberValue('FontScale','7')
$existing.SetMemberValue('FontScale.Legacy','8')
$existing.SetMemberValue('Color','123')
$otherScope=[helengine.EditorOverrideScope]::ForPlatform('x360')
$other=$state.GetOrCreateScopedPlatformOverride($otherScope)
$other.Payload=[byte[]]$baseRecord.Payload.Clone()
$other.SetPropertyOverride('FontScale')
$fixture=$wrappers.Wrap($baseRecord,$state)
$edited=Read-Fields $baseRecord.Payload
$edited['FontScale']=[BitConverter]::GetBytes([single]1)
$edited['Font']=Reference-Bytes $references['Title']
$result=Set-ComponentOverride $fixture $edited @('FontScale','Font') $references['Title'] $scope
if([Convert]::ToBase64String($wrappers.UnwrapBaseRecord($result).Payload) -ne [Convert]::ToBase64String($baseRecord.Payload)){throw 'Common payload changed in fixture.'}
if([Convert]::ToBase64String((Unchanged-ScopeBytes $fixture $scope)) -ne [Convert]::ToBase64String((Unchanged-ScopeBytes $result $scope))){throw 'Unrelated platform scope changed in fixture.'}
$actual=$null
foreach($candidate in $wrappers.ReadOverrideStates($result)){if($candidate.Scope.Equals($scope)){$actual=$candidate}}
if($null -eq $actual){throw 'PS1 scope was not serialized.'}
$effective=Read-Fields $actual.Payload
if([BitConverter]::ToSingle($effective['FontScale']) -ne 1){throw 'Edited FontScale lost during merge.'}
if([BitConverter]::ToString($effective['Color']) -ne '05-06-07-08'){throw 'Existing PS1 colour lost during merge.'}
if(!$actual.HasPropertyOverride('Color') -or !$actual.HasPropertyOverride('Font')){throw 'Property override markers lost during merge.'}
if($actual.HasMemberValue('FontScale') -or $actual.HasMemberValue('FontScale.Legacy')){throw 'Stale edited synthetic value survived.'}
if(!$actual.HasMemberValue('Color')){throw 'Unedited synthetic value was removed.'}
Write-Output 'PASS: real project selection for Release/Debug, inherited/explicit/group-end scope order, safe config-first rejection, round-trip/effective settings, common/other platform bytes, PS1 colour/synthetic preservation, and stale edited synthetic removal.'
