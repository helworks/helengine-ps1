using System.Text.Json;
using helengine.editor;

namespace helengine.ps1.builder;

/// <summary>
/// Serializes and parses the PS1 texture cook settings payload carried by builder-owned platform cook work items.
/// </summary>
public static class PlayStation1TextureCookSettingsSerializer {
    /// <summary>
    /// Serializes one PS1 texture settings payload into the stable string form stored on cook work items.
    /// </summary>
    /// <param name="settings">Settings payload to serialize.</param>
    /// <returns>Serialized PS1 texture settings string.</returns>
    public static string Serialize(TextureAssetProcessorSettings settings) {
        if (settings == null) {
            throw new ArgumentNullException(nameof(settings));
        }

        return JsonSerializer.Serialize(new Dictionary<string, object> {
            ["maxResolution"] = settings.MaxResolution,
            ["colorFormat"] = settings.ColorFormatId,
            ["alphaPrecision"] = settings.AlphaPrecision.ToString()
        });
    }

    /// <summary>
    /// Parses one PS1 texture settings payload from the string form stored on cook work items. The
    /// <c>maxResolution</c>, <c>colorFormat</c>, and <c>alphaPrecision</c> properties are all required; a payload
    /// missing any of them is a malformed PS1 texture cook settings payload and is rejected rather than
    /// silently defaulted.
    /// </summary>
    /// <param name="serializedSettings">Serialized settings string.</param>
    /// <returns>Parsed PS1 texture settings payload.</returns>
    public static TextureAssetProcessorSettings Deserialize(string serializedSettings) {
        if (string.IsNullOrWhiteSpace(serializedSettings)) {
            throw new ArgumentException("Serialized PS1 texture settings are required.", nameof(serializedSettings));
        }

        using JsonDocument document = JsonDocument.Parse(serializedSettings);
        JsonElement root = document.RootElement;
        if (!root.TryGetProperty("maxResolution", out JsonElement maxResolutionElement)) {
            throw new InvalidOperationException("Malformed PS1 texture cook settings payload: 'maxResolution' is required.");
        }
        if (!root.TryGetProperty("colorFormat", out JsonElement colorFormatElement)) {
            throw new InvalidOperationException("Malformed PS1 texture cook settings payload: 'colorFormat' is required.");
        }
        if (!root.TryGetProperty("alphaPrecision", out JsonElement alphaPrecisionElement)) {
            throw new InvalidOperationException("Malformed PS1 texture cook settings payload: 'alphaPrecision' is required.");
        }

        int maxResolution = maxResolutionElement.GetInt32();
        string colorFormatName = colorFormatElement.GetString();
        string alphaPrecisionName = alphaPrecisionElement.GetString();

        if (maxResolution < 0) {
            throw new InvalidOperationException("PS1 texture max resolution cannot be negative.");
        }
        if (!Enum.TryParse(colorFormatName, true, out TextureAssetColorFormat colorFormat)) {
            throw new InvalidOperationException($"Unsupported PS1 texture color format '{colorFormatName}'.");
        }
        // The shared importer can carry the source image's raw Rgba32 format into a builder-owned work item.
        // PS1 owns the final cook, so convert that source marker to the platform's ordinary indexed default.
        if (colorFormat == TextureAssetColorFormat.Rgba32) {
            return new TextureAssetProcessorSettings {
                // Preserve the authored platform override (PS1 control icons carry their
                // measured 26/27-pixel size) while converting only the unsupported source format.
                MaxResolution = maxResolution,
                ColorFormat = TextureAssetColorFormat.Indexed8,
                AlphaPrecision = TextureAssetAlphaPrecision.Binary
            };
        }

        if (colorFormat != TextureAssetColorFormat.Indexed4 && colorFormat != TextureAssetColorFormat.Indexed8
            && colorFormat != TextureAssetColorFormat.Ps1Bgr555) {
            throw new InvalidOperationException($"PS1 textures must cook to Indexed4, Indexed8 or Ps1Bgr555; received '{colorFormatName}'.");
        }
        if (!Enum.TryParse(alphaPrecisionName, true, out TextureAssetAlphaPrecision alphaPrecision)
            || (alphaPrecision != TextureAssetAlphaPrecision.Binary && alphaPrecision != TextureAssetAlphaPrecision.Opaque)) {
            throw new InvalidOperationException($"Unsupported PS1 texture alpha precision '{alphaPrecisionName}'; use Binary or Opaque.");
        }

        return new TextureAssetProcessorSettings {
            MaxResolution = maxResolution,
            ColorFormat = colorFormat,
            AlphaPrecision = alphaPrecision
        };
    }

    /// <summary>
    /// Parses a generated font-atlas request while accepting the shared editor's raw RGBA default.
    /// Generated font atlases are source images; PS1 still owns their final indexed cook.
    /// </summary>
    public static TextureAssetProcessorSettings DeserializeFontAtlas(string serializedSettings) {
        if (!string.IsNullOrWhiteSpace(serializedSettings)) {
            using JsonDocument document = JsonDocument.Parse(serializedSettings);
            if (document.RootElement.TryGetProperty("colorFormat", out JsonElement colorFormatElement)
                && string.Equals(colorFormatElement.GetString(), "Rgba32", StringComparison.OrdinalIgnoreCase)) {
                return new TextureAssetProcessorSettings {
                    MaxResolution = 256,
                    ColorFormat = TextureAssetColorFormat.Indexed4,
                    AlphaPrecision = TextureAssetAlphaPrecision.Binary
                };
            }
        }

        return Deserialize(serializedSettings);
    }
}
