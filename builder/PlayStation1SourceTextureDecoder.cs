namespace helengine.ps1.builder;

/// <summary>
/// Decodes one cook work item source into an uncooked straight-RGBA texture asset for the PS1 cook. Work item
/// sources are either authored image files or serialized engine texture assets that the editor generated for the
/// platform, such as the cached platform font atlases and the externalized editor default font atlas.
/// </summary>
public sealed class PlayStation1SourceTextureDecoder {
    /// <summary>
    /// Magic bytes that begin every serialized engine binary asset. Generated texture sources reach the cook under
    /// several extensions, so the payload itself decides which decoder runs.
    /// </summary>
    static readonly byte[] EngineAssetMagicBytes = [(byte)'H', (byte)'E', (byte)'L', (byte)'E'];

    /// <summary>
    /// Decodes one source texture into a raw RGBA texture asset.
    /// </summary>
    /// <param name="sourceAssetPath">Absolute source image or engine texture asset path.</param>
    /// <returns>Raw texture asset containing straight RGBA pixel bytes.</returns>
    public TextureAsset Decode(string sourceAssetPath) {
        if (string.IsNullOrWhiteSpace(sourceAssetPath)) {
            throw new ArgumentException("Source asset path is required.", nameof(sourceAssetPath));
        }
        if (!File.Exists(sourceAssetPath)) {
            throw new FileNotFoundException("Source texture file was not found.", sourceAssetPath);
        }

        if (HasEngineAssetMagic(sourceAssetPath)) {
            return DecodeEngineTextureAsset(sourceAssetPath);
        }

        if (string.Equals(Path.GetExtension(sourceAssetPath), ".tim", StringComparison.OrdinalIgnoreCase)) {
            using FileStream stream = File.OpenRead(sourceAssetPath);
            return new helengine.editor.PlayStationTimTextureImporter().ImportTexture(stream);
        }

        return DecodeImageFile(sourceAssetPath);
    }

    /// <summary>
    /// Determines whether one source file begins with the engine binary asset magic bytes.
    /// </summary>
    /// <param name="sourceAssetPath">Absolute source path to inspect.</param>
    /// <returns><c>true</c> when the file is a serialized engine asset.</returns>
    static bool HasEngineAssetMagic(string sourceAssetPath) {
        byte[] magicBuffer = new byte[EngineAssetMagicBytes.Length];
        using FileStream stream = new(sourceAssetPath, FileMode.Open, FileAccess.Read, FileShare.Read);
        if (stream.ReadAtLeast(magicBuffer, magicBuffer.Length, false) != magicBuffer.Length) {
            return false;
        }

        return magicBuffer.AsSpan().SequenceEqual(EngineAssetMagicBytes);
    }

    /// <summary>
    /// Decodes one authored image file through the existing portable editor image importer.
    /// </summary>
    /// <param name="sourceAssetPath">Absolute source image path.</param>
    /// <returns>Raw texture asset containing straight RGBA pixel bytes.</returns>
    TextureAsset DecodeImageFile(string sourceAssetPath) {
        using FileStream stream = File.OpenRead(sourceAssetPath);
        return new helengine.editor.MagickTextureImporter().ImportTexture(stream);
    }

    /// <summary>
    /// Reads one serialized engine texture and expands its validated linear pixel storage into straight RGBA.
    /// </summary>
    /// <param name="sourceAssetPath">Absolute engine texture asset path.</param>
    /// <returns>Raw texture asset containing straight RGBA pixel bytes.</returns>
    TextureAsset DecodeEngineTextureAsset(string sourceAssetPath) {
        Asset deserializedAsset = helengine.files.AssetSerializer.DeserializeFromBytes(File.ReadAllBytes(sourceAssetPath));
        if (deserializedAsset is not TextureAsset sourceTexture) {
            throw new InvalidOperationException($"PS1 texture cook source '{sourceAssetPath}' did not contain a texture asset.");
        }
        if (sourceTexture.Width < 1 || sourceTexture.Height < 1) {
            throw new InvalidOperationException("Source textures must have positive dimensions.");
        }

        byte[] decodedColors = TextureAssetPixelCodec.DecodeToRgba32(sourceTexture);

        return new TextureAsset {
            Width = sourceTexture.Width,
            Height = sourceTexture.Height,
            Colors = decodedColors,
            PaletteColors = Array.Empty<byte>(),
            ColorFormat = TextureAssetColorFormat.Rgba32,
            AlphaPrecision = TextureAssetAlphaPrecision.A8
        };
    }
}
