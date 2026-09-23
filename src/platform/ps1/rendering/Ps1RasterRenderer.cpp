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

constexpr unsigned CompletionStressTriangleCount = 64;

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
    const std::uint32_t peakPacketCount = m_stats.peakPacketCount;
    const std::uint32_t peakHighWaterBytes = m_stats.peakHighWaterBytes;
    m_stats = {};
    m_stats.phaseX = static_cast<std::uint16_t>(phaseX & (FullTurn - 1u));
    m_stats.phaseY = static_cast<std::uint16_t>(phaseY & (FullTurn - 1u));
    m_stats.sceneSequence = ++m_sceneSequence;
    m_stats.peakPacketCount = peakPacketCount;
    m_stats.peakHighWaterBytes = peakHighWaterBytes;
}

Ps1FrameContext* Ps1RasterRenderer::BeginContext(psyqo::GPU& gpu, std::uint32_t capacity) {
    Ps1FrameContext& context = m_contexts[gpu.getParity() & 1u];
    if (!context.Begin(m_stats.sceneSequence, capacity)) {
        if (!context.GuardsIntact()) {
            ++m_stats.guardFailures;
            m_stats.guardsIntact = false;
            m_stats.failure = Ps1RenderFailure::Guard;
        }
        ++m_stats.ownershipFailures;
        if (m_stats.failure == Ps1RenderFailure::None) m_stats.failure = Ps1RenderFailure::Ownership;
        return nullptr;
    }
    m_stats.capacityLimit = context.Capacity();
    return &context;
}

bool Ps1RasterRenderer::SubmitContext(psyqo::GPU& gpu, Ps1FrameContext& context, psyqo::Color clearColor) {
    m_stats.packetCount = context.UsedPackets();
    m_stats.highWaterBytes = context.HighWaterBytes();
    if (m_stats.packetCount > m_stats.peakPacketCount) m_stats.peakPacketCount = m_stats.packetCount;
    if (m_stats.highWaterBytes > m_stats.peakHighWaterBytes) m_stats.peakHighWaterBytes = m_stats.highWaterBytes;
    m_stats.capacityLimit = context.Capacity();
    m_stats.guardsIntact = context.GuardsIntact();
    if (!m_stats.guardsIntact) {
        ++m_stats.guardFailures;
        m_stats.failure = Ps1RenderFailure::Guard;
        return false;
    }
    if (!context.Submit(gpu, clearColor)) {
        ++m_stats.ownershipFailures;
        m_stats.failure = Ps1RenderFailure::Submission;
        context.Cancel();
        return false;
    }
    m_stats.lastSubmissionSequence = context.LastSubmissionSequence();
    return true;
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
        m_stats.failure = Ps1RenderFailure::Projection;
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
    return SubmitContext(gpu, *context);
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

    return SubmitContext(gpu, *context);
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
    return SubmitContext(gpu, *context);
}

bool Ps1RasterRenderer::QueueCapacityDiagnostic(psyqo::GPU& gpu) {
    ResetStats(0, 0);
    Ps1FrameContext* context = BeginContext(gpu, 1);
    if (context == nullptr) return false;

    auto* first = context->TryReserveTriangle();
    if (first == nullptr) {
        ++m_stats.capacityFailures;
        m_stats.failure = Ps1RenderFailure::Capacity;
        context->Cancel();
        return false;
    }
    const psyqo::Vertex a{{.x = 32, .y = 32}};
    const psyqo::Vertex b{{.x = 288, .y = 32}};
    const psyqo::Vertex c{{.x = 160, .y = 224}};
    first->primitive.setOpaque().setColor({{.r = 240, .g = 160, .b = 48}}).setPointA(a).setPointB(b).setPointC(c);
    const std::uint16_t bucket = 128;
    if (!context->Insert(*first, bucket)) {
        ++m_stats.capacityFailures;
        m_stats.failure = Ps1RenderFailure::Capacity;
        context->Cancel();
        return false;
    }

    // The second visible packet must fail at reservation, before any packet
    // storage is written. This is the intentional capacity fault.
    auto* second = context->TryReserveTriangle();
    if (second != nullptr) {
        ++m_stats.capacityFailures;
        m_stats.failure = Ps1RenderFailure::Capacity;
        context->Cancel();
        return false;
    }
    ++m_stats.capacityFailures;
    m_stats.submittedTriangles = 1;
    m_stats.guardsIntact = context->GuardsIntact();
    if (!m_stats.guardsIntact) ++m_stats.guardFailures;
    m_stats.failure = Ps1RenderFailure::Capacity;
    context->Cancel();
    return false;
}

