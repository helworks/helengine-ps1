using helengine.ps1.samplegen;

if (args.Length >= 1 && string.Equals(args[0], "--static-menu", StringComparison.Ordinal)) {
    if (args.Length != 5) {
        Console.Error.WriteLine("Usage: helengine.ps1.samplegen --static-menu <source-scene-path> <output-root> <source-revision> <source-blob-id>");
        return 2;
    }

    string staticMenuSourceScene = Path.GetFullPath(args[1]);
    string staticMenuOutputRoot = Path.GetFullPath(args[2]);
    string staticMenuScenePath = new Ps1StaticMenuExporter().Export(
        staticMenuSourceScene,
        staticMenuOutputRoot,
        args[3],
        args[4]);
    Console.WriteLine($"Scene: {staticMenuScenePath}");
    Console.WriteLine($"Report: {Path.Combine(staticMenuOutputRoot, "report.json")}");
    return 0;
}

// Two modes. Without --full the tool exports the fixed camera-and-cube subset used by the boot
// milestone. With --full it exports the whole authored scene unchanged; the engine's packager decides
// what PS1 receives from the scene's override scopes and the project's platform groups.
if (args.Length >= 1 && string.Equals(args[0], "--full", StringComparison.Ordinal)) {
    if (args.Length != 3) {
        Console.Error.WriteLine("Usage: helengine.ps1.samplegen --full <source-scene-path> <output-root>");
        return 2;
    }

    string fullSourceScene = Path.GetFullPath(args[1]);
    string fullOutputRoot = Path.GetFullPath(args[2]);
    string exportedScenePath = new Ps1SceneFullExporter().Export(fullSourceScene, fullOutputRoot);
    Console.WriteLine($"Scene: {exportedScenePath}");
    Console.WriteLine($"Report: {Path.Combine(fullOutputRoot, "report.json")}");
    return 0;
}

if (args.Length < 3 || args.Length > 4) {
    Console.Error.WriteLine("Usage: helengine.ps1.samplegen <project-root> <output-root> <engine-binary-root> [source-scene-path]");
    Console.Error.WriteLine("       helengine.ps1.samplegen --full <source-scene-path> <output-root>");
    Console.Error.WriteLine("       helengine.ps1.samplegen --static-menu <source-scene-path> <output-root> <source-revision> <source-blob-id>");
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
