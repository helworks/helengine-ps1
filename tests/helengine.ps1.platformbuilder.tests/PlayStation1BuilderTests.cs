using System.Text.Json;
using helengine.baseplatform.Builders;
using helengine.baseplatform.Definitions;
using helengine.baseplatform.Manifest;
using helengine.baseplatform.Profiles;
using helengine.baseplatform.Reporting;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;
using helengine.baseplatform.Targets;
using helengine.editor;
using helengine.ps1.builder;

namespace helengine.ps1.platformbuilder.tests;

/// <summary>
/// Verifies the PlayStation 1 platform builder contract: material cooking limits, manifest validation,
/// declared-only content staging, the generated startup catalog, and the build orchestration.
/// </summary>
public sealed class PlayStation1BuilderTests : IDisposable {
    readonly string Root = Path.Combine(Path.GetTempPath(), "ps1-builder-tests", Guid.NewGuid().ToString("N"));

    public PlayStation1BuilderTests() {
        Directory.CreateDirectory(Root);
    }

    public void Dispose() {
        if (Directory.Exists(Root)) Directory.Delete(Root, true);
    }

    static Dictionary<string, string> UnlitFields(string color = "#3F7FBFFF") => new(StringComparer.OrdinalIgnoreCase) {
        [PlayStation1MaterialSchemaIds.BaseColorFieldId] = color,
        [PlayStation1MaterialSchemaIds.LightingModeFieldId] = "unlit",
        [PlayStation1MaterialSchemaIds.TransparencyFieldId] = "opaque"
    };

    static PlatformMaterialCookRequest CookRequest(Dictionary<string, string> fields, string schemaId = PlayStation1MaterialSchemaIds.OpaqueSchemaId, string platform = "ps1") =>
        new("b933266da88a40ca995a62781f4e12a2", "materials/rendering/cube_test/CubeTestSolid.hasset", platform, "release", "ps1-opaque", schemaId, fields);

    static PlatformBuildManifest Manifest(string startupSceneId, PlatformBuildScene[] scenes, PlatformBuildArtifact[] artifacts, string[] features, string platform = "ps1") =>
        new(1, "demodisc", "1.0.0", "1.0.0+test", platform, "0.1.0", startupSceneId, scenes, [], artifacts, [],
            [], new PlatformContainerWritePlan("ps1", []), [],
            new PlatformBuildRuntimeFeatureManifest(features.Select(feature => new PlatformBuildRequiredRuntimeFeature(feature, RuntimeFeatureRequirementSourceKind.Scene, "cube_test", "test")).ToArray()));

    static PlatformBuildTargetVariant[] Variants() => [new PlatformBuildTargetVariant("ps1", "ps1", "ps1", "ps1")];

    static PlatformCookProfile[] CookProfiles() => [new PlatformCookProfile("ps1", "PS1", new PlatformCookProfileCapabilities("ps1-opaque", "none", "none", "hele", PlatformSerializationEndianness.LittleEndian))];

    static PlatformBuildScene Scene(string sceneId) => new(sceneId, sceneId, $"scenes/rendering/{sceneId}.helen", [], []);

    static PlatformBuildManifest CubeManifest() => Manifest(
        "cube_test",
        [Scene("cube_test")],
        [
            new PlatformBuildArtifact("cooked/scenes/cube_test.hasset", "cube_test", "sha256:aa", "scene", "shared"),
            new PlatformBuildArtifact("cooked/materials/cube_test_solid.hasset", "b933266da88a40ca995a62781f4e12a2", "sha256:bb", "material", "shared"),
            new PlatformBuildArtifact("cooked/engine/models/cube.hasset", "engine:model:cube", "sha256:cc", "model", "shared")
        ],
        ["scene", "mesh", "camera", "material", "light"]);

