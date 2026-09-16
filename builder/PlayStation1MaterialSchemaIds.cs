namespace helengine.ps1.builder;

/// <summary>Identifiers of the single opaque base-color material schema the PS1 milestone renders.</summary>
public static class PlayStation1MaterialSchemaIds {
    /// <summary>Schema id advertised in the platform definition and required by the cooker.</summary>
    public const string OpaqueSchemaId = "ps1-opaque-v1";

    /// <summary>Renderer family id written into cooked materials and validated by the native render manager.</summary>
    public const string RendererFamilyId = "ps1-opaque";

    /// <summary>Field carrying the #RRGGBB or #RRGGBBAA base color.</summary>
    public const string BaseColorFieldId = "base-color";

    /// <summary>Field carrying a texture path; any value is rejected because textures are unsupported.</summary>
    public const string TextureRelativePathFieldId = "texture-relative-path";

    /// <summary>Field selecting the lighting model; "unlit" and "vertex-lit" are accepted.</summary>
    public const string LightingModeFieldId = "lighting-mode";

    /// <summary>Lighting mode that ignores scene lights and draws the flat base color.</summary>
    public const string UnlitLightingMode = "unlit";

    /// <summary>Lighting mode that shades each vertex on the GTE from the scene's directional lights.</summary>
    public const string VertexLitLightingMode = "vertex-lit";

    /// <summary>
    /// Lighting mode applied when a material carries no PS1 override. Every other platform renders
    /// the shared authored materials lit, so shading is the faithful default and "unlit" is the
    /// explicit opt-out rather than the other way round.
    /// </summary>
    public const string DefaultLightingMode = VertexLitLightingMode;

    /// <summary>Field selecting transparency; only "opaque" is accepted.</summary>
    public const string TransparencyFieldId = "transparency";
}
