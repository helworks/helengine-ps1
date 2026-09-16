using helengine.baseplatform.Builders;
using helengine.baseplatform.Definitions;
using helengine.baseplatform.Descriptors;
using helengine.baseplatform.Reporting;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;

namespace helengine.ps1.builder;

/// <summary>
/// Platform discovery and build contract for the PlayStation target. The editor build graph stages,
/// cooks and generates Core; this builder validates the manifest, stages only declared cooked files,
/// writes the scene catalog beside generated Core, and runs the pinned native build that embeds the
/// content archive into the PS-X EXE.
/// </summary>
public sealed class PlayStation1PlatformAssetBuilder : IPlatformAssetBuilder {
    /// <summary>Name of the executable the native build must produce for the build to count as successful.</summary>
    public const string ExecutableFileName = "helengine_ps1.ps-exe";

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

        List<PlatformBuildDiagnostic> diagnostics = [];
        string stagingRoot = Path.Combine(request.WorkingRoot, "ps1-authored-content");
        string declarationsPath = Path.Combine(request.WorkingRoot, "ps1-authored-declarations.json");
        string outputExecutablePath = Path.Combine(request.OutputRoot, ExecutableFileName);
        try {
            cancellationToken.ThrowIfCancellationRequested();
            Directory.CreateDirectory(request.OutputRoot);
            // A stale executable must never masquerade as this build's result.
            if (File.Exists(outputExecutablePath)) File.Delete(outputExecutablePath);
            if (!string.Equals(request.Manifest.PlatformName, "ps1", StringComparison.OrdinalIgnoreCase)) {
                throw new InvalidOperationException($"PS1 builder received manifest for platform '{request.Manifest.PlatformName}'.");
            }
            if (string.IsNullOrWhiteSpace(request.GeneratedCoreCppRootPath) || !Directory.Exists(request.GeneratedCoreCppRootPath)) {
                throw new DirectoryNotFoundException($"PS1 builder requires the generated Core root; '{request.GeneratedCoreCppRootPath}' was not found.");
            }
            new PlayStation1SceneCompatibilityValidator().Validate(request.Manifest);

            progressReporter.Report(new PlatformBuildProgressUpdate("Stage Content", "manifest", 0, 1, "Staging declared PS1 cooked artifacts."));
            string packageSourceRoot = ResolvePackageSourceRoot(request.WorkingRoot);
            IReadOnlyList<PlayStation1StagedAsset> staged = new PlayStation1ContentStager().Stage(request.Manifest.CookedArtifacts, packageSourceRoot, stagingRoot, cancellationToken);
            PlayStation1ContentStager.WriteDeclarations(staged, declarationsPath);
            new PlayStation1StartupManifestWriter().Write(request.GeneratedCoreCppRootPath, request.Manifest);

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

        bool succeeded = diagnostics.Count == 0 && File.Exists(outputExecutablePath);
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
}
