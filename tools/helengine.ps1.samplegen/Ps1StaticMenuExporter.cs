using System.Security.Cryptography;
using System.Text.Json;
using helengine;

namespace helengine.ps1.samplegen;

/// <summary>Creates a static visual-only export of the committed demodisc main-menu scene.</summary>
public sealed class Ps1StaticMenuExporter {
    public const string SceneId = "Scenes/DemoDiscMainMenu.helen";

    static readonly HashSet<string> VisualComponentTypes = new(StringComparer.Ordinal) {
        "helengine.CameraComponent",
        "helengine.ViewportComponent",
        "helengine.LayoutComponent",
        "helengine.ClipRectComponent",
        "helengine.SpriteComponent",
        "helengine.TextComponent",
        "helengine.RoundedRectComponent"
    };

    static readonly HashSet<string> RemovedComponentTypes = new(StringComparer.Ordinal) {
        "helengine.AnimationPlayerComponent",
        "helengine.ScrollComponent",
        "DemoDisc.menu.MenuComponent",
        "DemoDisc.menu.MenuItemComponent",
        "DemoDisc.menu.MenuPanelComponent",
        "DemoDisc.menu.MenuBackgroundMotionComponent",
        "DemoDisc.menu.FooterIdentityMarqueeComponent"
    };

    static readonly JsonSerializerOptions ReportJsonOptions = new() {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    };

    /// <summary>Writes a filtered serializer-native scene and a manifest beside it.</summary>
    public string Export(string sourceScenePath, string outputRootPath, string sourceRevision, string sourceBlobId) {
        ArgumentNullException.ThrowIfNull(sourceScenePath);
        ArgumentNullException.ThrowIfNull(outputRootPath);
        ArgumentException.ThrowIfNullOrWhiteSpace(sourceRevision);
        ArgumentException.ThrowIfNullOrWhiteSpace(sourceBlobId);

        string sourcePath = Path.GetFullPath(sourceScenePath);
        if (!File.Exists(sourcePath)) throw new FileNotFoundException("Source scene was not found.", sourcePath);
        byte[] sourceBytes = File.ReadAllBytes(sourcePath);
        SceneAsset scene;
        try {
            scene = Ps1SceneSerialization.Deserialize(sourceBytes);
        } catch (Exception exception) {
            throw new InvalidOperationException($"Could not deserialize source scene '{sourcePath}'.", exception);
        }

        if (!string.Equals(scene.Id, SceneId, StringComparison.Ordinal)) {
            throw new InvalidOperationException($"Static-menu export requires scene ID '{SceneId}', but source scene ID is '{scene.Id}'.");
        }

        SceneEntityAsset[] roots = scene.RootEntities ?? [];
        List<RemovedComponentRecord> removed = [];
        List<ComponentRecord> allSourceComponents = [];
        foreach (SceneEntityAsset entity in Enumerate(roots)) {
            entity.Components = FilterBaseComponents(entity, allSourceComponents, removed);
            foreach (SceneEntityPlatformComponentOverrideAsset componentOverride in entity.PlatformComponentOverrides ?? []) {
                string scope = SceneOverrideScopePath.Format(componentOverride.Scope);
                SceneEntityPlatformAddedComponentAsset[] retainedAdded = [];
                foreach (SceneEntityPlatformAddedComponentAsset added in componentOverride.AddedComponents ?? []) {
                    if (added?.Component == null) {
                        throw new InvalidOperationException($"Static-menu export found an empty added component on entity '{entity.Name}' in scope '{scope}'.");
                    }

                    SceneComponentAssetRecord component = added.Component;
                    allSourceComponents.Add(new ComponentRecord(entity.Id, entity.Name, component.ComponentTypeId, scope));
                    if (IsRemoved(component.ComponentTypeId)) {
                        removed.Add(new RemovedComponentRecord(entity.Id, entity.Name, component.ComponentTypeId, scope));
                    } else {
                        EnsureVisualComponent(component.ComponentTypeId, entity.Name, scope);
                        retainedAdded = [.. retainedAdded, added];
                    }
                }
                componentOverride.AddedComponents = retainedAdded;
            }
        }

        VerifyNoRemovedComponents(roots);
        HashSet<string> retainedTypes = Enumerate(roots)
            .SelectMany(entity => (entity.Components ?? []).Select(component => component.ComponentTypeId)
                .Concat((entity.PlatformComponentOverrides ?? []).SelectMany(componentOverride =>
                    (componentOverride.AddedComponents ?? []).Select(added => added.Component.ComponentTypeId))))
            .ToHashSet(StringComparer.Ordinal);

        string outputRoot = Path.GetFullPath(outputRootPath);
        Directory.CreateDirectory(outputRoot);
        string scenePath = Path.Combine(outputRoot, "scene.helen");
        Ps1SceneSerialization.Write(scenePath, scene);

        ComponentCount[] byType = allSourceComponents
            .GroupBy(component => component.ComponentTypeId ?? "<null>", StringComparer.Ordinal)
            .OrderBy(group => group.Key, StringComparer.Ordinal)
            .Select(group => new ComponentCount(group.Key, group.Count(), group.Count(record => removed.Any(removal => removal.EntityId == record.EntityId && removal.ScopePath == record.ScopePath && removal.ComponentTypeId == record.ComponentTypeId))))
            .ToArray();
        string[] scopePaths = Enumerate(roots)
            .SelectMany(EnumerateScopePaths)
            .Distinct(StringComparer.Ordinal)
            .OrderBy(path => path, StringComparer.Ordinal)
            .ToArray();

        object report = new {
            sceneId = scene.Id,
            authoringAssetId = scene.AuthoringAssetId,
            sourceSceneHash = Hash(sourceBytes),
            outputSceneHash = Hash(File.ReadAllBytes(scenePath)),
            sourceRevision,
            sourceBlobId,
            retainedComponents = retainedTypes.OrderBy(type => type, StringComparer.Ordinal).Select(componentTypeId => new { componentTypeId }).ToArray(),
            componentCounts = new {
                source = allSourceComponents.Count,
                retained = allSourceComponents.Count - removed.Count,
                removed = removed.Count,
                byType
            },
            removedComponents = removed.Select(item => new {
                entityId = item.EntityId,
                entityName = item.EntityName,
                componentTypeId = item.ComponentTypeId,
                scopePath = item.ScopePath
            }).ToArray(),
            scopePaths
        };
        File.WriteAllText(Path.Combine(outputRoot, "report.json"), JsonSerializer.Serialize(report, ReportJsonOptions));
        return scenePath;
    }

