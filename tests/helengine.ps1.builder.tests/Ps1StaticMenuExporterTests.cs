using helengine;
using helengine.ps1.samplegen;
using System.Text.Json;

namespace helengine.ps1.builder.tests;

public sealed class Ps1StaticMenuExporterTests {
    const string SceneId = "Scenes/DemoDiscMainMenu.helen";
    const string SourceRevision = "5cc124eec06b8db729b2f9b15d99d26cb1ca8cc3";
    const string SourceBlobId = "7bdb310b5169ebf2c68a394a850f9eacf13cda6a";

    [Fact]
    public void Export_PreservesHierarchyAndVisualRecordsAndRemovesBehaviorEverywhere() {
        SceneAsset source = CreateScene();
        string root = NewOutputRoot();
        string sourcePath = WriteSource(root, source);

        string outputPath = new Ps1StaticMenuExporter().Export(sourcePath, root, SourceRevision, SourceBlobId);
        SceneAsset exported = Ps1SceneSerialization.Deserialize(File.ReadAllBytes(outputPath));
        SceneEntityAsset camera = Assert.Single(exported.RootEntities);
        SceneEntityAsset panel = Assert.Single(camera.Children);

        Assert.Equal(source.Id, exported.Id);
        Assert.Equal(source.AuthoringAssetId, exported.AuthoringAssetId);
        Assert.Equal(101u, camera.Id);
        Assert.Equal(202u, panel.Id);
        Assert.True(camera.Enabled);
        Assert.False(panel.Enabled);
        Assert.Equal(camera.LocalPosition, source.RootEntities[0].LocalPosition);
        Assert.Equal(panel.LocalPosition, source.RootEntities[0].Children[0].LocalPosition);
        Assert.Equal(camera.LocalScale, source.RootEntities[0].LocalScale);
        Assert.Equal(panel.LocalScale, source.RootEntities[0].Children[0].LocalScale);
        Assert.Equal(camera.LocalOrientation, source.RootEntities[0].LocalOrientation);
        Assert.Equal(panel.LocalOrientation, source.RootEntities[0].Children[0].LocalOrientation);
        Assert.Equal(new[] { "helengine.CameraComponent", "helengine.TextComponent", "helengine.LayoutComponent", "helengine.ClipRectComponent", "helengine.RoundedRectComponent" }, camera.Components.Select(component => component.ComponentTypeId));
        Assert.Equal(new[] { "helengine.ViewportComponent" }, panel.Components.Select(component => component.ComponentTypeId));
        Assert.Equal(new[] { 0, 1, 2, 3, 4 }, camera.Components.Select(component => component.ComponentIndex));
        Assert.Equal(new byte[] { 0, 1, 0xff, 0x7f }, camera.Components[1].Payload);
        Assert.Equal(new byte[] { 9, 8, 7, 6 }, panel.Components[0].Payload);
        Assert.Equal("camera-key", camera.Components[0].ComponentKey);
        Assert.Equal(new[] { "helengine.SpriteComponent" }, Assert.Single(panel.PlatformComponentOverrides).AddedComponents.Select(added => added.Component.ComponentTypeId));
        Assert.Equal(new byte[] { 3, 2, 1 }, panel.PlatformComponentOverrides[0].AddedComponents[0].Component.Payload);
        Assert.Equal("helengine.SpriteComponent-key", panel.PlatformComponentOverrides[0].AddedComponents[0].Component.ComponentKey);
        Assert.Equal(new[] { "removed-key" }, panel.PlatformComponentOverrides[0].RemovedComponentKeys);
        Assert.Equal("group:handheld/platform:ds", SceneOverrideScopePath.Format(panel.PlatformComponentOverrides[0].Scope));
        Assert.DoesNotContain(Enumerate(exported.RootEntities).SelectMany(entity => entity.Components), component => IsBehavior(component.ComponentTypeId));
        Assert.DoesNotContain(Enumerate(exported.RootEntities).SelectMany(entity => entity.PlatformComponentOverrides).SelectMany(item => item.AddedComponents), added => IsBehavior(added.Component.ComponentTypeId));

        using JsonDocument report = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "report.json")));
        JsonElement reportRoot = report.RootElement;
        Assert.Equal(SceneId, reportRoot.GetProperty("sceneId").GetString());
        Assert.Equal(SourceRevision, reportRoot.GetProperty("sourceRevision").GetString());
        Assert.Equal(SourceBlobId, reportRoot.GetProperty("sourceBlobId").GetString());
        Assert.Equal(Hash(sourcePath), reportRoot.GetProperty("sourceSceneHash").GetString());
        Assert.Equal(Hash(outputPath), reportRoot.GetProperty("outputSceneHash").GetString());
        Assert.Contains(reportRoot.GetProperty("retainedComponents").EnumerateArray(), component => component.GetProperty("componentTypeId").GetString() == "helengine.SpriteComponent");
        Assert.Contains(reportRoot.GetProperty("removedComponents").EnumerateArray(), component => component.GetProperty("componentTypeId").GetString() == "DemoDisc.menu.MenuComponent");
        Assert.Contains(reportRoot.GetProperty("removedComponents").EnumerateArray(), component => component.GetProperty("componentTypeId").GetString() == "DemoDisc.menu.MenuItemComponent" && component.GetProperty("scopePath").GetString() == "group:handheld/platform:ds");
        Assert.Contains(reportRoot.GetProperty("scopePaths").EnumerateArray(), scope => scope.GetString() == "group:handheld/platform:ds");
        Assert.Equal(9, reportRoot.GetProperty("componentCounts").GetProperty("source").GetInt32());
        Assert.Equal(7, reportRoot.GetProperty("componentCounts").GetProperty("retained").GetInt32());
        Assert.Equal(2, reportRoot.GetProperty("componentCounts").GetProperty("removed").GetInt32());
    }

    [Fact]
    public void Export_ClassifiesAssemblyQualifiedTypesWithoutChangingSerializedIds() {
        SceneAsset scene = CreateScene();
        SceneEntityAsset camera = scene.RootEntities[0];
        SceneEntityAsset panel = camera.Children[0];
        camera.Components[1].ComponentTypeId = "helengine.TextComponent, helengine.editor";
        camera.Components[^1].ComponentTypeId = "DemoDisc.menu.MenuComponent, DemoDisc";
        panel.PlatformComponentOverrides[0].AddedComponents[0].Component.ComponentTypeId = "helengine.SpriteComponent, helengine.ui";
        panel.PlatformComponentOverrides[0].AddedComponents[1].Component.ComponentTypeId = "DemoDisc.menu.MenuItemComponent, DemoDisc";
        string sourcePath = WriteSource(NewOutputRoot(), scene);

        string outputPath = new Ps1StaticMenuExporter().Export(sourcePath, Path.Combine(Path.GetDirectoryName(sourcePath)!, "out"), SourceRevision, SourceBlobId);
        SceneAsset exported = Ps1SceneSerialization.Deserialize(File.ReadAllBytes(outputPath));
        using JsonDocument reportDocument = JsonDocument.Parse(File.ReadAllText(Path.Combine(Path.GetDirectoryName(outputPath)!, "report.json")));
        JsonElement report = reportDocument.RootElement;

        Assert.Contains(exported.RootEntities[0].Components, component => component.ComponentTypeId == "helengine.TextComponent, helengine.editor");
        Assert.DoesNotContain(exported.RootEntities[0].Components, component => component.ComponentTypeId == "DemoDisc.menu.MenuComponent, DemoDisc");
        Assert.Contains(exported.RootEntities[0].Children[0].PlatformComponentOverrides[0].AddedComponents, added => added.Component.ComponentTypeId == "helengine.SpriteComponent, helengine.ui");
        Assert.DoesNotContain(exported.RootEntities[0].Children[0].PlatformComponentOverrides[0].AddedComponents, added => added.Component.ComponentTypeId == "DemoDisc.menu.MenuItemComponent, DemoDisc");
        Assert.Contains(report.GetProperty("retainedComponents").EnumerateArray(), component => component.GetProperty("componentTypeId").GetString() == "helengine.SpriteComponent, helengine.ui");
        Assert.Contains(report.GetProperty("removedComponents").EnumerateArray(), component => component.GetProperty("componentTypeId").GetString() == "DemoDisc.menu.MenuItemComponent, DemoDisc");
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void Export_UnknownComponentFailsWithEntityTypeAndScope(bool inOverride) {
        SceneAsset scene = CreateScene();
        SceneEntityAsset entity = scene.RootEntities[0].Children[0];
        if (inOverride) {
            entity.PlatformComponentOverrides[0].AddedComponents = [
                new SceneEntityPlatformAddedComponentAsset { Component = Component("project.MysteryComponent", [1]) }
            ];
        } else {
            entity.Components = [Component("project.MysteryComponent, DemoDisc", [1])];
        }
        if (inOverride) entity.PlatformComponentOverrides[0].AddedComponents[0].Component.ComponentTypeId = "project.MysteryComponent, DemoDisc";
        string sourcePath = WriteSource(NewOutputRoot(), scene);

        InvalidOperationException error = Assert.Throws<InvalidOperationException>(() =>
            new Ps1StaticMenuExporter().Export(sourcePath, Path.Combine(Path.GetDirectoryName(sourcePath)!, "out"), SourceRevision, SourceBlobId));

        Assert.Contains("Panel", error.Message);
        Assert.Contains("project.MysteryComponent, DemoDisc", error.Message);
        Assert.Contains(inOverride ? "group:handheld/platform:ds" : "Common", error.Message);
    }

    [Fact]
    public void Export_RejectsMalformedScene() {
        string root = NewOutputRoot();
        Directory.CreateDirectory(root);
        string sourcePath = Path.Combine(root, "malformed.helen");
        File.WriteAllBytes(sourcePath, [1, 2, 3, 4]);

        Assert.ThrowsAny<Exception>(() => new Ps1StaticMenuExporter().Export(sourcePath, Path.Combine(root, "out"), SourceRevision, SourceBlobId));
    }

    [Fact]
    public void Export_RejectsWrongSceneId() {
        SceneAsset scene = CreateScene();
        scene.Id = "Scenes/Other.helen";
        string sourcePath = WriteSource(NewOutputRoot(), scene);

        InvalidOperationException error = Assert.Throws<InvalidOperationException>(() =>
            new Ps1StaticMenuExporter().Export(sourcePath, Path.Combine(Path.GetDirectoryName(sourcePath)!, "out"), SourceRevision, SourceBlobId));

        Assert.Contains(SceneId, error.Message);
        Assert.Contains(scene.Id, error.Message);
    }

    static SceneAsset CreateScene() {
        SceneComponentAssetRecord[] baseComponents = [
            Component("helengine.CameraComponent", [0xaa]),
            Component("helengine.TextComponent", [0, 1, 0xff, 0x7f]),
            Component("helengine.LayoutComponent", [0x12, 0x34]),
            Component("helengine.ClipRectComponent", [0x56, 0x78]),
            Component("helengine.RoundedRectComponent", [0x9a, 0xbc]),
            Component("DemoDisc.menu.MenuComponent", [0x10])
        ];
        for (int i = 0; i < baseComponents.Length; i++) baseComponents[i].ComponentIndex = i;
        return new SceneAsset {
            Id = SceneId,
            AuthoringAssetId = "0123456789abcdef0123456789abcdef",
            RootEntities = [
                new SceneEntityAsset {
                    Id = 101,
                    Name = "Camera",
                    Enabled = true,
                    LocalPosition = new float3(1, 2, 3),
                    LocalScale = new float3(2, 3, 4),
                    LocalOrientation = new float4(0, 0.5f, 0, 0.5f),
                    Components = baseComponents,
                    Children = [
                        new SceneEntityAsset {
                            Id = 202,
                            Name = "Panel",
                            Enabled = false,
                            LocalPosition = new float3(4, 5, 6),
                            LocalScale = new float3(7, 8, 9),
                            LocalOrientation = new float4(0.5f, 0, 0.5f, 0),
                            Components = [ Component("helengine.ViewportComponent", [9, 8, 7, 6]) ],
                            PlatformComponentOverrides = [
                                new SceneEntityPlatformComponentOverrideAsset {
                                    Scope = [
                                        new SceneOverrideScopeStepAsset { Kind = SceneOverrideScopeStepKind.Group, Id = "handheld" },
                                        new SceneOverrideScopeStepAsset { Kind = SceneOverrideScopeStepKind.Platform, Id = "ds" }
                                    ],
                                    RemovedComponentKeys = ["removed-key"],
                                    AddedComponents = [
                                        new SceneEntityPlatformAddedComponentAsset { Component = Component("helengine.SpriteComponent", [3, 2, 1]) },
                                        new SceneEntityPlatformAddedComponentAsset { Component = Component("DemoDisc.menu.MenuItemComponent", [0x20]) }
                                    ]
                                }
                            ],
                            Children = []
                        }
                    ]
                }
            ],
            AssetReferences = []
        };
    }

    static SceneComponentAssetRecord Component(string typeId, byte[] payload) => new() {
        ComponentTypeId = typeId,
        ComponentKey = typeId == "helengine.CameraComponent" ? "camera-key" : typeId + "-key",
        Payload = payload
    };

    static string NewOutputRoot() => Path.Combine(Path.GetTempPath(), "helengine-static-menu-" + Guid.NewGuid().ToString("N"));

    static string WriteSource(string root, SceneAsset scene) {
        Directory.CreateDirectory(root);
        string path = Path.Combine(root, "source.helen");
        Ps1SceneSerialization.Write(path, scene);
        return path;
    }

    static string Hash(string path) => Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant();

    static bool IsBehavior(string typeId) => typeId is "DemoDisc.menu.MenuComponent" or "DemoDisc.menu.MenuItemComponent";

    static IEnumerable<SceneEntityAsset> Enumerate(IEnumerable<SceneEntityAsset> entities) {
        foreach (SceneEntityAsset entity in entities ?? []) {
            yield return entity;
            foreach (SceneEntityAsset child in Enumerate(entity.Children)) yield return child;
        }
    }
}
