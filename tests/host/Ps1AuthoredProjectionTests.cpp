#include "platform/ps1/rendering/Ps1AuthoredProjection.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

using helengine::ps1::Ps1AuthoredProjection;
using helengine::ps1::Ps1AuthoredTransform;
using helengine::ps1::Ps1GtePose;
using helengine::ps1::Ps1Quaternion;
using helengine::ps1::Ps1Vector3;

constexpr float kPi = 3.14159265358979f;

Ps1Quaternion YawQuaternion(float radians) {
    return Ps1Quaternion{0.0f, std::sin(radians * 0.5f), 0.0f, std::cos(radians * 0.5f)};
}

Ps1AuthoredTransform Transform(Ps1Vector3 position, Ps1Quaternion orientation, Ps1Vector3 scale) {
    Ps1AuthoredTransform transform{};
    transform.Position = position;
    transform.Orientation = orientation;
    transform.Scale = scale;
    return transform;
}

bool Near(float actual, float expected, float tolerance) { return std::fabs(actual - expected) <= tolerance; }

// Independent reference: camera space via the inverse camera rotation, then the documented
// engine-to-GTE axis flip (x, -y, -z), then the pinhole projection the GTE performs.
bool ReferenceProject(const Ps1AuthoredTransform& camera, const Ps1AuthoredTransform& object, Ps1Vector3 vertex,
                      float fieldOfViewRadians, float& screenX, float& screenY, float& depth) {
    const Ps1Vector3 scaled{vertex.X * object.Scale.X, vertex.Y * object.Scale.Y, vertex.Z * object.Scale.Z};
    const Ps1Vector3 rotated = Ps1AuthoredProjection::Rotate(scaled, object.Orientation);
    const Ps1Vector3 world{rotated.X + object.Position.X, rotated.Y + object.Position.Y, rotated.Z + object.Position.Z};
    const Ps1Vector3 relative{world.X - camera.Position.X, world.Y - camera.Position.Y, world.Z - camera.Position.Z};
    const Ps1Vector3 view = Ps1AuthoredProjection::Rotate(relative, Ps1AuthoredProjection::Conjugate(camera.Orientation));
    const float gteX = view.X * Ps1AuthoredProjection::UnitsPerEngineUnit;
    const float gteY = -view.Y * Ps1AuthoredProjection::UnitsPerEngineUnit;
    const float gteZ = -view.Z * Ps1AuthoredProjection::UnitsPerEngineUnit;
    if (gteZ <= 0.0f) return false;
    const float h = 120.0f / std::tan(fieldOfViewRadians * 0.5f);
    screenX = 160.0f + h * gteX / gteZ;
    screenY = 120.0f + h * gteY / gteZ;
    depth = gteZ;
    return true;
}

// Applies the fixed-point pose exactly as the GTE would: rotation in 4.12, translation in units.
void PoseProject(const Ps1GtePose& pose, const std::int16_t vertex[3], float& screenX, float& screenY, float& depth) {
    float transformed[3];
    for (int row = 0; row < 3; ++row) {
        transformed[row] = pose.Translation[row];
        for (int column = 0; column < 3; ++column) {
            transformed[row] += static_cast<float>(pose.Rotation[row][column]) * static_cast<float>(vertex[column]) / 4096.0f;
        }
    }
    depth = transformed[2];
    screenX = 160.0f + static_cast<float>(pose.ProjectionDistance) * transformed[0] / transformed[2];
    screenY = 120.0f + static_cast<float>(pose.ProjectionDistance) * transformed[1] / transformed[2];
}

int CheckPose(const Ps1AuthoredTransform& camera, const Ps1AuthoredTransform& object, float radius, int failureBase) {
    Ps1GtePose pose{};
    if (!Ps1AuthoredProjection::TryBuildPose(camera, object, radius, Ps1AuthoredProjection::TestFieldOfViewRadians, pose)) {
        return failureBase;
    }
    const Ps1Vector3 corners[] = {{0.5f, 0.5f, -0.5f}, {-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}, {-0.5f, 0.5f, -0.5f}};
    for (const Ps1Vector3& corner : corners) {
        std::int16_t gteVertex[3];
        if (!Ps1AuthoredProjection::TryConvertVertex(corner, gteVertex)) return failureBase + 1;
        float expectedX, expectedY, expectedDepth;
        if (!ReferenceProject(camera, object, corner, Ps1AuthoredProjection::TestFieldOfViewRadians, expectedX, expectedY, expectedDepth)) return failureBase + 2;
        float actualX, actualY, actualDepth;
        PoseProject(pose, gteVertex, actualX, actualY, actualDepth);
        // Half a pixel of tolerance covers 4.12 rounding of the rotation and integer translation.
        if (!Near(actualX, expectedX, 0.6f) || !Near(actualY, expectedY, 0.6f) || !Near(actualDepth, expectedDepth, 1.5f)) {
            std::printf("pose mismatch: expected %.2f,%.2f,%.2f actual %.2f,%.2f,%.2f\n", expectedX, expectedY, expectedDepth, actualX, actualY, actualDepth);
            return failureBase + 3;
        }
    }
    return 0;
}

}  // namespace

