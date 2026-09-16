using System.Security.Cryptography;
using System.Text.Json;
using System.Reflection;
using helengine.editor;

namespace helengine.ps1.samplegen;

/// <summary>
/// Reads the authored cube-test scene, selects its typed camera/cube subset, and writes a
/// serializer-native subset scene plus a provenance report. Cooking and generated Core are
/// deliberately outside this utility's current boundary.
/// </summary>
public sealed class Ps1SceneSubsetExporter {
    public const string SceneId = "scenes/rendering/cube_test.helen";
    public const string CameraComponentTypeId = "helengine.CameraComponent";
    public const string MeshComponentTypeId = "helengine.MeshComponent";
    public const string CubeTestSpinComponentTypeId = "city.rendering.CubeTestSpinComponent, gameplay";
    public const string OrbitComponentTypeId = "city.rendering.DemoDiscOrbitCameraComponent, gameplay";

    static readonly JsonSerializerOptions ReportJsonOptions = new() {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    };

    /// <summary>
    /// Exports one explicit source scene to <c>subset.helen</c> and <c>report.json</c>.
    /// </summary>
    public Ps1SceneExportResult Export(Ps1SceneExportRequest request) {
        ArgumentNullException.ThrowIfNull(request);
        string sourcePath = Path.GetFullPath(request.SourceScenePath);
        if (!File.Exists(sourcePath)) {
            throw new FileNotFoundException("Source scene was not found.", sourcePath);
        }
        if (!Directory.Exists(request.EngineBinaryRootPath)) {
            throw new DirectoryNotFoundException($"Engine binary root was not found: '{request.EngineBinaryRootPath}'.");
        }

        byte[] sourceBytes = File.ReadAllBytes(sourcePath);
        SceneAsset source = DeserializeScene(sourceBytes);
        if (!string.Equals(source.Id, SceneId, StringComparison.Ordinal)) {
            throw new InvalidOperationException($"Expected scene id '{SceneId}', received '{source.Id}'.");
        }

        string sourceHash = ComputeHash(sourceBytes);
        SceneAsset subset = SelectSubset(source);
        Ps1SubsetReport report = BuildReport(request, sourcePath, sourceHash, source, subset);

        Directory.CreateDirectory(request.OutputRootPath);
        string subsetScenePath = Path.Combine(request.OutputRootPath, "subset.helen");
        string reportPath = Path.Combine(request.OutputRootPath, "report.json");
        WriteScene(subsetScenePath, subset);
        File.WriteAllText(reportPath, JsonSerializer.Serialize(report, ReportJsonOptions));

        return new Ps1SceneExportResult(subsetScenePath, reportPath, subset.Id, sourceHash, report);
    }

