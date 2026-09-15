using helengine;
using helengine.ps1.samplegen;
using System.Security.Cryptography;

namespace helengine.ps1.builder.tests;

/// <summary>
/// Verifies the serializer-only authored cube-test subset export.
/// </summary>
public sealed class CubeTestSubsetTests {
    static string SourceProjectRoot => Environment.GetEnvironmentVariable("HELENGINE_PS1_PROJECT_ROOT") ?? @"C:\dev\helprojs\demodisc";
    static string SourceScenePath => Environment.GetEnvironmentVariable("HELENGINE_PS1_SOURCE_SCENE") ?? Path.Combine(SourceProjectRoot, "assets", "scenes", "rendering", "cube_test.helen");
    static string EngineBinaryRoot => Environment.GetEnvironmentVariable("HELENGINE_PS1_ENGINE_BINARY_ROOT") ?? @"C:\dev\helworks\helengine\helengine.ui\helengine.editor.app\bin\vs\Debug\net9.0-windows";
    static string TestOutputRoot => Environment.GetEnvironmentVariable("HELENGINE_PS1_TEST_OUTPUT_ROOT") ?? @"C:\dev\helworks\builds\helengine-ps1\cube-test-subset-tests";

    [Fact]
    public void ExportRealCubeTestScene_PreservesTypedSubsetAndOriginalIds() {
        SceneAsset source = LoadScene(SourceScenePath);
        SceneEntityAsset sourceCamera = source.RootEntities.Single(entity => entity.Name == "CubeTestCamera");
        SceneEntityAsset sourceCube = source.RootEntities.First(entity => entity.Name == "CubeTestCube");
        Ps1SceneExportResult result = ExportFixture();

        Assert.True(File.Exists(result.SubsetScenePath));
        Assert.True(File.Exists(result.ReportPath));
        Assert.Equal("scenes/rendering/cube_test.helen", result.SceneId);
        Assert.Equal(source.AuthoringAssetId, LoadScene(result.SubsetScenePath).AuthoringAssetId);
        Assert.Equal(sourceCamera.Id, result.Report.RetainedEntities.Single(entity => entity.Name == "CubeTestCamera").EntityId);
        Assert.Equal(sourceCube.Id, result.Report.RetainedEntities.Single(entity => entity.Name == "CubeTestCube").EntityId);
        Assert.Equal((ushort)16384, result.Report.Camera.LayerMask);
        Assert.Equal(0.1f, result.Report.Camera.NearPlaneDistance);
        Assert.Equal(64f, result.Report.Camera.FarPlaneDistance);
        Assert.Equal(100f / 255f, result.Report.Camera.ClearColor[0], 5);
        Assert.Equal(149f / 255f, result.Report.Camera.ClearColor[1], 5);
        Assert.Equal(237f / 255f, result.Report.Camera.ClearColor[2], 5);
        Assert.Equal(1f, result.Report.Camera.ClearColor[3]);
        Assert.Equal("Auto", result.Report.Camera.DepthPrepassMode);
        Assert.Equal(24f, result.Report.Camera.ShadowDistance);
        Assert.Equal("Disabled", result.Report.Camera.PostProcessTier);
        Assert.False(result.Report.Camera.ClearStencilEnabled);
        Assert.Equal(0f, result.Report.Spin.BaseYawRadians);
        Assert.Equal(MathF.PI / 2f, result.Report.Spin.AngularSpeedRadians, 5);

        SceneAsset scene = LoadScene(result.SubsetScenePath);
        SceneEntityAsset camera = Assert.Single(scene.RootEntities.Where(entity => entity.Name == "CubeTestCamera"));
        SceneEntityAsset cube = Assert.Single(scene.RootEntities.Where(entity => entity.Name == "CubeTestCube"));

        Assert.Equal(sourceCamera.Id, camera.Id);
        Assert.Equal(new float3(0f, 0f, 5f), camera.LocalPosition);
        Assert.Equal(float3.One, camera.LocalScale);
        Assert.Equal(sourceCube.Id, cube.Id);
        Assert.Equal(float3.Zero, cube.LocalPosition);
        Assert.Equal(float3.One, cube.LocalScale);
        Assert.Equal(new[] { "helengine.CameraComponent" }, camera.Components.Select(component => component.ComponentTypeId));
        Assert.Equal(
            new[] { "helengine.MeshComponent", Ps1SceneSubsetExporter.CubeTestSpinComponentTypeId },
            cube.Components.Select(component => component.ComponentTypeId));

        Assert.DoesNotContain(scene.RootEntities, entity => entity.Name.Contains("Ui", StringComparison.OrdinalIgnoreCase));
        Assert.DoesNotContain(scene.RootEntities, entity => entity.Name.Contains("Sun", StringComparison.OrdinalIgnoreCase));
        Assert.DoesNotContain(result.Report.ExcludedComponentTypeIds, componentTypeId => componentTypeId == Ps1SceneSubsetExporter.CameraComponentTypeId);
        Assert.Contains(result.Report.ExcludedComponentTypeIds, componentTypeId => componentTypeId.Contains("Orbit", StringComparison.OrdinalIgnoreCase));
        Assert.Equal(
            new[] { "b933266da88a40ca995a62781f4e12a2", "engine:model:cube" },
            scene.AssetReferences.Select(reference => reference.AssetId).OrderBy(id => id, StringComparer.Ordinal));
    }

