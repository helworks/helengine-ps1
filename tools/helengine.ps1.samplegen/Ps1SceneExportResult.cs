namespace helengine.ps1.samplegen;

/// <summary>
/// Paths and identity returned by a serializer-only authored subset export.
/// </summary>
public sealed class Ps1SceneExportResult {
    public Ps1SceneExportResult(
        string subsetScenePath,
        string reportPath,
        string sceneId,
        string sourceSceneHash,
        Ps1SubsetReport report) {
        SubsetScenePath = subsetScenePath;
        ReportPath = reportPath;
        SceneId = sceneId;
        SourceSceneHash = sourceSceneHash;
        Report = report;
    }

    public string SubsetScenePath { get; }

    public string ReportPath { get; }

    public string SceneId { get; }

    public string SourceSceneHash { get; }

    public Ps1SubsetReport Report { get; }
}
