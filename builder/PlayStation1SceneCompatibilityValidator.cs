using helengine.baseplatform.Manifest;

namespace helengine.ps1.builder;

/// <summary>Rejects build manifests outside the one-scene, camera-plus-cube PS1 milestone.</summary>
public sealed class PlayStation1SceneCompatibilityValidator {
    /// <summary>Runtime feature ids the PS1 authored runtime implements.</summary>
    static readonly HashSet<string> SupportedFeatures = new(StringComparer.OrdinalIgnoreCase) { "scene", "mesh", "camera", "material", "light" };

    /// <summary>Throws when the manifest declares more than one scene, an unknown startup scene, or unsupported features.</summary>
    /// <param name="manifest">Cooked build manifest produced by the editor build graph.</param>
    public void Validate(PlatformBuildManifest manifest) {
        if (manifest == null) throw new ArgumentNullException(nameof(manifest));
        if (manifest.Scenes.Length != 1) {
            throw new InvalidOperationException($"PS1 authored builds require exactly one scene; received {manifest.Scenes.Length}.");
        }
        if (string.IsNullOrWhiteSpace(manifest.StartupSceneId) || !manifest.Scenes.Any(scene => scene.SceneId == manifest.StartupSceneId)) {
            throw new InvalidOperationException($"PS1 startup scene '{manifest.StartupSceneId}' is missing from the scene manifest.");
        }
        foreach (PlatformBuildRequiredRuntimeFeature feature in manifest.RuntimeFeatureManifest.RequiredFeatures) {
            if (!SupportedFeatures.Contains(feature.FeatureId)) {
                throw new InvalidOperationException($"PS1 scene '{manifest.StartupSceneId}' requires unsupported runtime feature '{feature.FeatureId}' from '{feature.SourceId}'.");
            }
        }
    }
}