    /// <summary>
    /// Selects the camera and cube using authored names and component types. The method rejects
    /// ambiguity instead of silently choosing a scene object by incidental array order.
    /// </summary>
    public static SceneAsset SelectSubset(SceneAsset source) {
        ArgumentNullException.ThrowIfNull(source);
        SceneEntityAsset[] roots = source.RootEntities ?? Array.Empty<SceneEntityAsset>();
        List<SceneEntityAsset> cameras = new();
        List<SceneEntityAsset> cubes = new();
        foreach (SceneEntityAsset root in roots) {
            foreach (SceneEntityAsset entity in Enumerate(root)) {
                if (string.Equals(entity.Name, "CubeTestCamera", StringComparison.Ordinal)
                    && HasComponent(entity, CameraComponentTypeId)) {
                    cameras.Add(entity);
                }
                if (string.Equals(entity.Name, "CubeTestCube", StringComparison.Ordinal)
                    && HasComponent(entity, MeshComponentTypeId)
                    && HasComponent(entity, CubeTestSpinComponentTypeId)) {
                    cubes.Add(entity);
                }
            }
        }

        if (cameras.Count != 1) {
            throw new InvalidOperationException($"Expected exactly one authored CubeTestCamera with a {CameraComponentTypeId} component; found {cameras.Count}.");
        }

        SceneEntityAsset camera = cameras[0];
        SceneEntityAsset[] cameraRoot = FindContainingRoot(roots, camera);
        if (cameraRoot.Length != 1) {
            throw new InvalidOperationException("The authored CubeTestCamera must belong to exactly one root hierarchy.");
        }

        int cameraRootIndex = Array.IndexOf(roots, cameraRoot[0]);
        int nextCameraRootIndex = roots.Length;
        for (int rootIndex = cameraRootIndex + 1; rootIndex < roots.Length; rootIndex++) {
            if (Enumerate(roots[rootIndex]).Any(entity => HasComponent(entity, CameraComponentTypeId))) {
                nextCameraRootIndex = rootIndex;
                break;
            }
        }
        List<SceneEntityAsset> associatedCubes = cubes.Where(cube => {
            SceneEntityAsset[] cubeRoots = FindContainingRoot(roots, cube);
            if (cubeRoots.Length != 1) return false;
            int cubeRootIndex = Array.IndexOf(roots, cubeRoots[0]);
            return cubeRootIndex >= cameraRootIndex && cubeRootIndex < nextCameraRootIndex;
        }).ToList();
        if (associatedCubes.Count != 1) {
            throw new InvalidOperationException($"Expected exactly one authored CubeTestCube in the CubeTestCamera root hierarchy; found {associatedCubes.Count}.");
        }

        if (!ReferenceEquals(camera, cameraRoot[0])) {
            throw new InvalidOperationException("The authored CubeTestCamera must be a root entity; nested camera transforms are outside this fixed fixture subset.");
        }
        SceneEntityAsset[] cubeRoot = FindContainingRoot(roots, associatedCubes[0]);
        if (cubeRoot.Length != 1 || !ReferenceEquals(associatedCubes[0], cubeRoot[0])) {
            throw new InvalidOperationException("The authored CubeTestCube must be a root entity; nested cube transforms are outside this fixed fixture subset.");
        }

        ValidateAllowedComponents(camera, CameraComponentTypeId, OrbitComponentTypeId);
        ValidateRetainedComponents(associatedCubes[0], MeshComponentTypeId, CubeTestSpinComponentTypeId);
        SceneAssetReference[] dependencies = ResolveMeshDependencies(source, associatedCubes[0]);

        return new SceneAsset {
            Id = source.Id,
            RuntimeAssetId = source.RuntimeAssetId,
            AuthoringAssetId = source.AuthoringAssetId,
            FormerAuthoringAssetIds = source.FormerAuthoringAssetIds?.ToArray() ?? Array.Empty<string>(),
            SceneSettings = source.SceneSettings,
            Physics3DSceneFeatureFlags = source.Physics3DSceneFeatureFlags,
            RootEntities = [CloneEntity(camera, [CameraComponentTypeId]), CloneEntity(associatedCubes[0], [MeshComponentTypeId, CubeTestSpinComponentTypeId])],
            AssetReferences = dependencies
        };
    }

