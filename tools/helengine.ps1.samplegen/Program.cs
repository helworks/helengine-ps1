using helengine.ps1.samplegen;

// Two modes. Without --full the tool exports the fixed camera-and-cube subset used by the boot
// milestone. With --full it exports the whole authored scene, optionally copying another platform's
// entity-existence overrides onto PS1 for scenes that declare none for it.
if (args.Length >= 1 && string.Equals(args[0], "--full", StringComparison.Ordinal)) {
    if (args.Length < 3) {
        Console.Error.WriteLine("Usage: helengine.ps1.samplegen --full <source-scene-path> <output-root> [--mirror-existence-from <platform-id>]");
        return 2;
    }

    string fullSourceScene = Path.GetFullPath(args[1]);
    string fullOutputRoot = Path.GetFullPath(args[2]);
    string mirrorFrom = string.Empty;
    for (int index = 3; index < args.Length; index++) {
        if (!string.Equals(args[index], "--mirror-existence-from", StringComparison.Ordinal)) {
            Console.Error.WriteLine($"Unexpected argument '{args[index]}'.");
            return 2;
        }
        if (index + 1 >= args.Length) {
            Console.Error.WriteLine("--mirror-existence-from requires a platform id.");
            return 2;
        }
        mirrorFrom = args[++index];
    }

    string exportedScenePath = new Ps1SceneFullExporter().Export(fullSourceScene, fullOutputRoot, mirrorFrom);
    Console.WriteLine($"Scene: {exportedScenePath}");
    Console.WriteLine($"Report: {Path.Combine(fullOutputRoot, "report.json")}");
    Console.WriteLine($"Mirrored existence from: {(mirrorFrom.Length == 0 ? "(none)" : mirrorFrom)}");
    return 0;
}

if (args.Length < 3 || args.Length > 4) {
    Console.Error.WriteLine("Usage: helengine.ps1.samplegen <project-root> <output-root> <engine-binary-root> [source-scene-path]");
    Console.Error.WriteLine("       helengine.ps1.samplegen --full <source-scene-path> <output-root> [--mirror-existence-from <platform-id>]");
    return 2;
}

string projectRoot = Path.GetFullPath(args[0]);
string outputRoot = Path.GetFullPath(args[1]);
string engineBinaryRoot = Path.GetFullPath(args[2]);
string sourceScene = args.Length == 4
    ? Path.GetFullPath(args[3])
    : Path.Combine(projectRoot, "assets", "scenes", "rendering", "cube_test.helen");

Ps1SceneExportResult result = new Ps1SceneSubsetExporter().Export(
    new Ps1SceneExportRequest(projectRoot, sourceScene, outputRoot, engineBinaryRoot));
Console.WriteLine($"Subset scene: {result.SubsetScenePath}");
Console.WriteLine($"Report: {result.ReportPath}");
Console.WriteLine($"Source SHA-256: {result.SourceSceneHash}");
Console.WriteLine($"Retained entities: {string.Join(", ", result.Report.RetainedEntities.Select(entity => $"{entity.EntityId}:{entity.Name}"))}");
Console.WriteLine($"Spin: baseYaw={result.Report.Spin.BaseYawRadians:R}, angularSpeed={result.Report.Spin.AngularSpeedRadians:R}");
return 0;
