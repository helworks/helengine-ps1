#include "platform/ps1/rendering/Ps1AuthoredLighting.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

using helengine::ps1::Ps1AuthoredLighting;
using helengine::ps1::Ps1AuthoredTransform;
using helengine::ps1::Ps1DirectionalLight;
using helengine::ps1::Ps1GteLighting;
using helengine::ps1::Ps1Quaternion;
using helengine::ps1::Ps1Vector3;

Ps1Quaternion YawPitch(float yaw, float pitch) {
    // Matches float4.CreateFromYawPitchRoll with roll zero.
    const float hy = yaw * 0.5f, hp = pitch * 0.5f;
    const float sy = std::sin(hy), cy = std::cos(hy);
    const float sp = std::sin(hp), cp = std::cos(hp);
    return Ps1Quaternion{cy * sp, sy * cp, -sy * sp, cy * cp};
}

Ps1AuthoredTransform Transform(Ps1Vector3 position, Ps1Quaternion orientation) {
    Ps1AuthoredTransform t{};
    t.Position = position;
    t.Orientation = orientation;
    t.Scale = Ps1Vector3{1.0f, 1.0f, 1.0f};
    return t;
}

bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

// Independent reference: the GTE computes colour as material * (BK + sum over lights of
// max(0, dot(lightRow, normal)) * lightColour). One light, no ambient.
void ReferenceShade(Ps1Vector3 cameraSpaceLightDirection, Ps1Vector3 lightColor, Ps1Vector3 normalCameraSpace,
                    Ps1Vector3 materialColor, float& r, float& g, float& b) {
    // The light matrix row holds the direction *towards* the light, so a surface facing it is lit.
    float ndotl = -(cameraSpaceLightDirection.X * normalCameraSpace.X + cameraSpaceLightDirection.Y * normalCameraSpace.Y +
                    cameraSpaceLightDirection.Z * normalCameraSpace.Z);
    if (ndotl < 0.0f) ndotl = 0.0f;
    r = materialColor.X * lightColor.X * ndotl;
    g = materialColor.Y * lightColor.Y * ndotl;
    b = materialColor.Z * lightColor.Z * ndotl;
}

}  // namespace