    [Fact]
    public void ExportRealCubeTestScene_PreservesAuthoredPayloadsExactly() {
        SceneAsset source = LoadScene(SourceScenePath);
        Ps1SceneExportResult result = ExportFixture();
        SceneAsset subset = LoadScene(result.SubsetScenePath);

        SceneComponentAssetRecord sourceCamera = source.RootEntities.Single(entity => entity.Name == "CubeTestCamera").Components.Single(component => component.ComponentTypeId == Ps1SceneSubsetExporter.CameraComponentTypeId);
        SceneComponentAssetRecord exportedCamera = subset.RootEntities.Single(entity => entity.Name == "CubeTestCamera").Components.Single(component => component.ComponentTypeId == Ps1SceneSubsetExporter.CameraComponentTypeId);
        SceneComponentAssetRecord sourceSpin = source.RootEntities.First(entity => entity.Name == "CubeTestCube").Components.Single(component => component.ComponentTypeId == Ps1SceneSubsetExporter.CubeTestSpinComponentTypeId);
        SceneComponentAssetRecord exportedSpin = subset.RootEntities.Single(entity => entity.Name == "CubeTestCube").Components.Single(component => component.ComponentTypeId == Ps1SceneSubsetExporter.CubeTestSpinComponentTypeId);

        Assert.Equal(sourceCamera.Payload, exportedCamera.Payload);
        Assert.Equal(sourceSpin.Payload, exportedSpin.Payload);
        Assert.Equal(Convert.ToBase64String(sourceSpin.Payload), result.Report.RetainedComponents.Single(component => component.ComponentTypeId == Ps1SceneSubsetExporter.CubeTestSpinComponentTypeId).PayloadBase64);
    }

    [Fact]
    public void SelectSubset_RejectsAmbiguousCameraOrCube() {
        SceneAsset scene = new SceneAsset {
            Id = Ps1SceneSubsetExporter.SceneId,
            RootEntities = new[] {
                new SceneEntityAsset {
                    Id = 1,
                    Name = "CubeTestCamera",
                    Components = new[] { new SceneComponentAssetRecord { ComponentTypeId = Ps1SceneSubsetExporter.CameraComponentTypeId } },
                    Children = Array.Empty<SceneEntityAsset>()
                },
                new SceneEntityAsset {
                    Id = 2,
                    Name = "CubeTestCamera",
                    Components = new[] { new SceneComponentAssetRecord { ComponentTypeId = Ps1SceneSubsetExporter.CameraComponentTypeId } },
                    Children = Array.Empty<SceneEntityAsset>()
                }
            },
            AssetReferences = Array.Empty<SceneAssetReference>()
        };

        Assert.Throws<InvalidOperationException>(() => Ps1SceneSubsetExporter.SelectSubset(scene));
    }

    [Fact]
    public void SelectSubset_RejectsNestedTargetsInsteadOfPromotingLocalTransforms() {
        SceneEntityAsset camera = new SceneEntityAsset {
            Id = 10,
            Name = "CubeTestCamera",
            Components = new[] { new SceneComponentAssetRecord { ComponentTypeId = Ps1SceneSubsetExporter.CameraComponentTypeId } },
            Children = Array.Empty<SceneEntityAsset>()
        };
        SceneEntityAsset cube = new SceneEntityAsset {
            Id = 11,
            Name = "CubeTestCube",
            Components = new[] {
                new SceneComponentAssetRecord { ComponentTypeId = Ps1SceneSubsetExporter.MeshComponentTypeId },
                new SceneComponentAssetRecord { ComponentTypeId = Ps1SceneSubsetExporter.CubeTestSpinComponentTypeId }
            },
            Children = Array.Empty<SceneEntityAsset>()
        };
        SceneAsset scene = new SceneAsset {
            Id = Ps1SceneSubsetExporter.SceneId,
            RootEntities = new[] {
                new SceneEntityAsset {
                    Id = 1,
                    Name = "NestedParent",
                    Children = new[] { camera, cube }
                }
            },
            AssetReferences = Array.Empty<SceneAssetReference>()
        };

        Assert.Throws<InvalidOperationException>(() => Ps1SceneSubsetExporter.SelectSubset(scene));
    }

