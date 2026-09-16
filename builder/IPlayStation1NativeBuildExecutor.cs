namespace helengine.ps1.builder;

/// <summary>Runs the pinned PS1 native build for an authored scene package.</summary>
public interface IPlayStation1NativeBuildExecutor {
    /// <summary>Builds the authored PS-X EXE and copies the artifacts into the output root.</summary>
    /// <param name="generatedCoreCppRootPath">Final generated Core root, including the scene catalog sources.</param>
    /// <param name="stagedContentRootPath">Root holding only the manifest-declared cooked files.</param>
    /// <param name="declarationsPath">Declared-file JSON consumed by the archive packer.</param>
    /// <param name="outputRootPath">Directory receiving helengine_ps1.ps-exe and its manifest.</param>
    /// <param name="cancellationToken">Cancellation token.</param>
    void Build(string generatedCoreCppRootPath, string stagedContentRootPath, string declarationsPath, string outputRootPath, CancellationToken cancellationToken);
}
