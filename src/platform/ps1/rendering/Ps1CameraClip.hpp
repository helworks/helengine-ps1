#pragma once
#include "platform/ps1/rendering/Ps1AuthoredProjection.hpp"

namespace helengine::ps1 {

/** Camera-space vertex, carrying attributes through clipping before hardware perspective division. */
struct Ps1ClipVertex { float X, Y, Z, U, V, R, G, B; };

/** A triangle clipped by six convex planes has at most nine vertices. Spare slots simplify checked emission. */
struct Ps1ClipPolygon { Ps1ClipVertex Vertices[12]{}; unsigned Count = 0; };

class Ps1CameraClip final {
public:
    static constexpr unsigned Capacity = 12;
    /** Conservative enclosing box: wholly visible objects bypass all CPU vertex transforms and clipping. */
    static bool WhollyGteSafe(const Ps1GtePose& pose, float nearDepth, float farDepth) {
        const float radius = pose.BoundingRadiusUnits;
        if (!(radius >= 0) || !__builtin_isfinite(radius)) return false;
        const float x = static_cast<float>(fabs(pose.Translation[0]));
        const float y = static_cast<float>(fabs(pose.Translation[1]));
        const float z = static_cast<float>(pose.Translation[2]);
        const float h = static_cast<float>(pose.ProjectionDistance);
        return z - radius >= nearDepth && z + radius <= farDepth && z - radius > h * 0.5f + 2 &&
            z + radius <= 32760 && x + radius <= 32760 && y + radius <= 32760 &&
            x * h + radius * (h + 160) <= 160 * z && y * h + radius * (h + 120) <= 120 * z;
    }
    static bool Finite(const Ps1ClipVertex& v) {
        return __builtin_isfinite(v.X) && __builtin_isfinite(v.Y) && __builtin_isfinite(v.Z) &&
            __builtin_isfinite(v.U) && __builtin_isfinite(v.V) && __builtin_isfinite(v.R) &&
            __builtin_isfinite(v.G) && __builtin_isfinite(v.B);
    }

    /** Mirrors the GTE 4.12 transform without perspective division, using a wide accumulator before any narrowing. */
    static Ps1ClipVertex Transform(const Ps1GtePose& pose, const std::int16_t* p) {
        Ps1ClipVertex v{};
        float* components[3] = {&v.X, &v.Y, &v.Z};
        for (unsigned row = 0; row < 3; ++row) {
            std::int64_t sum = static_cast<std::int64_t>(pose.Translation[row]) * 4096;
            for (unsigned column = 0; column < 3; ++column) sum += static_cast<std::int64_t>(pose.Rotation[row][column]) * p[column];
            *components[row] = static_cast<float>(sum >> 12);
        }
        return v;
    }

    static bool Inside(const Ps1ClipVertex& v, float nearDepth, float farDepth, float h) {
        return v.Z >= nearDepth && v.Z <= farDepth && v.X * h >= -160.0f * v.Z &&
            v.X * h <= 160.0f * v.Z && v.Y * h >= -120.0f * v.Z && v.Y * h <= 120.0f * v.Z;
    }

    /** Sutherland-Hodgman, with two fixed polygons; no mesh-sized allocation or retained entity state. */
    static bool Clip(const Ps1ClipVertex (&triangle)[3], float nearDepth, float farDepth, float h, Ps1ClipPolygon& output) {
        output.Count = 0;
        if (!(nearDepth > 0) || !(farDepth >= nearDepth) || !(h > 0) ||
            !__builtin_isfinite(nearDepth) || !__builtin_isfinite(farDepth) || !__builtin_isfinite(h)) return false;
        for (const auto& v : triangle) { if (!Finite(v)) return false; output.Vertices[output.Count++] = v; }
        return Plane(output, h, 0, 160, 0) && Plane(output, -h, 0, 160, 0) &&
            Plane(output, 0, h, 120, 0) && Plane(output, 0, -h, 120, 0) &&
            Plane(output, 0, 0, 1, -nearDepth) && Plane(output, 0, 0, -1, farDepth);
    }

    static bool DepthBand(const Ps1ClipPolygon& input, float nearDepth, float farDepth, Ps1ClipPolygon& output) {
        output = input;
        return Plane(output, 0, 0, 1, -nearDepth) && Plane(output, 0, 0, -1, farDepth);
    }

    /** Uniform normalization preserves x/z and y/z; bands keep both IR and the GTE H/SZ divider representable. */
    static float BandRatio(float h) {
        float slope = 160.0f / h;
        if (slope < 1) slope = 1;
        const float ratio = 27000.0f / slope / (h * 0.5f + 2.0f);
        return ratio < 128.0f ? ratio : 128.0f;
    }
    static float Normalization(float nearDepth, float farDepth, float h) {
        float slope = 160.0f / h;
        if (slope < 1) slope = 1;
        float scale = 1.0f;
        const float minimum = (h * 0.5f + 2.0f) / nearDepth;
        const float maximum = 30000.0f / slope / farDepth;
        if (scale < minimum) scale = minimum;
        if (scale > maximum) scale = maximum;
        return scale;
    }

    static bool ToGte(const Ps1ClipVertex& v, float scale, std::int16_t (&result)[3]) {
        const float input[3] = {v.X, v.Y, v.Z};
        for (unsigned i = 0; i < 3; ++i) {
            const float raw = Ps1AuthoredProjection::Round(input[i] * scale);
            if (!__builtin_isfinite(raw) || raw < -32767 || raw > 32767) return false;
            result[i] = static_cast<std::int16_t>(raw);
        }
        return true;
    }

private:
    static Ps1ClipVertex Mix(const Ps1ClipVertex& a, const Ps1ClipVertex& b, float t) {
        return {a.X + (b.X-a.X)*t, a.Y + (b.Y-a.Y)*t, a.Z + (b.Z-a.Z)*t,
            a.U + (b.U-a.U)*t, a.V + (b.V-a.V)*t, a.R + (b.R-a.R)*t,
            a.G + (b.G-a.G)*t, a.B + (b.B-a.B)*t};
    }
    static bool Plane(Ps1ClipPolygon& polygon, float x, float y, float z, float offset) {
        if (polygon.Count == 0) return true;
        Ps1ClipPolygon next{};
        Ps1ClipVertex previous = polygon.Vertices[polygon.Count - 1];
        float previousDistance = x*previous.X + y*previous.Y + z*previous.Z + offset;
        for (unsigned i = 0; i < polygon.Count; ++i) {
            const Ps1ClipVertex current = polygon.Vertices[i];
            const float distance = x*current.X + y*current.Y + z*current.Z + offset;
            if (!__builtin_isfinite(distance) || !__builtin_isfinite(previousDistance)) return false;
            if ((distance >= 0) != (previousDistance >= 0)) {
                if (next.Count >= Capacity) return false;
                Ps1ClipVertex intersection = Mix(previous, current, previousDistance / (previousDistance - distance));
                // Keep exact depth-plane endpoints so rounding cannot place a valid intersection outside its OT range.
                if (x == 0 && y == 0) intersection.Z = -offset / z;
                next.Vertices[next.Count++] = intersection;
            }
            if (distance >= 0) {
                if (next.Count >= Capacity) return false;
                next.Vertices[next.Count++] = current;
            }
            previous = current;
            previousDistance = distance;
        }
        polygon = next;
        return true;
    }
};
static_assert(sizeof(Ps1ClipPolygon) <= 400, "Clipping scratch must remain independent of mesh size");

} // namespace helengine::ps1