    static Ps1SubsetReport BuildReport(
        Ps1SceneExportRequest request,
        string sourcePath,
        string sourceHash,
        SceneAsset source,
        SceneAsset subset) {
        HashSet<uint> retainedIds = Enumerate(subset.RootEntities).Select(entity => entity.Id).ToHashSet();
        List<SceneEntityAsset> allEntities = Enumerate(source.RootEntities).ToList();
        List<SceneComponentAssetRecord> retainedComponents = Enumerate(subset.RootEntities)
            .SelectMany(entity => entity.Components ?? Array.Empty<SceneComponentAssetRecord>())
            .ToList();
        HashSet<string> retainedTypes = retainedComponents
            .Select(component => component.ComponentTypeId)
            .ToHashSet(StringComparer.Ordinal);

        Ps1SubsetReport report = new() {
            SourceScenePath = sourcePath,
            SourceSceneHash = sourceHash,
            SceneId = source.Id,
            AuthoringAssetId = source.AuthoringAssetId,
            ExcludedEntityIds = allEntities.Where(entity => !retainedIds.Contains(entity.Id)).Select(entity => entity.Id).OrderBy(id => id).ToList(),
            ExcludedComponentTypeIds = allEntities
                .SelectMany(entity => entity.Components ?? Array.Empty<SceneComponentAssetRecord>())
                .Select(component => component.ComponentTypeId)
                .Where(typeId => !retainedTypes.Contains(typeId))
                .Distinct(StringComparer.Ordinal)
                .OrderBy(typeId => typeId, StringComparer.Ordinal)
                .ToList(),
            EngineAssemblies = BuildEngineAssemblyReport(request.EngineBinaryRootPath)
        };

        foreach (SceneEntityAsset entity in Enumerate(subset.RootEntities)) {
            report.RetainedEntities.Add(new Ps1RetainedEntityReport {
                EntityId = entity.Id,
                Name = entity.Name,
                LayerMask = entity.LayerMask,
                LocalPosition = entity.LocalPosition.ToString(),
                LocalScale = entity.LocalScale.ToString(),
                LocalOrientation = entity.LocalOrientation.ToString(),
                ComponentTypeIds = (entity.Components ?? Array.Empty<SceneComponentAssetRecord>()).Select(component => component.ComponentTypeId).ToList()
            });
            foreach (SceneComponentAssetRecord component in entity.Components ?? Array.Empty<SceneComponentAssetRecord>()) {
                byte[] payload = component.Payload ?? Array.Empty<byte>();
                report.RetainedComponents.Add(new Ps1RetainedComponentReport {
                    EntityId = entity.Id,
                    EntityName = entity.Name,
                    ComponentTypeId = component.ComponentTypeId,
                    ComponentIndex = component.ComponentIndex,
                    PayloadLength = payload.Length,
                    PayloadSha256 = ComputeHash(payload),
                    PayloadBase64 = Convert.ToBase64String(payload)
                });
            }
        }

        report.Dependencies = subset.AssetReferences.Select(reference => BuildDependencyReport(request.ProjectRootPath, reference)).ToList();
        SceneEntityAsset retainedCamera = subset.RootEntities.Single(entity => entity.Name == "CubeTestCamera");
        SceneComponentAssetRecord cameraRecord = retainedCamera.Components.Single(component => component.ComponentTypeId == CameraComponentTypeId);
        report.Camera = DecodeCameraPayload(cameraRecord.Payload);
        SceneEntityAsset retainedCube = subset.RootEntities.Single(entity => entity.Name == "CubeTestCube");
        SceneComponentAssetRecord spinRecord = retainedCube.Components.Single(component => component.ComponentTypeId == CubeTestSpinComponentTypeId);
        report.Spin = DecodeSpinPayload(spinRecord.Payload);
        return report;
    }

    static Ps1DependencyReport BuildDependencyReport(string projectRootPath, SceneAssetReference reference) {
        string sourcePath = string.Empty;
        string sourceHash = string.Empty;
        if (!string.Equals(reference.ProviderId, "engine", StringComparison.OrdinalIgnoreCase)) {
            if (string.IsNullOrWhiteSpace(reference.RelativePath)) {
                throw new InvalidOperationException(
                    $"Authored dependency '{reference.AssetId}' must provide a relative path under the project assets root.");
            }
            sourcePath = ResolveProjectAssetPath(projectRootPath, reference.RelativePath);
            if (!File.Exists(sourcePath)) {
                throw new FileNotFoundException($"Referenced authored asset is missing: '{sourcePath}'.", sourcePath);
            }
            sourceHash = ComputeFileHash(sourcePath);
        }

        return new Ps1DependencyReport {
            AssetId = reference.AssetId,
            RelativePath = reference.RelativePath,
            ProviderId = reference.ProviderId,
            ContentHash = reference.ContentHash,
            SourcePath = sourcePath,
            SourceFileSha256 = sourceHash
        };
    }

