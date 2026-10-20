using helengine;
using helengine.baseplatform.Manifest;
using helengine.editor;
using helengine.ps1.builder;

namespace helengine.ps1.platformbuilder.tests;

/// <summary>Checks native TIM decoding and the serialized PS1 cook contract with byte-exact fixtures.</summary>
public sealed class PlayStation1TextureTests {
    /// <summary>Checks low-nibble-first indices and multiple CLUT banks in both indexed depths.</summary>
    /// <param name="mode">TIM indexed mode.</param>
    /// <param name="entries">Number of colors per CLUT bank.</param>
    /// <param name="width">Decoded texel width of one word.</param>
    [Theory]
    [InlineData(0, 16, 4)]
    [InlineData(1, 256, 2)]
    public void ImportIndexedTim_DecodesIndicesAndSelectedBank(int mode, int entries, int width) {
        byte[] palette = new byte[entries * 4];
        palette[2] = 0x1f;
        palette[entries * 2 + 3] = 0x7c;
        using MemoryStream stream = CreateTim(mode, 1, 1, mode == 0 ? new byte[] { 0x10, 0 } : new byte[] { 0, 1 }, palette, entries);
        PlayStationTimTextureImporter importer = new();
        TextureAsset first = importer.ImportTexture(stream);
        Assert.Equal((ushort)width, first.Width);
        Assert.Equal(new byte[] { 0, 0, 0, 0, 255, 0, 0, 255 }, first.Colors.Take(8));
        Assert.True(stream.CanRead);
        stream.Position = 0;
        TextureAsset second = importer.ImportTexture(stream, 1);
        Assert.Equal(new byte[] { 0, 0, 0, 0, 0, 0, 255, 255 }, second.Colors.Take(8));
        stream.Position = 0;
        Assert.Throws<InvalidDataException>(() => importer.ImportTexture(stream, 2));
    }

    /// <summary>Checks color channel order, transparent zero and visible black independently of STP.</summary>
    [Fact]
    public void Import16BitTim_DecodesNativeColorWords() {
        using MemoryStream stream = CreateTim(2, 4, 1, new byte[] { 0x1f, 0, 0xe0, 3, 0, 0, 0, 0x80 });
        TextureAsset asset = new PlayStationTimTextureImporter().ImportTexture(stream);
        Assert.Equal(new byte[] { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 255 }, asset.Colors);
    }

    /// <summary>Checks RGB byte order and skips each row's word alignment padding.</summary>
    [Fact]
    public void Import24BitTim_SkipsRowPadding() {
        using MemoryStream stream = CreateTim(3, 2, 2, new byte[] { 11, 22, 33, 0xee, 44, 55, 66, 0xdd });
        TextureAsset asset = new PlayStationTimTextureImporter().ImportTexture(stream);
        Assert.Equal((ushort)1, asset.Width);
        Assert.Equal((ushort)2, asset.Height);
        Assert.Equal(new byte[] { 11, 22, 33, 255, 44, 55, 66, 255 }, asset.Colors);
        Assert.Equal(TextureAssetAlphaPrecision.Opaque, asset.AlphaPrecision);
    }

    /// <summary>Rejects truncated headers, palettes and pixel payloads rather than returning partial images.</summary>
    /// <param name="length">Truncated fixture length.</param>
    [Theory]
    [InlineData(0)]
    [InlineData(7)]
    [InlineData(19)]
    [InlineData(21)]
    public void ImportTim_RejectsTruncatedPayload(int length) {
        using MemoryStream complete = CreateTim(2, 1, 1, new byte[] { 0x1f, 0 });
        using MemoryStream truncated = new(complete.ToArray().Take(length).ToArray());
        Assert.Throws<EndOfStreamException>(() => new PlayStationTimTextureImporter().ImportTexture(truncated));
    }

    /// <summary>Rejects mixed modes, reserved flags, mismatched section sizes and absent indexed palettes.</summary>
    /// <param name="headerOffset">Byte to corrupt.</param>
    /// <param name="value">Invalid byte value.</param>
    [Theory]
    [InlineData(0, 17)]
    [InlineData(4, 4)]
    [InlineData(4, 16)]
    [InlineData(4, 0)]
    [InlineData(8, 12)]
    [InlineData(16, 0)]
    public void ImportTim_RejectsMalformedHeaders(int headerOffset, byte value) {
        using MemoryStream complete = CreateTim(2, 1, 1, new byte[] { 0x1f, 0 });
        byte[] bytes = complete.ToArray();
        bytes[headerOffset] = value;
        using MemoryStream stream = new(bytes);
        Assert.Throws<InvalidDataException>(() => new PlayStationTimTextureImporter().ImportTexture(stream));
    }

