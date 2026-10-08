param(
    [Parameter(Mandatory = $true)][string]$PublisherRoot,
    [Parameter(Mandatory = $true)][string]$ManifestPath,
    [Parameter(Mandatory = $true)][string]$FontMeasurementsPath,
    [Parameter(Mandatory = $true)][string]$ReceiptPath
)
$ErrorActionPreference = 'Stop'
foreach ($Name in @('helengine.core.dll','helengine.files.dll','helengine.editor.dll')) {
    $null = [Reflection.Assembly]::LoadFrom((Join-Path $PublisherRoot $Name))
}
$Manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
$Measurements = Get-Content -LiteralPath $FontMeasurementsPath -Raw | ConvertFrom-Json
$ContentRoot = $Manifest.authored_content_root
$ScenePath = Join-Path $ContentRoot 'cooked/scenes/demodiscmainmenu.hasset'
$Stream = [IO.File]::OpenRead($ScenePath)
try { $Scene = [helengine.PackagedAssetBinarySerializer]::DeserializeSceneAsset($Stream) } finally { $Stream.Dispose() }
$Texts = [Collections.Generic.List[object]]::new()
$Viewports = [Collections.Generic.List[object]]::new()
function Read-OrdinalFields($Record) {
    $Schema = if ($Record.ComponentTypeId -eq 'helengine.ViewportComponent') {
        [ordered]@{BindingMode='byte';FixedSize='int2';ReferenceHeight='int32';ReferenceWidth='int32';ScalingMode='byte';UpdateOrder='byte'}
    } else {
        [ordered]@{Alignment='int32';Color='byte4';ConvertTextToSprite='byte';Font='font';FontScale='float';OutlineColor='byte4';OutlineScale='float';Rotation='float';SelectionEnabled='byte';ShadowColor='byte4';ShadowOffset='float2';Size='int2';SourceRect='float4';Text='string';WrapText='byte'}
    }
    $Stream = [IO.MemoryStream]::new($Record.Payload,$false)
    $Reader = [helengine.EngineBinaryReader]::Create($Stream,[helengine.EngineBinaryEndianness]::LittleEndian)
    try {
        if ($Reader.ReadByte() -ne 1 -or $Reader.ReadInt32() -ne $Schema.Count) { throw "Unexpected packaged ordinal schema: $($Record.ComponentTypeId)" }
        $Values = @{}
        foreach ($Member in $Schema.GetEnumerator()) {
            $Values[$Member.Key] = switch ($Member.Value) {
                'float' { $Reader.ReadSingle() }
                'byte' { $Reader.ReadByte() }
                'int32' { $Reader.ReadInt32() }
                'int2' { $Reader.ReadInt2() }
                'float2' { $Reader.ReadFloat2() }
                'float4' { $Reader.ReadFloat4() }
                'byte4' { @($Reader.ReadByte(),$Reader.ReadByte(),$Reader.ReadByte(),$Reader.ReadByte()) }
                'string' { $Reader.ReadString() }
                'font' { [helengine.SceneAssetReferenceFactory]::ReadOptionalReference($Reader) }
                default { throw "Unsupported ordinal member kind: $($Member.Value)" }
            }
        }
        if ($Stream.Position -ne $Stream.Length) { throw "Unexpected packaged ordinal tail: $($Record.ComponentTypeId)" }
        return $Values
    } finally { $Reader.Dispose(); $Stream.Dispose() }
}
function Inspect-Entity($Entity) {
    foreach ($Record in $Entity.Components) {
        if ($Record.ComponentTypeId -notin @('helengine.TextComponent','helengine.ViewportComponent')) { continue }
        $Fields = Read-OrdinalFields $Record
        if ($Record.ComponentTypeId -eq 'helengine.TextComponent') {
            $Scale = $Fields['FontScale']
            $Font = $Fields['Font']
            if ($Scale -ne 1) { throw "Packaged text is still scaled: $($Entity.Name) $Scale" }
            if ($null -eq $Font -or $Font.RelativePath -notmatch 'demodiscps1(title|buttons|footer)\.hefont$') { throw "Packaged text uses an unexpected font: $($Entity.Name) $($Font.RelativePath)" }
            $Texts.Add([ordered]@{Entity=$Entity.Name;FontScale=$Scale;Font=$Font.RelativePath})
        } else {
            $Mode = $Fields['ScalingMode']
            if ($Mode -ne [helengine.ViewportComponent]::NoScalingMode) { throw "Packaged viewport still scales the menu: $($Entity.Name) $Mode" }
            $Viewports.Add([ordered]@{Entity=$Entity.Name;ScalingMode=$Mode})
        }
    }
    foreach ($Child in $Entity.Children) { Inspect-Entity $Child }
}
foreach ($Entity in $Scene.RootEntities) { Inspect-Entity $Entity }
if ($Texts.Count -ne 37 -or $Viewports.Count -ne 1) { throw "Unexpected packaged native text/viewport count: $($Texts.Count)/$($Viewports.Count)" }
$Fonts = @()
foreach ($Specification in @(@{Role='title';Pixels=13},@{Role='buttons';Pixels=21},@{Role='footer';Pixels=11})) {
    $BasePath = Join-Path $ContentRoot "cooked/fonts/demodiscps1$($Specification.Role)"
    $Stream = [IO.File]::OpenRead($BasePath+'.hefont')
    try { $Font = [helengine.files.FontAssetBinarySerializer]::Deserialize($Stream) } finally { $Stream.Dispose() }
    $Stream = [IO.File]::OpenRead($BasePath+'.hetex')
    try { $Texture = [helengine.files.AssetSerializer]::Deserialize($Stream) } finally { $Stream.Dispose() }
    $Expected = @($Measurements | Where-Object PixelSize -eq $Specification.Pixels)
    if ($Expected.Count -ne 1) { throw 'Native font measurement is missing or ambiguous.' }
    if ($Font.AtlasWidth -ne $Expected[0].AtlasWidth -or $Font.AtlasHeight -ne $Expected[0].AtlasHeight -or [Math]::Abs($Font.LineHeight-$Expected[0].LineHeight) -gt 0.0001) { throw "Packaged font differs from native-size raster measurements: $($Specification.Role)" }
    if ($Texture.Width -ne $Font.AtlasWidth -or $Texture.Height -ne $Font.AtlasHeight) { throw "Packaged atlas is resized: $($Specification.Role)" }
    if ($Texture.ColorFormat -ne [helengine.TextureAssetColorFormat]::Indexed4) { throw "Packaged atlas has unexpected PS1 format: $($Specification.Role)" }
    $Fonts += [ordered]@{Role=$Specification.Role;RasterPixelSize=$Specification.Pixels;LineHeight=$Font.LineHeight;AtlasWidth=$Font.AtlasWidth;AtlasHeight=$Font.AtlasHeight;TextureWidth=$Texture.Width;TextureHeight=$Texture.Height;ColorFormat=$Texture.ColorFormat.ToString();FontSha256=(Get-FileHash -LiteralPath ($BasePath+'.hefont')).Hash;TextureSha256=(Get-FileHash -LiteralPath ($BasePath+'.hetex')).Hash}
}
$Executable = @($Manifest.artifacts | Where-Object path -like '*.ps-exe')
if ($Executable.Count -ne 1 -or (Get-FileHash -LiteralPath $Executable[0].path).Hash.ToLowerInvariant() -ne $Executable[0].sha256.ToLowerInvariant()) { throw 'Built executable receipt mismatch.' }
[ordered]@{Passed=$true;TextCount=$Texts.Count;Texts=$Texts;Viewports=$Viewports;Fonts=$Fonts;Executable=$Executable[0].path;ExecutableSha256=$Executable[0].sha256;SceneSha256=(Get-FileHash -LiteralPath $ScenePath).Hash} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ReceiptPath -Encoding utf8
Write-Output "PASS: $($Texts.Count) packaged texts use FontScale=1; viewport has NoScaling; 13/21/11px cooked atlases retain their raster dimensions."
