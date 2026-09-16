#pragma once

#include <cstdint>

#include "platform/ps1/Ps1Math.hpp"
#include "platform/ps1/rendering/Ps1AuthoredProjection.hpp"

namespace helengine::ps1 {

/// <summary>One authored directional light reduced to what the GTE needs.</summary>
struct Ps1DirectionalLight {
    /// <summary>World-space direction the light travels, as LightDirectionUtility reports it.</summary>
    Ps1Vector3 Direction;

    /// <summary>Linear light colour, each channel in 0..1.</summary>
    Ps1Vector3 Color;

    /// <summary>Scalar multiplier applied to the colour.</summary>
    float Intensity;
};

/// <summary>
/// GTE lighting state for one camera: the light matrix whose rows are camera-space light
/// directions in 4.12, the colour matrix whose columns are the matching light colours, and the
/// background colour that carries ambient. All in the GTE's 4.12 fixed point.
/// </summary>
struct Ps1GteLighting {
    std::int16_t LightMatrix[3][3];
    std::int16_t ColorMatrix[3][3];
    std::int32_t BackgroundColor[3];
    std::uint32_t LightCount;
};

/// <summary>
/// Converts authored lights into GTE lighting state, using the same engine-to-GTE convention as
/// Ps1AuthoredProjection: camera space maps through (x, -y, -z). The GTE computes each vertex
/// colour as material * (background + sum over lights of max(0, lightRow . normal) * lightColour),
/// so the light matrix rows hold the direction from the surface toward the light, which is the
/// negation of the direction the light travels.
/// </summary>
class Ps1AuthoredLighting {
public:
    /// <summary>The GTE has three hardware light slots.</summary>
    static constexpr std::uint32_t MaxLights = 3;

    /// <summary>4.12 fixed-point one, matching the projection helper.</summary>
    static constexpr float FixedOne = Ps1AuthoredProjection::FixedOne;

    /// <summary>Returns the world direction a light with this orientation travels, engine forward being -Z.</summary>
    static Ps1Vector3 DirectionFromOrientation(Ps1Quaternion orientation) {
        return Ps1AuthoredProjection::Rotate(Ps1Vector3{0.0f, 0.0f, -1.0f}, orientation);
    }

