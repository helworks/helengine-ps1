param(
    [Parameter(Mandatory = $true)][string]$PublisherRoot,
    [Parameter(Mandatory = $true)][string]$TitleTestAssembly,
    [Parameter(Mandatory = $true)][string]$SourceFontPath,
    [Parameter(Mandatory = $true)][string]$ReceiptPath
)
$ErrorActionPreference = 'Stop'
$BuildRoot = 'C:\dev\helworks\builds\helengine-ps1\menu-20261007'
$env:TEMP = Join-Path $BuildRoot 'temp'
$env:TMP = $env:TEMP
$null = New-Item -ItemType Directory -Path $env:TEMP -Force
$DesktopRoot = Join-Path $env:ProgramFiles 'dotnet\shared\Microsoft.WindowsDesktop.App'
$Drawing = Get-ChildItem -LiteralPath $DesktopRoot -Filter 'System.Drawing.Common.dll' -Recurse -File | Sort-Object FullName -Descending | Select-Object -First 1
if (!$Drawing) { throw 'The Windows desktop runtime is required for real font import validation.' }
$null = [Reflection.Assembly]::LoadFrom($Drawing.FullName)
foreach ($Name in @('helengine.core.dll','helengine.files.dll','helengine.editor.dll','helengine.editor.windows.dll','helengine.editor.windows.gdiimporter.dll')) {
    $null = [Reflection.Assembly]::LoadFrom((Join-Path $PublisherRoot $Name))
}
$TitleAssembly = [Reflection.Assembly]::LoadFrom($TitleTestAssembly)
$FitPair = $TitleAssembly.GetType('DemoDisc.menu.DemoDiscTitleLayout',$true).GetMethod('FitPair')
$Options = [helengine.CoreInitializationOptions]::new()
$Options.ContentStreamSource = [helengine.HostFileSystemContentStreamSource]::new($BuildRoot)
$Entries = [helengine.RuntimeSceneCatalogEntry[]]@([helengine.RuntimeSceneCatalogEntry]::new('menu','cooked/menu.hasset'))
$Options.SceneCatalog = [helengine.RuntimeSceneCatalog]::new($Entries)
$Core = [helengine.Core]::new($Options)
$Core.Initialize($null,$null,$null,[helengine.PlatformInfo]::new('test','test'))
$Root = [helengine.Entity]::new($Core)
$Root.InitComponents()
$Root.InitChildren()
$Importer = [helengine.editor.GdiFontImporter]::new($null)
$Fonts = @{}
$Checks = [Collections.Generic.List[string]]::new()
foreach ($PixelSize in @(13,21,11)) {
    $Settings = [helengine.editor.FontAssetProcessorSettings]::new()
    $Settings.PixelSize = $PixelSize
    $Stream = [IO.File]::OpenRead($SourceFontPath)
    try { $Fonts[$PixelSize] = $Importer.ImportFont($Stream,$Settings) } finally { $Stream.Dispose() }
    $Font = $Fonts[$PixelSize]
    if ($Font.AtlasWidth -gt 256 -or $Font.AtlasHeight -gt 256) { throw "${PixelSize}px font exceeds one native PS1 texture page." }
    $Checks.Add("${PixelSize}px font fits one native texture page without atlas resizing.")
}
$Texts = @()
foreach ($Specification in @(
    @{Text='HELENGINE';PixelSize=13;Width=70;Height=16},
    @{Text='DEMO DISC';PixelSize=13;Width=70;Height=16},
    @{Text='SCENES';PixelSize=21;Width=175;Height=36},
    @{Text='HELENGINE DemoDisc';PixelSize=11;Width=320;Height=18}
)) {
    $Entity = [helengine.Entity]::new($Core)
    $Entity.InitComponents()
    $Entity.InitChildren()
    $Root.AddChild($Entity)
    $Text = [helengine.TextComponent]::new()
    $Text.Text = $Specification.Text
    $Text.Font = $Fonts[$Specification.PixelSize]
    $Text.FontScale = [single]1
    $Text.OutlineScale = [single]1
    $Text.Size = [helengine.int2]::new($Specification.Width,$Specification.Height)
    $Entity.AddComponent($Text)
    $Texts += $Text
}
$Viewport = [helengine.ViewportComponent]::new()
$Viewport.BindingMode = [helengine.ViewportComponent]::FixedBindingMode
$Viewport.FixedSize = [helengine.int2]::new(320,240)
$Viewport.ReferenceWidth = 1280
$Viewport.ReferenceHeight = 720
$Viewport.ScalingMode = [helengine.ViewportComponent]::NoScalingMode
$Root.AddComponent($Viewport)
for ($Index = 0; $Index -lt 1000; $Index++) {
    $Viewport.Update()
    $null = $FitPair.Invoke($null,[object[]]@($Texts[0],$Texts[1]))
}
foreach ($Text in $Texts) {
    if ($Text.FontScale -ne 1 -or $Text.OutlineScale -ne 1) { throw "Native-size text was scaled during viewport/menu updates: $($Text.Text)." }
}
$Checks.Add('A 320x240 NoScaling viewport and 1000 heading fits preserve FontScale=1 and integer effects for every text role.')
$RightEdges = @()
foreach ($Text in $Texts[0..1]) {
    $Width = [helengine.TextLayoutAlignmentUtils]::MeasureVisibleLineWidth($Text.Text,$Text.Font,[single]1,$Text.Font.AtlasWidth)
    if ($Width + $Text.OutlineScale + 1 -gt $Text.Size.X) { throw "Native-size title does not fit its text box: $($Text.Text)." }
    $Offset = [helengine.TextLayoutAlignmentUtils]::ResolveHorizontalOffset($Text.Alignment,$Text.Size.X,$Width)
    $RightEdges += 309 + [Math]::Round($Text.Parent.LocalPosition.X) + $Offset + $Width + $Text.OutlineScale
}
if ([Math]::Abs($RightEdges[0]-308) -gt 0.0001 -or [Math]::Abs($RightEdges[1]-308) -gt 0.0001) { throw 'The two native-size titles do not share their approved painted right edge.' }
$Checks.Add('Both 13px headings fit without reduction and paint the same right edge at pixel308.')
$CookSettings = [helengine.editor.TextureAssetProcessorSettings]::new()
$CookSettings.MaxResolution = 256
$CookSettings.ColorFormat = [helengine.TextureAssetColorFormat]::Indexed4
$CookSettings.AlphaPrecision = [helengine.TextureAssetAlphaPrecision]::Binary
$Processor = [helengine.editor.TextureAssetProcessor]::new()
foreach ($PixelSize in @(13,21,11)) {
    $Atlas = $Fonts[$PixelSize].SourceTextureAsset
    $Cooked = $Processor.Apply($Atlas,$CookSettings)
    if ($Cooked.Width -ne $Atlas.Width -or $Cooked.Height -ne $Atlas.Height) { throw "Texture cooking resized the ${PixelSize}px font atlas." }
    $Checks.Add("PS1 indexed4 cooking keeps the ${PixelSize}px atlas at its exact raster dimensions.")
    $Cooked.Dispose()
}
[ordered]@{Passed=$true;Checks=$Checks;FontScale=1;Framebuffer=@(320,240);PaintedTitleRightEdges=$RightEdges;SourceFontSha256=(Get-FileHash -LiteralPath $SourceFontPath).Hash;EngineSha256=(Get-FileHash -LiteralPath (Join-Path $PublisherRoot 'helengine.core.dll')).Hash;TitleTestAssemblySha256=(Get-FileHash -LiteralPath $TitleTestAssembly).Hash} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ReceiptPath -Encoding utf8
Write-Output "PASS: $($Checks.Count) real-engine native font/layout/cooking checks."
