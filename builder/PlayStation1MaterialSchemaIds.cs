namespace helengine.ps1.builder;

/// <summary>Identifiers of the single opaque unlit material schema the PS1 milestone renders.</summary>
public static class PlayStation1MaterialSchemaIds {
    /// <summary>Schema id advertised in the platform definition and required by the cooker.</summary>
    public const string UnlitSchemaId = "ps1-unlit-v1";

    /// <summary>Renderer family id written into cooked materials and validated by the native render manager.</summary>
    public const string RendererFamilyId = "ps1-unlit";

    /// <summary>Field carrying the #RRGGBB or #RRGGBBAA base color.</summary>
    public const string BaseColorFieldId = "base-color";

    /// <summary>Field carrying a texture path; any value is rejected because textures are unsupported.</summary>
    public const string TextureRelativePathFieldId = "texture-relative-path";

    /// <summary>Field selecting the lighting model; only "unlit" is accepted.</summary>
    public const string LightingModeFieldId = "lighting-mode";

    /// <summary>Field selecting transparency; only "opaque" is accepted.</summary>
    public const string TransparencyFieldId = "transparency";
}
