using helengine;
using helengine.editor;
using helengine.baseplatform.Manifest;
using helengine.baseplatform.Requests;
using helengine.ps1.builder;

namespace helengine.ps1.platformbuilder.tests;

/// <summary>Exercises platform-owned producers with a build policy different from the authoring host.</summary>
public sealed class PlayStation1CookNumericPolicyTests {
    /// <summary>Imported textures and generated font atlases carry the requested policy through their real executor.</summary>
    [Theory]
    [InlineData(32, "texture")]
    [InlineData(64, "texture")]
    [InlineData(32, "font-atlas-texture")]
    [InlineData(64, "font-atlas-texture")]
    public void TextureEnvelopeUsesBuildPolicy(int bits, string sourceKind) {
        string root = @"C:\temp\numeric-precision\ps1\cook-fixture";
        Directory.CreateDirectory(root);
        string sourcePath = Path.Combine(root, "source.hasset");
        string destination = Path.Combine(root, "cooked", "fixture.hetex");
        TextureAsset source = new TextureAsset { Width = 2, Height = 2, ColorFormat = TextureAssetColorFormat.Rgba32,
            AlphaPrecision = TextureAssetAlphaPrecision.Opaque, Colors = Enumerable.Repeat((byte)255, 16).ToArray() };
        File.WriteAllBytes(sourcePath, helengine.files.AssetSerializer.SerializeToBytes(source));
        NumericPrecisionPolicy policy = new NumericPrecisionPolicy(bits, bits);
        PlatformCookWorkItem item = new PlatformCookWorkItem("numeric", sourcePath, sourceKind, "ps1", "texture",
            "cooked/fixture.hetex", "numeric-texture", "source-hash", "settings-hash",
            PlayStation1TextureCookSettingsSerializer.Serialize(new TextureAssetProcessorSettings {
                MaxResolution = 256, ColorFormatId = "Ps1Bgr555", AlphaPrecision = TextureAssetAlphaPrecision.Opaque }), []);
        try {
            new PlayStation1PlatformCookWorkItemExecutor().Execute(item, root, policy);
            byte[] bytes = File.ReadAllBytes(destination);
            Assert.Equal(new byte[] { 1, (byte)bits, (byte)bits }, bytes.Skip(12).Take(3));
            using MemoryStream stream = new MemoryStream(bytes, false);
            TextureAsset restored = Assert.IsType<TextureAsset>(PackagedAssetBinarySerializer.Deserialize(stream, policy));
            Assert.Equal((ushort)2, restored.Width);
            Assert.Equal((ushort)2, restored.Height);
        } finally { File.Delete(sourcePath); File.Delete(destination); }
    }

    /// <summary>A foreign policy is rejected during staging validation before any SDK compilation or game launch.</summary>
    [Fact]
    public void StagedForeignPolicyFailsBeforeNativeBuild() {
        string root = @"C:\temp\numeric-precision\ps1\cook-fixture";
        Directory.CreateDirectory(root);
        string path = Path.Combine(root, "foreign.hasset");
        File.WriteAllBytes(path, helengine.files.AssetSerializer.SerializeToBytes(new TextureAsset {
            Width = 1, Height = 1, ColorFormat = TextureAssetColorFormat.Rgba32, Colors = new byte[4]
        }, new NumericPrecisionPolicy(64, 64)));
        try {
            InvalidOperationException error = Assert.Throws<InvalidOperationException>(() =>
                PlayStation1CookedAssetPolicyValidator.Validate([new PlayStation1StagedAsset("cooked/foreign.hasset", path)],
                    NumericPrecisionPolicy.Default, CancellationToken.None));
            Assert.Contains("cooked/foreign.hasset", error.Message);
            Assert.Contains("real-v1-s32-a32", error.Message);
        } finally { File.Delete(path); }
    }

    /// <summary>Font binaries share the format id but retain their independent fixed-width record schema.</summary>
    [Fact]
    public void FixedFontRecordIsNotAnAssetNumericEnvelope() {
        string root = @"C:\temp\numeric-precision\ps1\cook-fixture";
        Directory.CreateDirectory(root);
        string path = Path.Combine(root, "fixed.hefont");
        using (FileStream stream = File.Create(path)) {
            EngineBinaryHeaderSerializer.Write(stream, new EngineBinaryHeader(EngineBinaryEndianness.LittleEndian,
                FontAssetBinarySerializer.CurrentVersion, FontAssetBinarySerializer.FormatId,
                (ushort)FontAssetBinarySerializer.RecordKind, 1));
        }
        try { PlayStation1CookedAssetPolicyValidator.Validate([new PlayStation1StagedAsset("cooked/fixed.hefont", path)],
            NumericPrecisionPolicy.Default, CancellationToken.None); }
        finally { File.Delete(path); }
    }

    /// <summary>Standard and authored platform materials keep their build policy in the final runtime envelope.</summary>
    [Theory]
    [InlineData(32)]
    [InlineData(64)]
    public void MaterialEnvelopeUsesBuildPolicy(int bits) {
        NumericPrecisionPolicy policy = new NumericPrecisionPolicy(bits, bits);
        PlatformMaterialCookRequest request = new PlatformMaterialCookRequest("numeric-material", "material.hemat", "ps1", "release", "",
            PlayStation1MaterialSchemaIds.OpaqueSchemaId,
            new Dictionary<string, string> { [PlayStation1MaterialSchemaIds.BaseColorFieldId] = "#3F7FBFFF" }, policy);
        byte[] bytes = new PlayStation1MaterialCooker().Cook(request).CookedMaterialBytes;
        Assert.Same(policy, request.NumericPolicy);
        Assert.Equal(new byte[] { 1, (byte)bits, (byte)bits }, bytes.Skip(12).Take(3));
        using MemoryStream stream = new MemoryStream(bytes, false);
        PlatformMaterialAsset material = Assert.IsType<PlatformMaterialAsset>(PackagedAssetBinarySerializer.Deserialize(stream, policy));
        Assert.Equal((byte)0x3f, material.BaseColorR);
    }
}
