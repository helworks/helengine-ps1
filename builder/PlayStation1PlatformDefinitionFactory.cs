using helengine.baseplatform.Definitions;
using helengine.baseplatform.Profiles;
using helengine.editor;

namespace helengine.ps1.builder;

/// <summary>
/// Creates the editor-facing PS1 platform contract. The codegen profile carries every generic runtime
/// capability flag the freestanding PS1 Core build requires, so the editor build graph regenerates
/// Core and gameplay modules exactly as scripts/generate-core-boot.ps1 did for the boot milestone.
/// </summary>
public static class PlayStation1PlatformDefinitionFactory {
    /// <summary>Catalog features the PS1 runtime does not implement; ids must exist in the engine feature catalog.</summary>
    const string ForcedDisabledFeatures = "shaders;debug_overlay";

    /// <summary>
    /// Default cook settings for ordinary textures. The GPU samples paletted textures from a 256x256
    /// texture page, so an 8-bit CLUT at 64 pixels a side leaves room for several textures per page.
    /// </summary>
    /// <returns>Serialized default PS1 texture cook settings.</returns>
    static string CreateDefaultSerializedTextureCookSettings() {
        return PlayStation1TextureCookSettingsSerializer.Serialize(new TextureAssetProcessorSettings {
            MaxResolution = 64,
            ColorFormat = TextureAssetColorFormat.Indexed8,
            AlphaPrecision = TextureAssetAlphaPrecision.Binary
        });
    }

    /// <summary>
    /// Default cook settings for font atlases. Glyph coverage matters more than colour depth, so atlases
    /// take a 4-bit CLUT, which is a quarter of the VRAM an 8-bit page would cost.
    /// </summary>
    /// <returns>Serialized default PS1 font-atlas cook settings.</returns>
    static string CreateDefaultSerializedFontAtlasTextureCookSettings() {
        return PlayStation1TextureCookSettingsSerializer.Serialize(new TextureAssetProcessorSettings {
            MaxResolution = 256,
            ColorFormat = TextureAssetColorFormat.Indexed4,
            AlphaPrecision = TextureAssetAlphaPrecision.Binary
        });
    }

    /// <summary>Indexed and direct-color texture formats sampled by the PS1 GPU.</summary>
    /// <returns>Texture capability metadata for the PS1 builder-owned cook contracts.</returns>
    static PlatformTextureFormatCapabilityDefinition CreateTextureFormatCapabilities() {
        return new PlatformTextureFormatCapabilityDefinition(
            [
                TextureAssetColorFormat.Indexed4.ToString(),
                TextureAssetColorFormat.Indexed8.ToString(),
                TextureAssetColorFormat.Ps1Bgr555.ToString()
            ],
            [
                TextureAssetAlphaPrecision.Binary,
                TextureAssetAlphaPrecision.Opaque
            ],
            [
                new PlatformTextureFormatCombinationDefinition(TextureAssetColorFormat.Indexed4.ToString(), TextureAssetAlphaPrecision.Binary),
                new PlatformTextureFormatCombinationDefinition(TextureAssetColorFormat.Indexed4.ToString(), TextureAssetAlphaPrecision.Opaque),
                new PlatformTextureFormatCombinationDefinition(TextureAssetColorFormat.Indexed8.ToString(), TextureAssetAlphaPrecision.Binary),
                new PlatformTextureFormatCombinationDefinition(TextureAssetColorFormat.Indexed8.ToString(), TextureAssetAlphaPrecision.Opaque),
                new PlatformTextureFormatCombinationDefinition(TextureAssetColorFormat.Ps1Bgr555.ToString(), TextureAssetAlphaPrecision.Binary),
                new PlatformTextureFormatCombinationDefinition(TextureAssetColorFormat.Ps1Bgr555.ToString(), TextureAssetAlphaPrecision.Opaque)
            ]);
    }