    /// <summary>
    /// Builds the GTE lighting state. Fails when more lights are supplied than the hardware has
    /// slots, when a light has no usable direction, or when a value leaves the representable range.
    /// </summary>
    /// <param name="camera">World transform of the camera entity.</param>
    /// <param name="lights">Directional lights, or null when there are none.</param>
    /// <param name="lightCount">Number of lights supplied.</param>
    /// <param name="ambient">Ambient colour in 0..1 per channel; zero when the scene authors none.</param>
    /// <param name="lighting">Receives the state when the return value is true.</param>
    static bool TryBuild(const Ps1AuthoredTransform& camera, const Ps1DirectionalLight* lights, std::uint32_t lightCount,
                         Ps1Vector3 ambient, Ps1GteLighting& lighting) {
        if (lightCount > MaxLights) { return false; }
        if (lightCount > 0 && lights == nullptr) { return false; }
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                lighting.LightMatrix[row][column] = 0;
                lighting.ColorMatrix[row][column] = 0;
            }
        }
        for (std::uint32_t index = 0; index < lightCount; ++index) {
            const Ps1DirectionalLight& light = lights[index];
            // The direction the light travels, expressed in camera space, then negated so the row
            // points from the surface toward the light.
            const Ps1Vector3 cameraDirection = ToCameraSpace(camera, light.Direction);
            const float length = SquareRoot(cameraDirection.X * cameraDirection.X + cameraDirection.Y * cameraDirection.Y +
                                            cameraDirection.Z * cameraDirection.Z);
            if (!(length > 0.0001f)) { return false; }
            const float scale = -FixedOne / length;
            if (!TryStoreFixed(cameraDirection.X * scale, lighting.LightMatrix[index][0])) { return false; }
            if (!TryStoreFixed(cameraDirection.Y * scale, lighting.LightMatrix[index][1])) { return false; }
            if (!TryStoreFixed(cameraDirection.Z * scale, lighting.LightMatrix[index][2])) { return false; }

            // Colour matrix columns select the light; rows select the output channel.
            const float intensity = light.Intensity;
            if (!TryStoreFixed(light.Color.X * intensity * FixedOne, lighting.ColorMatrix[0][index])) { return false; }
            if (!TryStoreFixed(light.Color.Y * intensity * FixedOne, lighting.ColorMatrix[1][index])) { return false; }
            if (!TryStoreFixed(light.Color.Z * intensity * FixedOne, lighting.ColorMatrix[2][index])) { return false; }
        }
        const float ambientChannels[3] = {ambient.X, ambient.Y, ambient.Z};
        for (int channel = 0; channel < 3; ++channel) {
            const float value = Ps1AuthoredProjection::Round(ambientChannels[channel] * FixedOne);
            if (value < 0.0f || value > 2147483000.0f) { return false; }
            lighting.BackgroundColor[channel] = static_cast<std::int32_t>(value);
        }
        lighting.LightCount = lightCount;
        return true;
    }

    /// <summary>
    /// Folds the object's world rotation and the engine-to-GTE axis flip into the light matrix, so
    /// the hardware can consume model normals in their original local space without transforming
    /// each one. This is the standard GTE arrangement: the light rows absorb the object transform.
    /// Rejects non-uniform scale, which would need an inverse-transpose the GTE cannot express.
    /// </summary>
    /// <param name="lighting">Camera-space lighting state from TryBuild.</param>
    /// <param name="camera">World transform of the camera entity.</param>
    /// <param name="object">World transform of the drawable entity.</param>
    /// <param name="objectLightMatrix">Receives the per-object 4.12 light matrix.</param>
    static bool TryBuildObjectLightMatrix(const Ps1GteLighting& lighting, const Ps1AuthoredTransform& camera,
                                          const Ps1AuthoredTransform& object, std::int16_t (&objectLightMatrix)[3][3]) {
        const float scaleSpread = Ps1AuthoredProjection::Round((object.Scale.X - object.Scale.Y) * 1000.0f) +
                                  Ps1AuthoredProjection::Round((object.Scale.Y - object.Scale.Z) * 1000.0f);
        if (scaleSpread != 0.0f) { return false; }

        float cameraRotation[3][3];
        Ps1AuthoredProjection::RotationMatrixOf(camera.Orientation, cameraRotation);
        float view[3][3];
        for (int row = 0; row < 3; ++row) {
            const float sign = row == 0 ? 1.0f : -1.0f;
            for (int column = 0; column < 3; ++column) {
                view[row][column] = sign * cameraRotation[column][row];
            }
        }
        float objectRotation[3][3];
        Ps1AuthoredProjection::RotationMatrixOf(object.Orientation, objectRotation);
        float combined[3][3];
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                float sum = 0.0f;
                for (int inner = 0; inner < 3; ++inner) {
                    sum += view[row][inner] * objectRotation[inner][column];
                }
                combined[row][column] = sum;
            }
        }

        for (int light = 0; light < 3; ++light) {
            for (int column = 0; column < 3; ++column) {
                float sum = 0.0f;
                for (int inner = 0; inner < 3; ++inner) {
                    sum += static_cast<float>(lighting.LightMatrix[light][inner]) * combined[inner][column];
                }
                if (!TryStoreFixed(sum, objectLightMatrix[light][column])) { return false; }
            }
        }
        return true;
    }

    /// <summary>Converts a unit-length model normal to the GTE's 4.12 representation.</summary>
    static bool TryConvertNormal(Ps1Vector3 normal, std::int16_t (&result)[3]) {
        const float components[3] = {normal.X, normal.Y, normal.Z};
        for (int index = 0; index < 3; ++index) {
            if (!TryStoreFixed(components[index] * FixedOne, result[index])) { return false; }
        }
        return true;
    }

    /// <summary>
    /// Reproduces the GTE's normal-colour calculation in floating point, for host tests and for
    /// documenting exactly what the hardware path is expected to compute.
    /// </summary>
    static void ShadeReference(const Ps1GteLighting& lighting, const std::int16_t (&normal)[3], std::uint8_t materialRed,
                               std::uint8_t materialGreen, std::uint8_t materialBlue, float& red, float& green, float& blue) {
        float intensity[3] = {0.0f, 0.0f, 0.0f};
        for (std::uint32_t index = 0; index < lighting.LightCount && index < MaxLights; ++index) {
            float dot = 0.0f;
            for (int axis = 0; axis < 3; ++axis) {
                dot += static_cast<float>(lighting.LightMatrix[index][axis]) / FixedOne * static_cast<float>(normal[axis]) / FixedOne;
            }
            if (dot < 0.0f) { dot = 0.0f; }
            for (int channel = 0; channel < 3; ++channel) {
                intensity[channel] += dot * static_cast<float>(lighting.ColorMatrix[channel][index]) / FixedOne;
            }
        }
        const float material[3] = {static_cast<float>(materialRed), static_cast<float>(materialGreen), static_cast<float>(materialBlue)};
        float* outputs[3] = {&red, &green, &blue};
        for (int channel = 0; channel < 3; ++channel) {
            const float ambient = static_cast<float>(lighting.BackgroundColor[channel]) / FixedOne;
            float value = material[channel] * (ambient + intensity[channel]);
            if (value < 0.0f) { value = 0.0f; }
            if (value > 255.0f) { value = 255.0f; }
            *outputs[channel] = value;
        }
    }

private:
    /// <summary>Rotates a world vector into the camera's GTE-axis space, without translation.</summary>
    static Ps1Vector3 ToCameraSpace(const Ps1AuthoredTransform& camera, Ps1Vector3 world) {
        const Ps1Vector3 view = Ps1AuthoredProjection::Rotate(world, Ps1AuthoredProjection::Conjugate(camera.Orientation));
        return Ps1Vector3{view.X, -view.Y, -view.Z};
    }

    /// <summary>Rounds and range-checks one 4.12 entry.</summary>
    static bool TryStoreFixed(float value, std::int16_t& target) {
        const float rounded = Ps1AuthoredProjection::Round(value);
        if (rounded > 32767.0f || rounded < -32768.0f) { return false; }
        target = static_cast<std::int16_t>(rounded);
        return true;
    }

    /// <summary>Square root through the SDK math provider.</summary>
    static float SquareRoot(float value) { return static_cast<float>(sqrt(static_cast<double>(value))); }
};

}  // namespace helengine::ps1
