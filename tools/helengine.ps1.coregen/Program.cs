using System.Reflection;
using System.Runtime.Loader;

namespace helengine.ps1.coregen;

/// <summary>Invokes the engine-owned final Core translation-unit writer after successful conversion.</summary>
internal static class Program {
    /// <summary>Finalizes one generated Core directory with the existing engine export writer.</summary>
    /// <param name="args">Generated Core root followed by the engine binary directory.</param>
    /// <returns>Zero after a final Core translation unit has been written.</returns>
    static int Main(string[] args) {
        if (args.Length != 2) {
            Console.Error.WriteLine("Usage: coregen <generated-core-root> <engine-binary-root>");
            return 2;
        }
        string outputRoot = Path.GetFullPath(args[0]);
        string binaryRoot = Path.GetFullPath(args[1]);
        foreach (string requiredFile in new[] { "Core.cpp", "Core.hpp", "helcpp_config.hpp", "generated_unity.cpp" }) {
            if (!File.Exists(Path.Combine(outputRoot, requiredFile))) {
                throw new FileNotFoundException($"Core conversion input is missing: {requiredFile}");
            }
        }
        AssemblyLoadContext.Default.Resolving += (context, name) => {
            string path = Path.Combine(binaryRoot, name.Name + ".dll");
            return File.Exists(path) ? context.LoadFromAssemblyPath(path) : null;
        };
        Assembly editor = AssemblyLoadContext.Default.LoadFromAssemblyPath(Path.Combine(binaryRoot, "helengine.editor.dll"));
        Type writerType = editor.GetType("helengine.editor.EditorGeneratedCoreRegenerationService", true);
        MethodInfo writer = writerType.GetMethod("WriteGeneratedCoreTranslationUnit", BindingFlags.Static | BindingFlags.NonPublic)
            ?? throw new MissingMethodException("The engine's final Core translation-unit writer is unavailable.");
        writer.Invoke(null, new object[] { outputRoot });
        string finalSource = Path.Combine(outputRoot, "helengine_core_unity.cpp");
        if (!File.Exists(finalSource)) { throw new FileNotFoundException("Core finalization did not produce its translation unit.", finalSource); }
        Console.WriteLine(finalSource);
        return 0;
    }
}
