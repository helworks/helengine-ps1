using System.Reflection;

namespace helengine.ps1.platformbuilder.tests;

/// <summary>Exercises the native executor's fixed-directory handoff through a real child PowerShell wrapper without invoking the SDK.</summary>
public sealed class PlayStation1NativeBuildRunRootTests {
    /// <summary>Monolith builds without the environment variable bind the fixed native folder beside the output folder.</summary>
    [Fact]
    public void Monolith_without_env_passes_sibling_native_run_root() {
        string root = CreateFixture("monolith-default");
        try {
            string output = Path.Combine(root, "out", "package");
            string recordedRunRoot = RunExecutor(root, null, output);
            Assert.Equal(Path.GetFullPath(Path.Combine(root, "out", "native")), recordedRunRoot);
        } finally {
            DeleteFixture(root);
        }
    }

    /// <summary>Scene-module builds without the environment variable resolve the same sibling native folder instead of throwing.</summary>
    [Fact]
    public void Scene_modules_without_env_pass_sibling_native_run_root() {
        string root = CreateFixture("module-default");
        try {
            string generated = Path.Combine(root, "generated");
            Directory.CreateDirectory(Path.Combine(generated, "scene-modules"));
            File.WriteAllText(Path.Combine(generated, "scene-modules", "source-plan.json"), "{}");
            string output = Path.Combine(root, "out", "package");
            MethodInfo method = typeof(builder.PlayStation1NativeBuildExecutor).GetMethod("ResolveBuildRunRoot", BindingFlags.NonPublic | BindingFlags.Static);
            string resolved = (string)method.Invoke(null, new object[] { Path.Combine(root, "workspace", "helengine-ps1"), generated, output, string.Empty });
            Assert.Equal(Path.GetFullPath(Path.Combine(root, "out", "native")), resolved);
        } finally {
            DeleteFixture(root);
        }
    }

    /// <summary>An explicit environment run root wins over the default and is passed through unchanged, including spaces.</summary>
    [Fact]
    public void Explicit_env_run_root_wins() {
        string root = CreateFixture("explicit-root");
        try {
            string custom = Path.Combine(root, "custom run root");
            string recordedRunRoot = RunExecutor(root, custom, Path.Combine(root, "out", "package"));
            Assert.Equal(Path.GetFullPath(custom), recordedRunRoot);
        } finally {
            DeleteFixture(root);
        }
    }

    /// <summary>Runs the executor against the fixture wrapper with the run-root variable set or cleared, returning the run root the wrapper received.</summary>
    /// <param name="root">Fixture root created by CreateFixture.</param>
    /// <param name="configuredRunRoot">Value for the run-root environment variable, or null to leave it unset.</param>
    /// <param name="output">Output root handed to the executor.</param>
    /// <returns>The run root recorded in the wrapper manifest, asserted to have been bound as an argument.</returns>
    static string RunExecutor(string root, string configuredRunRoot, string output) {
        string generated = Path.Combine(root, "generated");
        Directory.CreateDirectory(generated);
        File.WriteAllText(Path.Combine(generated, "helengine_core_unity.cpp"), "// Empty wrapper handoff fixture; no SDK translation units.\n");
        File.WriteAllText(Path.Combine(generated, "cpp-conversion-report.json"), """
            {"diagnosticsSchema":"cpp-conversion-report.v2","diagnosticsVersion":2,"hasErrors":false,"errorCount":0,"warningCount":0,"infoCount":0,"diagnostics":[]}
            """);
        string previousRepository = Environment.GetEnvironmentVariable(builder.PlayStation1NativeBuildExecutor.RepositoryRootEnvironmentVariableName);
        string previousRunRoot = Environment.GetEnvironmentVariable(builder.PlayStation1NativeBuildExecutor.BuildRunRootEnvironmentVariableName);
        try {
            Environment.SetEnvironmentVariable(builder.PlayStation1NativeBuildExecutor.RepositoryRootEnvironmentVariableName, Path.Combine(root, "workspace", "helengine-ps1"));
            Environment.SetEnvironmentVariable(builder.PlayStation1NativeBuildExecutor.BuildRunRootEnvironmentVariableName, configuredRunRoot);
            new builder.PlayStation1NativeBuildExecutor().Build(generated, Path.Combine(root, "content"), Path.Combine(root, "declarations.json"), output, CancellationToken.None);
            using System.Text.Json.JsonDocument manifest = System.Text.Json.JsonDocument.Parse(File.ReadAllText(Path.Combine(output, "manifest.json")));
            Assert.True(manifest.RootElement.GetProperty("runRootArgumentBound").GetBoolean());
            Assert.True(manifest.RootElement.GetProperty("authored").GetBoolean());
            return manifest.RootElement.GetProperty("runRoot").GetString();
        } finally {
            Environment.SetEnvironmentVariable(builder.PlayStation1NativeBuildExecutor.RepositoryRootEnvironmentVariableName, previousRepository);
            Environment.SetEnvironmentVariable(builder.PlayStation1NativeBuildExecutor.BuildRunRootEnvironmentVariableName, previousRunRoot);
        }
    }

    /// <summary>Removes a fixture folder so no test output remains under the temp root.</summary>
    /// <param name="root">Fixture root created by CreateFixture.</param>
    static void DeleteFixture(string root) {
        if (Directory.Exists(root)) Directory.Delete(root, true);
        string parent = Path.GetDirectoryName(root);
        if (Directory.Exists(parent) && !Directory.EnumerateFileSystemEntries(parent).Any()) Directory.Delete(parent);
    }

    /// <summary>Writes a fixed fixture checkout whose wrapper records argument binding and produces only small handoff-test artifacts.</summary>
    /// <param name="name">Stable case directory beneath the workspace-owned test output root.</param>
    /// <returns>Fixture root; generated SDK inputs and outputs remain isolated from the actual build graph.</returns>
    static string CreateFixture(string name) {
        string root = Path.Combine("C:/temp/ps1-runroot-tests", name);
        string scripts = Path.Combine(root, "workspace", "helengine-ps1", "scripts");
        Directory.CreateDirectory(scripts);
        File.WriteAllText(Path.Combine(scripts, "build.ps1"), """
            param([string]$Configuration, [switch]$Authored, [string]$RunRoot,
                [string]$GeneratedCoreRoot, [string]$AuthoredContentRoot, [string]$AuthoredDeclarations)
            $ErrorActionPreference = 'Stop'
            $bound = $PSBoundParameters.ContainsKey('RunRoot')
            $RunRoot = [IO.Path]::GetFullPath($RunRoot)
            New-Item -ItemType Directory -Force -Path (Join-Path $RunRoot 'out') | Out-Null
            [IO.File]::WriteAllText((Join-Path $RunRoot 'out/helengine_ps1.ps-exe'), 'fixture-executable')
            [IO.File]::WriteAllText((Join-Path $RunRoot 'out/helengine_ps1.elf'), 'fixture-elf')
            [IO.File]::WriteAllText((Join-Path $RunRoot 'out/helengine_ps1.map'), 'fixture-map')
            $manifest = Join-Path $RunRoot 'manifest.json'
            @{ runRootArgumentBound = $bound; runRoot = $RunRoot; authored = [bool]$Authored } |
                ConvertTo-Json | Set-Content -LiteralPath $manifest -Encoding utf8
            Write-Output $manifest
            """);
        return root;
    }
}