bool Ps1RasterRenderer::QueueCompletionStress(psyqo::GPU& gpu) {
    ResetStats(0, 0);
    Ps1FrameContext* context = BeginContext(gpu);
    if (context == nullptr) return false;

    for (unsigned i = 0; i < CompletionStressTriangleCount; ++i) {
        auto* fragment = context->TryReserveTriangle();
        if (fragment == nullptr) {
            ++m_stats.capacityFailures;
            m_stats.failure = Ps1RenderFailure::Capacity;
            context->Cancel();
            return false;
        }
        const psyqo::Vertex a{{.x = 24, .y = 24}};
        const psyqo::Vertex b{{.x = 296, .y = 24}};
        const psyqo::Vertex c{{.x = static_cast<std::int16_t>(160 + (i & 7u) * 4),
                               .y = static_cast<std::int16_t>(216 - (i & 3u) * 3)}};
        fragment->primitive.setOpaque().setColor({{.r = static_cast<std::uint8_t>(32u + (i & 31u)),
                                                    .g = static_cast<std::uint8_t>(80u + (i & 63u)),
                                                    .b = 160}})
            .setPointA(a)
            .setPointB(b)
            .setPointC(c);
        if (!context->Insert(*fragment, static_cast<std::uint16_t>(i & 255u))) {
            ++m_stats.capacityFailures;
            m_stats.failure = Ps1RenderFailure::Capacity;
            context->Cancel();
            return false;
        }
        ++m_stats.submittedTriangles;
    }
    return SubmitContext(gpu, *context);
}

bool Ps1RasterRenderer::BeginAuthoredFrame(psyqo::GPU& gpu) {
    ResetStats(0, 0);
    m_authoredContext = nullptr;
    if (!m_initialized) {
        m_stats.failure = Ps1RenderFailure::NotInitialized;
        return false;
    }
    m_authoredContext = BeginContext(gpu);
    return m_authoredContext != nullptr;
}

void Ps1RasterRenderer::WriteAuthoredPose(const Ps1GtePose& pose) {
    psyqo::Matrix33 rotation;
    for (unsigned row = 0; row < 3; ++row) {
        rotation.vs[row].x = psyqo::FixedPoint<12>(static_cast<std::int32_t>(pose.Rotation[row][0]), psyqo::FixedPoint<12>::RAW);
        rotation.vs[row].y = psyqo::FixedPoint<12>(static_cast<std::int32_t>(pose.Rotation[row][1]), psyqo::FixedPoint<12>::RAW);
        rotation.vs[row].z = psyqo::FixedPoint<12>(static_cast<std::int32_t>(pose.Rotation[row][2]), psyqo::FixedPoint<12>::RAW);
    }
    psyqo::GTE::writeSafe<psyqo::GTE::PseudoRegister::Rotation>(rotation);
    psyqo::GTE::write<psyqo::GTE::Register::TRX, psyqo::GTE::Safe>(static_cast<std::uint32_t>(pose.Translation[0]));
    psyqo::GTE::write<psyqo::GTE::Register::TRY, psyqo::GTE::Safe>(static_cast<std::uint32_t>(pose.Translation[1]));
    psyqo::GTE::write<psyqo::GTE::Register::TRZ, psyqo::GTE::Safe>(static_cast<std::uint32_t>(pose.Translation[2]));
    psyqo::GTE::write<psyqo::GTE::Register::H, psyqo::GTE::Safe>(static_cast<std::uint32_t>(pose.ProjectionDistance));
    psyqo::GTE::write<psyqo::GTE::Register::DQA, psyqo::GTE::Safe>(static_cast<std::uint32_t>(0));
    psyqo::GTE::write<psyqo::GTE::Register::DQB, psyqo::GTE::Safe>(static_cast<std::uint32_t>(0));
    psyqo::GTE::write<psyqo::GTE::Register::OFX, psyqo::GTE::Safe>(
        static_cast<std::uint32_t>(psyqo::FixedPoint<16>(160.0).raw()));
    psyqo::GTE::write<psyqo::GTE::Register::OFY, psyqo::GTE::Safe>(
        static_cast<std::uint32_t>(psyqo::FixedPoint<16>(120.0).raw()));
}