    [Fact]
    public void MaterialCooker_CooksOpaqueUnlitBaseColor() {
        PlatformMaterialCookResult result = new PlayStation1MaterialCooker().Cook(CookRequest(UnlitFields()));

        helengine.Asset asset = helengine.AssetSerializer.Deserialize(new MemoryStream(result.CookedMaterialBytes));
        helengine.PlatformMaterialAsset material = Assert.IsType<helengine.PlatformMaterialAsset>(asset);
        Assert.Equal("b933266da88a40ca995a62781f4e12a2", material.Id);
        Assert.Equal("ps1-opaque", material.RendererFamilyId);
        Assert.Equal((0x3F, 0x7F, 0xBF, 0xFF), (material.BaseColorR, material.BaseColorG, material.BaseColorB, material.BaseColorA));
        Assert.False(material.Lit);
        Assert.Equal(string.Empty, material.TextureRelativePath);
    }

    [Fact]
    public void MaterialCooker_CooksVertexLitBaseColor() {
        Dictionary<string, string> fields = UnlitFields();
        fields[PlayStation1MaterialSchemaIds.LightingModeFieldId] = PlayStation1MaterialSchemaIds.VertexLitLightingMode;

        PlatformMaterialCookResult result = new PlayStation1MaterialCooker().Cook(CookRequest(fields));

        helengine.Asset asset = helengine.AssetSerializer.Deserialize(new MemoryStream(result.CookedMaterialBytes));
        helengine.PlatformMaterialAsset material = Assert.IsType<helengine.PlatformMaterialAsset>(asset);
        Assert.True(material.Lit);
        Assert.Equal((0x3F, 0x7F, 0xBF), (material.BaseColorR, material.BaseColorG, material.BaseColorB));
    }

    [Fact]
    public void MaterialCooker_DefaultsToVertexLitWhenNoLightingModeIsAuthored() {
        Dictionary<string, string> fields = UnlitFields();
        fields.Remove(PlayStation1MaterialSchemaIds.LightingModeFieldId);

        PlatformMaterialCookResult result = new PlayStation1MaterialCooker().Cook(CookRequest(fields));

        helengine.Asset asset = helengine.AssetSerializer.Deserialize(new MemoryStream(result.CookedMaterialBytes));
        helengine.PlatformMaterialAsset material = Assert.IsType<helengine.PlatformMaterialAsset>(asset);
        Assert.True(material.Lit);
    }

