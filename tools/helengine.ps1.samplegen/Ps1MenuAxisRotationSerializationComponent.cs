using helengine;

namespace DemoDisc.rendering;

/// <summary>Serialization-compatible projection of demodisc's existing AxisRotationComponent.</summary>
public sealed class AxisRotationComponent : UpdateComponent {
    public float3 Axis { get; set; }
    public float AngularSpeedRadiansPerSecond { get; set; }
}