void Ps1RasterRenderer::WriteAuthoredLighting(const Ps1GteLighting& lighting, psyqo::Color materialColor) {
    psyqo::Matrix33 lightMatrix;
    psyqo::Matrix33 colorMatrix;
    for (unsigned row = 0; row < 3; ++row) {
        lightMatrix.vs[row].x = psyqo::FixedPoint<12>(static_cast<std::int32_t>(lighting.LightMatrix[row][0]), psyqo::FixedPoint<12>::RAW);
        lightMatrix.vs[row].y = psyqo::FixedPoint<12>(static_cast<std::int32_t>(lighting.LightMatrix[row][1]), psyqo::FixedPoint<12>::RAW);
        lightMatrix.vs[row].z = psyqo::FixedPoint<12>(static_cast<std::int32_t>(lighting.LightMatrix[row][2]), psyqo::FixedPoint<12>::RAW);
        colorMatrix.vs[row].x = psyqo::FixedPoint<12>(static_cast<std::int32_t>(lighting.ColorMatrix[row][0]), psyqo::FixedPoint<12>::RAW);
        colorMatrix.vs[row].y = psyqo::FixedPoint<12>(static_cast<std::int32_t>(lighting.ColorMatrix[row][1]), psyqo::FixedPoint<12>::RAW);
        colorMatrix.vs[row].z = psyqo::FixedPoint<12>(static_cast<std::int32_t>(lighting.ColorMatrix[row][2]), psyqo::FixedPoint<12>::RAW);
    }
    psyqo::GTE::writeSafe<psyqo::GTE::PseudoRegister::Light>(lightMatrix);
    psyqo::GTE::writeSafe<psyqo::GTE::PseudoRegister::Color>(colorMatrix);
    psyqo::GTE::write<psyqo::GTE::Register::RBK, psyqo::GTE::Safe>(static_cast<std::uint32_t>(lighting.BackgroundColor[0]));
    psyqo::GTE::write<psyqo::GTE::Register::GBK, psyqo::GTE::Safe>(static_cast<std::uint32_t>(lighting.BackgroundColor[1]));
    psyqo::GTE::write<psyqo::GTE::Register::BBK, psyqo::GTE::Safe>(static_cast<std::uint32_t>(lighting.BackgroundColor[2]));
    // ncct multiplies its result by the material colour held in RGB, and copies RGB's
    // high byte (the CODE field) into every colour it emits.  Preloading the opaque
    // gouraud-triangle command word there means RGB0 reads back as a finished first word.
    const std::uint32_t code = psyqo::Prim::GouraudTriangle().setOpaque().getCommandWord();
    psyqo::GTE::write<psyqo::GTE::Register::RGB, psyqo::GTE::Safe>(code | (materialColor.packed & 0x00ffffffu));
}

