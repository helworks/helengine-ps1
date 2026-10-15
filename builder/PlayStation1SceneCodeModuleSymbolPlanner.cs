using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace helengine.ps1.builder;

/// <summary>Partitions separately compiled native units by actual MIPS symbol imports and the scenes that keep each unit reachable.</summary>
public sealed class PlayStation1SceneCodeModuleSymbolPlanner {
    /// <summary>Computes resident closure and shared scene dependency groups, rejecting unresolved generated roots and resident-to-overlay edges.</summary>
    /// <param name="sourcePlan">Authoritative scene/component source roots emitted by the editor.</param>
    /// <param name="symbolReport">Actual SDK candidate object definitions and undefined mangled symbols.</param>
    /// <returns>Native module groups with exact source ownership, import symbols and per-scene module dependency lists.</returns>
    public JsonObject Plan(JsonObject sourcePlan, JsonObject symbolReport) {
        Dictionary<string, JsonObject> units = symbolReport["units"].AsArray().ToDictionary(node => Path.GetFullPath(node["sourcePath"].GetValue<string>()), node => node.AsObject(), StringComparer.OrdinalIgnoreCase);
        Dictionary<string, List<string>> providers = new(StringComparer.Ordinal);
        foreach (KeyValuePair<string, JsonObject> unit in units) {
            foreach (JsonNode symbolNode in unit.Value["definedSymbols"].AsArray()) {
                string symbol = symbolNode.GetValue<string>();
                if (!providers.TryGetValue(symbol, out List<string> sources)) providers.Add(symbol, sources = []);
                sources.Add(unit.Key);
            }
        }
        HashSet<string> residentSeeds = ReadPaths(sourcePlan["residentRoots"].AsArray());
        string[] nativeImports = symbolReport["residentNativeImportSymbols"] is JsonArray nativeSymbols
            ? nativeSymbols.Select(symbol => symbol.GetValue<string>()).Distinct(StringComparer.Ordinal).OrderBy(symbol => symbol, StringComparer.Ordinal).ToArray()
            : [];
        HashSet<string> nativeRoots = new(StringComparer.OrdinalIgnoreCase);
        foreach (string symbol in nativeImports) {
            if (!providers.TryGetValue(symbol, out List<string> sources)) continue;
            nativeRoots.Add(sources.OrderBy(source => residentSeeds.Contains(source) ? 0 : 1).ThenBy(source => source, StringComparer.Ordinal).First());
        }
        residentSeeds.UnionWith(nativeRoots);
        Dictionary<string, HashSet<string>> edges = new(StringComparer.OrdinalIgnoreCase);
        foreach (KeyValuePair<string, JsonObject> unit in units) {
            HashSet<string> imports = new(StringComparer.OrdinalIgnoreCase);
            foreach (JsonNode symbolNode in unit.Value["undefinedSymbols"].AsArray()) {
                if (!providers.TryGetValue(symbolNode.GetValue<string>(), out List<string> sources)) continue;
                // Header template and RTTI weak definitions can appear in more than one object;
                // prefer their already-resident copy rather than making every inclusion a dependency.
                string target = sources.OrderBy(source => residentSeeds.Contains(source) ? 0 : 1).ThenBy(source => source, StringComparer.Ordinal).First();
                if (target != unit.Key) imports.Add(target);
            }
            edges.Add(unit.Key, imports);
        }
        HashSet<string> resident = Closure(residentSeeds, edges);
        Dictionary<string, HashSet<string>> scenesByUnit = new(StringComparer.OrdinalIgnoreCase);
        Dictionary<string, HashSet<string>> closureByScene = new(StringComparer.Ordinal);
        foreach (JsonObject scene in sourcePlan["scenes"].AsArray().Select(node => node.AsObject())) {
            string sceneId = scene["sceneId"].GetValue<string>();
            HashSet<string> closure = Closure(ReadPaths(scene["rootSources"].AsArray()), edges);
            closure.ExceptWith(resident);
            closureByScene.Add(sceneId, closure);
            foreach (string source in closure) {
                if (!scenesByUnit.TryGetValue(source, out HashSet<string> ids)) scenesByUnit.Add(source, ids = new(StringComparer.Ordinal));
                ids.Add(sceneId);
            }
        }
        Dictionary<string, string> owners = resident.ToDictionary(source => source, _ => "resident", StringComparer.OrdinalIgnoreCase);
        Dictionary<string, List<string>> groupedSources = new(StringComparer.Ordinal);
        foreach (KeyValuePair<string, HashSet<string>> pair in scenesByUnit) {
            string signature = string.Join("\n", pair.Value.OrderBy(id => id, StringComparer.Ordinal));
            string moduleId = "scene_" + Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(signature))).Substring(0, 12).ToLowerInvariant();
            owners.Add(pair.Key, moduleId);
            if (!groupedSources.TryGetValue(moduleId, out List<string> group)) groupedSources.Add(moduleId, group = []);
            group.Add(pair.Key);
        }
        int originalGroupCount = groupedSources.Count;
        CoalesceForSlotLimit(groupedSources, owners, closureByScene, edges);
        Dictionary<string, HashSet<string>> moduleEdges = ModuleEdges(groupedSources, owners, edges);
        JsonArray modules = new();
        foreach (KeyValuePair<string, List<string>> group in groupedSources.OrderBy(pair => pair.Key, StringComparer.Ordinal)) {
            HashSet<string> dependencies = new(StringComparer.Ordinal);
            HashSet<string> imports = new(StringComparer.Ordinal);
            foreach (string source in group.Value) {
                foreach (string target in edges[source]) {
                    string owner = owners[target];
                    if (owner != "resident" && owner != group.Key) dependencies.Add(owner);
                }
                foreach (JsonNode symbolNode in units[source]["undefinedSymbols"].AsArray()) {
                    string symbol = symbolNode.GetValue<string>();
                    if (!providers.TryGetValue(symbol, out List<string> candidates) || candidates.All(candidate => !group.Value.Contains(candidate, StringComparer.OrdinalIgnoreCase))) imports.Add(symbol);
                }
            }
            modules.Add(new JsonObject {
                ["moduleId"] = group.Key, ["sourcePaths"] = ToArray(group.Value),
                ["dependencyModuleIds"] = ToArray(dependencies), ["importSymbols"] = ToArray(imports)
            });
        }
        JsonArray scenes = new();
        foreach (KeyValuePair<string, HashSet<string>> scene in closureByScene) {
            scenes.Add(new JsonObject { ["sceneId"] = scene.Key, ["requiredCodeModuleIds"] = ToArray(Closure(scene.Value.Select(source => owners[source]).Distinct(StringComparer.Ordinal), moduleEdges)) });
        }
        foreach (string source in resident) {
            if (edges[source].Any(target => owners[target] != "resident")) throw new InvalidOperationException("Resident native unit imports unloadable code: " + source);
        }
        Dictionary<string, int> slots = AssignSlots(ModuleConflicts(groupedSources, owners, closureByScene, moduleEdges));
        foreach (JsonObject module in modules.Select(node => node.AsObject())) module["slot"] = slots[module["moduleId"].GetValue<string>()];
        HashSet<string> overlayDefinitions = new(StringComparer.Ordinal);
        foreach (string source in groupedSources.Values.SelectMany(group => group)) {
            overlayDefinitions.UnionWith(units[source]["definedSymbols"].AsArray().Select(symbol => symbol.GetValue<string>()));
        }
        string[] residentImports = modules.SelectMany(module => module["importSymbols"].AsArray().Select(symbol => symbol.GetValue<string>()))
            .Where(symbol => !overlayDefinitions.Contains(symbol)).Distinct(StringComparer.Ordinal).ToArray();
        return new JsonObject {
            ["schemaVersion"] = 1, ["residentSources"] = ToArray(resident), ["modules"] = modules, ["scenes"] = scenes,
            ["slotCoalescing"] = new JsonObject { ["maximumSlots"] = 8, ["originalModuleCount"] = originalGroupCount,
                ["finalModuleCount"] = groupedSources.Count, ["policy"] = "co-required-minimum-source-exposure" },
            ["residentImportSymbols"] = ToArray(residentImports),
            ["residentNativeImportSymbols"] = ToArray(nativeImports), ["nativeResidentRootSources"] = ToArray(nativeRoots),
            ["slots"] = new JsonArray(slots.Values.Distinct().OrderBy(index => index).Select(index => new JsonObject { ["index"] = index }).ToArray()),
            ["unitOwners"] = new JsonObject(owners.OrderBy(pair => pair.Key, StringComparer.Ordinal).Select(pair => KeyValuePair.Create<string, JsonNode>(pair.Key, JsonValue.Create(pair.Value))))
        };
    }

    /// <summary>Coalesces physical overlay groups only when the existing coloring exceeds eight arenas. The source-unit exposure heuristic is deterministic, not a RAM estimate.</summary>
    /// <param name="groups">Disjoint physical source groups, updated in place.</param>
    /// <param name="owners">Single physical owner for every included source.</param>
    /// <param name="scenes">Original generated source closure of each scene.</param>
    /// <param name="edges">Actual SDK source dependencies.</param>
    static void CoalesceForSlotLimit(Dictionary<string, List<string>> groups, Dictionary<string, string> owners,
        Dictionary<string, HashSet<string>> scenes, Dictionary<string, HashSet<string>> edges) {
        while (true) {
            Dictionary<string, HashSet<string>> dependencies = ModuleEdges(groups, owners, edges);
            Dictionary<string, HashSet<string>> conflicts = ModuleConflicts(groups, owners, scenes, dependencies);
            if (AssignSlots(conflicts).Values.All(slot => slot < 8)) return;
            Dictionary<string, HashSet<string>> usage = groups.Keys.ToDictionary(id => id, _ => new HashSet<string>(StringComparer.Ordinal), StringComparer.Ordinal);
            foreach (KeyValuePair<string, HashSet<string>> scene in scenes) {
                foreach (string id in Closure(scene.Value.Select(source => owners[source]), dependencies)) usage[id].Add(scene.Key);
            }
            string first = null;
            string second = null;
            long bestCost = long.MaxValue;
            foreach (string left in groups.Keys.OrderBy(id => id, StringComparer.Ordinal)) {
                foreach (string right in conflicts[left].OrderBy(id => id, StringComparer.Ordinal)) {
                    if (StringComparer.Ordinal.Compare(left, right) >= 0) continue;
                    long cost = checked((long)groups[left].Count * usage[right].Except(usage[left]).Count() +
                        (long)groups[right].Count * usage[left].Except(usage[right]).Count());
                    if (cost >= bestCost) continue;
                    first = left;
                    second = right;
                    bestCost = cost;
                }
            }
            if (first == null) throw new InvalidOperationException("Overlay coloring exceeds eight slots without any co-required groups to coalesce.");
            MergeGroups([first, second], groups, owners);
            // Contracting a DAG edge can create a cycle through an alternate path. Collapse
            // the complete induced SCC before computing dependencies or slot conflicts again.
            CollapseDependencyCycles(groups, owners, edges);
        }
    }

    /// <summary>Builds physical module imports from the unchanged generated SDK source edges.</summary>
    /// <param name="groups">Current physical groups.</param>
    /// <param name="owners">Current source owners.</param>
    /// <param name="edges">Actual source imports.</param>
    /// <returns>Dependency graph excluding resident code and intra-module calls.</returns>
    static Dictionary<string, HashSet<string>> ModuleEdges(Dictionary<string, List<string>> groups,
        Dictionary<string, string> owners, Dictionary<string, HashSet<string>> edges) {
        Dictionary<string, HashSet<string>> result = new(StringComparer.OrdinalIgnoreCase);
        foreach (KeyValuePair<string, List<string>> group in groups) {
            result.Add(group.Key, group.Value.SelectMany(source => edges[source]).Select(source => owners[source])
                .Where(owner => owner != "resident" && owner != group.Key).ToHashSet(StringComparer.Ordinal));
        }
        return result;
    }

    /// <summary>Uses transitive scene requirements so coalescing never assigns dependencies of a newly included source to overlapping arenas.</summary>
    /// <param name="groups">Current groups.</param>
    /// <param name="owners">Current source owners.</param>
    /// <param name="scenes">Original source closures.</param>
    /// <param name="dependencies">Current module dependency graph.</param>
    /// <returns>Undirected conflicts between all modules required together.</returns>
    static Dictionary<string, HashSet<string>> ModuleConflicts(Dictionary<string, List<string>> groups, Dictionary<string, string> owners,
        Dictionary<string, HashSet<string>> scenes, Dictionary<string, HashSet<string>> dependencies) {
        Dictionary<string, HashSet<string>> result = groups.Keys.ToDictionary(id => id, _ => new HashSet<string>(StringComparer.Ordinal), StringComparer.Ordinal);
        foreach (HashSet<string> sources in scenes.Values) {
            HashSet<string> ids = Closure(sources.Select(source => owners[source]), dependencies);
            foreach (string id in ids) result[id].UnionWith(ids.Where(other => other != id));
        }
        return result;
    }

    /// <summary>Retains the existing degree-first deterministic slot coloring for plans already within the native limit.</summary>
    /// <param name="conflicts">Physical module conflicts.</param>
    /// <returns>Nonoverlapping arena assignment.</returns>
    static Dictionary<string, int> AssignSlots(Dictionary<string, HashSet<string>> conflicts) {
        Dictionary<string, int> result = new(StringComparer.Ordinal);
        foreach (string id in conflicts.Keys.OrderByDescending(id => conflicts[id].Count).ThenBy(id => id, StringComparer.Ordinal)) {
            HashSet<int> unavailable = conflicts[id].Where(result.ContainsKey).Select(other => result[other]).ToHashSet();
            int slot = 0;
            while (unavailable.Contains(slot)) slot++;
            result.Add(id, slot);
        }
        return result;
    }

    /// <summary>Contracts every dependency SCC produced by a physical merge, keeping recursive ownership in one unloadable module.</summary>
    /// <param name="groups">Current physical groups.</param>
    /// <param name="owners">Current source owners.</param>
    /// <param name="edges">Actual source dependencies.</param>
    static void CollapseDependencyCycles(Dictionary<string, List<string>> groups, Dictionary<string, string> owners,
        Dictionary<string, HashSet<string>> edges) {
        bool changed = true;
        while (changed) {
            changed = false;
            Dictionary<string, HashSet<string>> dependencies = ModuleEdges(groups, owners, edges);
            foreach (string id in groups.Keys.OrderBy(value => value, StringComparer.Ordinal).ToArray()) {
                HashSet<string> reachable = Closure([id], dependencies);
                string[] cycle = reachable.Where(other => Closure([other], dependencies).Contains(id)).ToArray();
                if (cycle.Length < 2) continue;
                MergeGroups(cycle, groups, owners);
                changed = true;
                break;
            }
        }
    }

    /// <summary>Keeps the smallest original opaque ID as the merged identity and transfers every source exactly once; final TU pins and registrations use that same identity.</summary>
    /// <param name="ids">Groups being contracted.</param>
    /// <param name="groups">Physical source groups.</param>
    /// <param name="owners">Single source ownership map.</param>
    static void MergeGroups(IEnumerable<string> ids, Dictionary<string, List<string>> groups, Dictionary<string, string> owners) {
        string[] ordered = ids.Distinct(StringComparer.Ordinal).OrderBy(id => id, StringComparer.Ordinal).ToArray();
        string target = ordered[0];
        foreach (string id in ordered.Skip(1)) {
            foreach (string source in groups[id]) {
                groups[target].Add(source);
                owners[source] = target;
            }
            groups.Remove(id);
        }
    }

    /// <summary>Traverses actual symbol edges without recursion and rejects any missing candidate root.</summary>
    /// <param name="roots">Units required by a scene or resident bootstrap.</param>
    /// <param name="edges">Actual compiled native unit dependencies.</param>
    /// <returns>Complete reachable unit set.</returns>
    static HashSet<string> Closure(IEnumerable<string> roots, IReadOnlyDictionary<string, HashSet<string>> edges) {
        HashSet<string> result = new(StringComparer.OrdinalIgnoreCase);
        Queue<string> pending = new(roots);
        while (pending.Count != 0) {
            string source = pending.Dequeue();
            if (!result.Add(source)) continue;
            if (!edges.TryGetValue(source, out HashSet<string> imports)) throw new InvalidOperationException("Native symbol report omits required generated source: " + source);
            foreach (string target in imports) pending.Enqueue(target);
        }
        return result;
    }

    /// <summary>Normalizes generated source identities before matching SDK object records.</summary>
    /// <param name="paths">Source identity array.</param>
    /// <returns>Absolute, deduplicated source paths.</returns>
    static HashSet<string> ReadPaths(JsonArray paths) => paths.Select(path => Path.GetFullPath(path.GetValue<string>())).ToHashSet(StringComparer.OrdinalIgnoreCase);

    /// <summary>Serializes deterministic string sets without duplicate module or source identities.</summary>
    /// <param name="values">Names to serialize.</param>
    /// <returns>Ordered JSON string array.</returns>
    static JsonArray ToArray(IEnumerable<string> values) => new(values.Distinct(StringComparer.Ordinal).OrderBy(value => value, StringComparer.Ordinal).Select(value => JsonValue.Create(value)).ToArray());
}
