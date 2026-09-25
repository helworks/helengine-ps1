using System.Text.Json;

namespace helengine.ps1.builder;

/// <summary>
/// Fails closed on Core conversion diagnostics, with one PS1-local opt-in for the two
/// known checked-multiply diagnostics from the unused Batch2DBuffer implementation.
/// </summary>
public static class PlayStation1CodegenDiagnosticGate {
    const string ReportFileName = "cpp-conversion-report.json";
    const string Batch2DSourceSuffix = "/engine/helengine.core/managers/rendering/batch2d/Batch2DBuffer.cs";

    /// <summary>Validates the generated Core conversion report before native compilation.</summary>
    /// <param name="generatedCoreRootPath">Generated Core directory.</param>
    /// <param name="allowKnownBatch2DErrors">Whether to allow the exact known pair.</param>
    public static void Validate(string generatedCoreRootPath, bool allowKnownBatch2DErrors) {
        if (string.IsNullOrWhiteSpace(generatedCoreRootPath)) {
            throw new ArgumentException("Generated Core root is required.", nameof(generatedCoreRootPath));
        }

        string reportPath = Path.Combine(generatedCoreRootPath, ReportFileName);
        if (!File.Exists(reportPath)) {
            throw new FileNotFoundException("PS1 generated Core conversion report is missing.", reportPath);
        }

        using JsonDocument report = ReadReport(reportPath);
        JsonElement root = report.RootElement;
        if (root.ValueKind != JsonValueKind.Object
            || !root.TryGetProperty("diagnostics", out JsonElement diagnostics)
            || diagnostics.ValueKind != JsonValueKind.Array
            || !root.TryGetProperty("errorCount", out JsonElement errorCountElement)
            || !errorCountElement.TryGetInt32(out int reportedErrorCount)
            || reportedErrorCount < 0) {
            throw new InvalidDataException($"Malformed PS1 conversion report: {reportPath}");
        }

        List<JsonElement> allDiagnostics = diagnostics.EnumerateArray().ToList();
        int actualErrorCount = 0;
        foreach (JsonElement diagnostic in allDiagnostics) {
            if (diagnostic.ValueKind != JsonValueKind.Object
                || !diagnostic.TryGetProperty("severity", out JsonElement severity)
                || severity.ValueKind != JsonValueKind.String) {
                throw new InvalidDataException($"Malformed diagnostic in PS1 conversion report: {reportPath}");
            }
            if (string.Equals(severity.GetString(), "Error", StringComparison.OrdinalIgnoreCase)) actualErrorCount++;
        }

        if (actualErrorCount != reportedErrorCount) {
            throw new InvalidDataException(
                $"PS1 conversion report error count mismatch: errorCount={reportedErrorCount}, diagnostics contain {actualErrorCount} errors.");
        }
        if (actualErrorCount == 0) return;

        if (allowKnownBatch2DErrors && IsExactKnownBatch2DErrorPair(allDiagnostics)) return;

        throw new InvalidOperationException(
            $"PS1 generated Core contains {actualErrorCount} C++ conversion error(s); refusing native build. See '{reportPath}'.");
    }

    static JsonDocument ReadReport(string reportPath) {
        try {
            return JsonDocument.Parse(File.ReadAllBytes(reportPath));
        } catch (JsonException exception) {
            throw new InvalidDataException($"Malformed PS1 conversion report: {reportPath}", exception);
        }
    }

    static bool IsExactKnownBatch2DErrorPair(IReadOnlyList<JsonElement> diagnostics) {
        if (diagnostics.Count != 2) return false;
        foreach (JsonElement diagnostic in diagnostics) {
            if (!HasString(diagnostic, "severity", "Error")
                || !HasString(diagnostic, "code", "CPP1000")
                || !HasString(diagnostic, "sourceTypeName", "Batch2DBuffer")
                || !HasString(diagnostic, "sourceMemberName", "Batch2DBuffer")
                || !HasString(diagnostic, "syntaxKind", "MultiplyExpression")
                || !HasSourceSuffix(diagnostic)) {
                return false;
            }
        }
        return true;
    }

    static bool HasString(JsonElement element, string propertyName, string expected) =>
        element.TryGetProperty(propertyName, out JsonElement value)
        && value.ValueKind == JsonValueKind.String
        && string.Equals(value.GetString(), expected, StringComparison.Ordinal);

    static bool HasSourceSuffix(JsonElement diagnostic) {
        if (!diagnostic.TryGetProperty("filePath", out JsonElement filePath)
            || filePath.ValueKind != JsonValueKind.String) return false;
        string normalized = (filePath.GetString() ?? string.Empty).Replace('\\', '/');
        return normalized.EndsWith(Batch2DSourceSuffix, StringComparison.OrdinalIgnoreCase);
    }
}
