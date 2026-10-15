using System.Globalization;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;

namespace helengine.ps1.builder;

/// <summary>
/// Cooks opaque base-color materials with an optional packaged texture into the engine's platform
/// material payload. Lighting selects the unlit or GTE per-vertex directional path. Texture work
/// items remain owned by the shared packaging graph. Both common and legacy transparency fields
/// must select opaque rendering; tint alpha is preserved without enabling alpha blending.
/// </summary>
public sealed class PlayStation1MaterialCooker {
    /// <summary>Validates the requested material against the schema and serializes the cooked payload.</summary>
    /// <param name="request">Material identity, schema id and resolved field values supplied by the editor.</param>
    /// <returns>The serialized material with its cooked texture path and no programmable shader dependencies.</returns>
    public PlatformMaterialCookResult Cook(PlatformMaterialCookRequest request) {
        if (request == null) throw new ArgumentNullException(nameof(request));
        if (!string.Equals(request.TargetPlatformId, "ps1", StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"PS1 cannot cook materials for target platform '{request.TargetPlatformId}'.");
        }
        if (!string.Equals(request.SchemaId, PlayStation1MaterialSchemaIds.OpaqueSchemaId, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"PS1 does not support material schema '{request.SchemaId}'.");
        }

        string texture = Resolve(request.FieldValues, PlayStation1MaterialSchemaIds.TextureRelativePathFieldId);
        if (!string.IsNullOrWhiteSpace(texture)) {
            texture = NormalizeCookedTexturePath(texture);
        }
        string lighting = Resolve(request.FieldValues, PlayStation1MaterialSchemaIds.LightingModeFieldId, PlayStation1MaterialSchemaIds.DefaultLightingMode);
        bool lit = string.Equals(lighting, PlayStation1MaterialSchemaIds.VertexLitLightingMode, StringComparison.OrdinalIgnoreCase);
        if (!lit && !string.Equals(lighting, PlayStation1MaterialSchemaIds.UnlitLightingMode, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"PS1 {PlayStation1MaterialSchemaIds.OpaqueSchemaId} supports only '{PlayStation1MaterialSchemaIds.UnlitLightingMode}' and '{PlayStation1MaterialSchemaIds.VertexLitLightingMode}' lighting; '{request.MaterialRelativePath}' requested '{lighting}'.");
        }
        string transparency = Resolve(request.FieldValues, PlayStation1MaterialSchemaIds.TransparencyFieldId, "opaque");
        if (!string.Equals(transparency, "opaque", StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"PS1 {PlayStation1MaterialSchemaIds.OpaqueSchemaId} requires opaque materials; '{request.MaterialRelativePath}' requested '{transparency}'.");
        }
        string alphaMode = Resolve(request.FieldValues, PlayStation1MaterialSchemaIds.AlphaModeFieldId, "opaque");
        if (!string.Equals(alphaMode, "opaque", StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"PS1 {PlayStation1MaterialSchemaIds.OpaqueSchemaId} requires opaque materials; '{request.MaterialRelativePath}' requested alpha mode '{alphaMode}'.");
        }

        ParseColor(Resolve(request.FieldValues, PlayStation1MaterialSchemaIds.BaseColorFieldId, "#FFFFFFFF"), out byte r, out byte g, out byte b, out byte a);

        helengine.PlatformMaterialAsset cooked = new() {
            Id = request.MaterialAssetId,
            RendererFamilyId = PlayStation1MaterialSchemaIds.RendererFamilyId,
            TextureRelativePath = texture,
            DoubleSided = false,
            UseVertexColor = false,
            Lit = lit,
            BaseColorR = r,
            BaseColorG = g,
            BaseColorB = b,
            BaseColorA = a
        };
        return new PlatformMaterialCookResult(helengine.files.AssetSerializer.SerializeToBytes(cooked), []);
    }

    /// <summary>Validates the shared packager's content-relative texture artifact path without inventing a source importer or another texture payload.</summary>
    /// <param name="texturePath">Resolved texture dependency supplied by the material packaging graph.</param>
    /// <returns>The canonical path opened by the PS1 texture loader under the packaged content root.</returns>
    static string NormalizeCookedTexturePath(string texturePath) {
        string normalized = PlayStation1ContentStager.NormalizeRelativePath(texturePath);
        if (!normalized.StartsWith("cooked/", StringComparison.Ordinal)) {
            throw new InvalidOperationException($"PS1 texture binding must identify a cooked content-relative artifact, not an authored source: '{texturePath}'.");
        }
        return normalized;
    }

    /// <summary>Returns the trimmed field value or the fallback when the field is absent or blank.</summary>
    static string Resolve(IReadOnlyDictionary<string, string> values, string key, string fallback = "") {
        if (values == null) throw new ArgumentNullException(nameof(values));
        return values.TryGetValue(key, out string value) && !string.IsNullOrWhiteSpace(value) ? value.Trim() : fallback;
    }

    /// <summary>Parses #RRGGBB or #RRGGBBAA into channels, defaulting alpha to opaque.</summary>
    static void ParseColor(string value, out byte r, out byte g, out byte b, out byte a) {
        if (string.IsNullOrWhiteSpace(value)) throw new InvalidOperationException("PS1 base color is required.");
        string normalized = value.Trim();
        if (normalized.StartsWith('#')) normalized = normalized[1..];
        if (normalized.Length == 6) normalized += "FF";
        if (normalized.Length != 8) throw new InvalidOperationException($"PS1 color '{value}' must use #RRGGBB or #RRGGBBAA format.");
        try {
            r = byte.Parse(normalized.AsSpan(0, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            g = byte.Parse(normalized.AsSpan(2, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            b = byte.Parse(normalized.AsSpan(4, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            a = byte.Parse(normalized.AsSpan(6, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
        } catch (FormatException exception) {
            throw new InvalidOperationException($"PS1 color '{value}' contains invalid hexadecimal channels.", exception);
        }
    }
}