bool Ps1RasterRenderer::QueueAuthoredMesh(const Ps1GtePose& pose, const std::int16_t* vertices, std::uint32_t vertexCount,
                                          const std::uint16_t* indices, std::uint32_t indexCount, psyqo::Color color,
                                          bool doubleSided, const std::int16_t* normals, const Ps1GteLighting* lighting) {
    Ps1FrameContext* context = m_authoredContext;
    if (context == nullptr) {
        ++m_stats.ownershipFailures;
        m_stats.failure = Ps1RenderFailure::Ownership;
        return false;
    }
    if (vertices == nullptr || indices == nullptr || vertexCount == 0 || indexCount == 0 || indexCount % 3u != 0) {
        ++m_stats.invalid;
        m_stats.failure = Ps1RenderFailure::Projection;
        context->Cancel();
        m_authoredContext = nullptr;
        return false;
    }
    const bool lit = normals != nullptr && lighting != nullptr;
    WriteAuthoredPose(pose);
    if (lit) {
        WriteAuthoredLighting(*lighting, color);
    }
    for (std::uint32_t triangle = 0; triangle < indexCount; triangle += 3) {
        const std::uint16_t i0 = indices[triangle];
        const std::uint16_t i1 = indices[triangle + 1];
        const std::uint16_t i2 = indices[triangle + 2];
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) {
            ++m_stats.invalid;
            m_stats.failure = Ps1RenderFailure::Projection;
            context->Cancel();
            m_authoredContext = nullptr;
            return false;
        }
        const Ps1CubeMesh::Position p0{vertices[i0 * 3], vertices[i0 * 3 + 1], vertices[i0 * 3 + 2]};
        const Ps1CubeMesh::Position p1{vertices[i1 * 3], vertices[i1 * 3 + 1], vertices[i1 * 3 + 2]};
        const Ps1CubeMesh::Position p2{vertices[i2 * 3], vertices[i2 * 3 + 1], vertices[i2 * 3 + 2]};
        WriteVertex0(p0);
        WriteVertex1(p1);
        WriteVertex2(p2);
        psyqo::GTE::Kernels::rtpt();
        m_stats.projectionFlags = psyqo::GTE::readRaw<psyqo::GTE::Register::FLAG, psyqo::GTE::Safe>();
        if (m_stats.projectionFlags != 0) {
            ++m_stats.invalid;
            m_stats.failure = Ps1RenderFailure::Projection;
            context->Cancel();
            m_authoredContext = nullptr;
            return false;
        }
        psyqo::Vertex a{};
        psyqo::Vertex b{};
        psyqo::Vertex c{};
        psyqo::GTE::read<psyqo::GTE::Register::SXY0>(&a.packed);
        psyqo::GTE::read<psyqo::GTE::Register::SXY1>(&b.packed);
        psyqo::GTE::read<psyqo::GTE::Register::SXY2>(&c.packed);
        std::uint32_t rawZ0 = 0;
        std::uint32_t rawZ1 = 0;
        std::uint32_t rawZ2 = 0;
        psyqo::GTE::read<psyqo::GTE::Register::SZ1>(&rawZ0);
        psyqo::GTE::read<psyqo::GTE::Register::SZ2>(&rawZ1);
        psyqo::GTE::read<psyqo::GTE::Register::SZ3>(&rawZ2);
        psyqo::GTE::write<psyqo::GTE::Register::SXY0, psyqo::GTE::Unsafe>(a.packed);
        psyqo::GTE::write<psyqo::GTE::Register::SXY1, psyqo::GTE::Unsafe>(b.packed);
        psyqo::GTE::write<psyqo::GTE::Register::SXY2, psyqo::GTE::Safe>(c.packed);
        psyqo::GTE::Kernels::nclip();
        const std::int32_t mac0 = static_cast<std::int32_t>(psyqo::GTE::readRaw<psyqo::GTE::Register::MAC0, psyqo::GTE::Safe>());
        if (mac0 == 0) {
            ++m_stats.degenerate;
            continue;
        }
        if (mac0 > 0 && !doubleSided) {
            ++m_stats.backfaceRejected;
            continue;
        }
        std::uint16_t bucket = 0;
        if (!Ps1DepthBucket::TryMap(ReadDepth(rawZ0), ReadDepth(rawZ1), ReadDepth(rawZ2), bucket)) {
            ++m_stats.invalid;
            m_stats.failure = Ps1RenderFailure::Depth;
            context->Cancel();
            m_authoredContext = nullptr;
            return false;
        }
        if (lit) {
            // Second GTE pass over the same triangle: local normals in, per-vertex colours out.
            const Ps1CubeMesh::Position n0{normals[i0 * 3], normals[i0 * 3 + 1], normals[i0 * 3 + 2]};
            const Ps1CubeMesh::Position n1{normals[i1 * 3], normals[i1 * 3 + 1], normals[i1 * 3 + 2]};
            const Ps1CubeMesh::Position n2{normals[i2 * 3], normals[i2 * 3 + 1], normals[i2 * 3 + 2]};
            WriteVertex0(n0);
            WriteVertex1(n1);
            WriteVertex2(n2);
            psyqo::GTE::Kernels::ncct();
            std::uint32_t rgb0 = 0;
            std::uint32_t rgb1 = 0;
            std::uint32_t rgb2 = 0;
            psyqo::GTE::read<psyqo::GTE::Register::RGB0>(&rgb0);
            psyqo::GTE::read<psyqo::GTE::Register::RGB1>(&rgb1);
            psyqo::GTE::read<psyqo::GTE::Register::RGB2>(&rgb2);
            if (!m_stats.litSampled) {
                // One sample per frame: the colours the hardware actually produced, so shading is
                // observable evidence rather than something inferred from the light matrix.
                m_stats.litVertexColors[0] = rgb0 & 0x00ffffffu;
                m_stats.litVertexColors[1] = rgb1 & 0x00ffffffu;
                m_stats.litVertexColors[2] = rgb2 & 0x00ffffffu;
                m_stats.litSampled = true;
            }
            auto* fragment = context->TryReserveGouraud();
            if (fragment == nullptr) {
                ++m_stats.capacityFailures;
                m_stats.failure = Ps1RenderFailure::Capacity;
                context->Cancel();
                m_authoredContext = nullptr;
                return false;
            }
            fragment->primitive.setColorAPacked(rgb0);
            fragment->primitive.setColorB(psyqo::Color{.packed = rgb1 & 0x00ffffffu});
            fragment->primitive.setColorC(psyqo::Color{.packed = rgb2 & 0x00ffffffu});
            fragment->primitive.setPointA(a).setPointB(b).setPointC(c);
            if (!context->Insert(*fragment, bucket)) {
                ++m_stats.capacityFailures;
                m_stats.failure = Ps1RenderFailure::Capacity;
                context->Cancel();
                m_authoredContext = nullptr;
                return false;
            }
        } else {
            auto* fragment = context->TryReserveTriangle();
            if (fragment == nullptr) {
                ++m_stats.capacityFailures;
                m_stats.failure = Ps1RenderFailure::Capacity;
                context->Cancel();
                m_authoredContext = nullptr;
                return false;
            }
            fragment->primitive.setOpaque().setColor(color);
            fragment->primitive.setPointA(a).setPointB(b).setPointC(c);
            if (!context->Insert(*fragment, bucket)) {
                ++m_stats.capacityFailures;
                m_stats.failure = Ps1RenderFailure::Capacity;
                context->Cancel();
                m_authoredContext = nullptr;
                return false;
            }
        }
        ++m_stats.submittedTriangles;
    }
    return true;
}

