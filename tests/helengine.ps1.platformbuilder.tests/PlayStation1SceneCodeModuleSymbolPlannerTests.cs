using System.Text.Json.Nodes;

namespace helengine.ps1.builder.tests;

/// <summary>Checks actual symbol reachability rather than header include reachability when separating resident and scene code.</summary>
public sealed class PlayStation1SceneCodeModuleSymbolPlannerTests {
    /// <summary>Groups mutually dependent units once, shares common dependencies, and promotes imports reachable from boot to resident.</summary>
    [Fact]
    public void Plan_SymbolClosures_ShareCyclesAndKeepBootResident() {
        string root = Path.GetFullPath("C:/dev/helworks/builds/helengine-ps1/menu-20261007/scene-modules/buildgraph/planner-fixture");
        JsonObject plan = new() {
            ["residentRoots"] = new JsonArray(Path.Combine(root, "Core.cpp")),
            ["scenes"] = new JsonArray(
                Scene("menu", root, "Menu.cpp"), Scene("one", root, "First.cpp"), Scene("two", root, "Second.cpp"))
        };
        JsonObject symbols = new() { ["units"] = new JsonArray(
            Unit(root, "Core.cpp", ["core"], ["menu"]),
            Unit(root, "Menu.cpp", ["menu"], ["core"]),
            Unit(root, "First.cpp", ["first"], ["shared", "cycle"]),
            Unit(root, "Cycle.cpp", ["cycle"], ["first"]),
            Unit(root, "Second.cpp", ["second"], ["shared"]),
            Unit(root, "Shared.cpp", ["shared"], ["core", "sdk_external"]),
            Unit(root, "Unused.cpp", ["unused"], [])) };
        JsonObject result = new PlayStation1SceneCodeModuleSymbolPlanner().Plan(plan, symbols);
        Assert.Equal(2, result["residentSources"].AsArray().Count);
        Assert.Equal(3, result["modules"].AsArray().Count);
        JsonObject owners = result["unitOwners"].AsObject();
        Assert.Equal(owners[Path.Combine(root, "First.cpp")].GetValue<string>(), owners[Path.Combine(root, "Cycle.cpp")].GetValue<string>());
        Assert.DoesNotContain(Path.Combine(root, "Unused.cpp"), owners.Select(pair => pair.Key));
        Assert.Empty(result["scenes"].AsArray().Single(scene => scene["sceneId"].GetValue<string>() == "menu")["requiredCodeModuleIds"].AsArray());
        Assert.All(result["modules"].AsArray(), module => Assert.DoesNotContain("resident", module["dependencyModuleIds"].AsArray().Select(node => node.GetValue<string>())));
        Assert.Equal(new[] { "core", "sdk_external" }, result["residentImportSymbols"].AsArray().Select(node => node.GetValue<string>()).ToArray());
        JsonObject[] modules = result["modules"].AsArray().Select(node => node.AsObject()).ToArray();
        Assert.Equal(2, result["slots"].AsArray().Count);
        foreach (JsonObject scene in result["scenes"].AsArray().Select(node => node.AsObject())) {
            string[] required = scene["requiredCodeModuleIds"].AsArray().Select(node => node.GetValue<string>()).ToArray();
            int[] slots = modules.Where(module => required.Contains(module["moduleId"].GetValue<string>())).Select(module => module["slot"].GetValue<int>()).ToArray();
            Assert.Equal(slots.Length, slots.Distinct().Count());
        }
    }

    /// <summary>Refuses a stale or incomplete SDK symbol report instead of silently losing a scene's constructor.</summary>
    [Fact]
    public void Plan_MissingSceneObject_RejectsIncompleteSdkEvidence() {
        JsonObject plan = new() { ["residentRoots"] = new JsonArray(), ["scenes"] = new JsonArray(Scene("missing", "C:/dev/helworks", "Missing.cpp")) };
        Assert.Throws<InvalidOperationException>(() => new PlayStation1SceneCodeModuleSymbolPlanner().Plan(plan, new JsonObject { ["units"] = new JsonArray() }));
    }

