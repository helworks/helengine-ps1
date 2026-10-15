using System.Text;
using System.Text.Json;
using helengine.baseplatform.Manifest;

namespace helengine.ps1.builder;

/// <summary>
/// Copies only declared cooked files from the package-source tree into the archive staging root, and
/// writes the declared-file list that scripts/embed-authored-assets.py packs. Two kinds of file are
/// declared: the manifest's cooked artifacts, and the outputs of builder-owned cook work items, which
/// this builder produced itself and which the manifest therefore does not list. Nothing else reaches
/// the archive.
/// </summary>
public sealed class PlayStation1ContentStager {
    /// <summary>Stages every declared file and returns the staged list sorted by canonical path.</summary>
    /// <param name="artifacts">Cooked artifacts declared by the build manifest.</param>
    /// <param name="cookWorkItems">Builder-owned cook work items whose outputs this builder wrote.</param>
    /// <param name="sourceRootPath">Package-source root that owns the cooked files.</param>
    /// <param name="stagingRootPath">Staging root; previous content is removed only after input preflight succeeds.</param>
    /// <param name="cancellationToken">Cancellation token for the copy loop.</param>
    /// <returns>Staged assets keyed by the relative path the runtime will request.</returns>
    public IReadOnlyList<PlayStation1StagedAsset> Stage(
        IReadOnlyList<PlatformBuildArtifact> artifacts,
        IReadOnlyList<PlatformCookWorkItem> cookWorkItems,
        string sourceRootPath,
        string stagingRootPath,
        CancellationToken cancellationToken) {
        return StageSelected(artifacts, cookWorkItems, sourceRootPath, stagingRootPath, cancellationToken, null);
    }

    /// <summary>Stages declared artifacts and only work-item outputs reached by effective scene dependencies; null retains the legacy control-platform policy.</summary>
    /// <param name="artifacts">Declared cooked artifacts.</param>
    /// <param name="cookWorkItems">Builder-owned output producers.</param>
    /// <param name="sourceRootPath">Actual package source containing the output bytes.</param>
    /// <param name="stagingRootPath">Destination archive content tree.</param>
    /// <param name="cancellationToken">Cancellation for preflight and copies.</param>
    /// <param name="selectedRuntimePaths">Exact final scene dependency paths, or null for older manifests without dependency metadata.</param>
    /// <returns>The exact declared staged paths used by the archive writer.</returns>
    public IReadOnlyList<PlayStation1StagedAsset> StageSelected(
        IReadOnlyList<PlatformBuildArtifact> artifacts,
        IReadOnlyList<PlatformCookWorkItem> cookWorkItems,
        string sourceRootPath,
        string stagingRootPath,
        CancellationToken cancellationToken,
        IReadOnlyCollection<string> selectedRuntimePaths) {
        if (artifacts == null) throw new ArgumentNullException(nameof(artifacts));
        if (cookWorkItems == null) throw new ArgumentNullException(nameof(cookWorkItems));
        if (string.IsNullOrWhiteSpace(sourceRootPath)) throw new ArgumentException("Source root is required.", nameof(sourceRootPath));
        if (string.IsNullOrWhiteSpace(stagingRootPath)) throw new ArgumentException("Staging root is required.", nameof(stagingRootPath));
        cancellationToken.ThrowIfCancellationRequested();
        HashSet<string> selectedPaths = selectedRuntimePaths == null ? null : new(selectedRuntimePaths.Select(NormalizeRelativePath), StringComparer.Ordinal);

        string sourceRoot = Path.GetFullPath(sourceRootPath);
        string stagingRoot = Path.GetFullPath(stagingRootPath);
        if (IsSameOrNested(sourceRoot, stagingRoot) || IsSameOrNested(stagingRoot, sourceRoot)) {
            throw new InvalidOperationException("PS1 source and staging roots must not overlap.");
        }
        if (!Directory.Exists(sourceRoot)) throw new DirectoryNotFoundException($"PS1 package source root does not exist: {sourceRoot}");
        RejectReparseRootOrAncestor(sourceRoot);
        RejectReparseRootOrAncestor(stagingRoot);

        // Resolve the entire input list before replacing an earlier staging tree. A malformed
        // declaration late in the manifest must not leave a partial replacement behind.
        HashSet<string> paths = new(StringComparer.OrdinalIgnoreCase);
        List<(string RelativePath, string SourcePath, string DestinationPath)> planned = new(artifacts.Count);
        foreach (PlatformBuildArtifact artifact in artifacts) {
            cancellationToken.ThrowIfCancellationRequested();
            ArgumentNullException.ThrowIfNull(artifact);
            string relativePath = NormalizeRelativePath(artifact.RelativePath);
            if (!paths.Add(relativePath)) throw new InvalidOperationException($"PS1 cooked artifact path collision: '{relativePath}'.");
            PlanFile(relativePath);
        }

        foreach (PlatformCookWorkItem workItem in cookWorkItems) {
            cancellationToken.ThrowIfCancellationRequested();
            if (workItem == null) throw new ArgumentNullException(nameof(cookWorkItems));
            if (selectedPaths == null) {
                if (!ShouldStageCookWorkItem(workItem)) continue;
            } else if (!selectedPaths.Contains(NormalizeRelativePath(workItem.OutputRelativePath))) {
                continue;
            }
            string relativePath = NormalizeRelativePath(workItem.OutputRelativePath);
            // A cook work item that also appears as a manifest artifact is already staged; staging it
            // twice would be a path collision rather than a duplicate copy.
            if (!paths.Add(relativePath)) continue;
            PlanFile(relativePath);
        }

        cancellationToken.ThrowIfCancellationRequested();
        if (Directory.Exists(stagingRoot)) Directory.Delete(stagingRoot, true);
        Directory.CreateDirectory(stagingRoot);
        List<PlayStation1StagedAsset> staged = new(planned.Count);
        foreach (var file in planned.OrderBy(file => file.RelativePath, StringComparer.Ordinal)) {
            cancellationToken.ThrowIfCancellationRequested();
            RejectReparseRootOrAncestor(file.SourcePath);
            Directory.CreateDirectory(Path.GetDirectoryName(file.DestinationPath) ?? stagingRoot);
            File.Copy(file.SourcePath, file.DestinationPath, true);
            staged.Add(new PlayStation1StagedAsset(file.RelativePath, file.DestinationPath));
        }
        return staged;

        void PlanFile(string relativePath) {
            string sourcePath = ResolveUnderRoot(sourceRoot, relativePath);
            RejectReparseRootOrAncestor(sourcePath);
            if (!File.Exists(sourcePath)) {
                throw new FileNotFoundException($"PS1 cooked artifact '{relativePath}' was not found under package source root '{sourceRoot}'.", sourcePath);
            }
            planned.Add((relativePath, sourcePath, ResolveUnderRoot(stagingRoot, relativePath)));
        }
    }

