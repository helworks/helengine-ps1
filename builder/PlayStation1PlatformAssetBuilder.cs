using helengine.baseplatform.Builders;
using helengine.baseplatform.Definitions;
using helengine.baseplatform.Descriptors;
using helengine.baseplatform.Manifest;
using helengine.baseplatform.Reporting;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;

namespace helengine.ps1.builder;

/// <summary>
/// Platform discovery and build contract for the PlayStation target. The editor build graph stages,
/// cooks and generates Core; this builder validates the manifest, stages only declared cooked files,
/// writes the scene catalog beside generated Core, and runs the pinned native build. The legacy path
/// embeds content in a PS-X EXE; scene-module builds publish a CD image with per-scene code/content.
/// </summary>
public sealed class PlayStation1PlatformAssetBuilder : IPlatformAssetBuilder, IPlatformLayoutTargetProvider {
    /// <summary>Name of the executable the native build must produce for the build to count as successful.</summary>
    public const string ExecutableFileName = "helengine_ps1.ps-exe";

    /// <summary>CD entrypoint used when native scene modules and their content are loaded from the disc.</summary>
    public const string SceneModuleDiscEntryPointFileName = "helengine_ps1.cue";

    /// <summary>Cooks the opaque unlit material subset.</summary>
    readonly PlayStation1MaterialCooker MaterialCooker = new();

    /// <summary>Runs the native toolchain; replaceable for tests.</summary>
    readonly IPlayStation1NativeBuildExecutor NativeBuildExecutor;

    /// <summary>Creates the builder the editor discovers through its parameterless constructor.</summary>
    public PlayStation1PlatformAssetBuilder()
        : this(new PlayStation1NativeBuildExecutor()) {
    }

    /// <summary>Creates the builder with an explicit native build executor.</summary>
    /// <param name="nativeBuildExecutor">Native build runner.</param>
    public PlayStation1PlatformAssetBuilder(IPlayStation1NativeBuildExecutor nativeBuildExecutor) {
        NativeBuildExecutor = nativeBuildExecutor ?? throw new ArgumentNullException(nameof(nativeBuildExecutor));
        Descriptor = new PlatformBuilderDescriptor(
            "helengine.ps1.builder",
            "0.1.0",
            "ps1",
            new EngineCompatibilityRange("1.0.0", "999.0.0"),
            new ManifestCompatibilityRange(1, 1),
            ["ps1"],
            ["ps1"]);
        Definition = PlayStation1PlatformDefinitionFactory.Create();
    }

    /// <inheritdoc />
    public PlatformBuilderDescriptor Descriptor { get; }

    /// <inheritdoc />
    public PlatformDefinition Definition { get; }

    /// <inheritdoc />
    public PlatformMaterialCookResult CookMaterial(PlatformMaterialCookRequest request) => MaterialCooker.Cook(request);

