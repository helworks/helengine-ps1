namespace helengine.ps1.builder.tests;

/// <summary>Guards the explicit PS1 numeric combinations while rejecting unimplemented policy pairs.</summary>
public sealed class PlayStation1NumericPrecisionTests {
    /// <summary>Software binary16 evaluation is distinct from float evaluation; unsupported pairs remain rejected.</summary>
    [Fact]
    public void Ps1HalfPoliciesKeepTheirSelectedEvaluationWidth() {
        var capabilities = PlayStation1PlatformDefinitionFactory.Create().CodegenProfiles.Single().NumericCapabilities;
        capabilities.RequireSupported(new(32, 32));
        Assert.Throws<NotSupportedException>(() => capabilities.RequireSupported(new(16, 16)));
        Assert.Throws<NotSupportedException>(() => capabilities.RequireSupported(new(16, 64)));
        Assert.Throws<NotSupportedException>(() => capabilities.RequireSupported(new(32, 16)));
        Assert.Throws<NotSupportedException>(() => capabilities.RequireSupported(new(64, 64)));
        capabilities.RequireSupported(new(16, 32));
    }
}