    static SceneComponentAssetRecord[] FilterBaseComponents(SceneEntityAsset entity, List<ComponentRecord> source, List<RemovedComponentRecord> removed) {
        List<SceneComponentAssetRecord> retained = [];
        foreach (SceneComponentAssetRecord component in entity.Components ?? []) {
            if (component == null) throw new InvalidOperationException($"Static-menu export found an empty component on entity '{entity.Name}' in scope 'Common'.");
            source.Add(new ComponentRecord(entity.Id, entity.Name, component.ComponentTypeId, "Common"));
            if (IsRemoved(component.ComponentTypeId)) {
                removed.Add(new RemovedComponentRecord(entity.Id, entity.Name, component.ComponentTypeId, "Common"));
                continue;
            }

            EnsureVisualComponent(component.ComponentTypeId, entity.Name, "Common");
            retained.Add(component);
        }

        for (int i = 0; i < retained.Count; i++) retained[i].ComponentIndex = i;
        return retained.ToArray();
    }

    static bool IsRemoved(string componentTypeId) => RemovedComponentTypes.Contains(NormalizeTypeIdentity(componentTypeId));

    static void EnsureVisualComponent(string componentTypeId, string entityName, string scope) {
        if (VisualComponentTypes.Contains(NormalizeTypeIdentity(componentTypeId))) return;
        throw new InvalidOperationException($"Static-menu export found unknown component type '{componentTypeId}' on entity '{entityName}' in scope '{scope}'.");
    }

    static string NormalizeTypeIdentity(string componentTypeId) {
        if (string.IsNullOrWhiteSpace(componentTypeId)) return string.Empty;
        int assemblySeparator = componentTypeId.IndexOf(',');
        string typeName = assemblySeparator < 0 ? componentTypeId : componentTypeId[..assemblySeparator];
        return typeName.Trim();
    }

    static void VerifyNoRemovedComponents(SceneEntityAsset[] roots) {
        foreach (SceneEntityAsset entity in Enumerate(roots)) {
            foreach (SceneComponentAssetRecord component in entity.Components ?? []) {
                if (IsRemoved(component.ComponentTypeId)) throw new InvalidOperationException($"Forbidden component '{component.ComponentTypeId}' survived on entity '{entity.Name}' in scope 'Common'.");
            }
            foreach (SceneEntityPlatformComponentOverrideAsset componentOverride in entity.PlatformComponentOverrides ?? []) {
                string scope = SceneOverrideScopePath.Format(componentOverride.Scope);
                foreach (SceneEntityPlatformAddedComponentAsset added in componentOverride.AddedComponents ?? []) {
                    if (added?.Component != null && IsRemoved(added.Component.ComponentTypeId)) {
                        throw new InvalidOperationException($"Forbidden component '{added.Component.ComponentTypeId}' survived on entity '{entity.Name}' in scope '{scope}'.");
                    }
                }
            }
        }
    }

    static IEnumerable<string> EnumerateScopePaths(SceneEntityAsset entity) {
        foreach (SceneEntityPlatformExistenceOverrideAsset item in entity.PlatformExistenceOverrides ?? []) yield return SceneOverrideScopePath.Format(item.Scope);
        foreach (SceneEntityPlatformTransformOverrideAsset item in entity.PlatformTransformOverrides ?? []) yield return SceneOverrideScopePath.Format(item.Scope);
        foreach (SceneEntityPlatformComponentOverrideAsset item in entity.PlatformComponentOverrides ?? []) yield return SceneOverrideScopePath.Format(item.Scope);
    }

    static IEnumerable<SceneEntityAsset> Enumerate(IEnumerable<SceneEntityAsset> roots) {
        foreach (SceneEntityAsset root in roots ?? []) {
            if (root == null) continue;
            yield return root;
            foreach (SceneEntityAsset child in Enumerate(root.Children)) yield return child;
        }
    }

    static string Hash(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();

    sealed record ComponentRecord(uint EntityId, string EntityName, string ComponentTypeId, string ScopePath);
    sealed record RemovedComponentRecord(uint EntityId, string EntityName, string ComponentTypeId, string ScopePath);
    sealed record ComponentCount(string ComponentTypeId, int Source, int Removed);
}
