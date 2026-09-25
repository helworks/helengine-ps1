using System.Text.Json;
using helengine.ps1.builder;

namespace helengine.ps1.platformbuilder.tests;

public sealed class PlayStation1CodegenDiagnosticGateTests : IDisposable {
    readonly string Root = Path.Combine(Path.GetTempPath(), "ps1-codegen-gate-tests", Guid.NewGuid().ToString("N"));
    string ReportPath => Path.Combine(Root, "cpp-conversion-report.json");

    public PlayStation1CodegenDiagnosticGateTests() => Directory.CreateDirectory(Root);

    public void Dispose() {
        if (Directory.Exists(Root)) Directory.Delete(Root, true);
    }

    [Fact]
    public void Validate_AcceptsReportWithNoErrors() {
        WriteReport(0, [MakeDiagnostic("Warning"), MakeDiagnostic("Info")]);

        PlayStation1CodegenDiagnosticGate.Validate(Root);
    }

    [Fact]
    public void Validate_RejectsAnyCodegenErrors() {
        WriteReport(2, [MakeDiagnostic("Error"), MakeDiagnostic("Error")]);

        Assert.Throws<InvalidOperationException>(() => PlayStation1CodegenDiagnosticGate.Validate(Root));
    }

    [Fact]
    public void Validate_RejectsErrorCountMismatch() {
        WriteReport(1, [MakeDiagnostic("Error"), MakeDiagnostic("Error")]);

        Assert.Throws<InvalidDataException>(() => PlayStation1CodegenDiagnosticGate.Validate(Root));
    }

    [Fact]
    public void Validate_RejectsUnknownSeverity() {
        WriteReport(0, [MakeDiagnostic("Fatal")]);

        Assert.Throws<InvalidDataException>(() => PlayStation1CodegenDiagnosticGate.Validate(Root));
    }

    [Fact]
    public void Validate_RejectsHasErrorsContradiction() {
        WriteReport(0, [], hasErrors: true);

        Assert.Throws<InvalidDataException>(() => PlayStation1CodegenDiagnosticGate.Validate(Root));
    }

    [Theory]
    [InlineData("cpp-conversion-report.v1", 2)]
    [InlineData("cpp-conversion-report.v2", 1)]
    public void Validate_RejectsUnknownReportSchemaOrVersion(string schema, int version) {
        WriteReport(0, [], schema: schema, version: version);

        Assert.Throws<InvalidDataException>(() => PlayStation1CodegenDiagnosticGate.Validate(Root));
    }

    [Theory]
    [InlineData(1, 0)]
    [InlineData(0, -1)]
    public void Validate_RejectsMismatchedOrInvalidWarningAndInfoCounters(int warningCount, int infoCount) {
        WriteReport(0, [], warningCount: warningCount, infoCount: infoCount);

        Assert.Throws<InvalidDataException>(() => PlayStation1CodegenDiagnosticGate.Validate(Root));
    }

    [Fact]
    public void Validate_RejectsMalformedOrMissingReport() {
        File.WriteAllText(ReportPath, "{");
        Assert.Throws<InvalidDataException>(() => PlayStation1CodegenDiagnosticGate.Validate(Root));

        File.Delete(ReportPath);
        Assert.Throws<FileNotFoundException>(() => PlayStation1CodegenDiagnosticGate.Validate(Root));
    }

    void WriteReport(
        int errorCount,
        object[] diagnostics,
        string schema = "cpp-conversion-report.v2",
        int version = 2,
        bool? hasErrors = null,
        int? warningCount = null,
        int? infoCount = null) {
        Diagnostic[] typedDiagnostics = diagnostics.OfType<Diagnostic>().ToArray();
        int actualWarningCount = typedDiagnostics.Count(diagnostic => diagnostic.severity == "Warning");
        int actualInfoCount = typedDiagnostics.Count(diagnostic => diagnostic.severity == "Info");
        object report = new {
            diagnosticsSchema = schema,
            diagnosticsVersion = version,
            hasErrors = hasErrors ?? errorCount > 0,
            errorCount,
            warningCount = warningCount ?? actualWarningCount,
            infoCount = infoCount ?? actualInfoCount,
            diagnostics
        };
        File.WriteAllText(ReportPath, JsonSerializer.Serialize(report));
    }

    static Diagnostic MakeDiagnostic(string severity) => new(severity);

    sealed record Diagnostic(string severity);
}