    [Fact]
    public void SelectSubset_RejectsDuplicateCubesInCameraSegment() {
        SceneEntityAsset camera = new SceneEntityAsset {
            Id = 10,
            Name = "CubeTestCamera",
            Components = new[] { new SceneComponentAssetRecord { ComponentTypeId = Ps1SceneSubsetExporter.CameraComponentTypeId } },
            Children = Array.Empty<SceneEntityAsset>()
        };
        SceneEntityAsset cube1 = CreateCandidateCube(11);
        SceneEntityAsset cube2 = CreateCandidateCube(12);
        SceneAsset scene = new SceneAsset {
            Id = Ps1SceneSubsetExporter.SceneId,
            RootEntities = new[] { camera, cube1, cube2 },
            AssetReferences = Array.Empty<SceneAssetReference>()
        };

        Assert.Throws<InvalidOperationException>(() => Ps1SceneSubsetExporter.SelectSubset(scene));
    }

    [Fact]
    public void RequestRejectsOutputInsideSourceProjectBeforeAnyWrite() {
        string beforeHash = ComputeHash(SourceScenePath);
        string outputRoot = Path.Combine(SourceProjectRoot, "ps1-subset-test-" + Guid.NewGuid().ToString("N"));

        Assert.Throws<ArgumentException>(() => new Ps1SceneExportRequest(SourceProjectRoot, SourceScenePath, outputRoot, EngineBinaryRoot));
        Assert.Equal(beforeHash, ComputeHash(SourceScenePath));
        Assert.False(Directory.Exists(outputRoot));
    }

    [Fact]
    public void RequestRejectsSourceAliasOfOutputBeforeAnyWrite() {
        string beforeHash = ComputeHash(SourceScenePath);
        string outputRoot = Path.Combine(TestOutputRoot, "alias-" + Guid.NewGuid().ToString("N"));
        string aliasedSourcePath = Path.Combine(outputRoot, "subset.helen");

        Assert.Throws<ArgumentException>(() => new Ps1SceneExportRequest(SourceProjectRoot, aliasedSourcePath, outputRoot, EngineBinaryRoot));
        Assert.Equal(beforeHash, ComputeHash(SourceScenePath));
        Assert.False(Directory.Exists(outputRoot));
    }

    [Fact]
    public void ExportRealCubeTestScene_DoesNotModifySourceFixture() {
        string beforeHash = ComputeHash(SourceScenePath);
        ExportFixture();
        Assert.Equal(beforeHash, ComputeHash(SourceScenePath));
    }

    static Ps1SceneExportResult ExportFixture() {
        string outputRoot = Path.Combine(TestOutputRoot, Guid.NewGuid().ToString("N"));
        return new Ps1SceneSubsetExporter().Export(new Ps1SceneExportRequest(
            SourceProjectRoot,
            SourceScenePath,
            outputRoot,
            EngineBinaryRoot));
    }

    static SceneEntityAsset CreateCandidateCube(uint id) {
        return new SceneEntityAsset {
            Id = id,
            Name = "CubeTestCube",
            Components = new[] {
                new SceneComponentAssetRecord { ComponentTypeId = Ps1SceneSubsetExporter.MeshComponentTypeId },
                new SceneComponentAssetRecord { ComponentTypeId = Ps1SceneSubsetExporter.CubeTestSpinComponentTypeId }
            },
            Children = Array.Empty<SceneEntityAsset>()
        };
    }

    static SceneAsset LoadScene(string path) {
        using FileStream stream = File.OpenRead(path);
        return Assert.IsType<SceneAsset>(helengine.AssetSerializer.Deserialize(stream));
    }

    static string ComputeHash(string path) {
        using SHA256 sha256 = SHA256.Create();
        using FileStream stream = File.OpenRead(path);
        return Convert.ToHexString(sha256.ComputeHash(stream)).ToLowerInvariant();
    }
}
