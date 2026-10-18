using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json.Nodes;

namespace helengine.ps1.builder;

/// <summary>
/// Invokes scripts/build.ps1 in authored mode inside the PS1 implementation checkout named by
/// HELENGINE_PS1_REPOSITORY_ROOT, then copies the produced executable, manifest and link map to the
/// output root. Opt-in scene modules additionally publish the validated CD entrypoint and its
/// image/receipt; the build script owns the pinned Docker toolchain and retail memory check.
/// </summary>
public sealed class PlayStation1NativeBuildExecutor : IPlayStation1NativeBuildExecutor {
    /// <summary>Environment variable naming the helengine-ps1 implementation checkout.</summary>
    public const string RepositoryRootEnvironmentVariableName = "HELENGINE_PS1_REPOSITORY_ROOT";

    /// <summary>Environment variable selecting the fixed native build directory passed explicitly to the official wrapper.</summary>
    public const string BuildRunRootEnvironmentVariableName = "HELENGINE_PS1_BUILD_RUN_ROOT";

    /// <inheritdoc />
    public void Build(string generatedCoreCppRootPath, string stagedContentRootPath, string declarationsPath, string outputRootPath, CancellationToken cancellationToken) {
        if (string.IsNullOrWhiteSpace(generatedCoreCppRootPath)) throw new ArgumentException("Generated Core root is required.", nameof(generatedCoreCppRootPath));
        if (string.IsNullOrWhiteSpace(stagedContentRootPath)) throw new ArgumentException("Staged content root is required.", nameof(stagedContentRootPath));
        if (string.IsNullOrWhiteSpace(declarationsPath)) throw new ArgumentException("Declarations path is required.", nameof(declarationsPath));
        if (string.IsNullOrWhiteSpace(outputRootPath)) throw new ArgumentException("Output root is required.", nameof(outputRootPath));
        cancellationToken.ThrowIfCancellationRequested();

        string repositoryRoot = Environment.GetEnvironmentVariable(RepositoryRootEnvironmentVariableName) ?? string.Empty;
        if (string.IsNullOrWhiteSpace(repositoryRoot) || !Directory.Exists(repositoryRoot)) {
            throw new InvalidOperationException($"{RepositoryRootEnvironmentVariableName} must identify the PS1 implementation checkout.");
        }
        string buildRunRoot = ResolveBuildRunRoot(repositoryRoot, generatedCoreCppRootPath, Environment.GetEnvironmentVariable(BuildRunRootEnvironmentVariableName) ?? string.Empty);
        Directory.CreateDirectory(outputRootPath);
        PlayStation1CodegenDiagnosticGate.Validate(generatedCoreCppRootPath);
        int omittedBatchSources = PlayStation1UnusedBatch2DPruner.Prune(generatedCoreCppRootPath);
        if (omittedBatchSources != 0) {
            Console.WriteLine($"[ps1] omitted {omittedBatchSources} unused generated 2D batch sources");
        }
        string sceneModulePlan = PrepareSceneModules(repositoryRoot, generatedCoreCppRootPath, outputRootPath, cancellationToken);

        ProcessStartInfo info = new() {
            FileName = ResolvePowerShell(),
            WorkingDirectory = repositoryRoot,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true
        };
        info.ArgumentList.Add("-NoProfile");
        info.ArgumentList.Add("-File");
        info.ArgumentList.Add(Path.Combine(repositoryRoot, "scripts", "build.ps1"));
        info.ArgumentList.Add("-Configuration");
        info.ArgumentList.Add("Release");
        info.ArgumentList.Add("-Authored");
        if (!string.IsNullOrEmpty(buildRunRoot)) {
            info.ArgumentList.Add("-RunRoot");
            info.ArgumentList.Add(buildRunRoot);
        }
        info.ArgumentList.Add("-GeneratedCoreRoot");
        info.ArgumentList.Add(generatedCoreCppRootPath);
        info.ArgumentList.Add("-AuthoredContentRoot");
        info.ArgumentList.Add(stagedContentRootPath);
        info.ArgumentList.Add("-AuthoredDeclarations");
        info.ArgumentList.Add(declarationsPath);
        if (!string.IsNullOrEmpty(sceneModulePlan)) {
            info.ArgumentList.Add("-SceneModulePlan");
            info.ArgumentList.Add(sceneModulePlan);
        }

        using Process process = Process.Start(info) ?? throw new InvalidOperationException("Could not start the PS1 build wrapper.");
        Task<string> standardOutput = process.StandardOutput.ReadToEndAsync(cancellationToken);
        Task<string> standardError = process.StandardError.ReadToEndAsync(cancellationToken);
        process.WaitForExit();
        cancellationToken.ThrowIfCancellationRequested();
        string output = standardOutput.GetAwaiter().GetResult();
        string error = standardError.GetAwaiter().GetResult();
        File.WriteAllText(Path.Combine(outputRootPath, "native-build.stdout.log"), output);
        File.WriteAllText(Path.Combine(outputRootPath, "native-build.stderr.log"), error);
        if (process.ExitCode != 0) {
            throw new InvalidOperationException($"PS1 native build failed with exit code {process.ExitCode}.{Environment.NewLine}{error}{Environment.NewLine}{output}");
        }

        // build.ps1 prints the manifest path as its final line; the artifacts sit beside it.
        string manifestPath = output.Split('\n', StringSplitOptions.RemoveEmptyEntries).Select(line => line.Trim()).LastOrDefault(line => line.EndsWith("manifest.json", StringComparison.OrdinalIgnoreCase));
        if (string.IsNullOrWhiteSpace(manifestPath) || !File.Exists(manifestPath)) {
            throw new InvalidOperationException("PS1 native build did not report a manifest path.");
        }
        string runRoot = Path.GetDirectoryName(manifestPath) ?? throw new InvalidOperationException("PS1 manifest path has no directory.");
        string[] artifactNames = string.IsNullOrEmpty(sceneModulePlan)
            ? ["helengine_ps1.ps-exe", "helengine_ps1.elf", "helengine_ps1.map"]
            : ["helengine_ps1.ps-exe", "helengine_ps1.elf", "helengine_ps1.map", "helengine_ps1.bin", "helengine_ps1.json", "scene-modules-receipt.json", "helengine_ps1.cue"];
        foreach (string artifactName in artifactNames) {
            string artifactPath = Path.Combine(runRoot, "out", artifactName);
            if (!File.Exists(artifactPath)) throw new FileNotFoundException($"PS1 native build did not produce {artifactName}.", artifactPath);
        }
        // Validate the complete native product before replacing any successful legacy executable.
        // The disc entrypoint is copied last so a partial copy cannot be reported as a new CD build.
        foreach (string artifactName in artifactNames) {
            string artifactPath = Path.Combine(runRoot, "out", artifactName);
            File.Copy(artifactPath, Path.Combine(outputRootPath, artifactName), true);
        }
        File.Copy(manifestPath, Path.Combine(outputRootPath, "manifest.json"), true);
        string entryPoint = Path.Combine(outputRootPath, string.IsNullOrEmpty(sceneModulePlan) ? "helengine_ps1.ps-exe" : "helengine_ps1.cue");
        JsonObject launchArtifact = new() {
            ["entryPoint"] = Path.GetFullPath(entryPoint),
            ["artifactKind"] = string.IsNullOrEmpty(sceneModulePlan) ? "ps-exe" : "ps1-cd",
            ["artifactSha256"] = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(entryPoint)))
        };
        if (!string.IsNullOrEmpty(sceneModulePlan)) {
            string discPath = Path.Combine(outputRootPath, "helengine_ps1.bin");
            launchArtifact["discImagePath"] = Path.GetFullPath(discPath);
            using FileStream disc = File.OpenRead(discPath);
            launchArtifact["discImageSha256"] = Convert.ToHexString(SHA256.HashData(disc));
        }
        File.WriteAllText(Path.Combine(outputRootPath, "launch-artifact.json"), launchArtifact.ToJsonString());
        File.WriteAllText(Path.Combine(outputRootPath, "native-build-run-root.txt"), runRoot);
    }

    /// <summary>Resolves the configured native directory once and rejects scene-module builds without a fixed directory before candidate compilation.</summary>
    /// <param name="repositoryRoot">Platform checkout that must remain separate from native outputs.</param>
    /// <param name="generatedRoot">Generated source tree used to determine whether scene modules are enabled.</param>
    /// <param name="configuredRunRoot">Explicit fixed directory supplied by the build environment.</param>
    /// <returns>Normalized fixed directory, or empty when no native directory was selected.</returns>
    static string ResolveBuildRunRoot(string repositoryRoot, string generatedRoot, string configuredRunRoot) {
        if (string.IsNullOrWhiteSpace(configuredRunRoot)) {
            if (File.Exists(Path.Combine(generatedRoot, "scene-modules", "source-plan.json"))) {
                throw new InvalidOperationException($"{BuildRunRootEnvironmentVariableName} must identify a fixed native build directory for scene modules.");
            }
            return string.Empty;
        }
        if (!Path.IsPathFullyQualified(configuredRunRoot)) {
            throw new InvalidOperationException($"{BuildRunRootEnvironmentVariableName} must identify an absolute fixed directory.");
        }
        string runRoot = Path.TrimEndingDirectorySeparator(Path.GetFullPath(configuredRunRoot));
        if (runRoot.Equals(Path.TrimEndingDirectorySeparator(Path.GetPathRoot(runRoot)), StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException("The native build directory cannot be a volume root.");
        }
        foreach (string input in new[] { repositoryRoot, generatedRoot }) {
            string source = Path.TrimEndingDirectorySeparator(Path.GetFullPath(input));
            if (runRoot.Equals(source, StringComparison.OrdinalIgnoreCase)
                || runRoot.StartsWith(source + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)
                || source.StartsWith(runRoot + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) {
                throw new InvalidOperationException("The native build directory must not overlap source inputs: " + source);
            }
        }
        return runRoot;
    }

    /// <summary>Compiles candidate units in the pinned SDK, computes their actual symbol partition, and emits owning overlay registration sources.</summary>
    /// <param name="repositoryRoot">Platform checkout containing the official candidate compiler.</param>
    /// <param name="generatedRoot">Final generated source tree.</param>
    /// <param name="outputRoot">Fixed native artifact directory.</param>
    /// <param name="cancellationToken">Build cancellation signal.</param>
    /// <returns>Final partition plan, or empty for the unchanged monolithic build path.</returns>
    static string PrepareSceneModules(string repositoryRoot, string generatedRoot, string outputRoot, CancellationToken cancellationToken) {
        string sourcePlanPath = Path.Combine(generatedRoot, "scene-modules", "source-plan.json");
        if (!File.Exists(sourcePlanPath)) return string.Empty;
        string workspace = Path.GetFullPath(Path.Combine(repositoryRoot, ".."));
        string cacheRoot = Environment.GetEnvironmentVariable("HELENGINE_BUILD_CACHE_ROOT") ?? string.Empty;
        string inputRoot = workspace;
        string inputMount = "/workspace";
        string relativePlan = Path.GetRelativePath(inputRoot, sourcePlanPath).Replace('\\', '/');
        if (relativePlan.StartsWith("../", StringComparison.Ordinal) || Path.IsPathRooted(relativePlan)) {
            if (!Path.IsPathFullyQualified(cacheRoot)) throw new InvalidOperationException("External scene modules require HELENGINE_BUILD_CACHE_ROOT to name the mounted cache.");
            inputRoot = Path.GetFullPath(cacheRoot);
            inputMount = "/build-cache";
            relativePlan = Path.GetRelativePath(inputRoot, sourcePlanPath).Replace('\\', '/');
            if (relativePlan.StartsWith("../", StringComparison.Ordinal) || Path.IsPathRooted(relativePlan)) throw new InvalidOperationException("Scene code plan escapes the declared build cache.");
        }
        string candidatesRoot = Path.Combine(outputRoot, "scene-module-candidates");
        Directory.CreateDirectory(candidatesRoot);
        JsonObject sourcePlan = JsonNode.Parse(File.ReadAllText(sourcePlanPath)).AsObject();
        HashSet<string> omitted = Directory.EnumerateFiles(generatedRoot, "Batch2D*.cpp", SearchOption.AllDirectories)
            .Where(PlayStation1UnusedBatch2DPruner.IsPrunableBatchSource)
            .Select(Path.GetFileName).ToHashSet(StringComparer.Ordinal);
        sourcePlan["provenUnusedBatchSourceNames"] = new JsonArray(omitted.OrderBy(value => value, StringComparer.Ordinal).Select(value => JsonValue.Create(value)).ToArray());
        foreach (string key in new[] { "candidates", "residentRoots" }) {
            JsonArray values = sourcePlan[key].AsArray();
            for (int index = values.Count - 1; index >= 0; index--) {
                string source = key == "candidates" ? values[index]["sourcePath"].GetValue<string>() : values[index].GetValue<string>();
                if (omitted.Contains(Path.GetFileName(source))) values.RemoveAt(index);
            }
        }
        File.WriteAllText(sourcePlanPath, sourcePlan.ToJsonString());
        ProcessStartInfo info = new("docker") {
            WorkingDirectory = repositoryRoot, UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardOutput = true, RedirectStandardError = true
        };
        List<string> arguments = new() {
            "run", "--rm", "--mount", "type=bind,source=" + workspace + ",target=/workspace,readonly",
            "--mount", "type=bind,source=" + Path.GetFullPath(candidatesRoot) + ",target=/module-candidates"
        };
        if (inputMount != "/workspace") arguments.AddRange(new[] { "--mount", "type=bind,source=" + inputRoot + ",target=" + inputMount + ",readonly" });
        arguments.AddRange(new[] {
            "ghcr.io/grumpycoders/pcsx-redux-build@sha256:4f99ff0dd972096280cd6faf27c4255cc2bcf932397f8f9276ee22b6b1a67555",
            "python3", "-B", "/workspace/helengine-ps1/scripts/compile-scene-module-candidates.py",
            "--plan", inputMount + "/" + relativePlan, "--output", "/module-candidates", "--workspace", "/workspace",
            "--path-map", workspace, "/workspace"
        });
        if (inputMount != "/workspace") arguments.AddRange(new[] { "--path-map", inputRoot, inputMount });
        foreach (string argument in arguments) info.ArgumentList.Add(argument);
        using Process process = Process.Start(info) ?? throw new InvalidOperationException("Could not start the SDK scene-module candidate compiler.");
        Task<string> stdout = process.StandardOutput.ReadToEndAsync(cancellationToken);
        Task<string> stderr = process.StandardError.ReadToEndAsync(cancellationToken);
        process.WaitForExit();
        File.WriteAllText(Path.Combine(candidatesRoot, "candidate-compile.stdout.log"), stdout.GetAwaiter().GetResult());
        File.WriteAllText(Path.Combine(candidatesRoot, "candidate-compile.stderr.log"), stderr.GetAwaiter().GetResult());
        cancellationToken.ThrowIfCancellationRequested();
        if (process.ExitCode != 0) throw new InvalidOperationException("Actual PS1 SDK candidate compilation failed; see " + candidatesRoot);
        JsonObject symbols = JsonNode.Parse(File.ReadAllText(Path.Combine(candidatesRoot, "object-symbols.json"))).AsObject();
        JsonObject partition = new PlayStation1SceneCodeModuleSymbolPlanner().Plan(sourcePlan, symbols);
        return new PlayStation1SceneModuleBuildPlanWriter().WritePartition(generatedRoot, sourcePlan, partition);
    }

    /// <summary>Prefers PowerShell 7 and falls back to Windows PowerShell when pwsh is not installed.</summary>
    static string ResolvePowerShell() {
        string pathValue = Environment.GetEnvironmentVariable("PATH") ?? string.Empty;
        foreach (string directory in pathValue.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries)) {
            if (File.Exists(Path.Combine(directory, "pwsh.exe")) || File.Exists(Path.Combine(directory, "pwsh"))) {
                return "pwsh";
            }
        }
        return "powershell";
    }
}
