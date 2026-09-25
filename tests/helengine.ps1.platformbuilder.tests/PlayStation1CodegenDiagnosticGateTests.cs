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
    public void Validate_RejectsMalformedOrMissingReport() {
        File.WriteAllText(ReportPath, "{");
        Assert.Throws<InvalidDataException>(() => PlayStation1CodegenDiagnosticGate.Validate(Root, allowKnownBatch2DErrors: true));

        File.Delete(ReportPath);
        Assert.Throws<FileNotFoundException>(() => PlayStation1CodegenDiagnosticGate.Validate(Root, allowKnownBatch2DErrors: true));
    }

    void WriteReport(int errorCount, object[] diagnostics) => File.WriteAllText(ReportPath, JsonSerializer.Serialize(new { errorCount, diagnostics }));

    static Diagnostic KnownError() => new(
        "Error",
        "CPP1000",
        "Batch2DBuffer",
        "Batch2DBuffer",
        "MultiplyExpression",
        @"C:\dev\helworks\helengine\engine\helengine.core\managers\rendering\batch2d\Batch2DBuffer.cs");

    sealed record Diagnostic(string severity, string code, string sourceTypeName, string sourceMemberName, string syntaxKind, string filePath);
}
