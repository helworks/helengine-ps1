using helengine.editor;

namespace helengine.ps1.samplegen;

/// <summary>
/// Reads and writes serialized Helengine scenes. Shared by the subset exporter and the whole-scene
/// exporter so both produce byte-identical output for the same scene.
/// </summary>
public static class Ps1SceneSerialization {
    /// <summary>Deserializes one serialized scene.</summary>
    /// <param name="bytes">Serialized scene bytes.</param>
    /// <returns>The deserialized scene.</returns>
    public static SceneAsset Deserialize(byte[] bytes) {
        ArgumentNullException.ThrowIfNull(bytes);
        using MemoryStream stream = new(bytes, false);
        return (SceneAsset)(helengine.AssetSerializer.Deserialize(stream)
            ?? throw new InvalidOperationException("Scene deserialized to null."));
    }

    /// <summary>Writes one scene to disk in the engine's serializer-native form.</summary>
    /// <param name="path">Destination path.</param>
    /// <param name="scene">Scene to write.</param>
    public static void Write(string path, SceneAsset scene) {
        ArgumentNullException.ThrowIfNull(scene);
        using FileStream stream = File.Create(path);
        helengine.editor.AssetSerializer.Serialize(stream, scene);
    }
}
