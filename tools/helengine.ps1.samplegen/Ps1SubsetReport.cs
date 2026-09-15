namespace helengine.ps1.samplegen;

/// <summary>
/// Machine-readable record of the source identity, typed subset and excluded dependencies.
/// </summary>
public sealed class Ps1SubsetReport {
    public string SourceScenePath { get; set; }

    public string SourceSceneHash { get; set; }

    public string SceneId { get; set; }

    public string AuthoringAssetId { get; set; }

    public List<Ps1RetainedEntityReport> RetainedEntities { get; set; } = new();

    public List<Ps1RetainedComponentReport> RetainedComponents { get; set; } = new();

    public List<uint> ExcludedEntityIds { get; set; } = new();

    public List<string> ExcludedComponentTypeIds { get; set; } = new();

    public List<Ps1DependencyReport> Dependencies { get; set; } = new();

    public Ps1CameraPayloadReport Camera { get; set; }

    public Ps1SpinPayloadReport Spin { get; set; }

    public List<Ps1EngineAssemblyReport> EngineAssemblies { get; set; } = new();
}

public sealed class Ps1RetainedEntityReport {
    public uint EntityId { get; set; }

    public string Name { get; set; }

    public ushort LayerMask { get; set; }

    public string LocalPosition { get; set; }

    public string LocalScale { get; set; }

    public string LocalOrientation { get; set; }

    public List<string> ComponentTypeIds { get; set; } = new();
}

public sealed class Ps1RetainedComponentReport {
    public uint EntityId { get; set; }

    public string EntityName { get; set; }

    public string ComponentTypeId { get; set; }

    public int ComponentIndex { get; set; }

    public int PayloadLength { get; set; }

    public string PayloadSha256 { get; set; }

    public string PayloadBase64 { get; set; }
}

public sealed class Ps1DependencyReport {
    public string AssetId { get; set; }

    public string RelativePath { get; set; }

    public string ProviderId { get; set; }

    public string ContentHash { get; set; }

    public string SourcePath { get; set; }

    public string SourceFileSha256 { get; set; }
}

public sealed class Ps1CameraPayloadReport {
    public byte DrawOrder { get; set; }

    public ushort LayerMask { get; set; }

    public float[] Viewport { get; set; }

    public float NearPlaneDistance { get; set; }

    public float FarPlaneDistance { get; set; }

    public bool ClearColorEnabled { get; set; }

    public float[] ClearColor { get; set; }

    public bool ClearDepthEnabled { get; set; }

    public float ClearDepth { get; set; }

    public bool ClearStencilEnabled { get; set; }

    public byte ClearStencil { get; set; }

    public string DepthPrepassMode { get; set; }

    public byte DepthPrepassModeValue { get; set; }

    public float ShadowDistance { get; set; }

    public string PostProcessTier { get; set; }

    public byte PostProcessTierValue { get; set; }
}

public sealed class Ps1SpinPayloadReport {
    public float BaseYawRadians { get; set; }

    public float AngularSpeedRadians { get; set; }
}

public sealed class Ps1EngineAssemblyReport {
    public string FileName { get; set; }

    public string FullPath { get; set; }

    public string RequestedPath { get; set; }

    public long Length { get; set; }

    public string Sha256 { get; set; }
}
