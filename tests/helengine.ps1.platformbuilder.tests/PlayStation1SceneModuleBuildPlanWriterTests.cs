using helengine.baseplatform.Manifest;
using System.Text.Json.Nodes;

namespace helengine.ps1.builder.tests;

/// <summary>Verifies that scene caches are measured from the exact staged runtime bytes instead of the whole CD archive or sector-rounded payloads.</summary>
public sealed class PlayStation1SceneModuleBuildPlanWriterTests {
    /// <summary>Records only selected dependencies, authenticates staged bytes, and refuses any preload path missing from the actual package.</summary>
    [Fact]
    public void WriteSourcePlan_SelectedCookedPaths_UsesExactBytesAndRejectsMissingAssets() {
        string root = "C:/temp/numeric-precision/ps1/content-plan-fixture";
        Directory.CreateDirectory(Path.Combine(root, "scene-modules"));
        File.WriteAllText(Path.Combine(root, "generated_unity.cpp"), "#include \"ActualType.cpp\"");
        File.WriteAllText(Path.Combine(root, "helengine_core_unity.cpp"), "#include \"ActualType.cpp\"");
        File.WriteAllText(Path.Combine(root, "ActualType.cpp"), "int actual_type = 1;");
        string selected = Path.Combine(root, "selected.hasset");
        string unused = Path.Combine(root, "unused.hasset");
        File.WriteAllBytes(selected, new byte[3]);
        File.WriteAllBytes(unused, new byte[4096]);
        JsonObject plan = new() {
            ["numericPolicy"] = "real-v1-s32-a32",
            ["generatedCoreRoot"] = root,
            ["candidates"] = new JsonArray(), ["residentRoots"] = new JsonArray(),
            ["scenes"] = new JsonArray(new JsonObject { ["sceneId"] = "menu" })
        };
        string path = Path.Combine(root, "scene-modules", "source-plan.json");
        File.WriteAllText(path, plan.ToJsonString());
        PlatformBuildScene scene = new("menu", "Menu", "scenes/menu.helen", [],
            [new("cooked-dependency-paths", "[\"selected.hasset\"]")]);
        PlatformBuildManifest manifest = new(1, "cache-test", "1", "1", "ps1", "1", "menu", [scene], [], [], [], [],
            new PlatformContainerWritePlan("ps1", []), [], new PlatformBuildRuntimeFeatureManifest([]));
        PlayStation1SceneModuleBuildPlanWriter writer = new();
        byte[] originalPlan = File.ReadAllBytes(path);
        plan["numericPolicy"] = "real-v1-s64-a64";
        File.WriteAllText(path, plan.ToJsonString());
        Assert.Throws<InvalidOperationException>(() => writer.WriteSourcePlan(root, manifest, [new("selected.hasset", selected)]));
        Assert.Equal("real-v1-s64-a64", JsonNode.Parse(File.ReadAllText(path))["numericPolicy"].GetValue<string>());
        File.WriteAllBytes(path, originalPlan);
        writer.WriteSourcePlan(root, manifest, [new("selected.hasset", selected), new("unused.hasset", unused)]);
        JsonObject result = JsonNode.Parse(File.ReadAllText(path)).AsObject();
        Assert.Single(result["candidates"].AsArray());
        Assert.Equal(Path.GetFullPath(Path.Combine(root, "ActualType.cpp")), result["candidates"][0]["sourcePath"].GetValue<string>());
        Assert.Equal("unassigned", result["candidates"][0]["ownerModuleId"].GetValue<string>());
        Assert.Empty(result["residentRoots"].AsArray());
        Assert.Equal("make-nm-v1", result["nativeKernelPolicy"].GetValue<string>());
        Assert.Equal(2, result["assets"].AsArray().Count);
        Assert.Equal(3L, result["scenes"][0]["assetByteLength"].GetValue<long>());
        Assert.Equal(2083L, result["maximumSceneCacheByteLength"].GetValue<long>());
        Assert.Throws<InvalidOperationException>(() => writer.WriteSourcePlan(root, manifest, [new("unused.hasset", unused)]));
    }
}