int main() {
    const Ps1Quaternion identity{0.0f, 0.0f, 0.0f, 1.0f};
    const Ps1Vector3 one{1.0f, 1.0f, 1.0f};
    const Ps1AuthoredTransform authoredCamera = Transform({0.0f, 0.0f, 5.0f}, identity, one);
    const float cubeRadius = std::sqrt(0.75f);

    // Authored pose: camera five units in front of the origin looking down -Z at a unit cube.
    Ps1GtePose pose{};
    if (!Ps1AuthoredProjection::TryBuildPose(authoredCamera, Transform({0, 0, 0}, identity, one), cubeRadius, Ps1AuthoredProjection::TestFieldOfViewRadians, pose)) return 1;
    if (pose.Translation[0] != 0 || pose.Translation[1] != 0 || pose.Translation[2] != 640) return 2;
    if (pose.Rotation[0][0] != 4096 || pose.Rotation[1][1] != -4096 || pose.Rotation[2][2] != -4096) return 3;
    if (pose.Rotation[0][1] != 0 || pose.Rotation[1][2] != 0 || pose.Rotation[2][0] != 0) return 4;
    if (pose.ProjectionDistance != 208) return 5;
    std::int16_t vertex[3];
    if (!Ps1AuthoredProjection::TryConvertVertex({0.5f, -0.5f, 0.5f}, vertex) || vertex[0] != 64 || vertex[1] != -64 || vertex[2] != 64) return 6;
    // Engine +Y (up) becomes GTE -Y (screen up) and engine -Z (away from the camera) becomes GTE +Z (deeper).
    float x, y, depth;
    PoseProject(pose, vertex, x, y, depth);
    if (!Near(depth, 576.0f, 0.01f) || !Near(x, 160.0f + 208.0f * 64.0f / 576.0f, 0.01f) || !Near(y, 120.0f + 208.0f * 64.0f / 576.0f, 0.01f)) return 7;

    int result = CheckPose(authoredCamera, Transform({0, 0, 0}, identity, one), cubeRadius, 10);
    if (result != 0) return result;
    // Quarter turn about Y, the spin component's axis.
    result = CheckPose(authoredCamera, Transform({0, 0, 0}, YawQuaternion(kPi * 0.5f), one), cubeRadius, 20);
    if (result != 0) return result;
    // Arbitrary rotation and an offset object.
    result = CheckPose(authoredCamera, Transform({0.4f, -0.3f, 0.2f}, YawQuaternion(0.7f), one), cubeRadius, 30);
    if (result != 0) return result;
    // Translated and rotated camera.
    result = CheckPose(Transform({1.0f, 0.5f, 4.0f}, YawQuaternion(0.3f), one), Transform({0, 0, 0}, identity, one), cubeRadius, 40);
    if (result != 0) return result;
    // Non-unit scale folds into the rotation matrix.
    result = CheckPose(authoredCamera, Transform({0, 0, 0}, YawQuaternion(0.4f), {1.5f, 0.75f, 1.25f}), cubeRadius * 1.5f, 50);
    if (result != 0) return result;

    // Winding: the software cross product and the GTE-style sign must agree for a front face.
    const std::int16_t a[3] = {-64, -64, 64};
    const std::int16_t b[3] = {64, -64, 64};
    const std::int16_t c[3] = {64, 64, 64};
    float ax, ay, bx, by, cx, cy, unused;
    PoseProject(pose, a, ax, ay, unused);
    PoseProject(pose, b, bx, by, unused);
    PoseProject(pose, c, cx, cy, unused);
    const float signedArea = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
    if (signedArea == 0.0f) return 60;
    // Reversing the order flips the sign, so the renderer's cull decision is winding-sensitive.
    const float reversedArea = (cx - ax) * (by - ay) - (bx - ax) * (cy - ay);
    if (reversedArea != -signedArea) return 61;

    // Fixed-point overflow: a scale that pushes 4.12 rotation entries past the representable range is rejected.
    if (Ps1AuthoredProjection::TryBuildPose(authoredCamera, Transform({0, 0, 0}, identity, {9.0f, 9.0f, 9.0f}), cubeRadius * 9.0f, Ps1AuthoredProjection::TestFieldOfViewRadians, pose)) return 70;
    // Visibility is handled by camera planes and triangle clipping; valid transforms do not fail outside the old OT interval.
    if (!Ps1AuthoredProjection::TryBuildPose(Transform({0, 0, 1.2f}, identity, one), Transform({0, 0, 0}, identity, one), cubeRadius, Ps1AuthoredProjection::TestFieldOfViewRadians, pose)) return 71;
    if (!Ps1AuthoredProjection::TryBuildPose(Transform({0, 0, 12.0f}, identity, one), Transform({0, 0, 0}, identity, one), cubeRadius, Ps1AuthoredProjection::TestFieldOfViewRadians, pose)) return 72;
    if (!Ps1AuthoredProjection::TryBuildPose(authoredCamera, Transform({0, 0, 9.0f}, identity, one), cubeRadius, Ps1AuthoredProjection::TestFieldOfViewRadians, pose)) return 73;
    // Vertices outside the 16-bit GTE range are rejected.
    if (Ps1AuthoredProjection::TryConvertVertex({300.0f, 0.0f, 0.0f}, vertex)) return 74;
    return 0;
}