bool Ps1RasterRenderer::QueueAuthoredUiTriangle(const Ps1UiTriangle& triangle, psyqo::Color color,
                                                bool semiTransparent, std::uint16_t bucket) {
    Ps1FrameContext* context = m_authoredContext;
    if (context == nullptr) {
        ++m_stats.ownershipFailures;
        m_stats.failure = Ps1RenderFailure::Ownership;
        return false;
    }
    Ps1FrameContext::UiTriangleFragment* fragment = context->TryReserveUiTriangle();
    if (fragment == nullptr) {
        ++m_stats.capacityFailures;
        m_stats.failure = Ps1RenderFailure::Capacity;
        return false;
    }
    auto vertex = [](const Ps1UiVertex& input) {
        return psyqo::Vertex{{.x = input.x, .y = input.y}};
    };
    fragment->primitive = psyqo::Prim::Triangle(color);
    if (semiTransparent) fragment->primitive.setSemiTrans();
    fragment->primitive.setPointA(vertex(triangle.a));
    fragment->primitive.setPointB(vertex(triangle.b));
    fragment->primitive.setPointC(vertex(triangle.c));
    if (!context->InsertUiTriangle(*fragment, bucket)) {
        ++m_stats.ownershipFailures;
        m_stats.failure = Ps1RenderFailure::Ownership;
        return false;
    }
    ++m_stats.uiTriangles;
    return true;
}

