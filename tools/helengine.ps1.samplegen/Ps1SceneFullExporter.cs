using System.Security.Cryptography;
using System.Text.Json;
using helengine;
using helengine.editor;

namespace helengine.ps1.samplegen;

/// <summary>
/// Exports a whole authored scene for a PS1 build.
///
/// The subset exporter selects a fixed camera-and-cube fixture. This one keeps everything, because a
/// PS1 build of the real scene needs the real scene.
///
/// It can also copy another platform's entity-existence overrides onto PS1. An authored scene decides
/// per platform which entities exist, and an entity that declares no override for a platform exists
/// on it. The demodisc cube_test scene declares overrides for every console except PS1, so PS1 alone
/// receives the other platforms' camera rigs, their duplicate suns and the UI. Mirroring a comparable
/// console reuses the authored intent instead of inventing a selection here. It is a build-time
/// stand-in: the durable fix is authoring PS1 overrides in the editor like every other platform has.
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
    /// <param name="mirrorExistenceFromPlatformId">Platform whose existence overrides are copied onto PS1, or empty for none.</param>
    /// <returns>Path of the written scene.</returns>
    public string Export(string sourceScenePath, string outputRootPath, string mirrorExistenceFromPlatformId) {
        ArgumentNullException.ThrowIfNull(sourceScenePath);
        ArgumentNullException.ThrowIfNull(outputRootPath);

        string fullSourcePath = Path.GetFullPath(sourceScenePath);
        if (!File.Exists(fullSourcePath)) {
            throw new FileNotFoundException("Source scene was not found.", fullSourcePath);
        }

        byte[] sourceBytes = File.ReadAllBytes(fullSourcePath);
        SceneAsset source = Ps1SceneSerialization.Deserialize(sourceBytes);
        SceneEntityAsset[] roots = source.RootEntities ?? [];

        List<object> mirrored = [];
        if (!string.IsNullOrWhiteSpace(mirrorExistenceFromPlatformId)) {
            int applied = 0;
            foreach (SceneEntityAsset entity in Enumerate(roots)) {
                SceneEntityPlatformExistenceOverrideAsset[] overrides = entity.PlatformExistenceOverrides ?? [];
                SceneEntityPlatformExistenceOverrideAsset reference = overrides.FirstOrDefault(item =>
                    string.Equals(item.PlatformId, mirrorExistenceFromPlatformId, StringComparison.OrdinalIgnoreCase));
                if (reference == null) continue;
                if (overrides.Any(item => string.Equals(item.PlatformId, TargetPlatformId, StringComparison.OrdinalIgnoreCase))) {
                    // The scene already states what PS1 wants; authored data wins over the mirror.
                    continue;
                }
                entity.PlatformExistenceOverrides = [.. overrides, new SceneEntityPlatformExistenceOverrideAsset {
                    PlatformId = TargetPlatformId,
                    EnvironmentId = reference.EnvironmentId,
                    Exists = reference.Exists
                }];
                mirrored.Add(new { entityName = entity.Name, exists = reference.Exists });
                applied++;
            }
            if (applied == 0) {
                throw new InvalidOperationException(
                    $"Scene '{source.Id}' declares no entity-existence overrides for platform '{mirrorExistenceFromPlatformId}', so there was nothing to mirror onto {TargetPlatformId}.");
            }
        }

        Directory.CreateDirectory(outputRootPath);
        string scenePath = Path.Combine(outputRootPath, "scene.helen");
        Ps1SceneSerialization.Write(scenePath, source);

        object report = new {
            sourceScenePath = fullSourcePath,
            sourceSceneHash = Convert.ToHexString(SHA256.HashData(sourceBytes)).ToLowerInvariant(),
            sceneId = source.Id,
            authoringAssetId = source.AuthoringAssetId,
            mirrorExistenceFromPlatformId = mirrorExistenceFromPlatformId ?? string.Empty,
            mirroredExistenceOverrides = mirrored,
            rootEntityNames = roots.Select(entity => entity.Name).ToList(),
            existingPlatformIds = Enumerate(roots)
                .SelectMany(entity => entity.PlatformExistenceOverrides ?? [])
                .Select(item => item.PlatformId)
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .OrderBy(id => id, StringComparer.OrdinalIgnoreCase)
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
