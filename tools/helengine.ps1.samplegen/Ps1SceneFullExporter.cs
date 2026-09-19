using System.Security.Cryptography;
using System.Text.Json;
using helengine;
using helengine.editor;

namespace helengine.ps1.samplegen;

/// <summary>
/// Exports a whole authored scene for a PS1 build.
///
/// The subset exporter selects a fixed camera-and-cube fixture. This one keeps everything, because a
/// PS1 build of the real scene needs the real scene. Which entities PS1 receives is decided by the
/// scene's own override scopes (Common, platform groups, platforms) and resolved by the engine's
/// packager against the staged project's <c>settings/platform-groups.json</c>; nothing is patched here.
/// </summary>
public sealed class Ps1SceneFullExporter {
    /// <summary>Platform id the exported scene is built for.</summary>
    public const string TargetPlatformId = "ps1";

    static readonly JsonSerializerOptions ReportJsonOptions = new() {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    };

    /// <summary>
    /// Writes <c>scene.helen</c> and <c>report.json</c> for one source scene.
    /// </summary>
    /// <param name="sourceScenePath">Authored scene to export; never modified.</param>
    /// <param name="outputRootPath">Directory receiving the scene and its report.</param>
    /// <returns>Path of the written scene.</returns>
    public string Export(string sourceScenePath, string outputRootPath) {
        ArgumentNullException.ThrowIfNull(sourceScenePath);
        ArgumentNullException.ThrowIfNull(outputRootPath);

        string fullSourcePath = Path.GetFullPath(sourceScenePath);
        if (!File.Exists(fullSourcePath)) {
            throw new FileNotFoundException("Source scene was not found.", fullSourcePath);
        }

        byte[] sourceBytes = File.ReadAllBytes(fullSourcePath);
        SceneAsset source = Ps1SceneSerialization.Deserialize(sourceBytes);
        SceneEntityAsset[] roots = source.RootEntities ?? [];

        Directory.CreateDirectory(outputRootPath);
        string scenePath = Path.Combine(outputRootPath, "scene.helen");
        Ps1SceneSerialization.Write(scenePath, source);

        object report = new {
            sourceScenePath = fullSourcePath,
            sourceSceneHash = Convert.ToHexString(SHA256.HashData(sourceBytes)).ToLowerInvariant(),
            sceneId = source.Id,
            authoringAssetId = source.AuthoringAssetId,
            rootEntityNames = roots.Select(entity => entity.Name).ToList(),
            existenceOverrideScopes = Enumerate(roots)
                .SelectMany(entity => entity.PlatformExistenceOverrides ?? [])
                .Select(item => SceneOverrideScopePath.Format(item.Scope))
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .OrderBy(path => path, StringComparer.OrdinalIgnoreCase)
                .ToList(),
            retainedComponents = Enumerate(roots)
                .SelectMany(entity => (entity.Components ?? []).Select(component => component.ComponentTypeId))
                .Distinct(StringComparer.Ordinal)
                .OrderBy(id => id, StringComparer.Ordinal)
                .Select(id => new { componentTypeId = id })
                .ToList()
        };
        File.WriteAllText(Path.Combine(outputRootPath, "report.json"), JsonSerializer.Serialize(report, ReportJsonOptions));
        return scenePath;
    }

    /// <summary>Walks one entity tree depth first.</summary>
    /// <param name="roots">Root entities to walk.</param>
    /// <returns>Every entity in the trees.</returns>
    static IEnumerable<SceneEntityAsset> Enumerate(SceneEntityAsset[] roots) {
        foreach (SceneEntityAsset root in roots ?? []) {
            if (root == null) continue;
            yield return root;
            foreach (SceneEntityAsset child in Enumerate(root.Children ?? [])) {
                yield return child;
            }
        }
    }
}