    /// <summary>
    /// Keeps platform-specific generated control textures scoped to the target PS1 build. The
    /// authored blueprint contains every console's icon override, so the editor build graph can
    /// emit every variant's cook work item even though the packaged PS1 scene resolves only PS1.
    /// </summary>
    static bool ShouldStageCookWorkItem(PlatformCookWorkItem workItem) {
        if (workItem == null) throw new ArgumentNullException(nameof(workItem));
        if (!string.Equals(workItem.SourceAssetKind, "texture", StringComparison.OrdinalIgnoreCase)) return true;

        string sourcePath = (workItem.SourceAssetPath ?? string.Empty).Replace('\\', '/');
        const string marker = "textures/instructions/controls/generated/";
        int markerIndex = sourcePath.IndexOf(marker, StringComparison.OrdinalIgnoreCase);
        if (markerIndex < 0) return true;
        string platformAndRest = sourcePath[(markerIndex + marker.Length)..];
        int separatorIndex = platformAndRest.IndexOf('/');
        if (separatorIndex <= 0) return true;
        return string.Equals(platformAndRest[..separatorIndex], "ps1", StringComparison.OrdinalIgnoreCase);
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
        if (normalized.StartsWith('/') || Path.IsPathRooted(normalized) || normalized.Split('/').Any(segment =>
                segment == ".." || segment == "." || segment.Length == 0 ||
                segment.Contains(':') || segment.EndsWith('.') || segment.EndsWith(' '))) {
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

    /// <summary>Detects equality and parent-child overlap using complete path segments.</summary>
    static bool IsSameOrNested(string candidate, string root) {
        string normalizedCandidate = Path.TrimEndingDirectorySeparator(candidate);
        string normalizedRoot = Path.TrimEndingDirectorySeparator(root);
        return normalizedCandidate.Equals(normalizedRoot, StringComparison.OrdinalIgnoreCase) ||
            normalizedCandidate.StartsWith(Path.EndsInDirectorySeparator(normalizedRoot) ? normalizedRoot : normalizedRoot + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>Rejects filesystem aliases before copying content or clearing an existing staging tree.</summary>
    static void RejectReparseRootOrAncestor(string path) {
        for (string current = Path.GetFullPath(path); !string.IsNullOrEmpty(current); current = Path.GetDirectoryName(current)) {
            if ((File.Exists(current) || Directory.Exists(current)) && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0) {
                throw new InvalidOperationException($"PS1 package path contains a reparse point: '{current}'.");
            }
        }
    }
}
