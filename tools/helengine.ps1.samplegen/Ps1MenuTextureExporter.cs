using helengine;
using helengine.editor;
using System.Security.Cryptography;
using System.Text.Json;

namespace helengine.ps1.samplegen;

/// <summary>Replaces the main menu's animated line geometry with a rotating tiled PS1 texture.</summary>
public sealed class Ps1MenuTextureExporter {
    const string SceneId = "Scenes/DemoDiscMainMenu.helen";
    const string BackgroundName = "DemoDiscAnimatedBackground";
    const string SharedTextureReferencePath = "textures/menu/main_menu_background.png";
    const string MotionComponentType = "DemoDisc.menu.MenuBackgroundMotionComponent";
    const string RotationComponentType = "DemoDisc.rendering.AxisRotationComponent, DemoDisc";
    const string TextureReferencePath = "textures/menu/main_menu_background_ps1.png";
    const string TextureAssetId = "9ef42b9c5a7d4862a9d1336e1f9c72a4";
    const string SharedTextureAssetId = "c6b0ea0d4e534180af4313cce7d93f12";
    const int CanvasWidth = 1280;
    const int CanvasHeight = 720;
    const int Ps1TextureWidth = 240;
    const int Ps1TextureHeight = 240;
    const int TileWidth = 960;
    const int TileHeight = CanvasHeight;
    const float ClockwiseDegreesPerSecond = 1f;

    static readonly JsonSerializerOptions ReportJsonOptions = new() {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    };

    /// <summary>Writes a build-local menu scene whose background is drawn by four rotating texture quads.</summary>
    public string Export(string sourceScenePath, string outputRootPath, string textureAssetPath) {
        return Export(sourceScenePath, outputRootPath, textureAssetPath, TextureReferencePath, TextureAssetId);
    }

