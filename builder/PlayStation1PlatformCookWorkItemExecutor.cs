using helengine.baseplatform.Manifest;
using helengine.editor;

namespace helengine.ps1.builder;

/// <summary>
/// Executes builder-owned PS1 cook work items, writing cooked runtime payloads into the package source root.
/// The image work is the engine's own <see cref="TextureAssetProcessor"/>, which resizes and quantises; this
/// type only resolves the settings, the source and the destination for one work item.
/// </summary>
public sealed class PlayStation1PlatformCookWorkItemExecutor {
    /// <summary>Decodes authored source images and generated engine texture assets into straight RGBA.</summary>
    readonly PlayStation1SourceTextureDecoder SourceTextureDecoder = new();

    /// <summary>Resizes and quantises decoded textures into the cooked PS1 runtime form.</summary>
    readonly TextureAssetProcessor TextureProcessor = new();

    /// <summary>Source asset kinds this executor cooks.</summary>
    static readonly string[] SupportedSourceKinds = ["texture", "font-atlas-texture"];

    /// <summary>
    /// Cooks one source texture work item into the package source root.
    /// </summary>
    /// <param name="workItem">Work item naming the source asset, the serialized settings and the output path.</param>
    /// <param name="packageSourceRootPath">Package source root the content stager reads.</param>
    public void Execute(PlatformCookWorkItem workItem, string packageSourceRootPath) {
        if (workItem == null) {
            throw new ArgumentNullException(nameof(workItem));
        }
        if (string.IsNullOrWhiteSpace(packageSourceRootPath)) {
            throw new ArgumentException("Package source root path must be provided.", nameof(packageSourceRootPath));
        }
        if (!SupportedSourceKinds.Contains(workItem.SourceAssetKind, StringComparer.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"PS1 does not support cook work item source kind '{workItem.SourceAssetKind}'.");
        }

        TextureAssetProcessorSettings settings = PlayStation1TextureCookSettingsSerializer.Deserialize(workItem.SerializedPlatformSettings);
        TextureAsset decoded = SourceTextureDecoder.Decode(workItem.SourceAssetPath);
        TextureAsset cooked = TextureProcessor.Apply(decoded, settings);
        cooked.Id = workItem.OutputLogicalArtifactId;

        string destinationPath = Path.Combine(
            Path.GetFullPath(packageSourceRootPath),
            NormalizeRelativePath(workItem.OutputRelativePath).Replace('/', Path.DirectorySeparatorChar));
        string destinationDirectory = Path.GetDirectoryName(destinationPath)
            ?? throw new InvalidOperationException($"PS1 cooked texture destination directory could not be resolved for '{destinationPath}'.");
        Directory.CreateDirectory(destinationDirectory);
        File.WriteAllBytes(destinationPath, helengine.files.AssetSerializer.SerializeToBytes(cooked));
    }

    /// <summary>Normalizes one declared output path to forward slashes without a leading separator.</summary>
    /// <param name="relativePath">Output relative path declared by the work item.</param>
    /// <returns>Normalized relative path.</returns>
    static string NormalizeRelativePath(string relativePath) {
        if (string.IsNullOrWhiteSpace(relativePath)) {
            throw new InvalidOperationException("PS1 cook work items must declare an output relative path.");
        }

        return relativePath.Replace('\\', '/').TrimStart('/');
    }
}
