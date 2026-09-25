using System.Diagnostics;

namespace helengine.ps1.builder;

/// <summary>
/// Invokes scripts/build.ps1 in authored mode inside the PS1 implementation checkout named by
/// HELENGINE_PS1_REPOSITORY_ROOT, then copies the produced executable, manifest and link map to the
/// output root. The build script owns the pinned Docker toolchain and the retail memory check.
/// </summary>
public sealed class PlayStation1NativeBuildExecutor : IPlayStation1NativeBuildExecutor {
    /// <summary>Environment variable naming the helengine-ps1 implementation checkout.</summary>
    public const string RepositoryRootEnvironmentVariableName = "HELENGINE_PS1_REPOSITORY_ROOT";

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
        Directory.CreateDirectory(outputRootPath);
        PlayStation1CodegenDiagnosticGate.Validate(generatedCoreCppRootPath);
        int omittedBatchSources = PlayStation1UnusedBatch2DPruner.Prune(generatedCoreCppRootPath);
        if (omittedBatchSources != 0) {
            Console.WriteLine($"[ps1] omitted {omittedBatchSources} unused generated 2D batch sources");
        }

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
        info.ArgumentList.Add("-GeneratedCoreRoot");
        info.ArgumentList.Add(generatedCoreCppRootPath);
        info.ArgumentList.Add("-AuthoredContentRoot");
        info.ArgumentList.Add(stagedContentRootPath);
        info.ArgumentList.Add("-AuthoredDeclarations");
        info.ArgumentList.Add(declarationsPath);

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
        File.Copy(manifestPath, Path.Combine(outputRootPath, "manifest.json"), true);
        foreach (string artifactName in new[] { "helengine_ps1.ps-exe", "helengine_ps1.elf", "helengine_ps1.map" }) {
            string artifactPath = Path.Combine(runRoot, "out", artifactName);
            if (!File.Exists(artifactPath)) throw new FileNotFoundException($"PS1 native build did not produce {artifactName}.", artifactPath);
            File.Copy(artifactPath, Path.Combine(outputRootPath, artifactName), true);
        }
        File.WriteAllText(Path.Combine(outputRootPath, "native-build-run-root.txt"), runRoot);
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
