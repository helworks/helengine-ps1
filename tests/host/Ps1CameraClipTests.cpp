#include "platform/ps1/rendering/Ps1AuthoredProjection.hpp"
#include "platform/ps1/rendering/Ps1CameraClip.hpp"
#include <cassert>
#include <cmath>
#include <limits>

using namespace helengine::ps1;

int main() {
    const Ps1AuthoredTransform camera{{0, 0, 18}, {0, 0, 0, 1}, {1, 1, 1}};
    const Ps1AuthoredTransform object{{0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}};
    Ps1GtePose pose{};
    // Colored Cubes' valid authored pose is independent of an ordering-table depth partition.
    assert(Ps1AuthoredProjection::TryBuildPose(camera, object, 0.8660254f,
        Ps1AuthoredProjection::TestFieldOfViewRadians, pose));
    assert(pose.Translation[2] == 2304);
    const float nearDepth = 0.1f * 128, farDepth = 96.0f * 128;
    assert(Ps1CameraClip::WhollyGteSafe(pose, nearDepth, farDepth));
    assert(!Ps1CameraClip::WhollyGteSafe(pose, 2304, farDepth));
    const std::int16_t modelVertex[3] = {64, -64, 64};
    const auto transformed = Ps1CameraClip::Transform(pose, modelVertex);
    assert(transformed.X == 64 && transformed.Y == 64 && transformed.Z == 2240);
    assert(Ps1AuthoredProjection::ValidDepthRange(0.1f, 96));
    assert(!Ps1AuthoredProjection::ValidDepthRange(0, 96));
    assert(!Ps1AuthoredProjection::ValidDepthRange(1, 1));
    assert(!Ps1AuthoredProjection::ValidDepthRange(1, std::numeric_limits<float>::infinity()));
    std::uint16_t bucket = 0;
    assert(Ps1DepthBucket::BucketCount == 256 && Ps1DepthBucket::CombinedBucketCount == 512);
    assert(Ps1DepthBucket::TryMap(nearDepth, nearDepth, nearDepth, nearDepth, farDepth, bucket) && bucket == 0);
    assert(Ps1DepthBucket::TryMap(farDepth, farDepth, farDepth, nearDepth, farDepth, bucket) && bucket == 255);
    assert(Ps1DepthBucket::TryMap(2304, 2304, 2304, nearDepth, farDepth, bucket) && bucket == 47);
    assert(Ps1DepthBucket::TryMap(2304, 2304, 2304, nearDepth, 24 * 128, bucket) && bucket == 191);
    // Binary16 scene storage rounds authored 0.1 to this exact value; it is not rounded up to a fixed GTE near plane.
    assert(Ps1DepthBucket::TryMap(2304, 2304, 2304, 0.0999755859375f * 128, farDepth, bucket) && bucket == 47);
    assert(!Ps1DepthBucket::TryMap(nearDepth - 1, 20, 20, nearDepth, farDepth, bucket));
    assert(!Ps1DepthBucket::TryMap(20, farDepth + 1, 20, nearDepth, farDepth, bucket));
    bool visible = false;
    assert(Ps1AuthoredProjection::TryDepthVisibility(camera, object, 0.8660254f, 0.1f, 96, visible) && visible);
    Ps1AuthoredTransform outside = object;
    outside.Position.Z = 30;
    assert(Ps1AuthoredProjection::TryDepthVisibility(camera, outside, 1, 0.1f, 96, visible) && !visible);
    outside.Position.Z = -200;
    assert(Ps1AuthoredProjection::TryDepthVisibility(camera, outside, 1, 0.1f, 96, visible) && !visible);
    outside.Position.X = std::numeric_limits<float>::quiet_NaN();
    assert(!Ps1AuthoredProjection::TryDepthVisibility(camera, outside, 1, 0.1f, 96, visible));
    assert(!Ps1AuthoredProjection::TryBuildPose(camera, outside, 1, 1, pose));
    outside = object; outside.Scale.X = 9;
    assert(!Ps1AuthoredProjection::TryBuildPose(camera, outside, 1, 1, pose));

    // Near-plane intersection creates a quad, interpolating texture and lit colour along the original edge.
    const Ps1ClipVertex triangle[3] = {{-1,0,5,0,0,0,0,0},{1,0,15,1,0,200,100,50},{0,1,15,0,1,100,200,0}};
    Ps1ClipPolygon polygon{};
    assert(Ps1CameraClip::Clip(triangle, 10, 20, 208, polygon) && polygon.Count == 4);
    bool midpoint = false;
    for (unsigned i = 0; i < polygon.Count; ++i) {
        const auto& v = polygon.Vertices[i];
        assert(v.Z >= 10 && v.Z <= 20);
        if (v.X == 0 && v.Y == 0 && v.Z == 10) {
            assert(v.U == 0.5f && v.V == 0 && v.R == 100 && v.G == 50 && v.B == 25);
            midpoint = true;
        }
    }
    assert(midpoint);
    assert(Ps1CameraClip::Clip(triangle, 30, 40, 208, polygon) && polygon.Count == 0);
    assert(Ps1CameraClip::Clip(triangle, 1, 4, 208, polygon) && polygon.Count == 0);
    assert(Ps1CameraClip::Clip(triangle, 1, 10, 208, polygon) && polygon.Count == 3);
    midpoint = false;
    for (unsigned i = 0; i < polygon.Count; ++i) {
        const auto& v = polygon.Vertices[i];
        assert(v.Z >= 1 && v.Z <= 10);
        if (v.X == 0 && v.Y == 0 && v.Z == 10) {
            assert(v.U == 0.5f && v.R == 100 && v.G == 50 && v.B == 25);
            midpoint = true;
        }
    }
    assert(midpoint);
    const Ps1ClipVertex left[3] = {{-100,0,20,0,0,0,0,0},{-100,1,20,0,0,0,0,0},{-99,0,20,0,0,0,0,0}};
    assert(Ps1CameraClip::Clip(left, 1, 30, 208, polygon) && polygon.Count == 0);
    Ps1ClipVertex malformed[3] = {triangle[0],triangle[1],triangle[2]};
    malformed[1].U = std::numeric_limits<float>::quiet_NaN();
    assert(!Ps1CameraClip::Clip(malformed, 10, 20, 208, polygon));

    // A triangle spanning the entire camera range is split into bounded depth bands before RTPT.
    const Ps1ClipVertex spanning[3] = {{0,0,nearDepth,0,0,0,0,0},{8000,5000,farDepth,1,0,255,0,0},{-8000,-5000,farDepth,0,1,0,255,0}};
    assert(Ps1CameraClip::Clip(spanning, nearDepth, farDepth, 208, polygon));
    assert(polygon.Count >= 3);
    const float distances[] = {1.0f, 208.0f, 32767.0f};
    for (float h : distances) {
        Ps1ClipPolygon frustum{};
        assert(Ps1CameraClip::Clip(spanning, nearDepth, farDepth, h, frustum));
        float bandNear = nearDepth;
        unsigned count = 0;
        while (true) {
            float bandFar = bandNear * Ps1CameraClip::BandRatio(h);
            if (bandFar > farDepth) bandFar = farDepth;
            Ps1ClipPolygon band{};
            assert(Ps1CameraClip::DepthBand(frustum, bandNear, bandFar, band));
            const float scale = Ps1CameraClip::Normalization(bandNear, bandFar, h);
            for (unsigned i = 0; i < band.Count; ++i) {
                std::int16_t result[3];
                assert(Ps1CameraClip::ToGte(band.Vertices[i], scale, result));
                assert(result[2] > h * 0.5f && result[2] <= 32767);
            }
            assert(++count < 32);
            if (bandFar == farDepth) break;
            assert(bandFar > bandNear);
            bandNear = bandFar;
        }
    }
    // No normalization or projection change is needed for the legacy cube's safe camera-space interval.
    assert(Ps1CameraClip::Normalization(576, 704, 208) == 1);
    assert(sizeof(Ps1ClipPolygon) <= 400);
    // A clipped triangle always has bounded scratch, even when it crosses every side and depth plane.
    for (int x = -4; x <= 4; ++x) for (int y = -4; y <= 4; ++y) for (int z = -2; z <= 4; ++z) {
        const Ps1ClipVertex varied[3] = {{static_cast<float>(x*500),static_cast<float>(y*500),static_cast<float>(z*500),0,0,0,0,0},
            {2000,-2000,13000,1,0,255,0,0},{-2000,2000,10,0,1,0,255,0}};
        assert(Ps1CameraClip::Clip(varied, nearDepth, farDepth, 208, polygon));
        assert(polygon.Count <= 9);
        for (unsigned i = 0; i < polygon.Count; ++i) {
            assert(Ps1CameraClip::Finite(polygon.Vertices[i]));
            assert(polygon.Vertices[i].Z >= nearDepth && polygon.Vertices[i].Z <= farDepth);
        }
    }
}
