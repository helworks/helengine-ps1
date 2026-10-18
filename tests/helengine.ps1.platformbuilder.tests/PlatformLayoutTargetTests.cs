using helengine.baseplatform.Builders;
using helengine.ps1.builder;

namespace helengine.ps1.platformbuilder.tests;

/// <summary>Checks that the cook target describes the framebuffer currently initialized by Ps1Video.</summary>
public sealed class PlatformLayoutTargetTests {
    /// <summary>PS1 uses its real fixed NTSC framebuffer rather than generic project canvas dimensions.</summary>
    [Fact]
    public void Ps1Target_Matches320x240() {
        IPlatformLayoutTargetProvider provider = Assert.IsAssignableFrom<IPlatformLayoutTargetProvider>(new PlayStation1PlatformAssetBuilder());
        PlatformLayoutTarget target = provider.ResolveLayoutTarget(
            new PlatformLayoutTargetRequest("release", "ps1", new Dictionary<string, string>()));
        Assert.Equal(320, target.Width);
        Assert.Equal(240, target.Height);
        Assert.Equal(1, target.PixelAspect);
        Assert.False(target.HasDpi);
        Assert.Equal(0, target.Dpi);
    }
}
