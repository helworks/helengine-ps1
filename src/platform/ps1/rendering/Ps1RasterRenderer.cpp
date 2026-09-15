#include "platform/ps1/rendering/Ps1RasterRenderer.hpp"

#include "platform/ps1/rendering/Ps1CubeMesh.hpp"
#include "platform/ps1/rendering/Ps1DepthBucket.hpp"
#include "psyqo/fixed-point.hh"
#include "psyqo/gte-kernels.hh"
#include "psyqo/gte-registers.hh"
#include "psyqo/soft-math.hh"

#include <cstdint>

namespace helengine::ps1 {

namespace {

constexpr std::uint32_t PackXY(std::int16_t x, std::int16_t y) {
    return static_cast<std::uint32_t>(static_cast<std::uint16_t>(x)) |
           (static_cast<std::uint32_t>(static_cast<std::uint16_t>(y)) << 16);
}

void WriteVertex0(const Ps1CubeMesh::Position& p) {
    psyqo::GTE::write<psyqo::GTE::Register::VXY0, psyqo::GTE::Safe>(PackXY(p.X, p.Y));
    psyqo::GTE::write<psyqo::GTE::Register::VZ0, psyqo::GTE::Safe>(static_cast<std::uint32_t>(
        static_cast<std::uint16_t>(p.Z)));
}

void WriteVertex1(const Ps1CubeMesh::Position& p) {
    psyqo::GTE::write<psyqo::GTE::Register::VXY1, psyqo::GTE::Safe>(PackXY(p.X, p.Y));
    psyqo::GTE::write<psyqo::GTE::Register::VZ1, psyqo::GTE::Safe>(static_cast<std::uint32_t>(
        static_cast<std::uint16_t>(p.Z)));
}

void WriteVertex2(const Ps1CubeMesh::Position& p) {
    psyqo::GTE::write<psyqo::GTE::Register::VXY2, psyqo::GTE::Safe>(PackXY(p.X, p.Y));
    psyqo::GTE::write<psyqo::GTE::Register::VZ2, psyqo::GTE::Safe>(static_cast<std::uint32_t>(
        static_cast<std::uint16_t>(p.Z)));
}

std::int32_t ReadDepth(std::uint32_t raw) { return static_cast<std::int32_t>(static_cast<std::uint16_t>(raw)); }

}  // namespace

void Ps1RasterRenderer::Initialize() {
    m_initialized = true;
    m_sceneSequence = 0;
    m_stats = {};
}

void Ps1RasterRenderer::ResetStats(std::uint16_t phaseX, std::uint16_t phaseY) {
    m_stats = {};
    m_stats.phaseX = static_cast<std::uint16_t>(phaseX & (FullTurn - 1u));
    m_stats.phaseY = static_cast<std::uint16_t>(phaseY & (FullTurn - 1u));
    m_stats.sceneSequence = ++m_sceneSequence;
}

Ps1FrameContext* Ps1RasterRenderer::BeginContext(psyqo::GPU& gpu) {
    Ps1FrameContext& context = m_contexts[gpu.getParity() & 1u];
    if (!context.Begin(m_stats.sceneSequence)) {
        ++m_stats.ownershipFailures;
        m_stats.failure = Ps1RenderFailure::Ownership;
        return nullptr;
    }
    return &context;
}

bool Ps1RasterRenderer::ConfigureGte(std::uint16_t phaseX, std::uint16_t phaseY, std::int32_t trz) {
    if (trz - 112 < Ps1DepthBucket::MinimumDepth || trz + 112 > Ps1DepthBucket::MaximumDepth) {
        ++m_stats.invalid;
        m_stats.failure = Ps1RenderFailure::Projection;
        return false;
    }

    const auto angleX = psyqo::Angle(static_cast<std::int32_t>(phaseX & (FullTurn - 1u)), psyqo::Angle::RAW);
    const auto angleY = psyqo::Angle(static_cast<std::int32_t>(phaseY & (FullTurn - 1u)), psyqo::Angle::RAW);
    const auto rotationX = psyqo::SoftMath::generateRotationMatrix33(angleX, psyqo::SoftMath::Axis::X, m_trig);
    const auto rotationY = psyqo::SoftMath::generateRotationMatrix33(angleY, psyqo::SoftMath::Axis::Y, m_trig);
    psyqo::Matrix33 transform;
    psyqo::SoftMath::multiplyMatrix33(rotationX, rotationY, &transform);

    psyqo::GTE::writeSafe<psyqo::GTE::PseudoRegister::Rotation>(transform);
    psyqo::GTE::write<psyqo::GTE::Register::TRX, psyqo::GTE::Safe>(static_cast<std::uint32_t>(0));
    psyqo::GTE::write<psyqo::GTE::Register::TRY, psyqo::GTE::Safe>(static_cast<std::uint32_t>(0));
    psyqo::GTE::write<psyqo::GTE::Register::TRZ, psyqo::GTE::Safe>(static_cast<std::uint32_t>(trz));
    psyqo::GTE::write<psyqo::GTE::Register::H, psyqo::GTE::Safe>(256);
    // RTPT also evaluates depth cueing state; keep the unlit path independent
    // of whatever values a prior BIOS/GTE user left in DQA and DQB.
    psyqo::GTE::write<psyqo::GTE::Register::DQA, psyqo::GTE::Safe>(static_cast<std::uint32_t>(0));
    psyqo::GTE::write<psyqo::GTE::Register::DQB, psyqo::GTE::Safe>(static_cast<std::uint32_t>(0));
    psyqo::GTE::write<psyqo::GTE::Register::OFX, psyqo::GTE::Safe>(
        static_cast<std::uint32_t>(psyqo::FixedPoint<16>(160.0).raw()));
    psyqo::GTE::write<psyqo::GTE::Register::OFY, psyqo::GTE::Safe>(
        static_cast<std::uint32_t>(psyqo::FixedPoint<16>(120.0).raw()));
    return true;
}

bool Ps1RasterRenderer::ProjectFace(const Ps1CubeMesh::Face& face, psyqo::Vertex& a, psyqo::Vertex& b,
                                     psyqo::Vertex& c, std::int32_t& z0, std::int32_t& z1, std::int32_t& z2,
                                     std::int32_t& mac0) {
    const auto& p0 = Ps1CubeMesh::Positions[face.A];
    const auto& p1 = Ps1CubeMesh::Positions[face.B];
    const auto& p2 = Ps1CubeMesh::Positions[face.C];
    WriteVertex0(p0);
    WriteVertex1(p1);
    WriteVertex2(p2);
    psyqo::GTE::Kernels::rtpt();

    // FLAG is captured before any other GTE operation can overwrite it.
    m_stats.projectionFlags = psyqo::GTE::readRaw<psyqo::GTE::Register::FLAG, psyqo::GTE::Safe>();
    if (m_stats.projectionFlags != 0) {
        ++m_stats.invalid;
        return false;
    }

    psyqo::GTE::read<psyqo::GTE::Register::SXY0>(&a.packed);
    psyqo::GTE::read<psyqo::GTE::Register::SXY1>(&b.packed);
    psyqo::GTE::read<psyqo::GTE::Register::SXY2>(&c.packed);
    std::uint32_t rawZ0 = 0;
    std::uint32_t rawZ1 = 0;
    std::uint32_t rawZ2 = 0;
    psyqo::GTE::read<psyqo::GTE::Register::SZ1>(&rawZ0);
    psyqo::GTE::read<psyqo::GTE::Register::SZ2>(&rawZ1);
    psyqo::GTE::read<psyqo::GTE::Register::SZ3>(&rawZ2);
    z0 = ReadDepth(rawZ0);
    z1 = ReadDepth(rawZ1);
    z2 = ReadDepth(rawZ2);

    psyqo::GTE::write<psyqo::GTE::Register::SXY0, psyqo::GTE::Unsafe>(a.packed);
    psyqo::GTE::write<psyqo::GTE::Register::SXY1, psyqo::GTE::Unsafe>(b.packed);
    psyqo::GTE::write<psyqo::GTE::Register::SXY2, psyqo::GTE::Safe>(c.packed);
    psyqo::GTE::Kernels::nclip();
    mac0 = static_cast<std::int32_t>(psyqo::GTE::readRaw<psyqo::GTE::Register::MAC0, psyqo::GTE::Safe>());
    return true;
}

bool Ps1RasterRenderer::QueueClear(psyqo::GPU& gpu) {
    ResetStats(0, 0);
    if (!m_initialized) {
        m_stats.failure = Ps1RenderFailure::NotInitialized;
        return false;
    }
    Ps1FrameContext* context = BeginContext(gpu);
    if (context == nullptr) return false;
    m_stats.highWaterBytes = context->HighWaterBytes();
    if (!context->Submit(gpu)) {
        ++m_stats.ownershipFailures;
        m_stats.failure = Ps1RenderFailure::Submission;
        context->Cancel();
        return false;
    }
    m_stats.lastSubmissionSequence = context->LastSubmissionSequence();
    return true;
}

bool Ps1RasterRenderer::QueueCube(psyqo::GPU& gpu, std::uint16_t phaseX, std::uint16_t phaseY) {
    return QueueCubeAtTrz(gpu, phaseX, phaseY, NormalTrz);
}

bool Ps1RasterRenderer::QueueInvalidPose(psyqo::GPU& gpu) {
    return QueueCubeAtTrz(gpu, 0, 0, InvalidTrz);
}

bool Ps1RasterRenderer::QueueCubeAtTrz(psyqo::GPU& gpu, std::uint16_t phaseX, std::uint16_t phaseY,
                                       std::int32_t trz) {
    ResetStats(phaseX, phaseY);
    if (!m_initialized) {
        m_stats.failure = Ps1RenderFailure::NotInitialized;
        return false;
    }
    if (!ConfigureGte(m_stats.phaseX, m_stats.phaseY, trz)) return false;

    Ps1FrameContext* context = BeginContext(gpu);
    if (context == nullptr) return false;

    for (const auto& face : Ps1CubeMesh::Faces) {
        psyqo::Vertex a{};
        psyqo::Vertex b{};
        psyqo::Vertex c{};
        std::int32_t z0 = 0;
        std::int32_t z1 = 0;
        std::int32_t z2 = 0;
        std::int32_t mac0 = 0;
        if (!ProjectFace(face, a, b, c, z0, z1, z2, mac0)) {
            context->Cancel();
            return false;
        }
        if (mac0 == 0) {
            ++m_stats.degenerate;
            continue;
        }
        if (mac0 > 0) {
            ++m_stats.backfaceRejected;
            continue;
        }

        std::uint16_t bucket = 0;
        if (!Ps1DepthBucket::TryMap(z0, z1, z2, bucket)) {
            ++m_stats.invalid;
            m_stats.failure = Ps1RenderFailure::Depth;
            context->Cancel();
            return false;
        }
        auto* fragment = context->TryReserveTriangle();
        if (fragment == nullptr) {
            ++m_stats.capacityFailures;
            m_stats.failure = Ps1RenderFailure::Capacity;
            context->Cancel();
            return false;
        }
        fragment->primitive.setOpaque().setColor(psyqo::Color{.packed = face.Rgb});
        fragment->primitive.setPointA(a).setPointB(b).setPointC(c);
        if (!context->Insert(*fragment, bucket)) {
            ++m_stats.capacityFailures;
            m_stats.failure = Ps1RenderFailure::Capacity;
            context->Cancel();
            return false;
        }
        ++m_stats.submittedTriangles;
    }

    m_stats.highWaterBytes = context->HighWaterBytes();
    if (!context->Submit(gpu)) {
        ++m_stats.ownershipFailures;
        m_stats.failure = Ps1RenderFailure::Submission;
        context->Cancel();
        return false;
    }
    m_stats.lastSubmissionSequence = context->LastSubmissionSequence();
    return true;
}

bool Ps1RasterRenderer::QueueDepthDiagnostic(psyqo::GPU& gpu, bool reverseInsertion) {
    ResetStats(0, 0);
    m_stats.reverseInsertion = reverseInsertion;
    if (!m_initialized) {
        m_stats.failure = Ps1RenderFailure::NotInitialized;
        return false;
    }
    Ps1FrameContext* context = BeginContext(gpu);
    if (context == nullptr) return false;

    auto* near = context->TryReserveTriangle();
    auto* far = context->TryReserveTriangle();
    if (near == nullptr || far == nullptr) {
        ++m_stats.capacityFailures;
        m_stats.failure = Ps1RenderFailure::Capacity;
        context->Cancel();
        return false;
    }
    const psyqo::Vertex a{{.x = 100, .y = 80}};
    const psyqo::Vertex b{{.x = 220, .y = 80}};
    const psyqo::Vertex c{{.x = 160, .y = 180}};
    near->primitive.setOpaque().setColor({{.r = 240, .g = 64, .b = 64}}).setPointA(a).setPointB(b).setPointC(c);
    far->primitive.setOpaque().setColor({{.r = 64, .g = 220, .b = 96}}).setPointA(a).setPointB(b).setPointC(c);
    std::uint16_t nearBucket = 0;
    std::uint16_t farBucket = 0;
    if (!Ps1DepthBucket::TryMap(400, 400, 400, nearBucket) || !Ps1DepthBucket::TryMap(600, 600, 600, farBucket)) {
        ++m_stats.invalid;
        m_stats.failure = Ps1RenderFailure::Depth;
        context->Cancel();
        return false;
    }
    const bool firstNear = !reverseInsertion;
    const bool firstInserted = firstNear ? context->Insert(*near, nearBucket) : context->Insert(*far, farBucket);
    const bool secondInserted = firstNear ? context->Insert(*far, farBucket) : context->Insert(*near, nearBucket);
    if (!firstInserted || !secondInserted) {
        ++m_stats.ownershipFailures;
        m_stats.failure = Ps1RenderFailure::Ownership;
        context->Cancel();
        return false;
    }
    m_stats.submittedTriangles = 2;
    m_stats.highWaterBytes = context->HighWaterBytes();
    if (!context->Submit(gpu)) {
        ++m_stats.ownershipFailures;
        m_stats.failure = Ps1RenderFailure::Submission;
        context->Cancel();
        return false;
    }
    m_stats.lastSubmissionSequence = context->LastSubmissionSequence();
    return true;
}

}  // namespace helengine::ps1
