#pragma once

#include <cstdint>

#include "IRenderVisitor3D.hpp"
#include "RenderManager3D.hpp"

namespace helengine::ps1 {

class Ps1RuntimeMaterial;
class Ps1RuntimeModel;

/// <summary>
/// 3D render manager backing Core's authored scene on PS1. It builds runtime models and materials
/// from the cooked assets served by the embedded archive and visits the camera's render queue each
/// frame. Packet submission through the GTE renderer is the next checkpoint; this stage records
/// the visited drawable and its material so the loaded scene can be verified from the guest log.
/// </summary>
class Ps1RenderManager3D final : public ::RenderManager3D, public ::IRenderVisitor3D {
public:
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

    /// <summary>Drawable visited most recently, or null.</summary>
    ::IDrawable3D* LastDrawable() const { return LastDrawableValue; }

private:
    /// <summary>Validates the cooked material and creates the runtime material.</summary>
    ::RuntimeMaterial* BuildMaterialAsset(::PlatformMaterialAsset* materialAsset);

    std::uint32_t VisitedDrawables = 0;
    ::IDrawable3D* LastDrawableValue = nullptr;
};

}  // namespace helengine::ps1