    /// <summary>Roots the actual native host's generated providers and their dependencies while leaving unrelated Core candidates eligible for overlays.</summary>
    [Fact]
    public void Plan_NativeImports_KeepOnlyMandatoryProvidersResident() {
        string root = Path.GetFullPath("C:/dev/helworks/builds/helengine-ps1/menu-20261007/scene-modules/buildgraph/kernel-planner-fixture");
        JsonObject plan = new() {
            ["residentRoots"] = new JsonArray(Path.Combine(root, "Menu.cpp")),
            ["scenes"] = new JsonArray(Scene("menu", root, "Menu.cpp"), Scene("game", root, "Physics.cpp"))
        };
        JsonObject report = new() {
            ["residentNativeImportSymbols"] = new JsonArray("read_asset", "sdk_external"),
            ["units"] = new JsonArray(
                Unit(root, "Menu.cpp", ["menu"], []),
                Unit(root, "AssetReader.cpp", ["read_asset"], ["stream"]),
                Unit(root, "Stream.cpp", ["stream"], []),
                Unit(root, "Physics.cpp", ["physics"], ["read_asset"]),
                Unit(root, "UnrelatedCoreCodec.cpp", ["unused_codec"], []))
        };
        JsonObject result = new PlayStation1SceneCodeModuleSymbolPlanner().Plan(plan, report);
        Assert.Equal(new[] { Path.Combine(root, "AssetReader.cpp"), Path.Combine(root, "Menu.cpp"), Path.Combine(root, "Stream.cpp") },
            result["residentSources"].AsArray().Select(node => node.GetValue<string>()).ToArray());
        Assert.Single(result["nativeResidentRootSources"].AsArray());
        Assert.Single(result["modules"].AsArray());
        Assert.Equal(new[] { "read_asset" }, result["residentImportSymbols"].AsArray().Select(node => node.GetValue<string>()).ToArray());
        Assert.Null(result["unitOwners"][Path.Combine(root, "UnrelatedCoreCodec.cpp")]);
    }

    /// <summary>Preserves the same preferred bootstrap provider when a native import also has a weak definition in gameplay.</summary>
    [Fact]
    public void Plan_NativeWeakImport_PrefersAlreadyResidentProvider() {
        string root = Path.GetFullPath("C:/dev/helworks/builds/helengine-ps1/menu-20261007/scene-modules/buildgraph/kernel-weak-fixture");
        JsonObject plan = new() {
            ["residentRoots"] = new JsonArray(Path.Combine(root, "ZResident.cpp")),
            ["scenes"] = new JsonArray(Scene("game", root, "AGame.cpp"))
        };
        JsonObject report = new() {
            ["residentNativeImportSymbols"] = new JsonArray("shared_weak"),
            ["units"] = new JsonArray(Unit(root, "ZResident.cpp", ["shared_weak"], []), Unit(root, "AGame.cpp", ["game", "shared_weak"], []))
        };
        JsonObject result = new PlayStation1SceneCodeModuleSymbolPlanner().Plan(plan, report);
        Assert.Equal(Path.Combine(root, "ZResident.cpp"), result["nativeResidentRootSources"].AsArray().Single().GetValue<string>());
        Assert.Single(result["modules"].AsArray());
    }

    /// <summary>Coalesces ten simultaneously required groups into eight arenas while keeping unrelated scenes unloadable and every source/factory in one physical group.</summary>
    [Fact]
    public void Plan_TenCoRequiredGroups_CoalescesWithoutPromotingGameplay() {
        string root = Path.GetFullPath("C:/dev/helworks/builds/helengine-ps1/menu-20261007/scene-modules/native/slot-tests/fixture");
        JsonArray units = new();
        JsonArray scenes = new(Scene("menu", root, "Resident.cpp"));
        JsonArray allRoots = new();
        for (int i = 0; i < 10; i++) {
            string source = "Game" + i + ".cpp";
            units.Add(Unit(root, source, ["game" + i], []));
            scenes.Add(Scene("game" + i, root, source));
            allRoots.Add(Path.Combine(root, source));
        }
        units.Add(Unit(root, "Resident.cpp", ["resident"], []));
        scenes.Add(new JsonObject { ["sceneId"] = "all", ["rootSources"] = allRoots });
        JsonObject plan = new() { ["residentRoots"] = new JsonArray(Path.Combine(root, "Resident.cpp")), ["scenes"] = scenes };
        JsonObject result = new PlayStation1SceneCodeModuleSymbolPlanner().Plan(plan, new JsonObject { ["units"] = units });
        Assert.Equal(10, result["slotCoalescing"]["originalModuleCount"].GetValue<int>());
        Assert.Equal(8, result["modules"].AsArray().Count);
        Assert.Equal(8, result["slots"].AsArray().Count);
        Assert.Single(result["residentSources"].AsArray());
        Assert.Equal(10, result["modules"].AsArray().Sum(module => module["sourcePaths"].AsArray().Count));
        Assert.Equal(10, result["modules"].AsArray().SelectMany(module => module["sourcePaths"].AsArray().Select(source => source.GetValue<string>())).Distinct().Count());
        Assert.Empty(result["scenes"].AsArray().Single(scene => scene["sceneId"].GetValue<string>() == "menu")["requiredCodeModuleIds"].AsArray());
        Assert.All(result["scenes"].AsArray().Where(scene => scene["sceneId"].GetValue<string>().StartsWith("game")), scene => Assert.Single(scene["requiredCodeModuleIds"].AsArray()));
        Assert.Equal(8, result["scenes"].AsArray().Where(scene => scene["sceneId"].GetValue<string>().StartsWith("game"))
            .Select(scene => scene["requiredCodeModuleIds"].AsArray().Single().GetValue<string>()).Distinct().Count());
        Assert.Equal(result.ToJsonString(), new PlayStation1SceneCodeModuleSymbolPlanner().Plan(plan, new JsonObject { ["units"] = units.DeepClone() }).ToJsonString());
        AssertSceneSlotAndDependencySafety(result);
    }