    static string ResolveProjectAssetPath(string projectRootPath, string relativePath) {
        string assetsRoot = Path.GetFullPath(Path.Combine(projectRootPath, "assets"));
        if (!Directory.Exists(assetsRoot)) {
            throw new DirectoryNotFoundException($"Project assets root was not found: '{assetsRoot}'.");
        }
        DirectoryInfo assetsRootInfo = new(assetsRoot);
        if ((assetsRootInfo.Attributes & FileAttributes.ReparsePoint) != 0) {
            FileSystemInfo resolvedRoot = assetsRootInfo.ResolveLinkTarget(true);
            if (resolvedRoot != null && !string.Equals(
                    Path.GetFullPath(resolvedRoot.FullName).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
                    assetsRoot.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
                    StringComparison.OrdinalIgnoreCase)) {
                throw new InvalidOperationException($"Project assets root follows a link outside the project: '{assetsRoot}'.");
            }
        }
        string sourcePath = Path.GetFullPath(Path.Combine(assetsRoot, relativePath.Replace('/', Path.DirectorySeparatorChar).Replace(Path.AltDirectorySeparatorChar, Path.DirectorySeparatorChar)));
        if (!IsSameOrDescendant(sourcePath, assetsRoot)) {
            throw new InvalidOperationException($"Referenced authored asset path escapes the project assets root: '{relativePath}'.");
        }

        string relative = Path.GetRelativePath(assetsRoot, sourcePath);
        string currentPath = assetsRoot;
        foreach (string segment in relative.Split([Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar], StringSplitOptions.RemoveEmptyEntries)) {
            currentPath = Path.Combine(currentPath, segment);
            FileSystemInfo fileSystemInfo = Directory.Exists(currentPath)
                ? new DirectoryInfo(currentPath)
                : new FileInfo(currentPath);
            if ((fileSystemInfo.Attributes & FileAttributes.ReparsePoint) == 0) {
                continue;
            }

            FileSystemInfo resolved = fileSystemInfo.ResolveLinkTarget(true);
            if (resolved != null && !IsSameOrDescendant(Path.GetFullPath(resolved.FullName), assetsRoot)) {
                throw new InvalidOperationException($"Referenced authored asset path follows a link outside the project assets root: '{relativePath}'.");
            }
        }
        return sourcePath;
    }

