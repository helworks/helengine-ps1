using helengine;

namespace helengine.ps1.builder;

/// <summary>Rejects mismatched packaged numeric envelopes before expensive native compilation and disc publication.</summary>
public static class PlayStation1CookedAssetPolicyValidator {
    /// <summary>Checks precision-bearing engine assets while leaving separately specified font, shader and opaque wire formats intact.</summary>
    public static void Validate(IReadOnlyList<PlayStation1StagedAsset> assets, NumericPrecisionPolicy expectedPolicy, CancellationToken cancellationToken) {
        ArgumentNullException.ThrowIfNull(assets);
        RealBinaryCodec.ValidatePolicy(expectedPolicy);
        foreach (PlayStation1StagedAsset asset in assets) {
            cancellationToken.ThrowIfCancellationRequested();
            using FileStream stream = File.OpenRead(asset.SourcePath);
            if (stream.ReadByte() != 'H' || stream.ReadByte() != 'E' || stream.ReadByte() != 'L' || stream.ReadByte() != 'E') { continue; }
            stream.Position = 0;
            EngineBinaryHeader header = EngineBinaryHeaderSerializer.Read(stream);
            if (header.FormatId != PackagedAssetBinarySerializer.FormatId || header.RecordKind != (ushort)EditorBinaryRecordKind.Asset) { continue; }
            if (header.Version != PackagedAssetBinarySerializer.CurrentVersion) {
                throw new InvalidOperationException($"PS1 cooked asset '{asset.RelativePath}' has unsupported runtime version {header.Version}.");
            }
            using EngineBinaryReader reader = EngineBinaryReader.Create(stream, header.Endianness);
            try { RealBinaryCodec.ReadAndValidatePolicy(reader, expectedPolicy); }
            catch (InvalidOperationException error) {
                throw new InvalidOperationException($"PS1 cooked asset '{asset.RelativePath}': {error.Message}", error);
            }
        }
    }
}
