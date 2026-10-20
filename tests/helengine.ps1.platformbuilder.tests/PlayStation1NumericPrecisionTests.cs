namespace helengine.ps1.builder.tests;

/// <summary>Guards the baseline numeric contract while half storage and arithmetic await target verification.</summary>
public sealed class PlayStation1NumericPrecisionTests {
    /// <summary>Unverified half arithmetic cannot be silently compiled as a wider fallback.</summary>
    [Fact]
    public void Ps1RejectsUnavailableHalfArithmetic() {
        var capabilities = PlayStation1PlatformDefinitionFactory.Create().CodegenProfiles.Single().NumericCapabilities;
        capabilities.RequireSupported(new(32, 32));
        Assert.Throws<NotSupportedException>(() => capabilities.RequireSupported(new(16, 16)));
        Assert.Throws<NotSupportedException>(() => capabilities.RequireSupported(new(16, 32)));
    }
}