    /// <summary>Checks exact little-endian words and binary thresholding through a serializer round trip.</summary>
    [Fact]
    public void Cook16BitTexture_PreservesBlackTransparencyAndSerializedFormat() {
        TextureAsset source = new() {
            Width = 4, Height = 1, ColorFormat = TextureAssetColorFormat.Rgba32, AlphaPrecision = TextureAssetAlphaPrecision.A8,
            Colors = new byte[] { 255, 0, 0, 255, 0, 0, 0, 255, 10, 20, 30, 127, 0, 255, 0, 128 }
        };
        TextureAssetProcessorSettings settings = PlayStation1TextureCookSettingsSerializer.Deserialize(
            "{\"maxResolution\":256,\"colorFormat\":\"Ps1Bgr555\",\"alphaPrecision\":\"Binary\"}");
        TextureAsset cooked = new PlayStation1TextureCooker().Apply(source, settings);
        Assert.Equal(TextureAssetColorFormat.Ps1Bgr555, cooked.ColorFormat);
        Assert.Equal(new byte[] { 0x1f, 0x80, 0, 0x80, 0, 0, 0xe0, 0x83 }, cooked.Colors);
        TextureAsset roundTrip = Assert.IsType<TextureAsset>(helengine.files.AssetSerializer.DeserializeFromBytes(
            helengine.files.AssetSerializer.SerializeToBytes(cooked)));
        Assert.Equal(cooked.ColorFormat, roundTrip.ColorFormat);
        Assert.Equal(cooked.Colors, roundTrip.Colors);
        Assert.Equal(new byte[] { 255, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 0, 0, 255, 0, 255 }, TextureAssetPixelCodec.DecodeToRgba32(roundTrip));
        Assert.Contains("Ps1Bgr555", PlayStation1TextureCookSettingsSerializer.Serialize(settings));
    }

    /// <summary>Checks forced opacity, resize behavior and the hardware's uncapped-page rejection.</summary>
    [Fact]
    public void Cook16BitTexture_ResizesAndHonorsOpaqueCoverage() {
        TextureAsset source = new() {
            Width = 257, Height = 1, ColorFormat = TextureAssetColorFormat.Rgba32, AlphaPrecision = TextureAssetAlphaPrecision.A8,
            Colors = new byte[257 * 4]
        };
        TextureAssetProcessorSettings settings = new() {
            MaxResolution = 256, ColorFormatId = "Ps1Bgr555", AlphaPrecision = TextureAssetAlphaPrecision.Opaque
        };
        TextureAsset cooked = new PlayStation1TextureCooker().Apply(source, settings);
        Assert.Equal((ushort)256, cooked.Width);
        Assert.All(cooked.Colors.Where((value, index) => index % 2 == 1), value => Assert.Equal((byte)0x80, value));
        settings.MaxResolution = 0;
        Assert.Throws<InvalidOperationException>(() => new PlayStation1TextureCooker().Apply(source, settings));
    }

    /// <summary>Checks each hardware storage mode remains available to the indexed cook pipeline.</summary>
    /// <param name="format">Indexed storage format.</param>
    [Theory]
    [InlineData(TextureAssetColorFormat.Indexed4)]
    [InlineData(TextureAssetColorFormat.Indexed8)]
    public void CookIndexedTexture_UsesSharedQuantizer(TextureAssetColorFormat format) {
        TextureAsset source = new() {
            Width = 3, Height = 2, ColorFormat = TextureAssetColorFormat.Rgba32, AlphaPrecision = TextureAssetAlphaPrecision.A8,
            Colors = new byte[24]
        };
        TextureAsset cooked = new PlayStation1TextureCooker().Apply(source, new TextureAssetProcessorSettings {
            MaxResolution = 256, ColorFormat = format, AlphaPrecision = TextureAssetAlphaPrecision.Binary
        });
        Assert.Equal(format, cooked.ColorFormat);
        Assert.Equal(format == TextureAssetColorFormat.Indexed4 ? 3 : 6, cooked.Colors.Length);
    }

