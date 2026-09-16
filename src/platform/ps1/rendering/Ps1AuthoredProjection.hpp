#pragma once

#include <cstdint>

#include "platform/ps1/Ps1Math.hpp"
#include "platform/ps1/rendering/Ps1DepthBucket.hpp"

namespace helengine::ps1 {

/// <summary>Engine-space vector (right-handed, Y up, camera looks down -Z).</summary>
struct Ps1Vector3 {
    float X;
    float Y;
    float Z;
};

/// <summary>Engine-space rotation quaternion in (X, Y, Z, W) order.</summary>
struct Ps1Quaternion {
    float X;
    float Y;
    float Z;
    float W;
};

/// <summary>World transform of one authored entity as Core reports it.</summary>
struct Ps1AuthoredTransform {
    Ps1Vector3 Position;
    Ps1Quaternion Orientation;
    Ps1Vector3 Scale;
};

/// <summary>
/// GTE inputs for one drawable: object-to-camera rotation in 4.12 fixed point (row-major, applied
/// as rows times the vertex column), translation in GTE units, and the projection distance H.
/// </summary>
struct Ps1GtePose {
    std::int16_t Rotation[3][3];
    std::int32_t Translation[3];
    std::int32_t ProjectionDistance;
};

/// <summary>
/// Converts authored engine transforms into bounded GTE inputs. Conventions, all documented here:
/// engine space is right-handed with Y up and the camera looking down -Z; GTE space has X right,
/// Y down and Z into the screen, so camera space maps through (x, -y, -z), a proper rotation that
/// preserves triangle winding. One engine unit is 128 GTE units, the projection uses a 60 degree
/// vertical field of view over the 320x240 frame (H = 120 / tan(30 deg) = 208), and object scale is
/// folded into the rotation matrix so model vertices stay at unit scale in 16-bit GTE units.
/// </summary>
class Ps1AuthoredProjection {
public:
    /// <summary>GTE units per engine unit; a unit cube spans the same +/-64 as the diagnostic cube.</summary>
    static constexpr float UnitsPerEngineUnit = 128.0f;

    /// <summary>Vertical field of view used for authored cameras, which do not serialize one.</summary>
    static constexpr float DefaultFieldOfViewRadians = 1.0471976f;

    /// <summary>Half the framebuffer height, the projection plane distance reference.</summary>
    static constexpr float HalfScreenHeight = 120.0f;

    /// <summary>4.12 fixed-point scale of GTE rotation entries.</summary>
    static constexpr float FixedOne = 4096.0f;

    /// <summary>Rotates a vector by a unit quaternion (Hamilton convention, q v q*).</summary>
    static Ps1Vector3 Rotate(Ps1Vector3 value, Ps1Quaternion rotation) {
        float matrix[3][3];
        RotationMatrix(rotation, matrix);
        return Multiply(matrix, value);
    }

    /// <summary>Returns the inverse of a unit quaternion.</summary>
    static Ps1Quaternion Conjugate(Ps1Quaternion rotation) {
        return Ps1Quaternion{-rotation.X, -rotation.Y, -rotation.Z, rotation.W};
    }

