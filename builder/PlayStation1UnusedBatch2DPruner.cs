using System.Text.RegularExpressions;

namespace helengine.ps1.builder;

/// <summary>
/// Omits the shared 2D batching implementation from a PS1 unity build until a generated
/// runtime source actually uses it. The renderer still submits its own PS1 GPU packets.
/// </summary>
public static class PlayStation1UnusedBatch2DPruner {
    public static int Prune(string generatedCoreRootPath) {
        if (string.IsNullOrWhiteSpace(generatedCoreRootPath)) {
            throw new ArgumentException("Generated Core root is required.", nameof(generatedCoreRootPath));
        }

        string unityPath = Path.Combine(generatedCoreRootPath, "helengine_core_unity.cpp");
        if (!File.Exists(unityPath)) {
            throw new FileNotFoundException("Generated Core unity source is missing.", unityPath);
        }

        string[] batchSources = Directory.GetFiles(generatedCoreRootPath, "Batch2D*.cpp", SearchOption.AllDirectories);
        if (batchSources.Length == 0) {
            return 0;
        }

        HashSet<string> batchRelativePaths = new(StringComparer.OrdinalIgnoreCase);
        HashSet<string> batchTypeNames = new(StringComparer.Ordinal);
        foreach (string sourcePath in batchSources) {
            batchRelativePaths.Add(Path.GetRelativePath(generatedCoreRootPath, sourcePath).Replace('\\', '/'));
            batchTypeNames.Add(Path.GetFileNameWithoutExtension(sourcePath));
        }

        string typePattern = @"\b(?:" + string.Join("|", batchTypeNames.Select(Regex.Escape)) + @")\b";
        foreach (string sourcePath in Directory.GetFiles(generatedCoreRootPath, "*.*", SearchOption.AllDirectories)) {
            string extension = Path.GetExtension(sourcePath);
            if (!extension.Equals(".cpp", StringComparison.OrdinalIgnoreCase)
                && !extension.Equals(".hpp", StringComparison.OrdinalIgnoreCase)) {
                continue;
            }
            if (Path.GetFileName(sourcePath).StartsWith("Batch2D", StringComparison.OrdinalIgnoreCase)
                || Path.GetFileName(sourcePath).Equals("helengine_core_unity.cpp", StringComparison.OrdinalIgnoreCase)
                || Path.GetFileName(sourcePath).Equals("helengine_core_amalgamated.cpp", StringComparison.OrdinalIgnoreCase)
                || Path.GetFileName(sourcePath).Equals("generated_unity.cpp", StringComparison.OrdinalIgnoreCase)) {
                continue;
            }
            if (Regex.IsMatch(File.ReadAllText(sourcePath), typePattern, RegexOptions.CultureInvariant)) {
                throw new InvalidOperationException(
                    $"PS1 generated Core references 2D batching from '{sourcePath}'; the batch implementation must be ported before this build can use it.");
            }
        }

        string[] lines = File.ReadAllLines(unityPath);
        List<string> retained = new(lines.Length);
        int removed = 0;
        foreach (string line in lines) {
            Match include = Regex.Match(line, "^\\s*#include\\s+\"([^\"]+)\"\\s*$", RegexOptions.CultureInvariant);
            if (include.Success && batchRelativePaths.Contains(include.Groups[1].Value.Replace('\\', '/'))) {
                removed++;
                continue;
            }
            retained.Add(line);
        }
        if (removed != batchRelativePaths.Count) {
            throw new InvalidOperationException(
                $"Expected {batchRelativePaths.Count} generated 2D batch includes, found {removed}; refusing a partial PS1 prune.");
        }

        File.WriteAllLines(unityPath, retained);
        return removed;
    }
}