    /// <summary>Builds the PS1 platform definition.</summary>
    /// <returns>Platform definition consumed by the editor build graph.</returns>
    public static PlatformDefinition Create() {
        PlatformSettingDefinition[] buildSettings = [
            new("game-name", "Game Name", PlatformSettingKind.Text, "helengine", true, []),
            new("gpu-model-reduction", "Reduce GPU Models", PlatformSettingKind.Boolean, "false", true, []),
            new("gpu-model-maximum-vertices", "GPU Model Vertex Limit", PlatformSettingKind.Text, "256", true, []),
            new("gpu-model-maximum-indices", "GPU Model Index Limit", PlatformSettingKind.Text, "768", true, []),
            new("gpu-sphere-rings", "GPU Sphere Rings", PlatformSettingKind.Text, "6", true, []),
            new("gpu-sphere-sectors", "GPU Sphere Sectors", PlatformSettingKind.Text, "12", true, [])
        ];
        PlatformCodegenProfileDefinition codegen = new(
            "default",
            "Default",
            "PS1 C# to C++ generated runtime profile (freestanding, EASTL-backed provider)",
            PlatformCodegenLanguage.Cpp,
            PlatformSerializationEndianness.LittleEndian,
            [
                new(PlatformCodegenSettingIds.ForcedDisabledFeatures, "Forced Disabled Features", PlatformSettingKind.Text, ForcedDisabledFeatures, true, []),
                new("generated-math-convention", "Generated Math Convention", PlatformSettingKind.Text, "engine-row-vector", true, []),
                new("pointer-size-bytes", "Pointer Size (Bytes)", PlatformSettingKind.Text, "4", true, []),
                new("load-native-runtime-metadata", "Load Native Runtime Metadata", PlatformSettingKind.Boolean, "false", true, []),
                new("scene-code-modules", "Load Scene Code Modules", PlatformSettingKind.Boolean, "true", true, []),
                new("codegen-header-template-definitions", "Emit Header Template Definitions", PlatformSettingKind.Boolean, "true", true, []),
                new("codegen-module-lifetime-pins", "Protect Escaping Module Objects", PlatformSettingKind.Boolean, "true", true, []),
                new("codegen-use-std-string", "Use std::string", PlatformSettingKind.Boolean, "false", true, []),
                new("codegen-use-std-vector", "Use std::vector", PlatformSettingKind.Boolean, "false", true, []),
                new("codegen-use-std-unordered-map", "Use std::unordered_map", PlatformSettingKind.Boolean, "false", true, []),
                new("codegen-use-std-unordered-set", "Use std::unordered_set", PlatformSettingKind.Boolean, "false", true, []),
                new("codegen-use-std-shared-ptr", "Use std::shared_ptr", PlatformSettingKind.Boolean, "false", true, []),
                new("codegen-use-std-function", "Use std::function", PlatformSettingKind.Boolean, "false", true, []),
                new("codegen-use-std-chrono", "Use std::chrono", PlatformSettingKind.Boolean, "false", true, []),
                new("codegen-use-rtti", "Use Compiler RTTI", PlatformSettingKind.Boolean, "true", true, []),
                new("codegen-use-exceptions", "Use Exceptions", PlatformSettingKind.Boolean, "false", true, []),
                new("codegen-use-std-math", "Use Standard Math", PlatformSettingKind.Boolean, "false", true, []),
                new("codegen-runtime-math-header", "Runtime Math Header", PlatformSettingKind.Text, "Ps1Math.hpp", true, []),
                new("codegen-use-hosted-file-system", "Use Hosted File System", PlatformSettingKind.Boolean, "false", true, []),
                new("codegen-runtime-provider-header", "Runtime Provider Header", PlatformSettingKind.Text, "Ps1RuntimeProvider.hpp", true, []),
                new("codegen-compact-native-exception-messages", "Compact Native Exception Messages", PlatformSettingKind.Boolean, "true", true, [])
            ], new PlatformNumericCapabilities(helengine.NumericPrecisionPolicy.Default,
                [helengine.NumericPrecisionPolicy.Default, new helengine.NumericPrecisionPolicy(16, 32)]));
        PlatformMaterialSchemaDefinition materialSchema = new(
            PlayStation1MaterialSchemaIds.OpaqueSchemaId,
            "PS1 Opaque Unlit",
            [PlayStation1MaterialSchemaIds.RendererFamilyId],
            [
                new(PlayStation1MaterialSchemaIds.BaseColorFieldId, "Base Color", PlatformMaterialFieldKind.Color, "#FFFFFFFF", true, []),
                new(PlayStation1MaterialSchemaIds.TextureRelativePathFieldId, "Texture", PlatformMaterialFieldKind.Text, string.Empty, false, []),
                new(PlayStation1MaterialSchemaIds.LightingModeFieldId, "Lighting", PlatformMaterialFieldKind.Choice, PlayStation1MaterialSchemaIds.DefaultLightingMode, true,
                    [PlayStation1MaterialSchemaIds.UnlitLightingMode, PlayStation1MaterialSchemaIds.VertexLitLightingMode]),
                new(PlayStation1MaterialSchemaIds.TransparencyFieldId, "Transparency", PlatformMaterialFieldKind.Choice, "opaque", true, ["opaque"]),
                new(PlayStation1MaterialSchemaIds.AlphaModeFieldId, "Alpha Mode", PlatformMaterialFieldKind.Choice, "opaque", true, ["opaque"])
            ]);
        PlatformBuildProfileDefinition[] profiles = [
            new("release", "Release", "PS1 retail-memory release build", PlayStation1MaterialSchemaIds.RendererFamilyId, "default", buildSettings)
        ];
        return new PlatformDefinition(
            "ps1",
            "PlayStation",
            profiles,
            [new PlatformGraphicsProfileDefinition(PlayStation1MaterialSchemaIds.RendererFamilyId, "PS1 Opaque", "PSYQo GTE opaque renderer, flat or per-vertex directional shading", [])],
            [new PlatformAssetRequirementDefinition("scene", "Scene", true, ["helen"])],
            [materialSchema],
            [
                new PlatformComponentSupportRule("helengine.meshcomponent", PlatformComponentSupportKind.Transform, "One static triangle-list mesh is supported.", "Remove unsupported mesh components."),
                new PlatformComponentSupportRule("helengine.cameracomponent", PlatformComponentSupportKind.Transform, "One perspective camera is supported.", "Keep one authored camera."),
                new PlatformComponentSupportRule("helengine.lightcomponent", PlatformComponentSupportKind.Unsupported, "PS1 shades only directional lights.", "Replace point, spot and ambient lights with a directional light."),
                new PlatformComponentSupportRule("helengine.directionallightcomponent", PlatformComponentSupportKind.Transform, "Up to three directional lights shade vertices on the GTE.", "Keep at most three directional lights.")
            ],
            [codegen],
            [new PlatformStorageProfileDefinition("embedded-archive", "Embedded Archive", PlatformStorageProfileKind.LooseFiles, "ps1-embedded-archive", false)],
            [new PlatformMediaProfileDefinition("ps1-exe", "PS-X EXE", PlatformMediaLayoutKind.InstallTree, false, true)],
            new RuntimeGenerationContract(RuntimeMaterialResolutionMode.CookedPlatformOwned, true, PackagedPathPolicy.ContentRelativeOnly,
                ["HELENGINE_CODEGEN_FATAL_ONLY"], supportsSceneCodeModules: true),
            null,
            assetCookCapabilities: [
                new PlatformAssetCookCapabilityDefinition(
                    "texture",
                    "runtime-texture",
                    PlatformAssetCookOwnershipKind.BuilderOwned,
                    "ps1-texture",
                    CreateDefaultSerializedTextureCookSettings(),
                    CreateTextureFormatCapabilities()),
                new PlatformAssetCookCapabilityDefinition(
                    "font-atlas-texture",
                    "runtime-texture",
                    PlatformAssetCookOwnershipKind.BuilderOwned,
                    "ps1-font-atlas-texture",
                    CreateDefaultSerializedFontAtlasTextureCookSettings(),
                    CreateTextureFormatCapabilities())
            ]);
    }
}
