using helengine.baseplatform.Manifest;
using helengine.ps1.builder;

namespace helengine.ps1.platformbuilder.tests;

/// <summary>Checks PS1 startup validation without duplicating the shared native loading-policy producer.</summary>
public sealed class PlayStation1StartupLoadingManifestTests {
    /// <summary>Rejects nonpositive and nonfinite loading budgets before emitting any platform catalog.</summary>
    /// <param name="budget">Invalid cooperative frame budget.</param>
    [Theory]
    [InlineData(0d)]
    [InlineData(-1d)]
    [InlineData(double.NaN)]
    [InlineData(double.PositiveInfinity)]
    [InlineData(double.NegativeInfinity)]
    public void Write_InvalidBudget_Rejects(double budget) {
        PlatformBuildManifest manifest = CreateManifest("menu", ["menu", "loading"]);
        manifest.SceneLoadingBudgetMilliseconds = budget;
        manifest.LoadingScreenSceneId = "loading";
        Assert.Throws<InvalidOperationException>(() => new PlayStation1StartupManifestWriter().Write(OutputRoot("invalid-budget"), manifest));
    }

    /// <summary>Rejects unresolvable or duplicate loading scenes using exact operational IDs.</summary>
    /// <param name="loadingId">Selected loading overlay.</param>
    /// <param name="duplicate">Whether the catalog duplicates the loading scene.</param>
    [Theory]
    [InlineData("missing", false)]
    [InlineData("Loading", false)]
    [InlineData(" ", false)]
    [InlineData("loading", true)]
    public void Write_InvalidLoadingScene_Rejects(string loadingId, bool duplicate) {
        PlatformBuildManifest manifest = CreateManifest("menu", duplicate ? ["menu", "loading", "loading"] : ["menu", "loading"]);
        manifest.LoadingScreenSceneId = loadingId;
        Assert.Throws<InvalidOperationException>(() => new PlayStation1StartupManifestWriter().Write(OutputRoot("invalid-loading"), manifest));
    }

    /// <summary>Retains direct startup without an overlay and accepts a selected packaged loading scene with a custom budget.</summary>
    /// <param name="loadingId">Empty for legacy direct boot or a packaged loading scene ID.</param>
    [Theory]
    [InlineData("")]
    [InlineData("loading")]
    [InlineData("menu")]
    public void Write_ValidPolicy_PreservesCatalogWithoutProducingSharedManifest(string loadingId) {
        PlatformBuildManifest manifest = CreateManifest("menu", ["menu", "loading"]);
        manifest.SceneLoadingBudgetMilliseconds = 3.5d;
        manifest.LoadingScreenSceneId = loadingId;
        string root = OutputRoot(loadingId.Length == 0 ? "direct" : loadingId);
        string header = new PlayStation1StartupManifestWriter().Write(root, manifest);
        Assert.True(File.Exists(header));
        string source = File.ReadAllText(Path.Combine(root, "ps1_runtime_scene_catalog.cpp"));
        Assert.Contains("kStartupSceneId[] = \"menu\"", source);
        Assert.Contains("{\"loading\", \"cooked/scenes/loading.hasset\"}", source);
        Assert.DoesNotContain("he_get_runtime_scene_loading_budget_milliseconds", source);
        Assert.DoesNotContain("he_get_runtime_loading_screen_scene_id", source);
        Assert.False(File.Exists(Path.Combine(root, "runtime", "runtime_startup_manifest.cpp")));
    }

    /// <summary>Rejects a startup ID outside the final catalog even when the loading overlay is valid.</summary>
    [Fact]
    public void Write_MissingStartup_Rejects() {
        PlatformBuildManifest manifest = CreateManifest("missing", ["menu", "loading"]);
        manifest.LoadingScreenSceneId = "loading";
        Assert.Throws<InvalidOperationException>(() => new PlayStation1StartupManifestWriter().Write(OutputRoot("invalid-startup"), manifest));
    }

    /// <summary>Requires a loading scene's own cooked artifact instead of incorrectly mapping the only startup artifact to every scene.</summary>
    [Fact]
    public void Write_LoadingSceneWithoutCookedArtifact_Rejects() {
        PlatformBuildManifest manifest = new(1, "loading-policy-test", "1", "1", "ps1", "1", "menu",
            [new PlatformBuildScene("menu", "menu", "scenes/menu.helen", [], []), new PlatformBuildScene("loading", "loading", "scenes/loading.helen", [], [])],
            [], [new PlatformBuildArtifact("cooked/scenes/menu.hasset", "menu", "sha256:test", "scene", "shared")],
            [], [], new PlatformContainerWritePlan("ps1", []), [], new PlatformBuildRuntimeFeatureManifest([]));
        manifest.LoadingScreenSceneId = "loading";
        Assert.Throws<InvalidOperationException>(() => new PlayStation1StartupManifestWriter().Write(OutputRoot("uncooked-loading"), manifest));
    }

    /// <summary>Builds a resolved manifest with one cooked artifact per scene so tests exercise startup policy rather than cooking.</summary>
    /// <param name="startupId">Requested boot scene.</param>
    /// <param name="sceneIds">Operational scene identifiers in the packaged catalog.</param>
    /// <returns>Manifest consumed by the PS1 catalog writer.</returns>
    static PlatformBuildManifest CreateManifest(string startupId, string[] sceneIds) {
        return new PlatformBuildManifest(1, "loading-policy-test", "1", "1", "ps1", "1", startupId,
            sceneIds.Select(id => new PlatformBuildScene(id, id, "scenes/" + id + ".helen", [], [])).ToArray(),
            [], sceneIds.Distinct(StringComparer.Ordinal).Select(id => new PlatformBuildArtifact("cooked/scenes/" + id + ".hasset", id, "sha256:test", "scene", "shared")).ToArray(),
            [], [], new PlatformContainerWritePlan("ps1", []), [], new PlatformBuildRuntimeFeatureManifest([]));
    }

    /// <summary>Uses visible fixed workspace outputs for the focused catalog fixtures.</summary>
    /// <param name="caseId">Distinct validation case.</param>
    /// <returns>Generated fixture path beneath the PS1 loading test directory.</returns>
    static string OutputRoot(string caseId) => Path.Combine("C:/dev/helworks/builds/helengine-ps1/menu-20261007/style-final/startup-loading-tests", caseId);
}