    static bool IsSameOrDescendant(string path, string root) {
        string normalizedPath = Path.GetFullPath(path).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        string normalizedRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        return string.Equals(normalizedPath, normalizedRoot, StringComparison.OrdinalIgnoreCase)
            || normalizedPath.StartsWith(normalizedRoot + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
    }

    static Ps1CameraPayloadReport DecodeCameraPayload(byte[] payload) {
        EditorTaggedSceneComponentFieldReader fields = new(payload ?? Array.Empty<byte>());
        byte drawOrder = ReadRequiredByteField(fields, "CameraDrawOrder");
        ushort layerMask = ReadRequiredUInt16Field(fields, "LayerMask");
        float4 viewport = ReadRequiredFloat4Field(fields, "Viewport");
        float nearPlane = ReadRequiredSingleField(fields, "NearPlaneDistance");
        float farPlane = ReadRequiredSingleField(fields, "FarPlaneDistance");

        // CameraClearSettings and CameraRenderSettings are nullable nested objects in the
        // automatic persistence schema. Resolve the shared value walker from the exact loaded
        // engine core assembly so nullable markers and ordinal member ordering remain aligned
        // with the editor/runtime contract even when this utility is compiled against a
        // compatibility binary that predates the public walker type.
        (object walker, MethodInfo readValueMethod) = CreatePersistenceValueWalker();
        CameraClearSettings clear = ReadNestedField(fields, "ClearSettings", 25, walker, readValueMethod, typeof(CameraClearSettings)) is CameraClearSettings decodedClear
            ? decodedClear
            : throw new InvalidOperationException("Camera ClearSettings payload did not decode as CameraClearSettings.");
        CameraRenderSettings renderSettings = ReadNestedField(fields, "RenderSettings", 7, walker, readValueMethod, typeof(CameraRenderSettings)) as CameraRenderSettings
            ?? throw new InvalidOperationException("Camera RenderSettings payload did not decode as CameraRenderSettings.");
        if (!Enum.IsDefined(renderSettings.DepthPrepassMode) || !Enum.IsDefined(renderSettings.PostProcessTier)) {
            throw new InvalidOperationException($"Camera payload contains unsupported render settings values: depth={Convert.ToByte(renderSettings.DepthPrepassMode)}, postProcess={Convert.ToByte(renderSettings.PostProcessTier)}.");
        }
        return new Ps1CameraPayloadReport {
            DrawOrder = drawOrder,
            LayerMask = layerMask,
            Viewport = [viewport.X, viewport.Y, viewport.Z, viewport.W],
            NearPlaneDistance = nearPlane,
            FarPlaneDistance = farPlane,
            ClearColorEnabled = clear.ClearColorEnabled,
            ClearColor = [clear.ClearColor.X, clear.ClearColor.Y, clear.ClearColor.Z, clear.ClearColor.W],
            ClearDepthEnabled = clear.ClearDepthEnabled,
            ClearDepth = clear.ClearDepth,
            ClearStencilEnabled = clear.ClearStencilEnabled,
            ClearStencil = clear.ClearStencil,
            DepthPrepassMode = renderSettings.DepthPrepassMode.ToString(),
            DepthPrepassModeValue = (byte)renderSettings.DepthPrepassMode,
            ShadowDistance = renderSettings.ShadowDistance,
            PostProcessTier = renderSettings.PostProcessTier.ToString(),
            PostProcessTierValue = (byte)renderSettings.PostProcessTier
        };
    }

    static byte ReadRequiredByteField(EditorTaggedSceneComponentFieldReader fields, string fieldName) {
        using EngineBinaryReader reader = RequireField(fields, fieldName);
        byte value = reader.ReadByte();
        RequireFieldEnd(reader, fieldName, 1);
        return value;
    }

    static ushort ReadRequiredUInt16Field(EditorTaggedSceneComponentFieldReader fields, string fieldName) {
        using EngineBinaryReader reader = RequireField(fields, fieldName);
        ushort value = reader.ReadUInt16();
        RequireFieldEnd(reader, fieldName, 2);
        return value;
    }

    static float ReadRequiredSingleField(EditorTaggedSceneComponentFieldReader fields, string fieldName) {
        using EngineBinaryReader reader = RequireField(fields, fieldName);
        float value = reader.ReadSingle();
        RequireFieldEnd(reader, fieldName, 4);
        return value;
    }

    static float4 ReadRequiredFloat4Field(EditorTaggedSceneComponentFieldReader fields, string fieldName) {
        using EngineBinaryReader reader = RequireField(fields, fieldName);
        float4 value = reader.ReadFloat4();
        RequireFieldEnd(reader, fieldName, 16);
        return value;
    }

    static object ReadNestedField(
        EditorTaggedSceneComponentFieldReader fields,
        string fieldName,
        int expectedLength,
        object walker,
        MethodInfo readValueMethod,
        Type valueType) {
        using EngineBinaryReader reader = RequireField(fields, fieldName);
        object value;
        try {
            value = readValueMethod.Invoke(walker, [reader, valueType]);
        } catch (TargetInvocationException exception) {
            throw new InvalidOperationException(
                $"Engine persistence decoder failed for camera field '{fieldName}'.",
                exception.InnerException ?? exception);
        }
        RequireFieldEnd(reader, fieldName, expectedLength);
        return value;
    }

    static (object Walker, MethodInfo ReadValueMethod) CreatePersistenceValueWalker() {
        Assembly coreAssembly = typeof(SceneAsset).Assembly;
        Type walkerType = coreAssembly.GetType("helengine.ScenePersistenceValueWalker", throwOnError: false)
            ?? throw new InvalidOperationException(
                $"The loaded engine core assembly '{coreAssembly.Location}' does not expose ScenePersistenceValueWalker.");
        Type readerType = coreAssembly.GetType("helengine.IScenePersistenceAssetValueReader", throwOnError: false)
            ?? throw new InvalidOperationException(
                $"The loaded engine core assembly '{coreAssembly.Location}' does not expose IScenePersistenceAssetValueReader.");
        object reader = DispatchProxy.Create(readerType, typeof(NoAssetValueReaderProxy));
        ConstructorInfo constructor = walkerType.GetConstructor([readerType, typeof(string)])
            ?? throw new InvalidOperationException("The loaded engine core persistence walker has an unexpected constructor contract.");
        object walker = constructor.Invoke([reader, "PS1 authored subset report"]);
        MethodInfo readValueMethod = walkerType.GetMethod(
                "ReadValue",
                BindingFlags.Instance | BindingFlags.Public,
                binder: null,
                types: [typeof(EngineBinaryReader), typeof(Type)],
                modifiers: null)
            ?? throw new InvalidOperationException("The loaded engine core persistence walker has an unexpected ReadValue contract.");
        return (walker, readValueMethod);
    }

    public class NoAssetValueReaderProxy : DispatchProxy {
        protected override object Invoke(MethodInfo targetMethod, object[] args) {
            if (targetMethod.Name == "TryReadAssetValue" && args?.Length == 3) {
                args[2] = null;
                return false;
            }

            throw new NotSupportedException($"Unexpected persistence asset reader method '{targetMethod.Name}'.");
        }
    }

    static void RequireFieldEnd(EngineBinaryReader reader, string fieldName, int expectedLength) {
        if (reader.GetStreamPosition() != expectedLength) {
            throw new InvalidOperationException($"Camera field '{fieldName}' consumed {reader.GetStreamPosition()} bytes; expected {expectedLength} bytes in the current persistence schema.");
        }
    }

    static Ps1SpinPayloadReport DecodeSpinPayload(byte[] payload) {
        EditorTaggedSceneComponentFieldReader fields = new(payload);
        using EngineBinaryReader baseYawReader = RequireField(fields, "BaseYawRadians");
        using EngineBinaryReader angularSpeedReader = RequireField(fields, "AngularSpeedRadians");
        return new Ps1SpinPayloadReport {
            BaseYawRadians = baseYawReader.ReadSingle(),
            AngularSpeedRadians = angularSpeedReader.ReadSingle()
        };
    }

    static EngineBinaryReader RequireField(EditorTaggedSceneComponentFieldReader fields, string fieldName) {
        if (!fields.TryGetFieldReader(fieldName, out EngineBinaryReader reader)) {
            throw new InvalidOperationException($"Serialized component payload is missing required field '{fieldName}'.");
        }

        return reader;
    }

    static List<Ps1EngineAssemblyReport> BuildEngineAssemblyReport(string engineBinaryRootPath) {
        string[] names = ["helengine.core.dll", "helengine.editor.dll", "helengine.files.dll", "helengine.baseplatform.dll", "helengine.shader.dll", "helengine.shader.compilation.dll"];
        List<Ps1EngineAssemblyReport> result = new();
        foreach (string name in names) {
            string requestedPath = Path.Combine(engineBinaryRootPath, name);
            if (!File.Exists(requestedPath)) {
                throw new FileNotFoundException($"Required engine assembly is missing from explicit EngineBinaryRoot: '{requestedPath}'.", requestedPath);
            }
            string assemblySimpleName = Path.GetFileNameWithoutExtension(name);
            Assembly loadedAssembly = AppDomain.CurrentDomain.GetAssemblies()
                .FirstOrDefault(assembly => string.Equals(assembly.GetName().Name, assemblySimpleName, StringComparison.OrdinalIgnoreCase));
            loadedAssembly ??= Assembly.LoadFrom(requestedPath);
            string loadedPath = loadedAssembly.Location;
            if (string.IsNullOrWhiteSpace(loadedPath) || !File.Exists(loadedPath)) {
                throw new InvalidOperationException($"Loaded engine assembly '{assemblySimpleName}' has no readable file location.");
            }
            FileInfo info = new(loadedPath);
            result.Add(new Ps1EngineAssemblyReport {
                FileName = name,
                FullPath = loadedPath,
                RequestedPath = requestedPath,
                Length = info.Length,
                Sha256 = ComputeFileHash(loadedPath)
            });
        }
        return result;
    }

    static void ValidateRetainedComponents(SceneEntityAsset entity, params string[] expectedTypeIds) {
        SceneComponentAssetRecord[] components = entity.Components ?? Array.Empty<SceneComponentAssetRecord>();
        string[] actual = components.Select(component => component.ComponentTypeId).ToArray();
        if (!actual.OrderBy(typeId => typeId, StringComparer.Ordinal).SequenceEqual(expectedTypeIds.OrderBy(typeId => typeId, StringComparer.Ordinal), StringComparer.Ordinal)) {
            throw new InvalidOperationException($"Entity {entity.Id} '{entity.Name}' has unsupported retained components. Expected [{string.Join(", ", expectedTypeIds)}], received [{string.Join(", ", actual)}].");
        }
    }

    static void ValidateAllowedComponents(SceneEntityAsset entity, params string[] allowedTypeIds) {
        SceneComponentAssetRecord[] components = entity.Components ?? Array.Empty<SceneComponentAssetRecord>();
        string[] unsupported = components
            .Select(component => component.ComponentTypeId)
            .Where(typeId => !allowedTypeIds.Contains(typeId, StringComparer.Ordinal))
            .Distinct(StringComparer.Ordinal)
            .ToArray();
        if (unsupported.Length != 0) {
            throw new InvalidOperationException($"Entity {entity.Id} '{entity.Name}' has unsupported components: [{string.Join(", ", unsupported)}].");
        }
    }

    static bool HasComponent(SceneEntityAsset entity, string componentTypeId) {
        return (entity.Components ?? Array.Empty<SceneComponentAssetRecord>()).Any(component => string.Equals(component.ComponentTypeId, componentTypeId, StringComparison.Ordinal));
    }

    static SceneAssetReference[] ResolveMeshDependencies(SceneAsset source, SceneEntityAsset cube) {
        SceneComponentAssetRecord mesh = cube.Components.Single(component => component.ComponentTypeId == MeshComponentTypeId);
        EditorTaggedSceneComponentFieldReader fields = new(mesh.Payload ?? Array.Empty<byte>());
        List<SceneAssetReference> selected = new();
        using (EngineBinaryReader modelReader = RequireField(fields, "Model")) {
            SceneAssetReference model = SceneComponentBinaryFieldEncoding.ReadOptionalReference(modelReader);
            if (model != null) selected.Add(model);
        }
        using (EngineBinaryReader materialsReader = RequireField(fields, "Materials")) {
            selected.AddRange(SceneComponentBinaryFieldEncoding.ReadOptionalReferenceArray(materialsReader).Where(reference => reference != null));
        }
        if (selected.Count == 0) {
            throw new InvalidOperationException("CubeTestCube mesh payload does not contain a model or material reference.");
        }

        SceneAssetReference[] matching = source.AssetReferences
            .Where(sourceReference => selected.Any(reference => SameReference(sourceReference, reference)))
            .OrderBy(reference => reference.AssetId, StringComparer.Ordinal)
            .ToArray();
        if (matching.Length != selected.Select(reference => reference.AssetId).Distinct(StringComparer.Ordinal).Count()) {
            throw new InvalidOperationException("CubeTestCube references an asset that is absent from the scene asset-reference table.");
        }
        return matching;
    }

    static bool SameReference(SceneAssetReference left, SceneAssetReference right) {
        return string.Equals(left.AssetId, right.AssetId, StringComparison.Ordinal)
            && string.Equals(left.RelativePath, right.RelativePath, StringComparison.Ordinal)
            && string.Equals(left.ProviderId, right.ProviderId, StringComparison.Ordinal);
    }

    static SceneEntityAsset CloneEntity(SceneEntityAsset source, string[] componentTypeIds) {
        SceneComponentAssetRecord[] components = (source.Components ?? Array.Empty<SceneComponentAssetRecord>())
            .Where(component => componentTypeIds.Contains(component.ComponentTypeId, StringComparer.Ordinal))
            .Select(CloneComponent)
            .ToArray();
        return new SceneEntityAsset {
            Id = source.Id,
            Name = source.Name,
            IsStatic = source.IsStatic,
            Enabled = source.Enabled,
            LayerMask = source.LayerMask,
            LocalPosition = source.LocalPosition,
            LocalScale = source.LocalScale,
            LocalOrientation = source.LocalOrientation,
            Components = components,
            PlatformExistenceOverrides = Array.Empty<SceneEntityPlatformExistenceOverrideAsset>(),
            PlatformTransformOverrides = Array.Empty<SceneEntityPlatformTransformOverrideAsset>(),
            PlatformComponentOverrides = Array.Empty<SceneEntityPlatformComponentOverrideAsset>(),
            Children = Array.Empty<SceneEntityAsset>()
        };
    }

    static SceneComponentAssetRecord CloneComponent(SceneComponentAssetRecord source) {
        return new SceneComponentAssetRecord {
            ComponentKey = source.ComponentKey,
            ComponentTypeId = source.ComponentTypeId,
            ComponentIndex = source.ComponentIndex,
            Payload = source.Payload?.ToArray() ?? Array.Empty<byte>()
        };
    }

    static SceneEntityAsset[] FindContainingRoot(SceneEntityAsset[] roots, SceneEntityAsset target) {
        return roots.Where(root => Enumerate(root).Any(entity => ReferenceEquals(entity, target))).ToArray();
    }

    static IEnumerable<SceneEntityAsset> Enumerate(IEnumerable<SceneEntityAsset> entities) {
        foreach (SceneEntityAsset entity in entities ?? Array.Empty<SceneEntityAsset>()) {
            foreach (SceneEntityAsset nested in Enumerate(entity)) yield return nested;
        }
    }

    static IEnumerable<SceneEntityAsset> Enumerate(SceneEntityAsset entity) {
        if (entity == null) yield break;
        yield return entity;
        foreach (SceneEntityAsset child in entity.Children ?? Array.Empty<SceneEntityAsset>()) {
            foreach (SceneEntityAsset nested in Enumerate(child)) yield return nested;
        }
    }

    static SceneAsset DeserializeScene(byte[] bytes) {
        using MemoryStream stream = new(bytes, false);
        return (SceneAsset)(helengine.AssetSerializer.Deserialize(stream) ?? throw new InvalidOperationException("Source scene deserialized to null."));
    }

    static void WriteScene(string path, SceneAsset scene) {
        using FileStream stream = File.Create(path);
        helengine.editor.AssetSerializer.Serialize(stream, scene);
    }

    public static string ComputeFileHash(string path) {
        using SHA256 sha256 = SHA256.Create();
        using FileStream stream = File.OpenRead(path);
        return Convert.ToHexString(sha256.ComputeHash(stream)).ToLowerInvariant();
    }

    static string ComputeHash(byte[] bytes) {
        return Convert.ToHexString(SHA256.HashData(bytes ?? Array.Empty<byte>())).ToLowerInvariant();
    }
}
