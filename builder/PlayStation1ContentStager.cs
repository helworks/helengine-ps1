using System.Text;
using System.Text.Json;
using helengine.baseplatform.Manifest;

namespace helengine.ps1.builder;

/// <summary>
/// Copies only manifest-declared cooked files from the editor's package-source tree into the archive
/// staging root, and writes the declared-file list that scripts/embed-authored-assets.py packs.
/// Undeclared files never reach the archive.
/// </summary>
public sealed class PlayStation1ContentStager {
    /// <summary>Stages every declared artifact and returns the staged list sorted by canonical path.</summary>
    /// <param name="artifacts">Cooked artifacts declared by the build manifest.</param>
    /// <param name="sourceRootPath">Editor package-source root that owns the cooked files.</param>
    /// <param name="stagingRootPath">Fresh staging root; any previous content is removed.</param>
    /// <param name="cancellationToken">Cancellation token for the copy loop.</param>
    /// <returns>Staged assets keyed by the relative path the runtime will request.</returns>
    public IReadOnlyList<PlayStation1StagedAsset> Stage(
        IReadOnlyList<PlatformBuildArtifact> artifacts,
        string sourceRootPath,
        string stagingRootPath,
        CancellationToken cancellationToken) {
        if (artifacts == null) throw new ArgumentNullException(nameof(artifacts));
        if (string.IsNullOrWhiteSpace(sourceRootPath)) throw new ArgumentException("Source root is required.", nameof(sourceRootPath));
        if (string.IsNullOrWhiteSpace(stagingRootPath)) throw new ArgumentException("Staging root is required.", nameof(stagingRootPath));

        string sourceRoot = Path.GetFullPath(sourceRootPath);
        string stagingRoot = Path.GetFullPath(stagingRootPath);
        if (string.Equals(TrimSeparators(sourceRoot), TrimSeparators(stagingRoot), StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException("PS1 source and staging roots must be different directories.");
        }
        if (Directory.Exists(stagingRoot)) Directory.Delete(stagingRoot, true);
        Directory.CreateDirectory(stagingRoot);

        HashSet<string> paths = new(StringComparer.OrdinalIgnoreCase);
        List<PlayStation1StagedAsset> staged = new(artifacts.Count);
        foreach (PlatformBuildArtifact artifact in artifacts) {
            cancellationToken.ThrowIfCancellationRequested();
            string relativePath = NormalizeRelativePath(artifact.RelativePath);
            if (!paths.Add(relativePath)) throw new InvalidOperationException($"PS1 cooked artifact path collision: '{relativePath}'.");
            string sourcePath = ResolveUnderRoot(sourceRoot, relativePath);
            if (!File.Exists(sourcePath)) {
                throw new FileNotFoundException($"PS1 cooked artifact '{relativePath}' was not found under package source root '{sourceRoot}'.", sourcePath);
            }
            string destinationPath = ResolveUnderRoot(stagingRoot, relativePath);
            Directory.CreateDirectory(Path.GetDirectoryName(destinationPath) ?? stagingRoot);
            File.Copy(sourcePath, destinationPath, true);
            staged.Add(new PlayStation1StagedAsset(relativePath, destinationPath));
        }

        staged.Sort((left, right) => string.CompareOrdinal(left.RelativePath, right.RelativePath));
        return staged;
    }

    /// <summary>Writes the packer's declared-file JSON: a "files" array of relativePath/sourcePath objects.</summary>
    /// <param name="stagedAssets">Assets returned by <see cref="Stage"/>.</param>
    /// <param name="declarationsPath">Destination JSON path.</param>
    public static void WriteDeclarations(IReadOnlyList<PlayStation1StagedAsset> stagedAssets, string declarationsPath) {
        if (stagedAssets == null) throw new ArgumentNullException(nameof(stagedAssets));
        if (string.IsNullOrWhiteSpace(declarationsPath)) throw new ArgumentException("Declarations path is required.", nameof(declarationsPath));
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(declarationsPath)) ?? ".");
        var document = new {
            files = stagedAssets.OrderBy(asset => asset.RelativePath, StringComparer.Ordinal)
                .Select(asset => new { relativePath = asset.RelativePath, sourcePath = asset.SourcePath })
                .ToArray()
        };
        File.WriteAllText(declarationsPath, JsonSerializer.Serialize(document, new JsonSerializerOptions { WriteIndented = true }), new UTF8Encoding(false));
    }

    /// <summary>Canonicalizes a cooked artifact path to forward slashes and rejects rooted or escaping paths.</summary>
    public static string NormalizeRelativePath(string path) {
        if (string.IsNullOrWhiteSpace(path)) throw new InvalidOperationException("PS1 cooked artifact path is required.");
        string normalized = path.Replace('\\', '/');
        if (normalized.StartsWith('/') || Path.IsPathRooted(normalized) || normalized.Split('/').Any(segment => segment == ".." || segment == "." || segment.Length == 0)) {
            throw new InvalidOperationException($"PS1 cooked artifact path must remain relative and canonical: '{path}'.");
        }
        return normalized;
    }

    /// <summary>Resolves a relative path under a root and rejects anything that escapes it.</summary>
    static string ResolveUnderRoot(string root, string relativePath) {
        string fullRoot = TrimSeparators(Path.GetFullPath(root)) + Path.DirectorySeparatorChar;
        string fullPath = Path.GetFullPath(Path.Combine(fullRoot, relativePath.Replace('/', Path.DirectorySeparatorChar)));
        if (!fullPath.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"PS1 path escapes its root: '{relativePath}'.");
        }
        return fullPath;
    }

    /// <summary>Removes trailing separators so two spellings of one directory compare equal.</summary>
    static string TrimSeparators(string path) => path.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
}
