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
        WriteReport(0, []);

        PlayStation1CodegenDiagnosticGate.Validate(Root, allowKnownBatch2DErrors: false);
    }

    [Fact]
    public void Validate_AcceptsExactlyKnownBatch2DPairWhenOptedIn() {
        WriteReport(2, [KnownError(), KnownError()]);

        PlayStation1CodegenDiagnosticGate.Validate(Root, allowKnownBatch2DErrors: true);
    }

    [Fact]
    public void Validate_RejectsKnownBatch2DPairWithoutOptIn() {
        WriteReport(2, [KnownError(), KnownError()]);

        Assert.Throws<InvalidOperationException>(() => PlayStation1CodegenDiagnosticGate.Validate(Root, allowKnownBatch2DErrors: false));
    }

    [Theory]
    [MemberData(nameof(UnexpectedDiagnosticReports))]
    public void Validate_RejectsUnexpectedDiagnosticEvenWhenOptedIn(int errorCount, object[] diagnostics) {
        WriteReport(errorCount, diagnostics);

        Assert.Throws<InvalidOperationException>(() => PlayStation1CodegenDiagnosticGate.Validate(Root, allowKnownBatch2DErrors: true));
    }

    public static IEnumerable<object[]> UnexpectedDiagnosticReports() {
        yield return new object[] { 2, new object[] { KnownError(), KnownError() with { filePath = @"C:\engine\other\Batch2DBuffer.cs" } } };
        yield return new object[] { 2, new object[] { KnownError(), KnownError() with { code = "CPP1001" } } };
        yield return new object[] { 3, new object[] { KnownError(), KnownError(), KnownError() } };
    }

    [Fact]
    public void Validate_RejectsErrorCountMismatch() {
        WriteReport(1, [KnownError(), KnownError()]);

        Assert.Throws<InvalidDataException>(() => PlayStation1CodegenDiagnosticGate.Validate(Root, allowKnownBatch2DErrors: true));
    }

    [Fact]
    public void Validate_RejectsUnknownSeverity() {
        WriteReport(0, [KnownError() with { severity = "Fatal" }]);

        Assert.Throws<InvalidDataException>(() => PlayStation1CodegenDiagnosticGate.Validate(Root, allowKnownBatch2DErrors: true));
    }

    [Fact]
    public void Validate_RejectsHasErrorsContradiction() {
        WriteReport(0, [], hasErrors: true);

        Assert.Throws<InvalidDataException>(() => PlayStation1CodegenDiagnosticGate.Validate(Root, allowKnownBatch2DErrors: true));
    }

    [Theory]
    [InlineData("cpp-conversion-report.v1", 2)]
    [InlineData("cpp-conversion-report.v2", 1)]
    public void Validate_RejectsUnknownReportSchemaOrVersion(string schema, int version) {
        WriteReport(0, [], schema: schema, version: version);

        Assert.Throws<InvalidDataException>(() => PlayStation1CodegenDiagnosticGate.Validate(Root, allowKnownBatch2DErrors: true));
    }

    [Theory]
    [InlineData(1, 0)]
    [InlineData(0, -1)]
    public void Validate_RejectsMismatchedOrInvalidWarningAndInfoCounters(int warningCount, int infoCount) {
        WriteReport(0, [], warningCount: warningCount, infoCount: infoCount);

        Assert.Throws<InvalidDataException>(() => PlayStation1CodegenDiagnosticGate.Validate(Root, allowKnownBatch2DErrors: true));
    }

    [Fact]
    public void Validate_RejectsMalformedOrMissingReport() {
        File.WriteAllText(ReportPath, "{");
        Assert.Throws<InvalidDataException>(() => PlayStation1CodegenDiagnosticGate.Validate(Root, allowKnownBatch2DErrors: true));

        File.Delete(ReportPath);
        Assert.Throws<FileNotFoundException>(() => PlayStation1CodegenDiagnosticGate.Validate(Root, allowKnownBatch2DErrors: true));
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

    static Diagnostic KnownError() => new(
        "Error",
        "CPP1000",
        "Batch2DBuffer",
        "Batch2DBuffer",
        "MultiplyExpression",
        @"C:\dev\helworks\helengine\engine\helengine.core\managers\rendering\batch2d\Batch2DBuffer.cs");

    sealed record Diagnostic(string severity, string code, string sourceTypeName, string sourceMemberName, string syntaxKind, string filePath);
}
