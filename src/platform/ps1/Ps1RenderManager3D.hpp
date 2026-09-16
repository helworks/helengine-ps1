#pragma once

#include <cstdint>

#include "IRenderVisitor3D.hpp"
#include "RenderManager3D.hpp"
#include "platform/ps1/rendering/Ps1RasterRenderer.hpp"
#include "psyqo/gpu.hh"

namespace helengine::ps1 {

class Ps1RuntimeMaterial;
class Ps1RuntimeModel;

/// <summary>
/// 3D render manager backing Core's authored scene on PS1. It builds runtime models and materials
/// from the cooked assets served by the embedded archive, and each frame converts the loaded
/// camera and every visited drawable into bounded GTE poses submitted through the shared raster
/// renderer. Unsafe poses and unsupported assets fail explicitly instead of drawing approximations.
/// </summary>
class Ps1RenderManager3D final : public ::RenderManager3D, public ::IRenderVisitor3D {
public:
    /// <summary>Binds the PSYQo GPU and the frame renderer owned by the root scene.</summary>
    Ps1RenderManager3D(psyqo::GPU& gpu, Ps1RasterRenderer& renderer);

    ::RuntimeMaterial* BuildMaterialFromCooked(::PlatformMaterialAsset* materialAsset) override;
    ::RuntimeMaterial* BuildMaterialFromCooked(HeCppString cookedAssetPath, ::IContentStreamSource* contentStreamSource) override;
    ::RuntimeMaterial* BuildMaterialFromRawAsset(::ContentManager* assetContentManager, HeCppString materialAssetPath) override;
    ::RuntimeModel* BuildModelFromCooked(HeCppString cookedAssetPath, ::IContentStreamSource* contentStreamSource) override;
    ::RuntimeModel* BuildModelFromRaw(::ModelAsset* data) override;
    void Dispose() override;
    void Draw() override;
    void ReleaseMaterial(::RuntimeMaterial* material) override;
    void ReleaseModel(::RuntimeModel* model) override;
    void Visit(::IDrawable3D* drawable) override;

    /// <summary>Number of drawables visited during the most recent Draw.</summary>
    std::uint32_t LastVisitedDrawables() const { return VisitedDrawables; }

    /// <summary>True when the most recent Draw submitted its frame; false when it was rejected.</summary>
    bool LastFrameSubmitted() const { return FrameSubmitted; }

    /// <summary>Marker naming why the most recent Draw was rejected, or null when it succeeded.</summary>
    const char* LastFailureMarker() const { return FailureMarker; }

    /// <summary>GTE pose submitted for the most recently visited drawable; evidence that authored transforms drive the GTE.</summary>
    const Ps1GtePose& LastPose() const { return LastPoseValue; }

private:
    /// <summary>Validates the cooked material and creates the runtime material.</summary>
    ::RuntimeMaterial* BuildMaterialAsset(::PlatformMaterialAsset* materialAsset);

    /// <summary>Records a frame rejection once; the first failure wins.</summary>
    void Reject(const char* marker);

    psyqo::GPU& Gpu;
    Ps1RasterRenderer& Renderer;
    Ps1AuthoredTransform CameraTransform{};
    psyqo::Color ClearColor = Ps1FrameContext::DefaultClearColor;
    Ps1GtePose LastPoseValue{};
    std::uint32_t VisitedDrawables = 0;
    bool FrameOpen = false;
    bool FrameSubmitted = false;
    const char* FailureMarker = nullptr;
};

}  // namespace helengine::ps1
