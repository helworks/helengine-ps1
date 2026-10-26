#pragma once

#include "platform/ps1/rendering/Ps1AuthoredLighting.hpp"
#include "platform/ps1/rendering/Ps1AuthoredProjection.hpp"
#include "platform/ps1/rendering/Ps1CameraClip.hpp"
#include "platform/ps1/rendering/Ps1AuthoredUiGeometry.hpp"
#include "platform/ps1/rendering/Ps1CubeMesh.hpp"
#include "platform/ps1/rendering/Ps1FrameContext.hpp"
#include "platform/ps1/rendering/Ps1TexturedMesh.hpp"
#include "psyqo/gpu.hh"
#include "psyqo/primitives/common.hh"
#include "psyqo/trigonometry.hh"

#include <cstdint>

namespace helengine::ps1 {

enum class Ps1RenderFailure : std::uint8_t {
    None,
    NotInitialized,
    Ownership,
    Projection,
    Depth,
    Capacity,
    Guard,
    Submission,
};

/** Per-frame evidence kept in target RAM for low-frequency startup diagnostics. */
struct Ps1RenderStats {
    std::uint16_t phaseX = 0;
    std::uint16_t phaseY = 0;
    std::uint32_t sceneSequence = 0;
    std::uint32_t lastSubmissionSequence = 0;
    std::uint16_t submittedTriangles = 0;
    std::uint16_t backfaceRejected = 0;
    std::uint16_t degenerate = 0;
    std::uint16_t invalid = 0;
    std::uint16_t capacityFailures = 0;
    std::uint16_t uiSprites = 0;
    std::uint16_t uiTriangles = 0;
    std::uint16_t ownershipFailures = 0;
    std::uint16_t guardFailures = 0;
    std::uint32_t projectionFlags = 0;
    std::uint32_t packetCount = 0;
    std::uint32_t highWaterBytes = 0;
    std::uint32_t peakPacketCount = 0;
    std::uint32_t peakHighWaterBytes = 0;
    std::uint32_t capacityLimit = 0;
    /// <summary>Packed per-vertex colors the GTE produced for the frame's first lit triangle.</summary>
    std::uint32_t litVertexColors[3] = {0, 0, 0};
    /// <summary>True once litVertexColors holds a sample from this frame.</summary>
    bool litSampled = false;
    Ps1RenderFailure failure = Ps1RenderFailure::None;
    bool reverseInsertion = false;
    bool guardsIntact = true;
};

/**
 * Fixed-pose unlit renderer.  It owns two frame contexts and emits only flat,
 * opaque triangles; PSYQo retains ownership of presentation and VBlank.
 */
class Ps1RasterRenderer final {
  public:
    void Initialize();
    /** Advances parity ownership when a nonblocking module fence deliberately queues no DMA chain. */
    void AdvanceUnsubmittedFrame();
    bool QueueClear(psyqo::GPU& gpu);
    bool QueueCube(psyqo::GPU& gpu, std::uint16_t phaseX, std::uint16_t phaseY);
    bool QueueInvalidPose(psyqo::GPU& gpu);
    bool QueueDepthDiagnostic(psyqo::GPU& gpu, bool reverseInsertion);
    bool QueueCapacityDiagnostic(psyqo::GPU& gpu);
    bool QueueCompletionStress(psyqo::GPU& gpu);

    /**
     * Authored frame path used by the Core render manager: begin one frame context, queue each
     * drawable's triangles through the GTE with its own pose, then submit with the camera's clear
     * color. Any failure cancels the context and records the failure kind in the stats.
     */
    bool BeginAuthoredFrame(psyqo::GPU& gpu);
    /** Separates camera buckets and applies the authored clear policy within one frame. */
    bool BeginAuthoredCamera(bool clearEnabled, psyqo::Color clearColor, float nearPlane, float farPlane);
    bool QueueAuthoredMesh(const Ps1GtePose& pose, const std::int16_t* vertices, std::uint32_t vertexCount,
                           const std::uint16_t* indices, std::uint32_t indexCount, psyqo::Color color, bool doubleSided,
                           const std::int16_t* normals = nullptr, const Ps1GteLighting* lighting = nullptr,
                           const Ps1MeshUv* uvs = nullptr, const Ps1MeshTexture* texture = nullptr);
    bool QueueAuthoredUiTriangle(const Ps1UiTriangle& triangle, psyqo::Color color, bool semiTransparent,
                                 std::uint16_t bucket);
    bool QueueAuthoredUiSprite(const Ps1UiVertex (&vertices)[4], std::uint16_t vramX, std::uint16_t vramY,
                               std::uint16_t clutX, std::uint16_t clutY, std::uint16_t pixelsPerWord,
                               psyqo::Color modulation, bool semiTransparent, std::uint16_t bucket,
                               const void* cacheSource = nullptr, std::uint32_t cacheRevision = 0);
    bool SubmitAuthoredFrame(psyqo::GPU& gpu, psyqo::Color clearColor);
    void CancelAuthoredFrame();

    const Ps1RenderStats& Stats() const { return m_stats; }

  private:
    static constexpr std::int32_t NormalTrz = 512;
    static constexpr std::int32_t InvalidTrz = 200;
    static constexpr std::uint32_t FullTurn = 2048;

    void ResetStats(std::uint16_t phaseX, std::uint16_t phaseY);
    Ps1FrameContext* BeginContext(psyqo::GPU& gpu, std::uint32_t capacity = Ps1FrameContext::TriangleCapacity);
    bool QueueCubeAtTrz(psyqo::GPU& gpu, std::uint16_t phaseX, std::uint16_t phaseY, std::int32_t trz);
    bool SubmitContext(psyqo::GPU& gpu, Ps1FrameContext& context,
                       psyqo::Color clearColor = Ps1FrameContext::DefaultClearColor);
    void WriteAuthoredPose(const Ps1GtePose& pose);
    void WriteAuthoredLighting(const Ps1GteLighting& lighting, psyqo::Color materialColor);

    Ps1FrameContext* m_authoredContext = nullptr;
    float m_nearDepth = 0.0f;
    float m_farDepth = 0.0f;
    bool ConfigureGte(std::uint16_t phaseX, std::uint16_t phaseY, std::int32_t trz);
    bool ProjectFace(const Ps1CubeMesh::Face& face, psyqo::Vertex& a, psyqo::Vertex& b, psyqo::Vertex& c,
                     std::int32_t& z0, std::int32_t& z1, std::int32_t& z2, std::int32_t& mac0);

    bool m_initialized = false;
    std::uint32_t m_sceneSequence = 0;
    Ps1FrameContext m_contexts[2];
    psyqo::Trig<> m_trig;
    Ps1RenderStats m_stats;
};

}  // namespace helengine::ps1
