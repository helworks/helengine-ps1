using helengine;
using helengine.ps1.samplegen;

namespace helengine.ps1.builder.tests;

public sealed class Ps1MenuTextureExporterTests {
    const string SceneId = "Scenes/DemoDiscMainMenu.helen";

    [Fact]
    public void Export_UsesTheDemoDiscAuthoredTextureBackgroundWithPs1Variant() {
        string root = Path.Combine(Path.GetTempPath(), "helengine-menu-texture-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        string sourcePath = Path.Combine(root, "source.helen");
        string texturePath = Path.Combine(root, "background.png");
        File.WriteAllBytes(texturePath, [0x89, 0x50, 0x4e, 0x47]);
        Ps1SceneSerialization.Write(sourcePath, CreateScene());

        string exportedPath = new Ps1MenuTextureExporter().Export(sourcePath, Path.Combine(root, "out"), texturePath);
        SceneAsset exported = Ps1SceneSerialization.Deserialize(File.ReadAllBytes(exportedPath));
        SceneEntityAsset background = exported.RootEntities.Single(entity => entity.Name == "DemoDiscAnimatedBackground");

        SceneAssetReference texture = Assert.Single(exported.AssetReferences);
        Assert.Equal("9ef42b9c5a7d4862a9d1336e1f9c72a4", texture.AssetId);
        Assert.Equal("textures/menu/main_menu_background_ps1.png", texture.RelativePath);
        Assert.StartsWith("sha256:", texture.ContentHash);
        Assert.Equal(new float3(640f, 360f, 0f), background.LocalPosition);
        Assert.Equal(4, background.Children.Length);
        Assert.Equal(new[] {
            new float3(-960f, -720f, 0f),
            new float3(0f, -720f, 0f),
            new float3(-960f, 0f, 0f),
            new float3(0f, 0f, 0f)
        }, background.Children.Select(tile => tile.LocalPosition));
        Assert.All(background.Children, tile => {
            Assert.StartsWith("DemoDiscAnimatedBackgroundTile", tile.Name);
            Assert.Contains(tile.Components, component => component.ComponentTypeId == "helengine.SpriteComponent");
        });
        SceneComponentAssetRecord rotation = Assert.Single(background.Components);
        Assert.Equal("DemoDisc.rendering.AxisRotationComponent, DemoDisc", rotation.ComponentTypeId);
        Assert.NotEmpty(rotation.Payload);
        Assert.DoesNotContain(background.Components, component => component.ComponentTypeId == "DemoDisc.menu.MenuBackgroundMotionComponent");
        Assert.DoesNotContain(Enumerate(exported.RootEntities).SelectMany(entity => entity.Components), component => component.ComponentTypeId == "helengine.RoundedRectComponent");

        string report = File.ReadAllText(Path.Combine(Path.GetDirectoryName(exportedPath)!, "report.json"));
        Assert.Contains("\"authoringAssetId\": \"0123456789abcdef0123456789abcdef\"", report);
        Assert.Contains("\"componentTypeId\": \"DemoDisc.rendering.AxisRotationComponent, DemoDisc\"", report);
        Assert.Contains("\"nonBackgroundSceneContentPreserved\": true", report);
        Assert.Contains("\"outputQuads\": 4", report);
        Assert.Contains("\"clockwiseDegreesPerSecond\": 1", report);
    }

    [Fact]
    public void Export_CanMigrateTheLegacySceneToTheSharedHighResolutionTexture() {
        string root = Path.Combine(Path.GetTempPath(), "helengine-menu-shared-texture-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        string sourcePath = Path.Combine(root, "source.helen");
        string texturePath = Path.Combine(root, "background.png");
        File.WriteAllBytes(texturePath, [0x89, 0x50, 0x4e, 0x47]);
        Ps1SceneSerialization.Write(sourcePath, CreateLegacyScene());

        string exportedPath = new Ps1MenuTextureExporter().Export(
            sourcePath,
            Path.Combine(root, "out"),
            texturePath,
            "textures/menu/main_menu_background.png",
            "c6b0ea0d4e534180af4313cce7d93f12");
        SceneAsset exported = Ps1SceneSerialization.Deserialize(File.ReadAllBytes(exportedPath));
        SceneAssetReference texture = Assert.Single(exported.AssetReferences);
        SceneEntityAsset background = exported.RootEntities.Single(entity => entity.Name == "DemoDiscAnimatedBackground");

        Assert.Equal("textures/menu/main_menu_background.png", texture.RelativePath);
        Assert.Equal("c6b0ea0d4e534180af4313cce7d93f12", texture.AssetId);
        Assert.Equal(4, background.Children.Length);
        Assert.Contains("\"removedRoundedRectangles\": 140", File.ReadAllText(Path.Combine(Path.GetDirectoryName(exportedPath)!, "report.json")));
    }

    static SceneAsset CreateScene() => new() {
        Id = SceneId,
        AuthoringAssetId = "0123456789abcdef0123456789abcdef",
        RootEntities = [
            new SceneEntityAsset {
                Id = 101,
                Name = "DemoDiscAnimatedBackground",
                Enabled = true,
                LocalPosition = new float3(640f, 360f, 0f),
                LocalScale = new float3(1f, 1f, 1f),
                LocalOrientation = new float4(0f, 0f, 0f, 1f),
                Components = [Component("DemoDisc.rendering.AxisRotationComponent, DemoDisc")],
                Children = Enumerable.Range(0, 4).Select(index => new SceneEntityAsset {
                    Id = (uint)(102 + index),
                    Name = "DemoDiscAnimatedBackgroundTile",
                    Enabled = true,
                    LocalPosition = new float3(index % 2 == 0 ? -960f : 0f, index < 2 ? -720f : 0f, 0f),
                    LocalScale = new float3(1f, 1f, 1f),
                    LocalOrientation = new float4(0f, 0f, 0f, 1f),
                    Components = [Component("helengine.SpriteComponent")]
                }).ToArray()
            }
        ],
        AssetReferences = []
    };

    static SceneAsset CreateLegacyScene() => new() {
        Id = SceneId,
        AuthoringAssetId = "0123456789abcdef0123456789abcdef",
        RootEntities = [
            new SceneEntityAsset {
                Id = 101,
                Name = "DemoDiscAnimatedBackground",
                Enabled = true,
                LocalPosition = new float3(640f, 360f, 0f),
                LocalScale = new float3(1f, 1f, 1f),
                LocalOrientation = new float4(0f, 0f, 0f, 1f),
                Components = [Component("DemoDisc.menu.MenuBackgroundMotionComponent")],
                Children = [
                    new SceneEntityAsset {
                        Id = 102,
                        Name = "DemoDiscAnimatedBackgroundGrid",
                        Enabled = true,
                        LocalScale = new float3(1f, 1f, 1f),
                        LocalOrientation = new float4(0f, 0f, 0f, 1f),
                        Components = Enumerable.Range(0, 29).Select(_ => Component("helengine.RoundedRectComponent")).ToArray()
                    },
                    new SceneEntityAsset {
                        Id = 103,
                        Name = "DemoDiscAnimatedBackgroundScanlines",
                        Enabled = true,
                        LocalScale = new float3(1f, 1f, 1f),
                        LocalOrientation = new float4(0f, 0f, 0f, 1f),
                        Components = Enumerable.Range(0, 111).Select(_ => Component("helengine.RoundedRectComponent")).ToArray()
                    }
                ]
            }
        ],
        AssetReferences = []
    };

    static SceneComponentAssetRecord Component(string typeId) => new() {
        ComponentTypeId = typeId,
        ComponentKey = typeId,
        Payload = []
    };

    static IEnumerable<SceneEntityAsset> Enumerate(IEnumerable<SceneEntityAsset> entities) {
        foreach (SceneEntityAsset entity in entities ?? []) {
            yield return entity;
            foreach (SceneEntityAsset child in Enumerate(entity.Children)) yield return child;
        }
    }
}
