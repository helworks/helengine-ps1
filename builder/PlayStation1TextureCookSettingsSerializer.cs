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
            ["colorFormat"] = settings.ColorFormat.ToString(),
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

        if (!Enum.TryParse(colorFormatName, true, out TextureAssetColorFormat colorFormat)) {
            throw new InvalidOperationException($"Unsupported PS1 texture color format '{colorFormatName}'.");
        }
        // The GPU samples 4-bit and 8-bit paletted textures out of a texture page and nothing else, so a
        // direct-colour request would be silently ignored at draw time rather than honoured.
        if (colorFormat != TextureAssetColorFormat.Indexed4 && colorFormat != TextureAssetColorFormat.Indexed8) {
            throw new InvalidOperationException($"PS1 textures must cook to Indexed4 or Indexed8; received '{colorFormatName}'.");
        }
        if (!Enum.TryParse(alphaPrecisionName, true, out TextureAssetAlphaPrecision alphaPrecision)) {
            throw new InvalidOperationException($"Unsupported PS1 texture alpha precision '{alphaPrecisionName}'.");
        }

        return new TextureAssetProcessorSettings {
            MaxResolution = maxResolution,
            ColorFormat = colorFormat,
            AlphaPrecision = alphaPrecision
        };
    }
}
