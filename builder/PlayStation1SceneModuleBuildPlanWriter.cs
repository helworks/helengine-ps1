using helengine.baseplatform.Manifest;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace helengine.ps1.builder;

/// <summary>Completes editor code-partition inputs with the actual staged cooked assets and source paths used by the native PS1 build.</summary>
public sealed class PlayStation1SceneModuleBuildPlanWriter {
    /// <summary>Rebases source identities to the final native cache and authenticates every scene preload path against staged bytes.</summary>
    /// <param name="generatedRoot">Final generated Core tree passed to the native builder.</param>
    /// <param name="manifest">Authoritative cooked scene selection.</param>
    /// <param name="stagedAssets">All files staged by the official content stager.</param>
    /// <returns>Source-plan path, or empty when scene code overlays were not selected.</returns>
    public string WriteSourcePlan(string generatedRoot, PlatformBuildManifest manifest, IReadOnlyList<PlayStation1StagedAsset> stagedAssets) {
        string path = Path.Combine(generatedRoot, "scene-modules", "source-plan.json");
        if (!File.Exists(path)) return string.Empty;
        JsonObject plan = JsonNode.Parse(File.ReadAllText(path)).AsObject();
        if (plan["numericPolicy"]?.GetValue<string>() != manifest.NumericPolicy.Identity) throw new InvalidOperationException("Scene source-plan numeric policy differs from build manifest.");
        string previousRoot = plan["generatedCoreRoot"].GetValue<string>();
        RebaseSourcePaths(plan, previousRoot, generatedRoot);
        plan["generatedCoreRoot"] = Path.GetFullPath(generatedRoot);
        JsonArray candidates = plan["candidates"].AsArray();
        HashSet<string> existingSources = candidates.Select(candidate => Path.GetFullPath(candidate["sourcePath"].GetValue<string>())).ToHashSet(StringComparer.OrdinalIgnoreCase);
        foreach (string source in Directory.EnumerateFiles(generatedRoot, "*.cpp", SearchOption.AllDirectories).OrderBy(value => value, StringComparer.Ordinal)) {
            string relative = Path.GetRelativePath(generatedRoot, source).Replace('\\', '/');
            if (relative.StartsWith("scene-modules/", StringComparison.Ordinal) || Path.GetFileName(source) == "helengine_core_unity.cpp" ||
                Path.GetFileName(source) == "generated_unity.cpp" ||
                Path.GetFileName(source) == "ps1_runtime_scene_catalog.cpp" || !existingSources.Add(Path.GetFullPath(source))) continue;
            // Late runtime units remain candidates. Native host imports decide whether they
            // belong to the kernel; merely being emitted after the editor plan is not a root.
            candidates.Add(new JsonObject { ["sourcePath"] = Path.GetFullPath(source), ["ownerModuleId"] = "unassigned" });
        }
        Dictionary<string, PlayStation1StagedAsset> stagedByPath = stagedAssets.ToDictionary(asset => asset.RelativePath, StringComparer.Ordinal);
        JsonArray assets = new();
        Dictionary<string, long> sizes = new(StringComparer.Ordinal);
        foreach (PlayStation1StagedAsset asset in stagedAssets.OrderBy(asset => asset.RelativePath, StringComparer.Ordinal)) {
            long length = new FileInfo(asset.SourcePath).Length;
            sizes.Add(asset.RelativePath, length);
            using FileStream stream = File.OpenRead(asset.SourcePath);
            assets.Add(new JsonObject {
                ["relativePath"] = asset.RelativePath, ["byteLength"] = length,
                ["sha256"] = Convert.ToHexString(SHA256.HashData(stream))
            });
        }
        plan["assets"] = assets;
        Dictionary<string, string[]> dependenciesByScene = manifest.Scenes.ToDictionary(scene => scene.SceneId,
            scene => JsonSerializer.Deserialize<string[]>(scene.ResolvedMetadata.Single(pair => pair.Key == "cooked-dependency-paths").Value)
                .Select(PlayStation1ContentStager.NormalizeRelativePath).Distinct(StringComparer.Ordinal).OrderBy(value => value, StringComparer.Ordinal).ToArray(), StringComparer.Ordinal);
        string[] persistentPaths = string.IsNullOrEmpty(manifest.LoadingScreenSceneId) ? [] : dependenciesByScene[manifest.LoadingScreenSceneId];
        string[] bootstrapPaths = dependenciesByScene[manifest.StartupSceneId].Concat(persistentPaths).Distinct(StringComparer.Ordinal).OrderBy(value => value, StringComparer.Ordinal).ToArray();
        plan["bootstrapAssetPaths"] = new JsonArray(bootstrapPaths.Select(value => JsonValue.Create(value)).ToArray());
        plan["persistentAssetPaths"] = new JsonArray(persistentPaths.Select(value => JsonValue.Create(value)).ToArray());
        long peak = 0;
        foreach (JsonObject scene in plan["scenes"].AsArray().Select(node => node.AsObject())) {
            string sceneId = scene["sceneId"].GetValue<string>();
            string[] dependencies = dependenciesByScene[sceneId];
            long bytes = 0;
            foreach (string dependency in dependencies) {
                if (!stagedByPath.ContainsKey(dependency)) throw new InvalidOperationException($"Scene '{sceneId}' preload dependency '{dependency}' is not a staged runtime artifact.");
                bytes = checked(bytes + sizes[dependency]);
            }
            scene["assetPaths"] = new JsonArray(dependencies.Select(value => JsonValue.Create(value)).ToArray());
            scene["assetByteLength"] = bytes;
            scene["cacheMetadataByteLength"] = checked(dependencies.Length * 32L);
            scene["dmaWindowByteLength"] = 2048;
            string[] activePaths = dependencies.Concat(persistentPaths).Distinct(StringComparer.Ordinal).ToArray();
            long activeBytes = 0;
            foreach (string activePath in activePaths) activeBytes = checked(activeBytes + sizes[activePath]);
            scene["activeCacheByteLength"] = checked(activeBytes + activePaths.Length * 32L + 2048L);
            peak = Math.Max(peak, scene["activeCacheByteLength"].GetValue<long>());
        }
        plan["maximumSceneCacheByteLength"] = peak;
        plan["nativeKernelPolicy"] = "make-nm-v1";
        plan["defines"] = new JsonArray("HELENGINE_CODE_MODULE_ID=\"\"");
        File.WriteAllText(path, plan.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
        return path;
    }

    /// <summary>Emits each overlay's owning factories after actual SDK symbol partitioning, using module-scoped registration and explicit unregister.</summary>
    /// <param name="generatedRoot">Source tree containing the original typed deserializers.</param>
    /// <param name="sourcePlan">Final source and factory manifest.</param>
    /// <param name="partition">Symbol-based resident and overlay assignments.</param>
    /// <returns>Partition manifest path consumed by the official native build wrapper.</returns>
    public string WritePartition(string generatedRoot, JsonObject sourcePlan, JsonObject partition) {
        string root = Path.Combine(generatedRoot, "scene-modules");
        foreach (JsonObject module in partition["modules"].AsArray().Select(node => node.AsObject())) {
            string moduleId = module["moduleId"].GetValue<string>();
            string[] sources = module["sourcePaths"].AsArray().Select(node => node.GetValue<string>()).ToArray();
            JsonObject[] factories = sourcePlan["componentFactories"].AsArray().Select(node => node.AsObject())
                .Where(factory => sources.Contains(factory["deserializerSourcePath"].GetValue<string>(), StringComparer.OrdinalIgnoreCase)).ToArray();
            StringBuilder cpp = new("// Generated by helengine.ps1.builder; do not edit.\n#include \"Core.hpp\"\n#include \"RuntimeComponentRegistry.hpp\"\n");
            foreach (JsonObject factory in factories) cpp.Append("#include \"").Append(factory["deserializerClassName"].GetValue<string>()).AppendLine(".hpp\"");
            bool physics = sources.Any(source => Path.GetFileName(source) == "HelPhysicsRuntimeComponentRegistration.cpp");
            if (physics) cpp.AppendLine("#include \"HelPhysicsRuntimeComponentRegistration.hpp\"");
            cpp.AppendLine("extern \"C\" void he_ps1_module_register(::Core* core, ::RuntimeComponentRegistry* registry) {");
            cpp.AppendLine("    (void)core;");
            foreach (JsonObject factory in factories) cpp.Append("    registry->RegisterForModule(\"").Append(moduleId).Append("\", new ::").Append(factory["deserializerClassName"].GetValue<string>()).AppendLine("());");
            if (physics) cpp.AppendLine("    ::HelPhysicsRuntimeComponentRegistration::Register(core);");
            cpp.AppendLine("}");
            cpp.AppendLine("extern \"C\" void he_ps1_module_unregister(::Core* core, ::RuntimeComponentRegistry* registry) {");
            cpp.AppendLine("    (void)core; (void)registry;");
            if (physics) cpp.AppendLine("    core->ReleasePhysicsRuntimeRegistration();");
            cpp.AppendLine("}");
            string registrationPath = Path.Combine(root, moduleId + ".cpp");
            File.WriteAllText(registrationPath, cpp.ToString(), new UTF8Encoding(false));
            module["registrationSourcePath"] = registrationPath;
        }
        partition["numericPolicy"] = sourcePlan["numericPolicy"].DeepClone();
        partition["generatedCoreRoot"] = Path.GetFullPath(generatedRoot);
        partition["sourcePlanPath"] = Path.Combine(root, "source-plan.json");
        partition["assets"] = sourcePlan["assets"].DeepClone();
        partition["bootstrapSceneIds"] = sourcePlan["bootstrapSceneIds"].DeepClone();
        partition["bootstrapAssetPaths"] = sourcePlan["bootstrapAssetPaths"].DeepClone();
        partition["persistentAssetPaths"] = sourcePlan["persistentAssetPaths"].DeepClone();
        partition["maximumSceneCacheByteLength"] = sourcePlan["maximumSceneCacheByteLength"].DeepClone();
        foreach (JsonObject scene in partition["scenes"].AsArray().Select(node => node.AsObject())) {
            JsonObject source = sourcePlan["scenes"].AsArray().Select(node => node.AsObject()).Single(node => node["sceneId"].GetValue<string>() == scene["sceneId"].GetValue<string>());
            scene["cookedRelativePath"] = source["cookedRelativePath"].DeepClone();
            scene["assetPaths"] = source["assetPaths"].DeepClone();
            scene["assetByteLength"] = source["assetByteLength"].DeepClone();
        }
        string output = Path.Combine(root, "partition-plan.json");
        File.WriteAllText(output, partition.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
        return output;
    }

    /// <summary>Rebases only source identity fields and rejects paths outside the original generated root.</summary>
    /// <param name="node">Plan node to visit.</param>
    /// <param name="previousRoot">Editor intermediate source root.</param>
    /// <param name="finalRoot">Final cache source root.</param>
    static void RebaseSourcePaths(JsonNode node, string previousRoot, string finalRoot) {
        if (node is JsonObject obj) {
            foreach (string key in obj.Select(pair => pair.Key).ToArray()) {
                if (key == "sourcePath" || key == "componentSourcePath" || key == "deserializerSourcePath") {
                    obj[key] = RebasePath(obj[key].GetValue<string>(), previousRoot, finalRoot);
                } else if (key == "residentRoots" || key == "rootSources") {
                    obj[key] = new JsonArray(obj[key].AsArray().Select(value => JsonValue.Create(RebasePath(value.GetValue<string>(), previousRoot, finalRoot))).ToArray());
                } else if (obj[key] is JsonArray || obj[key] is JsonObject) RebaseSourcePaths(obj[key], previousRoot, finalRoot);
            }
        } else if (node is JsonArray array) {
            foreach (JsonNode item in array) if (item is JsonObject || item is JsonArray) RebaseSourcePaths(item, previousRoot, finalRoot);
        }
    }

    /// <summary>Resolves an existing source file under the final cache while refusing traversal or stale source identities.</summary>
    /// <param name="path">Previous absolute generated source identity.</param>
    /// <param name="previousRoot">Source root written into the editor plan.</param>
    /// <param name="finalRoot">Native cache root used by this build.</param>
    /// <returns>Absolute existing final source path.</returns>
    static string RebasePath(string path, string previousRoot, string finalRoot) {
        string relative = Path.GetRelativePath(previousRoot, path);
        if (Path.IsPathRooted(relative) || relative == ".." || relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal)) throw new InvalidOperationException("Scene module source escapes generated root: " + path);
        string result = Path.GetFullPath(Path.Combine(finalRoot, relative));
        if (!File.Exists(result)) throw new FileNotFoundException("Scene module source was not synchronized into the native cache.", result);
        return result;
    }
}