    /// <inheritdoc />
    public Task<PlatformBuildReport> BuildAsync(
        PlatformBuildRequest request,
        IPlatformBuildProgressReporter progressReporter,
        IPlatformBuildDiagnosticReporter diagnosticReporter,
        CancellationToken cancellationToken) {
        if (request == null) throw new ArgumentNullException(nameof(request));
        if (progressReporter == null) throw new ArgumentNullException(nameof(progressReporter));
        if (diagnosticReporter == null) throw new ArgumentNullException(nameof(diagnosticReporter));

        Definition.CodegenProfiles.Single().NumericCapabilities.RequireSupported(request.NumericPolicy);
        if (request.NumericPolicy.Identity != request.Manifest.NumericPolicy.Identity) {
            throw new InvalidOperationException("PS1 build request and content manifest have different numeric policies.");
        }
        foreach (PlatformBuildCodeModule module in request.Manifest.CodeModules) { module.RequireNumericPolicy(request.NumericPolicy); }
        List<PlatformBuildDiagnostic> diagnostics = [];
        string stagingRoot = Path.Combine(request.WorkingRoot, "ps1-authored-content");
        string declarationsPath = Path.Combine(request.WorkingRoot, "ps1-authored-declarations.json");
        string outputExecutablePath = Path.Combine(request.OutputRoot, ExecutableFileName);
        bool sceneModuleMode = !string.IsNullOrEmpty(request.GeneratedCoreCppRootPath) &&
            File.Exists(Path.Combine(request.GeneratedCoreCppRootPath, "scene-modules", "source-plan.json"));
        string outputEntryPointPath = sceneModuleMode ? Path.Combine(request.OutputRoot, SceneModuleDiscEntryPointFileName) : outputExecutablePath;
        try {
            cancellationToken.ThrowIfCancellationRequested();
            Directory.CreateDirectory(request.OutputRoot);
            // A stale executable must never masquerade as this build's result.
            if (File.Exists(outputEntryPointPath)) File.Delete(outputEntryPointPath);
            if (!string.Equals(request.Manifest.PlatformName, "ps1", StringComparison.OrdinalIgnoreCase)) {
                throw new InvalidOperationException($"PS1 builder received manifest for platform '{request.Manifest.PlatformName}'.");
            }
            if (string.IsNullOrWhiteSpace(request.GeneratedCoreCppRootPath) || !Directory.Exists(request.GeneratedCoreCppRootPath)) {
                throw new DirectoryNotFoundException($"PS1 builder requires the generated Core root; '{request.GeneratedCoreCppRootPath}' was not found.");
            }
            new PlayStation1SceneCompatibilityValidator().Validate(request.Manifest);

            string packageSourceRoot = ResolvePackageSourceRoot(request.WorkingRoot);
            // Builder-owned cooks run before staging, because they write the artifacts staging then collects.
            PlatformCookWorkItem[] cookWorkItems = request.Manifest.PlatformCookWorkItems ?? [];
            PlayStation1PlatformCookWorkItemExecutor cookExecutor = new();
            for (int index = 0; index < cookWorkItems.Length; index++) {
                cancellationToken.ThrowIfCancellationRequested();
                PlatformCookWorkItem workItem = cookWorkItems[index];
                if (!string.Equals(workItem.TargetPlatformId, "ps1", StringComparison.OrdinalIgnoreCase)) {
                    throw new InvalidOperationException($"PS1 builder received a cook work item for platform '{workItem.TargetPlatformId}'.");
                }
                cookExecutor.Execute(workItem, packageSourceRoot, request.NumericPolicy);
                progressReporter.Report(new PlatformBuildProgressUpdate(
                    "Cook Platform Assets", workItem.OutputLogicalArtifactId, index + 1, cookWorkItems.Length,
                    $"Cooked '{workItem.OutputRelativePath}'."));
            }

            progressReporter.Report(new PlatformBuildProgressUpdate("Stage Content", "manifest", 0, 1, "Staging declared PS1 cooked artifacts."));
            HashSet<string> selectedRuntimePaths = null;
            if (request.Manifest.Scenes.Any(scene => scene.ResolvedMetadata.Any(pair => pair.Key == "cooked-dependency-paths"))) {
                selectedRuntimePaths = new(StringComparer.Ordinal);
                foreach (PlatformBuildScene scene in request.Manifest.Scenes) {
                    string encodedPaths = scene.ResolvedMetadata.Single(pair => pair.Key == "cooked-dependency-paths").Value;
                    string[] dependencies = System.Text.Json.JsonSerializer.Deserialize<string[]>(encodedPaths)
                        ?? throw new InvalidOperationException($"Scene '{scene.SceneId}' has no resolved runtime dependencies.");
                    foreach (string path in dependencies) selectedRuntimePaths.Add(PlayStation1ContentStager.NormalizeRelativePath(path));
                }
            }
            IReadOnlyList<PlayStation1StagedAsset> staged = new PlayStation1ContentStager().StageSelected(request.Manifest.CookedArtifacts, cookWorkItems, packageSourceRoot, stagingRoot, cancellationToken, selectedRuntimePaths);
            PlayStation1CookedAssetPolicyValidator.Validate(staged, request.NumericPolicy, cancellationToken);
            PlayStation1ContentStager.WriteDeclarations(staged, declarationsPath);
            new PlayStation1StartupManifestWriter().Write(request.GeneratedCoreCppRootPath, request.Manifest);
            new PlayStation1SceneModuleBuildPlanWriter().WriteSourcePlan(request.GeneratedCoreCppRootPath, request.Manifest, staged);

            progressReporter.Report(new PlatformBuildProgressUpdate("Build Native Executable", ExecutableFileName, 0, 1, "Building authored PS1 runtime."));
            NativeBuildExecutor.Build(request.GeneratedCoreCppRootPath, stagingRoot, declarationsPath, request.OutputRoot, cancellationToken);
            progressReporter.Report(new PlatformBuildProgressUpdate("Build Native Executable", ExecutableFileName, 1, 1, "Built authored PS1 runtime."));
        } catch (OperationCanceledException) {
            throw;
        } catch (Exception exception) {
            PlatformBuildDiagnostic diagnostic = new(
                PlatformBuildDiagnosticSeverity.Error,
                "PS1BUILD_FAILED",
                exception.Message,
                request.Manifest.StartupSceneId,
                string.Empty,
                "ps1-builder");
            diagnostics.Add(diagnostic);
            diagnosticReporter.Report(diagnostic);
        }

        bool succeeded = diagnostics.Count == 0 && File.Exists(outputEntryPointPath);
        PlatformBuildItemOutcomeKind outcome = succeeded ? PlatformBuildItemOutcomeKind.Succeeded : PlatformBuildItemOutcomeKind.Failed;
        return Task.FromResult(new PlatformBuildReport(
            succeeded,
            [.. diagnostics],
            request.Manifest.Scenes.Select(scene => new PlatformBuildItemOutcome(scene.SceneId, outcome)).ToArray(),
            request.Manifest.LooseAssets.Select(asset => new PlatformBuildItemOutcome(asset.AssetId, outcome)).ToArray()));
    }

    /// <summary>Locates the editor-staged package-source tree that owns the cooked files.</summary>
    static string ResolvePackageSourceRoot(string workingRootPath) {
        if (string.IsNullOrWhiteSpace(workingRootPath)) {
            throw new ArgumentException("Builder working root is required.", nameof(workingRootPath));
        }
        string packageSourceRoot = Path.Combine(Path.GetFullPath(workingRootPath), "package-source");
        if (!Directory.Exists(packageSourceRoot)) {
            throw new DirectoryNotFoundException($"PS1 builder package source root '{packageSourceRoot}' was not found.");
        }
        return packageSourceRoot;
    }
    /// <summary>Describes the fixed W320 NTSC progressive framebuffer initialized by Ps1Video.</summary>
    /// <param name="request">Effective build selection; this backend currently has one fixed video target.</param>
    /// <returns>The actual 320 by 240 square-pixel output without invented physical DPI.</returns>
    public PlatformLayoutTarget ResolveLayoutTarget(PlatformLayoutTargetRequest request) {
        ArgumentNullException.ThrowIfNull(request);
        return new PlatformLayoutTarget(320, 240, 1d, false, 0d, 1d);
    }

}