bool Ps1RasterRenderer::QueueAuthoredUiSprite(const Ps1UiVertex (&vertices)[4], std::uint16_t vramX,
                                              std::uint16_t vramY, std::uint16_t clutX, std::uint16_t clutY,
                                              std::uint16_t pixelsPerWord, bool fourBit,
                                              psyqo::Color modulation, bool semiTransparent, std::uint16_t bucket,
                                              const void* cacheSource, std::uint32_t cacheRevision) {
    Ps1FrameContext* context = m_authoredContext;
    if (context == nullptr) {
        ++m_stats.ownershipFailures;
        m_stats.failure = Ps1RenderFailure::Ownership;
        return false;
    }
    Ps1FrameContext::TexturedQuadFragment* fragment = context->TryReserveTexturedQuad();
    if (fragment == nullptr) {
        ++m_stats.capacityFailures;
        m_stats.failure = Ps1RenderFailure::Capacity;
        return false;
    }
    if (!context->ReuseTexturedQuad(*fragment, cacheSource, cacheRevision)) {
        const std::uint16_t baseU = static_cast<std::uint16_t>((vramX % 64u) * pixelsPerWord);
        const std::uint16_t baseV = static_cast<std::uint16_t>(vramY % 256u);
        const auto point = [](const Ps1UiVertex& input) {
            return psyqo::Vertex{{.x = input.x, .y = input.y}};
        };
        const auto uv = [baseU, baseV](const Ps1UiVertex& input) {
            return psyqo::PrimPieces::UVCoords{static_cast<std::uint8_t>(baseU + input.u),
                                                static_cast<std::uint8_t>(baseV + input.v)};
        };
        const auto paddedUv = [baseU, baseV](const Ps1UiVertex& input) {
            return psyqo::PrimPieces::UVCoordsPadded{static_cast<std::uint8_t>(baseU + input.u),
                                                      static_cast<std::uint8_t>(baseV + input.v), 0};
        };
        psyqo::PrimPieces::TPageAttr page;
        page.setPageX(static_cast<std::uint8_t>(vramX / 64u));
        page.setPageY(static_cast<std::uint8_t>(vramY / 256u));
        page.set(fourBit ? psyqo::Prim::TPageAttr::Tex4Bits : psyqo::Prim::TPageAttr::Tex8Bits);
        if (semiTransparent) page.set(psyqo::Prim::TPageAttr::HalfBackAndHalfFront);
        const psyqo::PrimPieces::ClutIndex clut(static_cast<std::uint16_t>(clutX / 16u), clutY);
        fragment->primitive = psyqo::Prim::TexturedQuad(modulation);
        if (semiTransparent) fragment->primitive.setSemiTrans();
        else fragment->primitive.setOpaque();
        fragment->primitive.pointA = point(vertices[0]);
        fragment->primitive.uvA = uv(vertices[0]);
        fragment->primitive.clutIndex = clut;
        fragment->primitive.pointB = point(vertices[1]);
        fragment->primitive.uvB = uv(vertices[1]);
        fragment->primitive.tpage = page;
        fragment->primitive.pointC = point(vertices[2]);
        fragment->primitive.uvC = paddedUv(vertices[2]);
        fragment->primitive.pointD = point(vertices[3]);
        fragment->primitive.uvD = paddedUv(vertices[3]);
        context->RememberTexturedQuad(*fragment, cacheSource, cacheRevision);
    }
    if (!context->Insert(*fragment, bucket)) {
        ++m_stats.ownershipFailures;
        m_stats.failure = Ps1RenderFailure::Ownership;
        return false;
    }
    ++m_stats.uiSprites;
    m_stats.uiTriangles = static_cast<std::uint16_t>(m_stats.uiTriangles + 2u);
    return true;
}

void Ps1RasterRenderer::CancelAuthoredFrame() {
    Ps1FrameContext* context = m_authoredContext;
    m_authoredContext = nullptr;
    if (context != nullptr && context->IsBuilding()) {
        context->Cancel();
    }
}

bool Ps1RasterRenderer::SubmitAuthoredFrame(psyqo::GPU& gpu, psyqo::Color clearColor) {
    Ps1FrameContext* context = m_authoredContext;
    m_authoredContext = nullptr;
    if (context == nullptr) {
        ++m_stats.ownershipFailures;
        m_stats.failure = Ps1RenderFailure::Ownership;
        return false;
    }
    return SubmitContext(gpu, *context, clearColor);
}

}  // namespace helengine::ps1
