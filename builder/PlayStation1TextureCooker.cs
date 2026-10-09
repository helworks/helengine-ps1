using helengine.editor;

namespace helengine.ps1.builder;

/// <summary>Resizes source images and emits the PS1's indexed or native direct-color payloads.</summary>
public sealed class PlayStation1TextureCooker {
    /// <summary>Shared resize and indexed quantization pipeline.</summary>
    readonly TextureAssetProcessor Processor = new();

    /// <summary>Cooks a source image and enforces the GPU's 256 by 256 texel page bounds.</summary>
    /// <param name="source">Decoded source image.</param>
    /// <param name="settings">Validated PS1 format and coverage settings.</param>
    /// <returns>A serialized-engine texture suitable for PS1 VRAM upload.</returns>
    public TextureAsset Apply(TextureAsset source, TextureAssetProcessorSettings settings) {
        if (source == null) {
            throw new ArgumentNullException(nameof(source));
        } else if (settings == null) {
            throw new ArgumentNullException(nameof(settings));
        }
        bool directColor = settings.ColorFormatId == TextureAssetColorFormat.Ps1Bgr555.ToString();
        if ((!directColor && settings.ColorFormatId != TextureAssetColorFormat.Indexed4.ToString()
            && settings.ColorFormatId != TextureAssetColorFormat.Indexed8.ToString())
            || (settings.AlphaPrecision != TextureAssetAlphaPrecision.Binary && settings.AlphaPrecision != TextureAssetAlphaPrecision.Opaque)) {
            throw new InvalidOperationException("PS1 cook requires Indexed4, Indexed8 or Ps1Bgr555 with Binary or Opaque coverage.");
        }
        TextureAsset cooked = Processor.Apply(source, directColor ? new TextureAssetProcessorSettings {
            MaxResolution = settings.MaxResolution,
            ColorFormat = TextureAssetColorFormat.Rgba32,
            AlphaPrecision = settings.AlphaPrecision
        } : settings);
        if (cooked.Width > 256 || cooked.Height > 256) {
            throw new InvalidOperationException("PS1 textures must fit within a 256 by 256 texel page.");
        }
        return directColor ? Ps1TexturePixelCodec.Encode(cooked, settings.AlphaPrecision) : cooked;
    }
}
