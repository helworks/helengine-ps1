using helengine.ps1.builder;

namespace helengine.ps1.platformbuilder.tests;

/// <summary>Preserves the shared sprite geometry implementation in PS1 builds while excluding unused GPU batching.</summary>
public sealed class PlayStation1UnusedBatch2DPrunerTests {
    /// <summary>Exercises actual unity pruning when command payloads reference geometry and geometry references vertex values.</summary>
    [Fact]
    public void Prune_RetainsGeometryValues_AndOmitsWriterEmitter() {
        string root = Path.Combine(@"C:\temp", "helengine-ps1-pruner-tests");
        Directory.CreateDirectory(root);
        try {
            string[] sources = { "Batch2DGeometry.cpp", "Batch2DVertex.cpp", "Batch2DWriter.cpp", "Batch2DDrawableEmitter.cpp", "SpriteQuad2D.cpp", "RenderCommandList2D.cpp" };
            foreach (string name in sources) File.WriteAllText(Path.Combine(root, name), string.Empty);
            File.WriteAllText(Path.Combine(root, "Batch2DGeometry.cpp"), "Batch2DVertex ResolveSpriteQuad();");
            File.WriteAllText(Path.Combine(root, "RenderCommandList2D.cpp"), "Batch2DGeometry::CreateQuad();");
            string unity = Path.Combine(root, "helengine_core_unity.cpp");
            File.WriteAllLines(unity, sources.Select(name => "#include \"" + name + "\""));

            Assert.Equal(2, PlayStation1UnusedBatch2DPruner.Prune(root));

            string result = File.ReadAllText(unity);
            Assert.Contains("Batch2DGeometry.cpp", result);
            Assert.Contains("Batch2DVertex.cpp", result);
            Assert.Contains("SpriteQuad2D.cpp", result);
            Assert.DoesNotContain("Batch2DWriter.cpp", result);
            Assert.DoesNotContain("Batch2DDrawableEmitter.cpp", result);
            Assert.False(PlayStation1UnusedBatch2DPruner.IsPrunableBatchSource("Batch2DGeometry.cpp"));
            Assert.True(PlayStation1UnusedBatch2DPruner.IsPrunableBatchSource("Batch2DWriter.cpp"));
        } finally {
            Directory.Delete(root, true);
        }
    }

    /// <summary>Keeps the existing explicit failure when generated runtime code starts using a pruned batching implementation.</summary>
    [Fact]
    public void Prune_RejectsRuntimeWriterReference() {
        string root = Path.Combine(@"C:\temp", "helengine-ps1-pruner-reference-tests");
        Directory.CreateDirectory(root);
        try {
            File.WriteAllText(Path.Combine(root, "Batch2DWriter.cpp"), string.Empty);
            File.WriteAllText(Path.Combine(root, "Runtime.cpp"), "Batch2DWriter writer;");
            File.WriteAllText(Path.Combine(root, "helengine_core_unity.cpp"), "#include \"Batch2DWriter.cpp\"");
            Assert.Throws<InvalidOperationException>(() => PlayStation1UnusedBatch2DPruner.Prune(root));
        } finally {
            Directory.Delete(root, true);
        }
    }
}