    /// <summary>Writes four tiled background quads using the requested DemoDisc-owned texture variant.</summary>
    public string Export(string sourceScenePath, string outputRootPath, string textureAssetPath, string textureReferencePath, string textureAssetId) {
        ArgumentException.ThrowIfNullOrWhiteSpace(sourceScenePath);
        ArgumentException.ThrowIfNullOrWhiteSpace(outputRootPath);
        ArgumentException.ThrowIfNullOrWhiteSpace(textureAssetPath);
        ArgumentException.ThrowIfNullOrWhiteSpace(textureReferencePath);
        ArgumentException.ThrowIfNullOrWhiteSpace(textureAssetId);
        string sourcePath = Path.GetFullPath(sourceScenePath);
        if (!File.Exists(sourcePath)) throw new FileNotFoundException("Main-menu source scene was not found.", sourcePath);
        string textureSourcePath = Path.GetFullPath(textureAssetPath);
        if (!File.Exists(textureSourcePath)) throw new FileNotFoundException("PS1 menu background texture was not found.", textureSourcePath);
        string textureContentHash = "sha256:" + Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(textureSourcePath))).ToLowerInvariant();
        byte[] sourceBytes = File.ReadAllBytes(sourcePath);
        SceneAsset originalScene = Ps1SceneSerialization.Deserialize(sourceBytes);
        SceneAsset scene = Ps1SceneSerialization.Deserialize(sourceBytes);
        if (!string.Equals(scene.Id, SceneId, StringComparison.Ordinal)) {
            throw new InvalidOperationException($"PS1 menu texture export requires scene ID '{SceneId}', got '{scene.Id}'.");
        }

        SceneEntityAsset[] entities = Enumerate(scene.RootEntities ?? []).ToArray();
        SceneEntityAsset background = entities.SingleOrDefault(entity => string.Equals(entity.Name, BackgroundName, StringComparison.Ordinal))
            ?? throw new InvalidOperationException($"Main menu is missing '{BackgroundName}'.");
        SceneEntityAsset[] previousBackgroundChildren = background.Children ?? [];
        int removedShapeCount = Enumerate(previousBackgroundChildren).Sum(entity => (entity.Components ?? []).Count(component =>
            string.Equals(component?.ComponentTypeId, "helengine.RoundedRectComponent", StringComparison.Ordinal)));
        bool isSharedTextureBackground = previousBackgroundChildren.Length == 4
            && previousBackgroundChildren.All(entity => (entity.Components ?? []).Any(component => IsComponent(component?.ComponentTypeId, "helengine.SpriteComponent")));
        bool isLegacyShapeBackground = previousBackgroundChildren.Length == 2
            && previousBackgroundChildren.Any(entity => string.Equals(entity.Name, "DemoDiscAnimatedBackgroundGrid", StringComparison.Ordinal))
            && previousBackgroundChildren.Any(entity => string.Equals(entity.Name, "DemoDiscAnimatedBackgroundScanlines", StringComparison.Ordinal))
            && removedShapeCount == 140;
        if (!isSharedTextureBackground && !isLegacyShapeBackground) {
            throw new InvalidOperationException("Main menu background must contain either the four shared texture tiles or the known 140-shape legacy background.");
        }

        SceneAssetReference textureReference = SceneAssetReferenceFactory.CreateFileSystemReference(textureAssetId, textureReferencePath, textureContentHash);
        uint firstTileId = NextEntityId(entities, 4);
        bool isPs1Variant = string.Equals(textureReferencePath, TextureReferencePath, StringComparison.OrdinalIgnoreCase)
            || string.Equals(textureAssetId, TextureAssetId, StringComparison.OrdinalIgnoreCase);
        float halfCanvasWidth = CanvasWidth / 2f;
        float halfCanvasHeight = CanvasHeight / 2f;
        background.IsStatic = false;
        background.LocalPosition = new float3(halfCanvasWidth, halfCanvasHeight, 0f);
        background.LocalScale = new float3(1f, 1f, 1f);
        background.LocalOrientation = new float4(0f, 0f, 0f, 1f);
        background.Components = (background.Components ?? [])
            .Where(component => !IsComponent(component?.ComponentTypeId, MotionComponentType)
                && !IsComponent(component?.ComponentTypeId, RotationComponentType))
            .Append(CreateRotationRecord())
            .Select((component, index) => {
                component.ComponentIndex = index;
                return component;
            })
            .ToArray();
        background.Children = [
            CreateTile(firstTileId, 0, new float3(-TileWidth, -TileHeight, 0f), textureReference),
            CreateTile(firstTileId + 1u, 1, new float3(0f, -TileHeight, 0f), textureReference),
            CreateTile(firstTileId + 2u, 2, new float3(-TileWidth, 0f, 0f), textureReference),
            CreateTile(firstTileId + 3u, 3, new float3(0f, 0f, 0f), textureReference)
        ];

        List<SceneAssetReference> references = (scene.AssetReferences ?? [])
            .Where(reference => !string.Equals(reference.RelativePath, SharedTextureReferencePath, StringComparison.OrdinalIgnoreCase)
                && !string.Equals(reference.RelativePath, textureReferencePath, StringComparison.OrdinalIgnoreCase))
            .ToList();
        if (!references.Any(reference => string.Equals(reference.RelativePath, textureReferencePath, StringComparison.OrdinalIgnoreCase))) {
            references.Add(textureReference);
        }
        scene.AssetReferences = references.ToArray();

        string outputRoot = Path.GetFullPath(outputRootPath);
        Directory.CreateDirectory(outputRoot);
        string scenePath = Path.Combine(outputRoot, "scene.helen");
        Ps1SceneSerialization.Write(scenePath, scene);
        string preservedSceneHash = VerifyOnlyBackgroundChanged(originalScene, File.ReadAllBytes(scenePath), background.Name);
        object report = new {
            sceneId = scene.Id,
            authoringAssetId = scene.AuthoringAssetId,
            sourceSceneHash = Convert.ToHexString(SHA256.HashData(sourceBytes)).ToLowerInvariant(),
            outputSceneHash = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(scenePath))).ToLowerInvariant(),
            preservedSceneContentHash = preservedSceneHash,
            nonBackgroundSceneContentPreserved = true,
            retainedComponents = Enumerate(scene.RootEntities ?? [])
                .SelectMany(entity => entity.Components ?? [])
                .Select(component => new { componentTypeId = component.ComponentTypeId })
                .ToArray(),
            backgroundTexture = new {
                relativePath = textureReferencePath,
                assetId = textureAssetId,
                contentHash = textureContentHash,
                width = isPs1Variant ? Ps1TextureWidth : 960,
                height = isPs1Variant ? Ps1TextureHeight : 720,
                indexedBitsPerPixel = isPs1Variant ? 4 : 0,
                textureBytes = isPs1Variant ? Ps1TextureWidth * Ps1TextureHeight / 2 : 960 * 720 * 3,
                outputQuads = 4,
                replacedSharedTiles = previousBackgroundChildren.Length,
                removedRoundedRectangles = removedShapeCount,
                removedMotionComponent = true,
                rotationComponentType = RotationComponentType,
                clockwiseDegreesPerSecond = ClockwiseDegreesPerSecond
            }
        };
        File.WriteAllText(Path.Combine(outputRoot, "report.json"), JsonSerializer.Serialize(report, ReportJsonOptions));
        return scenePath;
    }

    static string VerifyOnlyBackgroundChanged(SceneAsset originalScene, byte[] outputBytes, string backgroundName) {
        SceneAsset outputScene = Ps1SceneSerialization.Deserialize(outputBytes);
        SceneEntityAsset originalBackground = Enumerate(originalScene.RootEntities ?? [])
            .Single(entity => string.Equals(entity.Name, backgroundName, StringComparison.Ordinal));
        if (!ReplaceEntity(outputScene.RootEntities, backgroundName, originalBackground)) {
            throw new InvalidOperationException($"Exported scene lost '{backgroundName}'; refusing to replace the authored scene.");
        }
        outputScene.AssetReferences = originalScene.AssetReferences;

        byte[] originalCanonical = SerializeScene(originalScene);
        byte[] restoredCanonical = SerializeScene(outputScene);
        string originalHash = Convert.ToHexString(SHA256.HashData(originalCanonical)).ToLowerInvariant();
        string restoredHash = Convert.ToHexString(SHA256.HashData(restoredCanonical)).ToLowerInvariant();
        if (!string.Equals(originalHash, restoredHash, StringComparison.Ordinal)) {
            throw new InvalidOperationException("Menu background export changed scene content outside the background subtree; refusing to replace the authored scene.");
        }
        return originalHash;
    }

    static byte[] SerializeScene(SceneAsset scene) {
        using MemoryStream stream = new();
        helengine.editor.AssetSerializer.Serialize(stream, scene);
        return stream.ToArray();
    }

    static bool ReplaceEntity(SceneEntityAsset[] entities, string name, SceneEntityAsset replacement) {
        if (entities == null) return false;
        for (int index = 0; index < entities.Length; index++) {
            SceneEntityAsset entity = entities[index];
            if (string.Equals(entity?.Name, name, StringComparison.Ordinal)) {
                entities[index] = replacement;
                return true;
            }
            if (ReplaceEntity(entity?.Children, name, replacement)) return true;
        }
        return false;
    }

    static SceneEntityAsset CreateTile(uint id, int tileIndex, float3 position, SceneAssetReference reference) => new() {
        Id = id,
        Name = "DemoDiscAnimatedBackgroundTile" + tileIndex.ToString(System.Globalization.CultureInfo.InvariantCulture),
        IsStatic = false,
        Enabled = true,
        LocalPosition = position,
        LocalScale = new float3(1f, 1f, 1f),
        LocalOrientation = new float4(0f, 0f, 0f, 1f),
        Components = [CreateSpriteRecord(reference, 0, new int2(TileWidth, TileHeight), new float4(0f, 0f, 1f, 1f))]
    };

    static SceneComponentAssetRecord CreateSpriteRecord(SceneAssetReference reference, int index, int2 size, float4 sourceRect) {
        SpriteComponent sprite = new() {
            RenderOrder2D = 0,
            SourceRect = sourceRect,
            Size = size,
            Color = new byte4(255, 255, 255, 255)
        };
        EntityComponentSaveState saveState = new();
        saveState.SetAssetReference("Texture", reference);
        SceneComponentAssetRecord record = new ComponentPersistenceRegistry()
            .GetDescriptor(sprite)
            .SerializeComponent(sprite, index, saveState);
        record.ComponentKey = "menu-background-tile-" + index.ToString(System.Globalization.CultureInfo.InvariantCulture);
        return record;
    }

    static SceneComponentAssetRecord CreateRotationRecord() {
        DemoDisc.rendering.AxisRotationComponent rotation = new() {
            Axis = new float3(0f, 0f, 1f),
            AngularSpeedRadiansPerSecond = ClockwiseDegreesPerSecond * (MathF.PI / 180f)
        };
        SceneComponentAssetRecord record = new ComponentPersistenceRegistry()
            .GetDescriptor(rotation)
            .SerializeComponent(rotation, 0, new EntityComponentSaveState());
        record.ComponentTypeId = RotationComponentType;
        record.ComponentKey = "ps1-menu-background-rotation";
        return record;
    }

    static bool IsComponent(string componentTypeId, string expectedTypeId) =>
        string.Equals((componentTypeId ?? string.Empty).Split(',')[0].Trim(), (expectedTypeId ?? string.Empty).Split(',')[0].Trim(), StringComparison.Ordinal);

    static uint NextEntityId(IEnumerable<SceneEntityAsset> entities, uint requiredCount) {
        uint max = entities.Select(entity => entity.Id).DefaultIfEmpty(0u).Max();
        if (max > uint.MaxValue - requiredCount) throw new InvalidOperationException("Cannot allocate menu texture entity ids; scene id space is exhausted.");
        return max + 1u;
    }

    static IEnumerable<SceneEntityAsset> Enumerate(IEnumerable<SceneEntityAsset> roots) {
        foreach (SceneEntityAsset root in roots ?? []) {
            if (root == null) continue;
            yield return root;
            foreach (SceneEntityAsset child in Enumerate(root.Children ?? [])) yield return child;
        }
    }
}