int main() {
    const Ps1Quaternion identity{0.0f, 0.0f, 0.0f, 1.0f};
    // Camera at (0,0,5) looking down -Z, exactly the authored cube_test camera.
    const Ps1AuthoredTransform camera = Transform({0.0f, 0.0f, 5.0f}, identity);

    // A sun pointing straight down: authored forward is (0,0,-1), so pitch -90 degrees aims it at -Y.
    Ps1DirectionalLight down{};
    down.Direction = Ps1Vector3{0.0f, -1.0f, 0.0f};
    down.Color = Ps1Vector3{1.0f, 1.0f, 1.0f};
    down.Intensity = 1.0f;

    Ps1GteLighting lighting{};
    if (!Ps1AuthoredLighting::TryBuild(camera, &down, 1, Ps1Vector3{0.0f, 0.0f, 0.0f}, lighting)) return 1;

    // Engine -Y maps to GTE +Y, so a downward sun travels along +Y in GTE camera space. The light
    // matrix row holds the direction from the surface toward the light, so it is the negation:
    // -1 in 4.12. That makes the top face, whose GTE normal is (0,-1,0), dot to +1 and light up.
    if (lighting.LightMatrix[0][0] != 0 || lighting.LightMatrix[0][2] != 0) return 2;
    if (lighting.LightMatrix[0][1] != -4096) return 3;
    // Unused light rows must be zero so stray normals cannot contribute.
    for (int row = 1; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (lighting.LightMatrix[row][col] != 0) return 4;
        }
    }
    // Colour matrix column 0 carries the first light's colour at full intensity.
    if (lighting.ColorMatrix[0][0] != 4096 || lighting.ColorMatrix[1][0] != 4096 || lighting.ColorMatrix[2][0] != 4096) return 5;
    // No ambient in this scene: the background colour registers stay at zero.
    if (lighting.BackgroundColor[0] != 0 || lighting.BackgroundColor[1] != 0 || lighting.BackgroundColor[2] != 0) return 6;

    // Half intensity halves the colour matrix entry.
    Ps1DirectionalLight half = down;
    half.Intensity = 0.5f;
    Ps1GteLighting halfLighting{};
    if (!Ps1AuthoredLighting::TryBuild(camera, &half, 1, Ps1Vector3{0.0f, 0.0f, 0.0f}, halfLighting)) return 7;
    if (halfLighting.ColorMatrix[0][0] != 2048) return 8;

    // A coloured light keeps its channels independent.
    Ps1DirectionalLight warm = down;
    warm.Color = Ps1Vector3{1.0f, 0.5f, 0.25f};
    Ps1GteLighting warmLighting{};
    if (!Ps1AuthoredLighting::TryBuild(camera, &warm, 1, Ps1Vector3{0.0f, 0.0f, 0.0f}, warmLighting)) return 9;
    if (warmLighting.ColorMatrix[0][0] != 4096 || warmLighting.ColorMatrix[1][0] != 2048 || warmLighting.ColorMatrix[2][0] != 1024) return 10;

    // Ambient reaches the background registers, scaled to the GTE's 0..4096 range.
    Ps1GteLighting ambientLighting{};
    if (!Ps1AuthoredLighting::TryBuild(camera, &down, 1, Ps1Vector3{0.25f, 0.5f, 1.0f}, ambientLighting)) return 11;
    if (ambientLighting.BackgroundColor[0] != 1024 || ambientLighting.BackgroundColor[1] != 2048 || ambientLighting.BackgroundColor[2] != 4096) return 12;

    // Shading parity against the independent reference, for the authored sun orientation.
    const Ps1Quaternion sunOrientation = YawPitch(-0.65f, -0.85f);
    Ps1DirectionalLight sun{};
    sun.Direction = Ps1AuthoredLighting::DirectionFromOrientation(sunOrientation);
    sun.Color = Ps1Vector3{1.0f, 1.0f, 1.0f};
    sun.Intensity = 1.0f;
    Ps1GteLighting sunLighting{};
    if (!Ps1AuthoredLighting::TryBuild(camera, &sun, 1, Ps1Vector3{0.0f, 0.0f, 0.0f}, sunLighting)) return 13;

    // The cube sits at the origin unrotated, so build its object light matrix once. The hardware
    // consumes local normals, so the object transform and axis flip live in the light matrix.
    std::int16_t objectLight[3][3];
    if (!Ps1AuthoredLighting::TryBuildObjectLightMatrix(sunLighting, camera, Transform({0, 0, 0}, identity), objectLight)) return 14;
    Ps1GteLighting objectLighting = sunLighting;
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) objectLighting.LightMatrix[r][c] = objectLight[r][c];

    // The six cube face normals in engine space, converted the same way vertices are.
    const Ps1Vector3 faceNormals[] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    int litFaces = 0;
    for (const Ps1Vector3& n : faceNormals) {
        std::int16_t gteNormal[3];
        if (!Ps1AuthoredLighting::TryConvertNormal(n, gteNormal)) return 14;
        // Camera has identity rotation, so engine -> GTE is the documented (x, -y, -z) flip.
        const Ps1Vector3 cameraNormal{n.X, -n.Y, -n.Z};
        const Ps1Vector3 cameraLight{sun.Direction.X, -sun.Direction.Y, -sun.Direction.Z};
        float er, eg, eb;
        ReferenceShade(cameraLight, sun.Color, cameraNormal, Ps1Vector3{1.0f, 1.0f, 1.0f}, er, eg, eb);

        float ar, ag, ab;
        Ps1AuthoredLighting::ShadeReference(objectLighting, gteNormal, 255, 255, 255, ar, ag, ab);
        if (!Near(ar / 255.0f, er, 0.02f) || !Near(ag / 255.0f, eg, 0.02f) || !Near(ab / 255.0f, eb, 0.02f)) {
            std::printf("face %.0f,%.0f,%.0f expected %.3f actual %.3f\n", n.X, n.Y, n.Z, er, ar / 255.0f);
            return 15;
        }
        if (er > 0.01f) ++litFaces;
    }
    // The authored sun points down and to one side, so it lights some faces and not others.
    if (litFaces == 0 || litFaces == 6) return 16;

    // A surface facing away is clamped to black rather than going negative.
    std::int16_t up[3];
    if (!Ps1AuthoredLighting::TryConvertNormal({0.0f, 1.0f, 0.0f}, up)) return 17;
    std::int16_t downNormal[3];
    if (!Ps1AuthoredLighting::TryConvertNormal({0.0f, -1.0f, 0.0f}, downNormal)) return 18;
    float ur, ug, ub, dr, dg, db;
    Ps1AuthoredLighting::ShadeReference(objectLighting, up, 255, 255, 255, ur, ug, ub);
    Ps1AuthoredLighting::ShadeReference(objectLighting, downNormal, 255, 255, 255, dr, dg, db);
    if (ur <= 0.0f) return 19;
    if (dr != 0.0f || dg != 0.0f || db != 0.0f) return 20;

    // Channels saturate at 255 rather than wrapping.
    Ps1DirectionalLight bright = down;
    bright.Intensity = 1.0f;
    Ps1GteLighting brightLighting{};
    if (!Ps1AuthoredLighting::TryBuild(camera, &bright, 1, Ps1Vector3{1.0f, 1.0f, 1.0f}, brightLighting)) return 21;
    std::int16_t upNormal[3];
    Ps1AuthoredLighting::TryConvertNormal({0.0f, 1.0f, 0.0f}, upNormal);
    std::int16_t brightObject[3][3];
    if (!Ps1AuthoredLighting::TryBuildObjectLightMatrix(brightLighting, camera, Transform({0, 0, 0}, identity), brightObject)) return 21;
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) brightLighting.LightMatrix[r][c] = brightObject[r][c];
    float sr, sg, sb;
    Ps1AuthoredLighting::ShadeReference(brightLighting, upNormal, 255, 255, 255, sr, sg, sb);
    if (sr != 255.0f || sg != 255.0f || sb != 255.0f) return 22;

    // No lights at all is valid: everything falls back to ambient only.
    Ps1GteLighting none{};
    if (!Ps1AuthoredLighting::TryBuild(camera, nullptr, 0, Ps1Vector3{0.5f, 0.5f, 0.5f}, none)) return 23;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (none.LightMatrix[row][col] != 0 || none.ColorMatrix[row][col] != 0) return 24;
        }
    }
    if (none.BackgroundColor[0] != 2048) return 25;

    // More lights than the GTE's three hardware slots is rejected rather than silently dropped.
    Ps1DirectionalLight many[4] = {down, down, down, down};
    Ps1GteLighting tooMany{};
    if (Ps1AuthoredLighting::TryBuild(camera, many, 4, Ps1Vector3{0.0f, 0.0f, 0.0f}, tooMany)) return 26;

    // A zero-length direction is rejected rather than producing a degenerate light row.
    Ps1DirectionalLight degenerate = down;
    degenerate.Direction = Ps1Vector3{0.0f, 0.0f, 0.0f};
    Ps1GteLighting degenerateLighting{};
    if (Ps1AuthoredLighting::TryBuild(camera, &degenerate, 1, Ps1Vector3{0.0f, 0.0f, 0.0f}, degenerateLighting)) return 27;
    return 0;
}
