using System.Text.Json;

namespace helengine.ps1.builder;

/// <summary>
/// Fails closed on Core conversion diagnostics before native compilation.
/// </summary>
public static class PlayStation1CodegenDiagnosticGate {
    const string ReportFileName = "cpp-conversion-report.json";
    const string ExpectedDiagnosticsSchema = "cpp-conversion-report.v2";
    const int ExpectedDiagnosticsVersion = 2;
    /// <summary>Validates the generated Core conversion report before native compilation.</summary>
    /// <param name="generatedCoreRootPath">Generated Core directory.</param>
    public static void Validate(string generatedCoreRootPath) {
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
            || !HasString(root, "diagnosticsSchema", ExpectedDiagnosticsSchema)
            || !root.TryGetProperty("diagnosticsVersion", out JsonElement versionElement)
            || !versionElement.TryGetInt32(out int version)
            || version != ExpectedDiagnosticsVersion
            || !root.TryGetProperty("diagnostics", out JsonElement diagnostics)
            || diagnostics.ValueKind != JsonValueKind.Array
            || !root.TryGetProperty("hasErrors", out JsonElement hasErrorsElement)
            || hasErrorsElement.ValueKind is not (JsonValueKind.True or JsonValueKind.False)
            || !root.TryGetProperty("errorCount", out JsonElement errorCountElement)
            || !errorCountElement.TryGetInt32(out int reportedErrorCount)
            || reportedErrorCount < 0
            || !root.TryGetProperty("warningCount", out JsonElement warningCountElement)
            || !warningCountElement.TryGetInt32(out int reportedWarningCount)
            || reportedWarningCount < 0
            || !root.TryGetProperty("infoCount", out JsonElement infoCountElement)
            || !infoCountElement.TryGetInt32(out int reportedInfoCount)
            || reportedInfoCount < 0) {
            throw new InvalidDataException($"Malformed PS1 conversion report: {reportPath}");
        }

        List<JsonElement> allDiagnostics = diagnostics.EnumerateArray().ToList();
        int actualErrorCount = 0;
        int actualWarningCount = 0;
        int actualInfoCount = 0;
        foreach (JsonElement diagnostic in allDiagnostics) {
            if (diagnostic.ValueKind != JsonValueKind.Object
                || !diagnostic.TryGetProperty("severity", out JsonElement severity)
                || severity.ValueKind != JsonValueKind.String) {
                throw new InvalidDataException($"Malformed diagnostic in PS1 conversion report: {reportPath}");
            }
            switch (severity.GetString()) {
                case "Error": actualErrorCount++; break;
                case "Warning": actualWarningCount++; break;
                case "Info": actualInfoCount++; break;
                default:
                    throw new InvalidDataException($"Unknown diagnostic severity in PS1 conversion report: {reportPath}");
            }
        }

        if (actualErrorCount != reportedErrorCount
            || actualWarningCount != reportedWarningCount
            || actualInfoCount != reportedInfoCount) {
            throw new InvalidDataException(
                $"PS1 conversion report count mismatch: reported errors/warnings/info={reportedErrorCount}/{reportedWarningCount}/{reportedInfoCount}, "
                + $"diagnostics contain {actualErrorCount}/{actualWarningCount}/{actualInfoCount}.");
        }
        if (hasErrorsElement.GetBoolean() != (actualErrorCount > 0)) {
            throw new InvalidDataException(
                $"PS1 conversion report hasErrors mismatch: hasErrors={hasErrorsElement.GetBoolean()}, errorCount={actualErrorCount}.");
        }
        if (actualErrorCount != 0) {
            throw new InvalidOperationException(
                $"PS1 generated Core contains {actualErrorCount} C++ conversion error(s); refusing native build. See '{reportPath}'.");
        }
    }

    static JsonDocument ReadReport(string reportPath) {
        try {
            return JsonDocument.Parse(File.ReadAllBytes(reportPath));
        } catch (JsonException exception) {
            throw new InvalidDataException($"Malformed PS1 conversion report: {reportPath}", exception);
        }
    }

    static bool HasString(JsonElement element, string propertyName, string expected) =>
        element.TryGetProperty(propertyName, out JsonElement value)
        && value.ValueKind == JsonValueKind.String
        && string.Equals(value.GetString(), expected, StringComparison.Ordinal);
}
