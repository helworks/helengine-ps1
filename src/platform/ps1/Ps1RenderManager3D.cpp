#include "platform/ps1/Ps1RenderManager3D.hpp"

#include "Asset.hpp"
#include "AssetSerializer.hpp"
#include "Core.hpp"
#include "Entity.hpp"
#include "ICamera.hpp"
#include "IContentStreamSource.hpp"
#include "IDrawable3D.hpp"
#include "IRenderQueue3D.hpp"
#include "ModelAsset.hpp"
#include "ObjectManager.hpp"
#include "PlatformMaterialAsset.hpp"
#include "RuntimeMaterial.hpp"
#include "RuntimeModel.hpp"
#include "common/syscalls/syscalls.h"
#include "platform/ps1/Ps1RuntimeProvider.hpp"
#include "platform/ps1/rendering/Ps1RuntimeMaterial.hpp"
#include "platform/ps1/rendering/Ps1RuntimeModel.hpp"
#include "runtime/native_cast.hpp"
#include "runtime/native_list.hpp"
#include "system/io/stream.hpp"

namespace helengine::ps1 {
namespace {

/// <summary>Renderer family the cooked material must declare; written by the PS1 material cooker.</summary>
constexpr const char kRendererFamilyId[] = "ps1-unlit";

/// <summary>Opens and deserializes one cooked asset through Core's content source.</summary>
::Asset* Deserialize(::IContentStreamSource* source, const HeCppString& path) {
    if (source == nullptr) {
        he_cpp_custom::Fail("PS1 render manager received a null content stream source");
    }
    ::Stream* stream = source->OpenRead(path);
    if (stream == nullptr) {
        he_cpp_custom::Fail("PS1 content source returned a null stream");
    }
    ::Asset* asset = ::AssetSerializer::Deserialize(stream);
    delete stream;
    if (asset == nullptr) {
        he_cpp_custom::Fail("PS1 cooked asset deserialized to null");
    }
    return asset;
}

}  // namespace

::RuntimeMaterial* Ps1RenderManager3D::BuildMaterialAsset(::PlatformMaterialAsset* materialAsset) {
    if (materialAsset == nullptr) {
        he_cpp_custom::Fail("PS1 cooked material is null");
    }
    if (materialAsset->Lit || !materialAsset->TextureRelativePath.empty() || materialAsset->RendererFamilyId != kRendererFamilyId) {
        ramsyscall_printf("PS1_AUTHORED_MATERIAL_UNSUPPORTED family=%s lit=%u textured=%u\n", materialAsset->RendererFamilyId.c_str(),
                          materialAsset->Lit ? 1u : 0u, materialAsset->TextureRelativePath.empty() ? 0u : 1u);
        he_cpp_custom::Fail("PS1 authored scenes require opaque ps1-unlit materials without textures");
    }
    ramsyscall_printf("PS1_AUTHORED_MATERIAL id=%s color=%02X%02X%02X\n", materialAsset->get_Id().c_str(),
                      static_cast<unsigned>(materialAsset->BaseColorR), static_cast<unsigned>(materialAsset->BaseColorG),
                      static_cast<unsigned>(materialAsset->BaseColorB));
    return new Ps1RuntimeMaterial(materialAsset->BaseColorR, materialAsset->BaseColorG, materialAsset->BaseColorB, materialAsset->DoubleSided);
}

::RuntimeMaterial* Ps1RenderManager3D::BuildMaterialFromCooked(::PlatformMaterialAsset* materialAsset) {
    return BuildMaterialAsset(materialAsset);
}

::RuntimeMaterial* Ps1RenderManager3D::BuildMaterialFromCooked(HeCppString cookedAssetPath, ::IContentStreamSource* contentStreamSource) {
    ::Asset* rawAsset = Deserialize(contentStreamSource, cookedAssetPath);
    ::PlatformMaterialAsset* material = he_cpp_try_cast<::PlatformMaterialAsset>(rawAsset);
    if (material == nullptr) {
        he_cpp_custom::Fail("PS1 cooked material asset has the wrong type");
    }
    ::RuntimeMaterial* runtimeMaterial = BuildMaterialAsset(material);
    delete material;
    return runtimeMaterial;
}

::RuntimeMaterial* Ps1RenderManager3D::BuildMaterialFromRawAsset(::ContentManager*, HeCppString) {
    he_cpp_custom::Fail("PS1 authored scenes do not load raw material assets");
}

::RuntimeModel* Ps1RenderManager3D::BuildModelFromCooked(HeCppString cookedAssetPath, ::IContentStreamSource* contentStreamSource) {
    ::Asset* rawAsset = Deserialize(contentStreamSource, cookedAssetPath);
    ::ModelAsset* modelAsset = he_cpp_try_cast<::ModelAsset>(rawAsset);
    if (modelAsset == nullptr) {
        he_cpp_custom::Fail("PS1 cooked model asset has the wrong type");
    }
    Ps1RuntimeModel* runtimeModel = new Ps1RuntimeModel();
    if (!runtimeModel->Initialize(modelAsset)) {
        he_cpp_custom::Fail("PS1 cooked model is not a bounded 16-bit triangle list");
    }
    ramsyscall_printf("PS1_AUTHORED_MODEL id=%s vertices=%u indices=%u\n", modelAsset->get_Id().c_str(),
                      static_cast<unsigned>(runtimeModel->VertexCount()), static_cast<unsigned>(runtimeModel->IndexCount()));
    modelAsset->Dispose();
    delete modelAsset;
    return runtimeModel;
}

::RuntimeModel* Ps1RenderManager3D::BuildModelFromRaw(::ModelAsset*) {
    he_cpp_custom::Fail("PS1 authored scenes do not build raw model assets");
}

void Ps1RenderManager3D::Dispose() {
    VisitedDrawables = 0;
    LastDrawableValue = nullptr;
}

void Ps1RenderManager3D::Draw() {
    VisitedDrawables = 0;
    LastDrawableValue = nullptr;
    if (OwnerCore == nullptr || OwnerCore->get_ObjectManager() == nullptr) {
        return;
    }
    ::List<::ICamera*>* cameras = OwnerCore->get_ObjectManager()->get_Cameras();
    if (cameras == nullptr || cameras->get_Count() == 0) {
        return;
    }
    ::ICamera* camera = cameras->get_Item(0);
    if (camera == nullptr || camera->get_RenderQueue3D() == nullptr) {
        return;
    }
    camera->get_RenderQueue3D()->VisitOrdered(this);
}

void Ps1RenderManager3D::ReleaseMaterial(::RuntimeMaterial* material) {
    if (material == nullptr) {
        return;
    }
    material->Dispose();
    delete material;
}

void Ps1RenderManager3D::ReleaseModel(::RuntimeModel* model) {
    if (model == nullptr) {
        return;
    }
    model->Dispose();
    delete model;
}

void Ps1RenderManager3D::Visit(::IDrawable3D* drawable) {
    if (drawable == nullptr) {
        return;
    }
    ++VisitedDrawables;
    LastDrawableValue = drawable;
}

}  // namespace helengine::ps1
