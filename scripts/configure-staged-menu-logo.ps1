<#
.SYNOPSIS
Sets the staged DemoDisc logo's PS1 texture resolution without changing other platforms.
#>
param(
    [Parameter(Mandatory=$true)][string]$StagedProjectRoot,
    [Parameter(Mandatory=$true)][string]$PublisherRoot,
    [Parameter(Mandatory=$true)][string]$ReceiptPath,
    [ValidateRange(1,256)][int]$MaxResolution=256
)
$ErrorActionPreference='Stop'
$BuildRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../builds/helengine-ps1'))
$StageRoot=[IO.Path]::GetFullPath($StagedProjectRoot)
if(!$StageRoot.StartsWith($BuildRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {throw 'Logo configuration requires a PS1 project stage inside the workspace build root.'}
$SettingsPath=Join-Path $StageRoot 'assets/textures/menu/helengine-logo.png.hasset'
foreach($Name in @('helengine.core.dll','helengine.files.dll','helengine.editor.dll')) {
    [Reflection.Assembly]::LoadFrom((Join-Path $PublisherRoot $Name))|Out-Null
}
$Stream=[IO.File]::OpenRead($SettingsPath)
try{$Settings=[helengine.editor.TextureAssetImportSettingsBinarySerializer]::Deserialize($Stream)}finally{$Stream.Dispose()}
$BeforeHash=(Get-FileHash -LiteralPath $SettingsPath).Hash
$Ps1Settings=$Settings.Processor.Platforms['ps1']
if($null -eq $Ps1Settings) {
    $Ps1Settings=[helengine.editor.TextureAssetProcessorSettings]::new()
    $Ps1Settings.ColorFormat=[helengine.TextureAssetColorFormat]::Indexed8
    $Ps1Settings.AlphaPrecision=[helengine.TextureAssetAlphaPrecision]::Binary
    $Settings.Processor.Platforms.Add('ps1',$Ps1Settings)
}
$Ps1Settings.MaxResolution=$MaxResolution
$Stream=[IO.MemoryStream]::new()
try {
    [helengine.editor.TextureAssetImportSettingsBinarySerializer]::Serialize($Stream,$Settings)
    $Bytes=$Stream.ToArray()
}finally{$Stream.Dispose()}
$Stream=[IO.MemoryStream]::new($Bytes,$false)
try{$RoundTrip=[helengine.editor.TextureAssetImportSettingsBinarySerializer]::Deserialize($Stream)}finally{$Stream.Dispose()}
if($RoundTrip.Processor.Platforms['ps1'].MaxResolution -ne $MaxResolution) {throw 'Logo resolution did not round-trip.'}
[IO.File]::WriteAllBytes($SettingsPath,$Bytes)
[ordered]@{Platform='ps1';Asset='textures/menu/helengine-logo.png';MaxResolution=$MaxResolution;ColorFormat=$Ps1Settings.ColorFormatId;BeforeSha256=$BeforeHash;AfterSha256=(Get-FileHash -LiteralPath $SettingsPath).Hash}|ConvertTo-Json|Set-Content -LiteralPath $ReceiptPath
Write-Output "PS1 menu logo: MaxResolution=$MaxResolution, $($Ps1Settings.ColorFormatId)."
