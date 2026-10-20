using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;
using helengine.ps1.builder;

namespace helengine.ps1.platformbuilder.tests;

/// <summary>Verifies real material serialization for the PS1 textured opaque path without filesystem fixtures or native renderer substitutes.</summary>
public sealed class PlayStation1TexturedMaterialTests {
    /// <summary>Preserves the material identity, packaged texture dependency, tint and selected lighting mode through engine serialization.</summary>
    /// <param name="lighting">The existing unlit or directional vertex-lit mode.</param>
    [Theory]
    [InlineData("unlit")]
    [InlineData("vertex-lit")]
    public void CookPreservesCanonicalTextureAndOpaqueMaterial(string lighting) {
        Dictionary<string, string> fields = Fields("cooked/imported/texture-identity", lighting);
        PlatformMaterialCookRequest request = Request(fields);
        PlatformMaterialCookResult result = new PlayStation1MaterialCooker().Cook(request);
        using MemoryStream stream = new MemoryStream(result.CookedMaterialBytes, false);
        helengine.PlatformMaterialAsset material = Assert.IsType<helengine.PlatformMaterialAsset>(helengine.files.AssetSerializer.Deserialize(stream));
        Assert.Equal("material-identity", material.Id);
        Assert.Equal("ps1-opaque", material.RendererFamilyId);
        Assert.Equal("cooked/imported/texture-identity", material.TextureRelativePath);
        Assert.Equal(lighting == "vertex-lit", material.Lit);
        Assert.False(material.DoubleSided);
        Assert.False(material.UseVertexColor);
        Assert.Equal(0x3F, material.BaseColorR);
        Assert.Equal(0x7F, material.BaseColorG);
        Assert.Equal(0xBF, material.BaseColorB);
        Assert.Equal(255, material.BaseColorA);
        Assert.Empty(result.ReferencedShaderDependencies);
        Assert.Equal("cooked/imported/texture-identity", request.FieldValues[PlayStation1MaterialSchemaIds.TextureRelativePathFieldId]);
        Assert.Equal("cooked/imported/texture-identity", fields[PlayStation1MaterialSchemaIds.TextureRelativePathFieldId]);
    }

    /// <summary>Retains the previously supported untextured opaque path and faithful default lighting.</summary>
    [Fact]
    public void EmptyTextureRetainsColorOnlyAndDefaultLighting() {
        Dictionary<string, string> fields = Fields(string.Empty, "vertex-lit");
        fields.Remove(PlayStation1MaterialSchemaIds.LightingModeFieldId);
        PlatformMaterialCookResult result = new PlayStation1MaterialCooker().Cook(Request(fields));
        using MemoryStream stream = new MemoryStream(result.CookedMaterialBytes, false);
        helengine.PlatformMaterialAsset material = Assert.IsType<helengine.PlatformMaterialAsset>(helengine.files.AssetSerializer.Deserialize(stream));
        Assert.Equal(string.Empty, material.TextureRelativePath);
        Assert.True(material.Lit);
        Assert.Equal(255, material.BaseColorA);
        Assert.Empty(result.ReferencedShaderDependencies);
    }

    /// <summary>Normalizes a valid Windows separator spelling without mutating authored request fields.</summary>
    [Fact]
    public void CookNormalizesCookedRelativeSeparators() {
        Dictionary<string, string> fields = Fields("cooked\\imported\\texture.hetex", "unlit");
        PlatformMaterialCookResult result = new PlayStation1MaterialCooker().Cook(Request(fields));
        using MemoryStream stream = new MemoryStream(result.CookedMaterialBytes, false);
        helengine.PlatformMaterialAsset material = Assert.IsType<helengine.PlatformMaterialAsset>(helengine.files.AssetSerializer.Deserialize(stream));
        Assert.Equal("cooked/imported/texture.hetex", material.TextureRelativePath);
        Assert.Equal("cooked\\imported\\texture.hetex", fields[PlayStation1MaterialSchemaIds.TextureRelativePathFieldId]);
    }

    /// <summary>Rejects authored inputs, absolute paths and path traversal before producing a misleading runtime binding.</summary>
    /// <param name="path">A binding that cannot be opened safely under the content root.</param>
    [Theory]
    [InlineData("textures/cube.png")]
    [InlineData("../cooked/imported/texture")]
    [InlineData("cooked/../texture")]
    [InlineData("cooked//texture")]
    [InlineData("/cooked/imported/texture")]
    [InlineData("C:/cooked/imported/texture")]
    public void CookRejectsNonCanonicalTextureBindings(string path) {
        Assert.Throws<InvalidOperationException>(() => new PlayStation1MaterialCooker().Cook(Request(Fields(path, "unlit"))));
    }

    /// <summary>Texture support does not silently relax existing opacity or lighting requirements.</summary>
    /// <param name="field">The existing material constraint being violated.</param>
    /// <param name="value">The unsupported requested behavior.</param>
    [Theory]
    [InlineData("transparency", "blend")]
    [InlineData("alpha-mode", "alpha-blend")]
    [InlineData("lighting-mode", "lit")]
    public void TexturedCookStillRejectsUnsupportedMaterialFields(string field, string value) {
        Dictionary<string, string> fields = Fields("cooked/imported/texture", "unlit");
        fields[field] = value;
        Assert.Throws<InvalidOperationException>(() => new PlayStation1MaterialCooker().Cook(Request(fields)));
    }