    /// <summary>Checks unsupported alpha precision is rejected before cooking a native texture.</summary>
    [Fact]
    public void DeserializeNativeTexture_RejectsUnsupportedAlpha() {
        Assert.Throws<InvalidOperationException>(() => PlayStation1TextureCookSettingsSerializer.Deserialize(
            "{\"maxResolution\":256,\"colorFormat\":\"Ps1Bgr555\",\"alphaPrecision\":\"A8\"}"));
    }

    /// <summary>Exercises file routing, builder-owned cooking, serialization and runtime reading as one pipeline.</summary>
    [Fact]
    public void CookTimFile_ProducesNativeRuntimeTexture() {
        string outputRoot = Path.GetFullPath(@"C:\temp\numeric-precision\ps1\texture-source-tests");
        Directory.CreateDirectory(outputRoot);
        string sourcePath = Path.Combine(outputRoot, "source.TIM");
        using MemoryStream tim = CreateTim(2, 2, 1, new byte[] { 0x1f, 0, 0, 0x80 });
        File.WriteAllBytes(sourcePath, tim.ToArray());
        PlatformCookWorkItem workItem = new("tim-test", sourcePath, "texture", "ps1", "texture",
            "cooked/texture.heltex", "tim-test-texture", "source-hash", "settings-hash",
            "{\"maxResolution\":256,\"colorFormat\":\"Ps1Bgr555\",\"alphaPrecision\":\"Binary\"}", []);
        new PlayStation1PlatformCookWorkItemExecutor().Execute(workItem, outputRoot, NumericPrecisionPolicy.Default);
        string cookedPath = Path.Combine(outputRoot, "cooked", "texture.heltex");
        using FileStream stream = File.OpenRead(cookedPath);
        using TextureAsset runtime = Assert.IsType<TextureAsset>(PackagedAssetBinarySerializer.Deserialize(stream, NumericPrecisionPolicy.Default));
        Assert.Equal("tim-test-texture", runtime.Id);
        Assert.Equal(TextureAssetColorFormat.Ps1Bgr555, runtime.ColorFormat);
        Assert.Equal(new byte[] { 0x1f, 0x80, 0, 0x80 }, runtime.Colors);
        TextureAsset decodedSource = new PlayStation1SourceTextureDecoder().Decode(cookedPath);
        Assert.Equal(new byte[] { 255, 0, 0, 255, 0, 0, 0, 255 }, decodedSource.Colors);
    }

    /// <summary>Builds a standard TIM with nonzero VRAM coordinates to keep addressing separate from image dimensions.</summary>
    /// <param name="mode">Standard TIM depth selector.</param>
    /// <param name="widthInWords">Pixel row width in 16-bit words.</param>
    /// <param name="height">Pixel rows.</param>
    /// <param name="pixels">Native pixel payload.</param>
    /// <param name="palette">Optional native CLUT words.</param>
    /// <param name="paletteWidth">CLUT row width.</param>
    /// <returns>Readable fixture stream positioned at its header.</returns>
    static MemoryStream CreateTim(int mode, ushort widthInWords, ushort height, byte[] pixels, byte[] palette = null, int paletteWidth = 0) {
        MemoryStream stream = new();
        using (BinaryWriter writer = new(stream, System.Text.Encoding.UTF8, true)) {
            writer.Write(0x10u);
            writer.Write((uint)(mode | (palette == null ? 0 : 8)));
            if (palette != null) {
                WriteSection(writer, (ushort)paletteWidth, (ushort)(palette.Length / (paletteWidth * 2)), palette);
            }
            WriteSection(writer, widthInWords, height, pixels);
        }
        stream.Position = 0;
        return stream;
    }

    /// <summary>Writes a byte-exact section with explicit destination coordinates.</summary>
    /// <param name="writer">Fixture destination.</param>
    /// <param name="width">Row width in words.</param>
    /// <param name="height">Number of rows.</param>
    /// <param name="payload">Native word bytes.</param>
    static void WriteSection(BinaryWriter writer, ushort width, ushort height, byte[] payload) {
        writer.Write((uint)(payload.Length + 12));
        writer.Write((ushort)320);
        writer.Write((ushort)256);
        writer.Write(width);
        writer.Write(height);
        writer.Write(payload);
    }
}
