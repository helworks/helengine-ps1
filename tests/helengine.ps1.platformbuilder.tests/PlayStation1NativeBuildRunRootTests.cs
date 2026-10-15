namespace helengine.ps1.platformbuilder.tests;

/// <summary>Exercises the native executor's fixed-directory handoff through a real child PowerShell wrapper without invoking the SDK.</summary>
public sealed class PlayStation1NativeBuildRunRootTests {
    /// <summary>Rejects module builds before reports, plan parsing or candidate compilation when the fixed run directory is absent.</summary>
    [Fact]
    public void Build_WhenSceneModulesHaveNoRunRoot_RejectsBeforePreparingCandidates() {
        string root = CreateFixture("module-missing-root");
        string generated = Path.Combine(root, "generated");
        Directory.CreateDirectory(Path.Combine(generated, "scene-modules"));
        File.WriteAllText(Path.Combine(generated, "scene-modules", "source-plan.json"), "invalid plan must not be parsed");
        string previousRepository = Environment.GetEnvironmentVariable(builder.PlayStation1NativeBuildExecutor.RepositoryRootEnvironmentVariableName);
        string previousRunRoot = Environment.GetEnvironmentVariable(builder.PlayStation1NativeBuildExecutor.BuildRunRootEnvironmentVariableName);
        try {
            Environment.SetEnvironmentVariable(builder.PlayStation1NativeBuildExecutor.RepositoryRootEnvironmentVariableName, Path.Combine(root, "workspace", "helengine-ps1"));
            Environment.SetEnvironmentVariable(builder.PlayStation1NativeBuildExecutor.BuildRunRootEnvironmentVariableName, null);
            InvalidOperationException error = Assert.Throws<InvalidOperationException>(() => new builder.PlayStation1NativeBuildExecutor().Build(
                generated, Path.Combine(root, "content"), Path.Combine(root, "declarations.json"), Path.Combine(root, "output"), CancellationToken.None));
            Assert.Contains(builder.PlayStation1NativeBuildExecutor.BuildRunRootEnvironmentVariableName, error.Message, StringComparison.Ordinal);
            Assert.False(Directory.Exists(Path.Combine(root, "output", "scene-module-candidates")));
        } finally {
            Environment.SetEnvironmentVariable(builder.PlayStation1NativeBuildExecutor.RepositoryRootEnvironmentVariableName, previousRepository);
            Environment.SetEnvironmentVariable(builder.PlayStation1NativeBuildExecutor.BuildRunRootEnvironmentVariableName, previousRunRoot);
        }
    }

    /// <summary>Verifies explicit fixed-directory binding survives spaces and that the absent-setting legacy route still omits the argument.</summary>
    [Theory]
    [InlineData(true)]
    [InlineData(false)]
    public void Build_WhenWrapperRuns_ForwardsConfiguredRootAndPreservesLegacyAbsence(bool configured) {
        string root = CreateFixture(configured ? "configured-root" : "legacy-root");
        string repository = Path.Combine(root, "workspace", "helengine-ps1");
        string generated = Path.Combine(root, "generated");
        Directory.CreateDirectory(generated);
        File.WriteAllText(Path.Combine(generated, "helengine_core_unity.cpp"), "// Empty wrapper handoff fixture; no SDK translation units.\n");
        File.WriteAllText(Path.Combine(generated, "cpp-conversion-report.json"), """
            {"diagnosticsSchema":"cpp-conversion-report.v2","diagnosticsVersion":2,"hasErrors":false,"errorCount":0,"warningCount":0,"infoCount":0,"diagnostics":[]}
            """);
        string configuredRoot = Path.Combine(root, "workspace", "builds", "helengine-ps1", "fixed native run root");
        string expectedRoot = configured ? configuredRoot : Path.Combine(root, "workspace", "builds", "helengine-ps1", "legacy-fixed");
        string previousRepository = Environment.GetEnvironmentVariable(builder.PlayStation1NativeBuildExecutor.RepositoryRootEnvironmentVariableName);
        string previousRunRoot = Environment.GetEnvironmentVariable(builder.PlayStation1NativeBuildExecutor.BuildRunRootEnvironmentVariableName);
        try {
            Environment.SetEnvironmentVariable(builder.PlayStation1NativeBuildExecutor.RepositoryRootEnvironmentVariableName, repository);
            Environment.SetEnvironmentVariable(builder.PlayStation1NativeBuildExecutor.BuildRunRootEnvironmentVariableName, configured ? configuredRoot : null);
            string output = Path.Combine(root, "output");
            new builder.PlayStation1NativeBuildExecutor().Build(generated, Path.Combine(root, "content"), Path.Combine(root, "declarations.json"), output, CancellationToken.None);
            using System.Text.Json.JsonDocument manifest = System.Text.Json.JsonDocument.Parse(File.ReadAllText(Path.Combine(output, "manifest.json")));
            Assert.Equal(configured, manifest.RootElement.GetProperty("runRootArgumentBound").GetBoolean());
            Assert.Equal(Path.GetFullPath(expectedRoot), manifest.RootElement.GetProperty("runRoot").GetString());
            Assert.True(manifest.RootElement.GetProperty("authored").GetBoolean());
            Assert.Equal(Path.GetFullPath(expectedRoot), File.ReadAllText(Path.Combine(output, "native-build-run-root.txt")));
            Assert.Equal("fixture-executable", File.ReadAllText(Path.Combine(output, "helengine_ps1.ps-exe")));
        } finally {
            Environment.SetEnvironmentVariable(builder.PlayStation1NativeBuildExecutor.RepositoryRootEnvironmentVariableName, previousRepository);
            Environment.SetEnvironmentVariable(builder.PlayStation1NativeBuildExecutor.BuildRunRootEnvironmentVariableName, previousRunRoot);
        }
    }

    /// <summary>Writes a fixed fixture checkout whose wrapper records argument binding and produces only small handoff-test artifacts.</summary>
    /// <param name="name">Stable case directory beneath the workspace-owned test output root.</param>
    /// <returns>Fixture root; generated SDK inputs and outputs remain isolated from the actual build graph.</returns>
    static string CreateFixture(string name) {
        string root = Path.Combine("C:/dev/helworks/builds/helengine-ps1/menu-20261007/scene-modules/native-run-root-tests", name);
        string scripts = Path.Combine(root, "workspace", "helengine-ps1", "scripts");
        Directory.CreateDirectory(scripts);
        File.WriteAllText(Path.Combine(scripts, "build.ps1"), """
            param([string]$Configuration, [switch]$Authored, [string]$RunRoot,
                [string]$GeneratedCoreRoot, [string]$AuthoredContentRoot, [string]$AuthoredDeclarations)
            $ErrorActionPreference = 'Stop'
            $bound = $PSBoundParameters.ContainsKey('RunRoot')
            if (!$bound) {
                $workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
                $RunRoot = Join-Path $workspace 'builds/helengine-ps1/legacy-fixed'
            }
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