    /// <summary>Preserves authored tint alpha after the actual schema-selection and reference-projection boundary while the render mode remains opaque.</summary>
    /// <param name="color">RGBA authored color, including the exact GoalFlagPole and GoalFlagBanner values.</param>
    /// <param name="red">Expected red channel under the engine's RGBA convention.</param>
    /// <param name="green">Expected green channel.</param>
    /// <param name="blue">Expected blue channel.</param>
    /// <param name="alpha">Expected stored tint alpha, which does not activate blending.</param>
    [Theory]
    [InlineData("#FFE5E7EB", 255, 229, 231, 235)]
    [InlineData("#FFFF5A5A", 255, 255, 90, 90)]
    [InlineData("#3F7FBF00", 63, 127, 191, 0)]
    public void ProjectedOpaqueTintPreservesAllRgbaChannels(string color, int red, int green, int blue, int alpha) {
        Dictionary<string, string> fields = Fields(string.Empty, "vertex-lit");
        fields[PlayStation1MaterialSchemaIds.BaseColorFieldId] = color;
        fields[PlayStation1MaterialSchemaIds.AlphaModeFieldId] = "opaque";
        PlatformMaterialCookResult result = new PlayStation1MaterialCooker().Cook(ProjectedRequest(fields));
        using MemoryStream stream = new MemoryStream(result.CookedMaterialBytes, false);
        helengine.PlatformMaterialAsset material = Assert.IsType<helengine.PlatformMaterialAsset>(helengine.files.AssetSerializer.Deserialize(stream));
        Assert.Equal(red, material.BaseColorR);
        Assert.Equal(green, material.BaseColorG);
        Assert.Equal(blue, material.BaseColorB);
        Assert.Equal(alpha, material.BaseColorA);
        Assert.Equal(color, fields[PlayStation1MaterialSchemaIds.BaseColorFieldId]);
    }

    /// <summary>Common alpha mode and legacy transparency survive real schema projection and reject nonopaque behavior even when tint alpha is FF.</summary>
    /// <param name="field">Common or legacy render-mode field.</param>
    /// <param name="mode">Unsupported blend, additive or mask behavior.</param>
    [Theory]
    [InlineData("alpha-mode", "alpha-blend")]
    [InlineData("alpha-mode", "additive")]
    [InlineData("alpha-mode", "mask")]
    [InlineData("transparency", "blend")]
    [InlineData("transparency", "mask")]
    public void ProjectionCannotEraseUnsupportedRenderMode(string field, string mode) {
        Dictionary<string, string> fields = Fields(string.Empty, "unlit");
        fields[field] = mode;
        PlatformMaterialCookRequest request = ProjectedRequest(fields);
        Assert.Equal(mode, request.FieldValues[field]);
        Assert.Equal("#3F7FBFFF", request.FieldValues[PlayStation1MaterialSchemaIds.BaseColorFieldId]);
        Assert.Throws<InvalidOperationException>(() => new PlayStation1MaterialCooker().Cook(request));
    }

    /// <summary>Uses the same schema selection and typed-reference projection services as the shared material packaging boundary.</summary>
    /// <param name="fields">Authored settings inherited by the PS1 material.</param>
    /// <returns>A cooked-platform request retaining the selected render-mode fields.</returns>
    static PlatformMaterialCookRequest ProjectedRequest(Dictionary<string, string> fields) {
        helengine.editor.MaterialAssetProcessorSettings settings = new helengine.editor.MaterialAssetProcessorSettings {
            SchemaId = "standard-shader-v1",
            FieldValues = new Dictionary<string, string>(fields, StringComparer.OrdinalIgnoreCase)
        };
        new helengine.editor.MaterialAssetSchemaSettingsService().EnsureSelectedSchema(settings, PlayStation1PlatformDefinitionFactory.Create().MaterialSchemas);
        Dictionary<string, string> projected = new helengine.editor.MaterialAssetReferenceProjectionService().CreateResolvedFieldValues(settings);
        return new PlatformMaterialCookRequest("material-identity", "materials/games/tilt/GoalFlagPole.hasset", "ps1", "release", "ps1-opaque", settings.SchemaId, projected, helengine.NumericPrecisionPolicy.Default);
    }

    /// <summary>Creates the real builder request with fields projected by the shared material packaging pipeline.</summary>
    /// <param name="fields">Resolved PS1 schema values.</param>
    /// <returns>A request using the existing opaque schema and profile.</returns>
    static PlatformMaterialCookRequest Request(Dictionary<string, string> fields) {
        return new PlatformMaterialCookRequest("material-identity", "materials/cube.hasset", "ps1", "release", "ps1-opaque", PlayStation1MaterialSchemaIds.OpaqueSchemaId, fields, helengine.NumericPrecisionPolicy.Default);
    }

    /// <summary>Provides only the existing schema fields so texture support does not introduce a new material ABI.</summary>
    /// <param name="texture">Cooked content-relative texture path, or empty for color-only.</param>
    /// <param name="lighting">The selected existing lighting mode.</param>
    /// <returns>Independent resolved field values supplied to the request.</returns>
    static Dictionary<string, string> Fields(string texture, string lighting) {
        return new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase) {
            [PlayStation1MaterialSchemaIds.BaseColorFieldId] = "#3F7FBFFF",
            [PlayStation1MaterialSchemaIds.TextureRelativePathFieldId] = texture,
            [PlayStation1MaterialSchemaIds.LightingModeFieldId] = lighting,
            [PlayStation1MaterialSchemaIds.TransparencyFieldId] = "opaque"
        };
    }
}
