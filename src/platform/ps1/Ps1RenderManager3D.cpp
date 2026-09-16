#include "platform/ps1/Ps1RenderManager3D.hpp"

#include "Asset.hpp"
#include "AssetSerializer.hpp"
#include "CameraClearSettings.hpp"
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
#include "float3.hpp"
#include "float4.hpp"
#include "platform/ps1/Ps1RuntimeProvider.hpp"
#include "platform/ps1/rendering/Ps1AuthoredProjection.hpp"
#include "platform/ps1/rendering/Ps1RuntimeMaterial.hpp"
#include "platform/ps1/rendering/Ps1RuntimeModel.hpp"
#include "runtime/array.hpp"
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

/// <summary>Reads an entity's world transform into the projection helper's representation.</summary>
Ps1AuthoredTransform TransformOf(::Entity* entity) {
    const ::float3 position = entity->get_Position();
    const ::float4 orientation = entity->get_Orientation();
    const ::float3 scale = entity->get_Scale();
    Ps1AuthoredTransform transform{};
    transform.Position = Ps1Vector3{position.X, position.Y, position.Z};
    transform.Orientation = Ps1Quaternion{orientation.X, orientation.Y, orientation.Z, orientation.W};
    transform.Scale = Ps1Vector3{scale.X, scale.Y, scale.Z};
    return transform;
}

/// <summary>Converts a 0..1 color channel to the GPU's 8-bit channel.</summary>
std::uint8_t Channel(float value) {
    if (value <= 0.0f) return 0;
    if (value >= 1.0f) return 255;
    return static_cast<std::uint8_t>(value * 255.0f + 0.5f);
}

}  // namespace

Ps1RenderManager3D::Ps1RenderManager3D(psyqo::GPU& gpu, Ps1RasterRenderer& renderer)
    : Gpu(gpu), Renderer(renderer) {}

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
        he_cpp_custom::Fail("PS1 cooked model is not a bounded 16-bit triangle list in GTE range");
    }
    ramsyscall_printf("PS1_AUTHORED_MODEL id=%s vertices=%u indices=%u radius_milli=%d\n", modelAsset->get_Id().c_str(),
                      static_cast<unsigned>(runtimeModel->VertexCount()), static_cast<unsigned>(runtimeModel->IndexCount()),
                      static_cast<int>(runtimeModel->BoundingRadius() * 1000.0f));
    modelAsset->Dispose();
    delete modelAsset;
    return runtimeModel;
}

::RuntimeModel* Ps1RenderManager3D::BuildModelFromRaw(::ModelAsset*) {
    he_cpp_custom::Fail("PS1 authored scenes do not build raw model assets");
}

void Ps1RenderManager3D::Dispose() {
    VisitedDrawables = 0;
    FrameOpen = false;
    FrameSubmitted = false;
    FailureMarker = nullptr;
}

void Ps1RenderManager3D::Reject(const char* marker) {
    if (FailureMarker == nullptr) {
        FailureMarker = marker;
    }
}

void Ps1RenderManager3D::Draw() {
    VisitedDrawables = 0;
    FrameOpen = false;
    FrameSubmitted = false;
    FailureMarker = nullptr;
    if (OwnerCore == nullptr || OwnerCore->get_ObjectManager() == nullptr) {
        Reject("PS1_RENDER_NO_CORE");
        return;
    }
    ::List<::ICamera*>* cameras = OwnerCore->get_ObjectManager()->get_Cameras();
    if (cameras == nullptr || cameras->get_Count() == 0) {
        Reject("PS1_RENDER_NO_CAMERA");
        return;
    }
    ::ICamera* camera = cameras->get_Item(0);
    if (camera == nullptr || camera->get_Parent() == nullptr || camera->get_RenderQueue3D() == nullptr) {
        Reject("PS1_RENDER_CAMERA_INCOMPLETE");
        return;
    }
    CameraTransform = TransformOf(camera->get_Parent());
    const ::CameraClearSettings clearSettings = camera->get_ClearSettings();
    ClearColor = clearSettings.ClearColorEnabled
        ? psyqo::Color{{.r = Channel(clearSettings.ClearColor.X), .g = Channel(clearSettings.ClearColor.Y), .b = Channel(clearSettings.ClearColor.Z)}}
        : Ps1FrameContext::DefaultClearColor;
    if (!Renderer.BeginAuthoredFrame(Gpu)) {
        Reject("PS1_RENDER_FRAME_BEGIN_FAILED");
        return;
    }
    FrameOpen = true;
    camera->get_RenderQueue3D()->VisitOrdered(this);
    if (!FrameOpen) {
        return;
    }
    FrameOpen = false;
    if (!Renderer.SubmitAuthoredFrame(Gpu, ClearColor)) {
        Reject("PS1_RENDER_SUBMISSION_FAILED");
        return;
    }
    FrameSubmitted = true;
}

void Ps1RenderManager3D::Visit(::IDrawable3D* drawable) {
    if (drawable == nullptr || !FrameOpen) {
        return;
    }
    ++VisitedDrawables;
    Ps1RuntimeModel* model = dynamic_cast<Ps1RuntimeModel*>(drawable->get_Model());
    if (model == nullptr || drawable->get_Parent() == nullptr) {
        he_cpp_custom::Fail("Authored drawable has no PS1 runtime model or parent entity");
    }
    Array<::RuntimeMaterial*>* materials = drawable->get_Materials();
    Ps1RuntimeMaterial* material = materials == nullptr || materials->Length == 0 ? nullptr : dynamic_cast<Ps1RuntimeMaterial*>((*materials)[0]);
    if (material == nullptr) {
        he_cpp_custom::Fail("Authored drawable has no PS1 runtime material");
    }
    Ps1GtePose pose{};
    if (!Ps1AuthoredProjection::TryBuildPose(CameraTransform, TransformOf(drawable->get_Parent()), model->BoundingRadius(),
                                             Ps1AuthoredProjection::DefaultFieldOfViewRadians, pose)) {
        Reject("PS1_RENDER_POSE_REJECTED");
        FrameOpen = false;
        Renderer.CancelAuthoredFrame();
        return;
    }
    LastPoseValue = pose;
    const psyqo::Color color{{.r = material->Red(), .g = material->Green(), .b = material->Blue()}};
    if (!Renderer.QueueAuthoredMesh(pose, model->GteVertices(), model->VertexCount(), model->Indices(), model->IndexCount(), color,
                                    material->DoubleSided())) {
        Reject("PS1_RENDER_MESH_REJECTED");
        FrameOpen = false;
    }
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

}  // namespace helengine::ps1
