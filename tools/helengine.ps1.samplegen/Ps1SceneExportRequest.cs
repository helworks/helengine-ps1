namespace helengine.ps1.samplegen;

/// <summary>
/// Explicit inputs for the serializer-only authored PS1 subset export.
/// </summary>
public sealed class Ps1SceneExportRequest {
    public Ps1SceneExportRequest(
        string projectRootPath,
        string sourceScenePath,
        string outputRootPath,
        string engineBinaryRootPath) {
        ProjectRootPath = RequirePath(projectRootPath, nameof(projectRootPath));
        SourceScenePath = RequirePath(sourceScenePath, nameof(sourceScenePath));
        OutputRootPath = RequirePath(outputRootPath, nameof(outputRootPath));
        EngineBinaryRootPath = RequirePath(engineBinaryRootPath, nameof(engineBinaryRootPath));
        ValidateOutputPathSafety();
    }

    public string ProjectRootPath { get; }

    public string SourceScenePath { get; }

    public string OutputRootPath { get; }

    /// <summary>
    /// Directory containing the exact engine assemblies used by the exporter host.
    /// </summary>
    public string EngineBinaryRootPath { get; }

    void ValidateOutputPathSafety() {
        if (IsSameOrDescendant(OutputRootPath, ProjectRootPath)) {
            throw new ArgumentException(
                $"Output root '{OutputRootPath}' must be outside source project root '{ProjectRootPath}'.",
                nameof(OutputRootPath));
        }

        string subsetPath = Path.GetFullPath(Path.Combine(OutputRootPath, "subset.helen"));
        string reportPath = Path.GetFullPath(Path.Combine(OutputRootPath, "report.json"));
        if (string.Equals(SourceScenePath, subsetPath, StringComparison.OrdinalIgnoreCase)
            || string.Equals(SourceScenePath, reportPath, StringComparison.OrdinalIgnoreCase)) {
            throw new ArgumentException(
                $"Source scene '{SourceScenePath}' aliases an exporter output file.",
                nameof(SourceScenePath));
        }
    }

    static bool IsSameOrDescendant(string path, string root) {
        string normalizedPath = TrimDirectorySeparator(Path.GetFullPath(path));
        string normalizedRoot = TrimDirectorySeparator(Path.GetFullPath(root));
        return string.Equals(normalizedPath, normalizedRoot, StringComparison.OrdinalIgnoreCase)
            || normalizedPath.StartsWith(normalizedRoot + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
    }

    static string TrimDirectorySeparator(string value) {
        return value.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
    }

    static string RequirePath(string value, string parameterName) {
        if (string.IsNullOrWhiteSpace(value)) {
            throw new ArgumentException("A path is required.", parameterName);
        }

        return Path.GetFullPath(value);
    }
}