    [Theory]
    [InlineData("texture-relative-path", "textures/cube.png", "texture")]
    [InlineData("lighting-mode", "lit", "vertex-lit")]
    [InlineData("transparency", "blend", "opaque")]
    [InlineData("alpha-mode", "alpha-blend", "opaque")]
    public void MaterialCooker_RejectsUnsupportedFields(string fieldId, string value, string expectedMessageFragment) {
        Dictionary<string, string> fields = UnlitFields();
        fields[fieldId] = value;

        InvalidOperationException error = Assert.Throws<InvalidOperationException>(() => new PlayStation1MaterialCooker().Cook(CookRequest(fields)));
        Assert.Contains(expectedMessageFragment, error.Message, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void MaterialCooker_RejectsForeignSchemaAndPlatform() {
        Assert.Throws<InvalidOperationException>(() => new PlayStation1MaterialCooker().Cook(CookRequest(UnlitFields(), schemaId: "n64-unlit-v1")));
        Assert.Throws<InvalidOperationException>(() => new PlayStation1MaterialCooker().Cook(CookRequest(UnlitFields(), platform: "n64")));
    }

    [Fact]
    public void SceneCompatibilityValidator_AcceptsMenuStartupAndCubeTargetScenes() {
        PlatformBuildManifest manifest = Manifest("menu", [Scene("menu"), Scene("cube_test")], [], ["scene", "render2d", "sprites", "text2d", "animation"]);

        new PlayStation1SceneCompatibilityValidator().Validate(manifest);
    }

    [Fact]
    public void SceneCompatibilityValidator_AcceptsSingleCubeScene() {
        new PlayStation1SceneCompatibilityValidator().Validate(CubeManifest());
    }

    [Fact]
    public void SceneCompatibilityValidator_RejectsMissingStartupAndUnsupportedFeatures() {
        PlayStation1SceneCompatibilityValidator validator = new();
        Assert.Throws<InvalidOperationException>(() => validator.Validate(Manifest("other", [Scene("cube_test")], [], ["scene"])));
        InvalidOperationException error = Assert.Throws<InvalidOperationException>(() => validator.Validate(Manifest("cube_test", [Scene("cube_test")], [], ["scene", "physics3d.diagnostics"])));
        Assert.Contains("physics3d.diagnostics", error.Message);
    }

    [Fact]
    public void ContentStager_StagesOnlyDeclaredArtifactsAndWritesDeclarations() {
        string packageSource = Path.Combine(Root, "package-source");
        WriteFile(Path.Combine(packageSource, "cooked", "scenes", "cube_test.hasset"), "HELE");
        WriteFile(Path.Combine(packageSource, "cooked", "materials", "cube_test_solid.hasset"), "MAT");
        WriteFile(Path.Combine(packageSource, "cooked", "engine", "models", "cube.hasset"), "MDL");
        WriteFile(Path.Combine(packageSource, "cooked", "stray.bin"), "STRAY");
        string staging = Path.Combine(Root, "staging");

        IReadOnlyList<PlayStation1StagedAsset> staged = new PlayStation1ContentStager().Stage(CubeManifest().CookedArtifacts, [], packageSource, staging, CancellationToken.None);
        string declarationsPath = Path.Combine(Root, "declarations.json");
        PlayStation1ContentStager.WriteDeclarations(staged, declarationsPath);

        Assert.Equal(3, staged.Count);
        Assert.True(File.Exists(Path.Combine(staging, "cooked", "scenes", "cube_test.hasset")));
        Assert.False(File.Exists(Path.Combine(staging, "cooked", "stray.bin")));
        using JsonDocument document = JsonDocument.Parse(File.ReadAllText(declarationsPath));
        string[] relativePaths = document.RootElement.GetProperty("files").EnumerateArray().Select(entry => entry.GetProperty("relativePath").GetString()).ToArray();
        Assert.Equal(new[] { "cooked/engine/models/cube.hasset", "cooked/materials/cube_test_solid.hasset", "cooked/scenes/cube_test.hasset" }, relativePaths);
        foreach (JsonElement entry in document.RootElement.GetProperty("files").EnumerateArray()) {
            Assert.True(File.Exists(entry.GetProperty("sourcePath").GetString()));
        }
    }

    [Fact]
    public void ContentStager_StagesBuilderOwnedCookOutputs() {
        // Cooked textures are produced by this builder rather than declared in the manifest, so a
        // stager that only reads the manifest leaves them out of the archive and the runtime cannot
        // open them.
        string packageSource = Path.Combine(Root, "package-source");
        WriteFile(Path.Combine(packageSource, "cooked", "scenes", "cube_test.hasset"), "HELE");
        WriteFile(Path.Combine(packageSource, "cooked", "materials", "cube_test_solid.hasset"), "MAT");
        WriteFile(Path.Combine(packageSource, "cooked", "engine", "models", "cube.hasset"), "MDL");
        WriteFile(Path.Combine(packageSource, "cooked", "imported", "atlas.hetex"), "TEX");
        string staging = Path.Combine(Root, "staging");

        IReadOnlyList<PlayStation1StagedAsset> staged = new PlayStation1ContentStager().Stage(
            CubeManifest().CookedArtifacts,
            [CookWorkItem("cooked/imported/atlas.hetex")],
            packageSource,
            staging,
            CancellationToken.None);

        Assert.Equal(4, staged.Count);
        Assert.True(File.Exists(Path.Combine(staging, "cooked", "imported", "atlas.hetex")));
        Assert.Contains(staged, asset => asset.RelativePath == "cooked/imported/atlas.hetex");
    }

    [Fact]
    public void ContentStager_DoesNotStageACookOutputTwiceWhenTheManifestAlsoDeclaresIt() {
        string packageSource = Path.Combine(Root, "package-source");
        WriteFile(Path.Combine(packageSource, "cooked", "scenes", "cube_test.hasset"), "HELE");
        WriteFile(Path.Combine(packageSource, "cooked", "materials", "cube_test_solid.hasset"), "MAT");
        WriteFile(Path.Combine(packageSource, "cooked", "engine", "models", "cube.hasset"), "MDL");
        string staging = Path.Combine(Root, "staging");

        IReadOnlyList<PlayStation1StagedAsset> staged = new PlayStation1ContentStager().Stage(
            CubeManifest().CookedArtifacts,
            [CookWorkItem("cooked/scenes/cube_test.hasset")],
            packageSource,
            staging,
            CancellationToken.None);

        Assert.Equal(3, staged.Count);
    }

    /// <summary>Builds one cook work item whose only interesting field here is its output path.</summary>
    /// <param name="outputRelativePath">Path the cook wrote under the package source root.</param>
    /// <returns>Cook work item for staging tests.</returns>
    static PlatformCookWorkItem CookWorkItem(string outputRelativePath, string sourceAssetPath = "source.png") =>
        new("work-" + outputRelativePath, sourceAssetPath, "texture", "ps1", "runtime-texture", outputRelativePath, "artifact-1", "sha256:aa", "sha256:bb", "{}", []);

    [Fact]
    public void ContentStager_StagesOnlyPs1GeneratedControlTextures() {
        string packageSource = Path.Combine(Root, "package-source");
        WriteFile(Path.Combine(packageSource, "cooked", "scenes", "cube_test.hasset"), "HELE");
        WriteFile(Path.Combine(packageSource, "cooked", "materials", "cube_test_solid.hasset"), "MAT");
        WriteFile(Path.Combine(packageSource, "cooked", "engine", "models", "cube.hasset"), "MDL");
        WriteFile(Path.Combine(packageSource, "cooked", "imported", "ps1.hetex"), "PS1");
        WriteFile(Path.Combine(packageSource, "cooked", "imported", "n64.hetex"), "N64");
        WriteFile(Path.Combine(packageSource, "cooked", "imported", "shared.hetex"), "SHARED");

        IReadOnlyList<PlayStation1StagedAsset> staged = new PlayStation1ContentStager().Stage(
            CubeManifest().CookedArtifacts,
            [
                CookWorkItem("cooked/imported/ps1.hetex", "textures/instructions/controls/generated/ps1/dpad.png"),
                CookWorkItem("cooked/imported/n64.hetex", "textures/instructions/controls/generated/n64/dpad.png"),
                CookWorkItem("cooked/imported/shared.hetex", "textures/ui/shared.png")
            ],
            packageSource,
            Path.Combine(Root, "staging"),
            CancellationToken.None);

        Assert.Contains(staged, asset => asset.RelativePath == "cooked/imported/ps1.hetex");
        Assert.Contains(staged, asset => asset.RelativePath == "cooked/imported/shared.hetex");
        Assert.DoesNotContain(staged, asset => asset.RelativePath == "cooked/imported/n64.hetex");
    }

    [Fact]
    public void ContentStager_RejectsEscapingCollidingAndMissingPaths() {
        string packageSource = Path.Combine(Root, "package-source");
        WriteFile(Path.Combine(packageSource, "cooked", "a.hasset"), "A");
        PlayStation1ContentStager stager = new();
        string staging = Path.Combine(Root, "staging");

        Assert.Throws<InvalidOperationException>(() => stager.Stage([new PlatformBuildArtifact("../a.hasset", "sha256:aa", "scene", "shared")], [], packageSource, staging, CancellationToken.None));
        Assert.Throws<InvalidOperationException>(() => stager.Stage([new PlatformBuildArtifact("cooked/a.hasset", "sha256:aa", "scene", "shared"), new PlatformBuildArtifact("cooked\\a.hasset", "sha256:aa", "scene", "shared")], [], packageSource, staging, CancellationToken.None));
        Assert.Throws<FileNotFoundException>(() => stager.Stage([new PlatformBuildArtifact("cooked/missing.hasset", "sha256:aa", "scene", "shared")], [], packageSource, staging, CancellationToken.None));
    }

    [Theory]
    [InlineData("equal")]
    [InlineData("staging-parent")]
    [InlineData("source-parent")]
    public void ContentStager_RejectsOverlappingRootsWithoutDeletingInputs(string overlap) {
        string source = Path.Combine(Root, "package-source");
        string staging = overlap switch {
            "equal" => source,
            "staging-parent" => Root,
            _ => Path.Combine(source, "staging")
        };
        WriteFile(Path.Combine(source, "input.hasset"), "original source");
        WriteFile(Path.Combine(staging, "keep.txt"), "original staging");

        Assert.Throws<InvalidOperationException>(() => new PlayStation1ContentStager().Stage(
            [], [], source, staging, CancellationToken.None));

        Assert.Equal("original source", File.ReadAllText(Path.Combine(source, "input.hasset")));
        Assert.Equal("original staging", File.ReadAllText(Path.Combine(staging, "keep.txt")));
    }

    [Fact]
    public void ContentStager_CancellationPreservesPreviousStaging() {
        string source = Path.Combine(Root, "package-source");
        Directory.CreateDirectory(source);
        string staging = Path.Combine(Root, "staging");
        WriteFile(Path.Combine(staging, "keep.txt"), "previous staging");
        using CancellationTokenSource cancellation = new();
        cancellation.Cancel();

        Assert.ThrowsAny<OperationCanceledException>(() => new PlayStation1ContentStager().Stage(
            [], [], source, staging, cancellation.Token));

        Assert.Equal("previous staging", File.ReadAllText(Path.Combine(staging, "keep.txt")));
    }

    [Theory]
    [InlineData("missing")]
    [InlineData("collision")]
    [InlineData("traversal")]
    [InlineData("trailing-dot")]
    [InlineData("trailing-space")]
    [InlineData("stream")]
    public void ContentStager_InvalidDeclarationPreservesPreviousStaging(string failure) {
        string source = Path.Combine(Root, "package-source");
        WriteFile(Path.Combine(source, "cooked", "valid.hasset"), "valid");
        string staging = Path.Combine(Root, "staging");
        WriteFile(Path.Combine(staging, "keep.txt"), "previous staging");
        string invalidPath = failure switch {
            "collision" => "cooked\\valid.hasset",
            "traversal" => "../invalid.hasset",
            "trailing-dot" => "cooked/valid.hasset.",
            "trailing-space" => "cooked/valid.hasset ",
            "stream" => "cooked/valid.hasset:stream",
            _ => "cooked/missing.hasset"
        };
        PlatformBuildArtifact[] artifacts = [
            new("cooked/valid.hasset", "sha256:aa", "scene", "shared"),
            new(invalidPath, "sha256:bb", "scene", "shared")
        ];

        Exception exception = Record.Exception(() => new PlayStation1ContentStager().Stage(
            artifacts, [], source, staging, CancellationToken.None));
        if (failure == "missing") Assert.IsType<FileNotFoundException>(exception);
        else Assert.IsType<InvalidOperationException>(exception);

        Assert.Equal("previous staging", File.ReadAllText(Path.Combine(staging, "keep.txt")));
        Assert.False(File.Exists(Path.Combine(staging, "cooked", "valid.hasset")));
        Assert.Equal("valid", File.ReadAllText(Path.Combine(source, "cooked", "valid.hasset")));
    }

    [Fact]
    public void StartupManifestWriter_WritesCatalogForStartupScene() {
        string generatedCore = Path.Combine(Root, "generated-core");
        new PlayStation1StartupManifestWriter().Write(generatedCore, CubeManifest());

        string header = File.ReadAllText(Path.Combine(generatedCore, "ps1_runtime_scene_catalog.hpp"));
        string source = File.ReadAllText(Path.Combine(generatedCore, "ps1_runtime_scene_catalog.cpp"));
        Assert.Contains("he_ps1_runtime_startup_scene_id", header);
        Assert.Contains("he_ps1_runtime_scene_catalog_entries", header);
        Assert.Contains("HEPs1RuntimeSceneCatalogEntry", header);
        Assert.Contains("{\"cube_test\", \"cooked/scenes/cube_test.hasset\"}", source);
        Assert.Contains("kStartupSceneId[] = \"cube_test\"", source);
    }

    [Fact]
    public void StartupManifestWriter_RejectsSceneWithoutCookedArtifact() {
        PlatformBuildManifest manifest = Manifest("cube_test", [Scene("cube_test")], [new PlatformBuildArtifact("cooked/materials/m.hasset", "m", "sha256:aa", "material", "shared")], ["scene"]);
        Assert.Throws<InvalidOperationException>(() => new PlayStation1StartupManifestWriter().Write(Path.Combine(Root, "generated-core"), manifest));
    }

    [Fact]
    public void PlatformDefinition_DeclaresPs1CodegenProfileAndCookedMaterials() {
        PlatformDefinition definition = PlayStation1PlatformDefinitionFactory.Create();
        Assert.Equal("ps1", definition.PlatformId);
        PlatformCodegenProfileDefinition codegen = Assert.Single(definition.CodegenProfiles);
        Dictionary<string, string> settings = codegen.Settings.ToDictionary(setting => setting.SettingId, setting => setting.DefaultValue, StringComparer.OrdinalIgnoreCase);
        Assert.Equal("Ps1RuntimeProvider.hpp", settings["codegen-runtime-provider-header"]);
        Assert.Equal("Ps1Math.hpp", settings["codegen-runtime-math-header"]);
        Assert.Equal("false", settings["codegen-use-std-string"]);
        Assert.Equal("false", settings["codegen-use-hosted-file-system"]);
        Assert.Equal("true", settings["codegen-use-rtti"]);
        Assert.Equal("false", settings["codegen-use-exceptions"]);
        Assert.Equal("4", settings["pointer-size-bytes"]);
        Assert.Equal("engine-row-vector", settings["generated-math-convention"]);
        Assert.Contains("shaders", settings[PlatformCodegenSettingIds.ForcedDisabledFeatures]);
        Assert.Equal(PlatformSerializationEndianness.LittleEndian, codegen.Endianness);
        Assert.Equal(RuntimeMaterialResolutionMode.CookedPlatformOwned, definition.RuntimeGenerationContract.MaterialResolutionMode);
        Assert.Contains("HELENGINE_CODEGEN_FATAL_ONLY", definition.RuntimeGenerationContract.PortableInputPreprocessorSymbols);
    }

    [Fact]
    public void FontAtlasSettings_ConvertsGeneratedRawRgbaToPs1IndexedDefaults() {
        TextureAssetProcessorSettings settings = PlayStation1TextureCookSettingsSerializer.DeserializeFontAtlas(
            "{\"maxResolution\":512,\"colorFormat\":\"Rgba32\",\"alphaPrecision\":\"Full\"}");

        Assert.Equal(256, settings.MaxResolution);
        Assert.Equal(TextureAssetColorFormat.Indexed4, settings.ColorFormat);
        Assert.Equal(TextureAssetAlphaPrecision.Binary, settings.AlphaPrecision);
    }

    [Fact]
    public void TextureSettings_ConvertsGeneratedRawRgbaToPs1IndexedDefaults() {
        TextureAssetProcessorSettings settings = PlayStation1TextureCookSettingsSerializer.Deserialize(
            "{\"maxResolution\":512,\"colorFormat\":\"Rgba32\",\"alphaPrecision\":\"Full\"}");

        Assert.Equal(512, settings.MaxResolution);
        Assert.Equal(TextureAssetColorFormat.Indexed8, settings.ColorFormat);
        Assert.Equal(TextureAssetAlphaPrecision.Binary, settings.AlphaPrecision);
    }

    [Fact]
    public void TextureSettings_PreservesAuthoredPs1IconResolutionWhenConvertingRawRgba() {
        TextureAssetProcessorSettings settings = PlayStation1TextureCookSettingsSerializer.Deserialize(
            "{\"maxResolution\":27,\"colorFormat\":\"Rgba32\",\"alphaPrecision\":\"Full\"}");

        Assert.Equal(27, settings.MaxResolution);
        Assert.Equal(TextureAssetColorFormat.Indexed8, settings.ColorFormat);
        Assert.Equal(TextureAssetAlphaPrecision.Binary, settings.AlphaPrecision);
    }

    [Fact]
    public async Task Builder_StagesWritesCatalogAndRunsNativeBuild() {
        string working = Path.Combine(Root, "working");
        string packageSource = Path.Combine(working, "package-source");
        WriteFile(Path.Combine(packageSource, "cooked", "scenes", "cube_test.hasset"), "HELE");
        WriteFile(Path.Combine(packageSource, "cooked", "materials", "cube_test_solid.hasset"), "MAT");
        WriteFile(Path.Combine(packageSource, "cooked", "engine", "models", "cube.hasset"), "MDL");
        string generatedCore = Path.Combine(Root, "generated-core");
        Directory.CreateDirectory(generatedCore);
        string output = Path.Combine(Root, "output");
        RecordingNativeBuildExecutor executor = new();
        PlayStation1PlatformAssetBuilder builder = new(executor);
        PlatformBuildRequest request = new(CubeManifest(), Variants(), CookProfiles(), output, working, "release", "ps1-opaque", "default", null, null, null, generatedCore);

        PlatformBuildReport report = await builder.BuildAsync(request, new NullProgressReporter(), new CollectingDiagnosticReporter(), CancellationToken.None);

        Assert.True(report.Succeeded, string.Join("\n", report.Diagnostics.Select(diagnostic => diagnostic.Message)));
        Assert.Equal(generatedCore, executor.GeneratedCoreRoot);
        Assert.True(File.Exists(Path.Combine(executor.StagingRoot, "cooked", "scenes", "cube_test.hasset")));
        Assert.True(File.Exists(executor.DeclarationsPath));
        Assert.True(File.Exists(Path.Combine(generatedCore, "ps1_runtime_scene_catalog.cpp")));
        Assert.Equal(output, executor.OutputRoot);
    }

    [Fact]
    public async Task Builder_ReportsFailureForForeignPlatformManifestWithoutRunningNativeBuild() {
        string working = Path.Combine(Root, "working");
        Directory.CreateDirectory(Path.Combine(working, "package-source"));
        RecordingNativeBuildExecutor executor = new();
        PlayStation1PlatformAssetBuilder builder = new(executor);
        PlatformBuildManifest manifest = Manifest("cube_test", [Scene("cube_test")], [], ["scene"], platform: "n64");
        PlatformBuildRequest request = new(manifest, Variants(), CookProfiles(), Path.Combine(Root, "output"), working, "release", "ps1-opaque", "default", null, null, null, Path.Combine(Root, "generated-core"));
        CollectingDiagnosticReporter diagnostics = new();

        PlatformBuildReport report = await builder.BuildAsync(request, new NullProgressReporter(), diagnostics, CancellationToken.None);

        Assert.False(report.Succeeded);
        Assert.Null(executor.OutputRoot);
        Assert.Contains(diagnostics.Diagnostics, diagnostic => diagnostic.Severity == PlatformBuildDiagnosticSeverity.Error);
    }

    static void WriteFile(string path, string content) {
        Directory.CreateDirectory(Path.GetDirectoryName(path));
        File.WriteAllText(path, content);
    }

    /// <summary>Records the native build request instead of running Docker, and produces the expected executable.</summary>
    sealed class RecordingNativeBuildExecutor : IPlayStation1NativeBuildExecutor {
        public string GeneratedCoreRoot;
        public string StagingRoot;
        public string DeclarationsPath;
        public string OutputRoot;

        public void Build(string generatedCoreCppRootPath, string stagedContentRootPath, string declarationsPath, string outputRootPath, CancellationToken cancellationToken) {
            GeneratedCoreRoot = generatedCoreCppRootPath;
            StagingRoot = stagedContentRootPath;
            DeclarationsPath = declarationsPath;
            OutputRoot = outputRootPath;
            Directory.CreateDirectory(outputRootPath);
            File.WriteAllBytes(Path.Combine(outputRootPath, "helengine_ps1.ps-exe"), [1, 2, 3]);
        }
    }

    sealed class NullProgressReporter : IPlatformBuildProgressReporter {
        public void Report(PlatformBuildProgressUpdate update) {
        }
    }

    sealed class CollectingDiagnosticReporter : IPlatformBuildDiagnosticReporter {
        public readonly List<PlatformBuildDiagnostic> Diagnostics = [];

        public void Report(PlatformBuildDiagnostic diagnostic) {
            Diagnostics.Add(diagnostic);
        }
    }
}
