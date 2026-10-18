using System.Reflection;

namespace helengine.ps1.builder.tests;

/// <summary>Verifies fixed external build directories without writing into source inputs.</summary>
public sealed class PlayStation1NativeBuildPathTests {
    /// <summary>Allows the explicitly configured scratch directory outside the checkout.</summary>
    [Fact]
    public void ExternalRunRoot_IsAccepted() {
        Assert.Equal(Path.GetFullPath(@"C:\temp\viewport-layout\ps1\native"), Resolve(@"C:\temp\viewport-layout\ps1\native"));
    }

    /// <summary>Rejects relative paths, volume roots and overlaps with native or generated sources.</summary>
    [Theory]
    [InlineData("relative")]
    [InlineData(@"C:\")]
    [InlineData(@"C:\dev\helworks\helengine-ps1")]
    [InlineData(@"C:\dev\helworks\helengine-ps1\out")]
    [InlineData(@"C:\dev")]
    [InlineData(@"C:\temp\helengine\generated-core\out")]
    public void InputOverlapOrUnqualifiedRunRoot_IsRejected(string path) {
        TargetInvocationException error = Assert.Throws<TargetInvocationException>(() => Resolve(path));
        Assert.IsType<InvalidOperationException>(error.InnerException);
    }

    /// <summary>Calls the native runner's path validation without launching Docker or changing process state.</summary>
    static string Resolve(string configured) {
        MethodInfo method = typeof(PlayStation1NativeBuildExecutor).GetMethod("ResolveBuildRunRoot", BindingFlags.NonPublic | BindingFlags.Static);
        return (string)method.Invoke(null, new object[] { @"C:\dev\helworks\helengine-ps1", @"C:\temp\helengine\generated-core", configured });
    }
}