    /// <summary>
    /// Builds the GTE pose for one object seen by one camera. Fails when a rotation entry leaves the
    /// 4.12 range, when the object's bounding sphere leaves the ordering-table depth range, or when
    /// the projection distance is not representable. Nothing is submitted for a rejected pose.
    /// </summary>
    /// <param name="camera">World transform of the camera entity.</param>
    /// <param name="object">World transform of the drawable entity.</param>
    /// <param name="boundingRadiusEngineUnits">Bounding sphere radius of the scaled model in engine units.</param>
    /// <param name="fieldOfViewRadians">Vertical field of view.</param>
    /// <param name="pose">Receives the pose when the return value is true.</param>
    static bool TryBuildPose(const Ps1AuthoredTransform& camera, const Ps1AuthoredTransform& object,
                             float boundingRadiusEngineUnits, float fieldOfViewRadians, Ps1GtePose& pose) {
        if (!(fieldOfViewRadians > 0.0f) || !(fieldOfViewRadians < 3.1f) || !(boundingRadiusEngineUnits >= 0.0f)) {
            return false;
        }
        float cameraRotation[3][3];
        RotationMatrix(camera.Orientation, cameraRotation);
        // C = F * Rcam^T with F = diag(1, -1, -1): world -> camera -> GTE axes.
        float view[3][3];
        for (int row = 0; row < 3; ++row) {
            const float sign = row == 0 ? 1.0f : -1.0f;
            for (int column = 0; column < 3; ++column) {
                view[row][column] = sign * cameraRotation[column][row];
            }
        }
        float objectRotation[3][3];
        RotationMatrix(object.Orientation, objectRotation);
        const float scale[3] = {object.Scale.X, object.Scale.Y, object.Scale.Z};
        float combined[3][3];
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                float sum = 0.0f;
                for (int inner = 0; inner < 3; ++inner) {
                    sum += view[row][inner] * objectRotation[inner][column];
                }
                combined[row][column] = sum * scale[column];
            }
        }
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                const float raw = Round(combined[row][column] * FixedOne);
                if (raw > 32767.0f || raw < -32768.0f) {
                    return false;
                }
                pose.Rotation[row][column] = static_cast<std::int16_t>(raw);
            }
        }

        const Ps1Vector3 relative{object.Position.X - camera.Position.X, object.Position.Y - camera.Position.Y,
                                  object.Position.Z - camera.Position.Z};
        const Ps1Vector3 translation = Multiply(view, relative);
        const float translationUnits[3] = {translation.X * UnitsPerEngineUnit, translation.Y * UnitsPerEngineUnit,
                                           translation.Z * UnitsPerEngineUnit};
        for (int index = 0; index < 3; ++index) {
            if (translationUnits[index] > 8388607.0f || translationUnits[index] < -8388608.0f) {
                return false;
            }
            pose.Translation[index] = static_cast<std::int32_t>(Round(translationUnits[index]));
        }

        float maxScale = fabs(scale[0]);
        if (fabs(scale[1]) > maxScale) maxScale = fabs(scale[1]);
        if (fabs(scale[2]) > maxScale) maxScale = fabs(scale[2]);
        const float radiusUnits = boundingRadiusEngineUnits * maxScale * UnitsPerEngineUnit;
        const float nearest = static_cast<float>(pose.Translation[2]) - radiusUnits;
        const float farthest = static_cast<float>(pose.Translation[2]) + radiusUnits;
        if (nearest < static_cast<float>(Ps1DepthBucket::MinimumDepth) || farthest > static_cast<float>(Ps1DepthBucket::MaximumDepth)) {
            return false;
        }

        const float projectionDistance = Round(HalfScreenHeight / tan(fieldOfViewRadians * 0.5f));
        if (!(projectionDistance >= 1.0f) || projectionDistance > 32767.0f) {
            return false;
        }
        pose.ProjectionDistance = static_cast<std::int32_t>(projectionDistance);
        return true;
    }

    /// <summary>Rounds half away from zero using the SDK floor/ceil, which the freestanding toolchain provides.</summary>
    static float Round(float value) {
        return value >= 0.0f ? static_cast<float>(floor(static_cast<double>(value) + 0.5)) : static_cast<float>(ceil(static_cast<double>(value) - 0.5));
    }

    /// <summary>Converts a unit-scale model vertex to 16-bit GTE units; out-of-range vertices are rejected.</summary>
    static bool TryConvertVertex(Ps1Vector3 vertex, std::int16_t (&result)[3]) {
        const float components[3] = {vertex.X, vertex.Y, vertex.Z};
        for (int index = 0; index < 3; ++index) {
            const float scaled = Round(components[index] * UnitsPerEngineUnit);
            if (scaled > 32767.0f || scaled < -32768.0f) {
                return false;
            }
            result[index] = static_cast<std::int16_t>(scaled);
        }
        return true;
    }

    /// <summary>Public access to the quaternion rotation matrix, shared with the lighting helper.</summary>
    static void RotationMatrixOf(Ps1Quaternion rotation, float (&matrix)[3][3]) { RotationMatrix(rotation, matrix); }

private:

    /// <summary>Row-major rotation matrix of a unit quaternion (columns are the rotated basis vectors).</summary>
    static void RotationMatrix(Ps1Quaternion q, float (&matrix)[3][3]) {
        const float xx = q.X * q.X, yy = q.Y * q.Y, zz = q.Z * q.Z;
        const float xy = q.X * q.Y, xz = q.X * q.Z, yz = q.Y * q.Z;
        const float wx = q.W * q.X, wy = q.W * q.Y, wz = q.W * q.Z;
        matrix[0][0] = 1.0f - 2.0f * (yy + zz);
        matrix[0][1] = 2.0f * (xy - wz);
        matrix[0][2] = 2.0f * (xz + wy);
        matrix[1][0] = 2.0f * (xy + wz);
        matrix[1][1] = 1.0f - 2.0f * (xx + zz);
        matrix[1][2] = 2.0f * (yz - wx);
        matrix[2][0] = 2.0f * (xz - wy);
        matrix[2][1] = 2.0f * (yz + wx);
        matrix[2][2] = 1.0f - 2.0f * (xx + yy);
    }

    /// <summary>Multiplies a row-major matrix by a column vector.</summary>
    static Ps1Vector3 Multiply(const float (&matrix)[3][3], Ps1Vector3 value) {
        return Ps1Vector3{
            matrix[0][0] * value.X + matrix[0][1] * value.Y + matrix[0][2] * value.Z,
            matrix[1][0] * value.X + matrix[1][1] * value.Y + matrix[1][2] * value.Z,
            matrix[2][0] * value.X + matrix[2][1] * value.Y + matrix[2][2] * value.Z,
        };
    }
};

}  // namespace helengine::ps1
