namespace helengine.ps1.builder;

/// <summary>Console entry point used only for a manual smoke check of the builder assembly.</summary>
public static class Program {
    /// <summary>Prints the builder identity when invoked with <c>--describe</c>; otherwise prints usage.</summary>
    /// <param name="args">Command-line arguments.</param>
    /// <returns>Zero on success.</returns>
    public static int Main(string[] args) {
        if (args.Length == 1 && string.Equals(args[0], "--describe", StringComparison.OrdinalIgnoreCase)) {
            PlayStation1PlatformAssetBuilder builder = new();
            Console.WriteLine(builder.Descriptor.BuilderId);
            Console.WriteLine(builder.Descriptor.TargetPlatformId);
            Console.WriteLine(builder.Definition.DisplayName);
            return 0;
        }

        Console.WriteLine("helengine.ps1.builder --describe");
        return 0;
    }
}
