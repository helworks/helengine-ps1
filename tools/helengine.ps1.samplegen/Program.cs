using helengine.ps1.samplegen;

if (args.Length < 3 || args.Length > 4) {
    Console.Error.WriteLine("Usage: helengine.ps1.samplegen <project-root> <output-root> <engine-binary-root> [source-scene-path]");
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