    /// <summary>Contracts an induced dependency cycle across a merged alternate path, retaining each source's physical owner exactly once.</summary>
    [Fact]
    public void PhysicalCoalescing_InducedDependencyCycle_ContractsWholeScc() {
        Dictionary<string, List<string>> groups = new(StringComparer.Ordinal) { ["a"] = ["A", "C"], ["b"] = ["B"], ["unrelated"] = ["D"] };
        Dictionary<string, string> owners = new(StringComparer.OrdinalIgnoreCase) { ["A"] = "a", ["B"] = "b", ["C"] = "a", ["D"] = "unrelated" };
        Dictionary<string, HashSet<string>> edges = new(StringComparer.OrdinalIgnoreCase) {
            ["A"] = new(["B", "C"]), ["B"] = new(["C"]), ["C"] = new(), ["D"] = new()
        };
        System.Reflection.MethodInfo collapse = typeof(PlayStation1SceneCodeModuleSymbolPlanner).GetMethod("CollapseDependencyCycles",
            System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Static);
        Assert.NotNull(collapse);
        collapse.Invoke(null, [groups, owners, edges]);
        Assert.Equal(2, groups.Count);
        Assert.Equal(new[] { "A", "B", "C" }, groups["a"].Order().ToArray());
        Assert.Equal("a", owners["B"]);
        Assert.Equal("unrelated", owners["D"]);
        Assert.Equal(4, groups.Values.SelectMany(sources => sources).Distinct().Count());
    }

    /// <summary>Checks final transitive dependencies and physical slots, including additional dependencies introduced by grouping.</summary>
    /// <param name="result">Final partition returned by the production planner.</param>
    static void AssertSceneSlotAndDependencySafety(JsonObject result) {
        Dictionary<string, JsonObject> modules = result["modules"].AsArray().ToDictionary(module => module["moduleId"].GetValue<string>(), module => module.AsObject());
        foreach (JsonNode scene in result["scenes"].AsArray()) {
            string[] ids = scene["requiredCodeModuleIds"].AsArray().Select(id => id.GetValue<string>()).ToArray();
            Assert.Equal(ids.Length, ids.Select(id => modules[id]["slot"].GetValue<int>()).Distinct().Count());
            foreach (string id in ids) Assert.All(modules[id]["dependencyModuleIds"].AsArray(), dependency => Assert.Contains(dependency.GetValue<string>(), ids));
        }
    }

    /// <summary>Creates a scene's exact generated constructor root for the native graph fixture.</summary>
    /// <param name="id">Operational scene identity.</param>
    /// <param name="root">Fixture source root.</param>
    /// <param name="source">Generated source basename.</param>
    /// <returns>Scene root record.</returns>
    static JsonObject Scene(string id, string root, string source) => new() { ["sceneId"] = id, ["rootSources"] = new JsonArray(Path.Combine(root, source)) };

    /// <summary>Represents definitions and imports measured from one compiled MIPS translation unit.</summary>
    /// <param name="root">Fixture source root.</param>
    /// <param name="source">Generated source basename.</param>
    /// <param name="defined">Defined symbols.</param>
    /// <param name="imports">Undefined symbols.</param>
    /// <returns>Native unit record.</returns>
    static JsonObject Unit(string root, string source, string[] defined, string[] imports) => new() {
        ["sourcePath"] = Path.Combine(root, source),
        ["definedSymbols"] = new JsonArray(defined.Select(value => JsonValue.Create(value)).ToArray()),
        ["undefinedSymbols"] = new JsonArray(imports.Select(value => JsonValue.Create(value)).ToArray())
    };
}
