using System.Globalization;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;

namespace helengine.ps1.builder;

/// <summary>
/// Cooks the opaque base-color material subset into the engine's platform material payload. The
/// lighting mode selects between the flat unlit path and the GTE per-vertex directional path.
/// Anything the PS1 milestone cannot represent (textures, transparency, translucent alpha) is
/// rejected explicitly instead of being approximated.
/// </summary>
public sealed class PlayStation1MaterialCooker {
    /// <summary>Validates the requested material against the schema and serializes the cooked payload.</summary>
    /// <param name="request">Material identity, schema id and resolved field values supplied by the editor.</param>
    /// <returns>The serialized platform material bytes with no shader dependencies.</returns>
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
            throw new InvalidOperationException($"PS1 {PlayStation1MaterialSchemaIds.OpaqueSchemaId} does not support texture bindings ('{request.MaterialRelativePath}').");
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

        ParseColor(Resolve(request.FieldValues, PlayStation1MaterialSchemaIds.BaseColorFieldId, "#FFFFFFFF"), out byte r, out byte g, out byte b, out byte a);
        if (a != 255) {
            throw new InvalidOperationException($"PS1 {PlayStation1MaterialSchemaIds.OpaqueSchemaId} requires an opaque base color alpha of FF; '{request.MaterialRelativePath}' has alpha {a:X2}.");
        }

        helengine.PlatformMaterialAsset cooked = new() {
            Id = request.MaterialAssetId,
            RendererFamilyId = PlayStation1MaterialSchemaIds.RendererFamilyId,
            TextureRelativePath = string.Empty,
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
