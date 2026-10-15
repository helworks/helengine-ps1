#include "platform/ps1/rendering/Ps1RuntimeModel.hpp"
#include "platform/ps1/rendering/Ps1RuntimeMaterial.hpp"
#include "platform/ps1/rendering/Ps1RuntimeTexture.hpp"
#include "platform/ps1/Ps1RenderManager2D.hpp"
#include "ModelAsset.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
#include <stdexcept>
using namespace helengine::ps1;
int main() {
    float3 positions[] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    float2 uv[] = {{0, 0}, {1, 0}, {0, 1}};
    std::uint16_t indices[] = {0, 1, 2};
    Array<float3> p{positions, 3}; Array<float2> t{uv, 3}; Array<std::uint16_t> i{indices, 3};
    ModelAsset asset; asset.Positions = &p; asset.TexCoords = &t; asset.Indices16 = &i;
    Ps1RuntimeModel model; assert(model.Initialize(&asset));
    uv[1].X = 0.2f; assert(model.TextureCoordinates()[1].U == 1);
    assert(model.VertexCount() == 3 && model.Indices()[2] == 2);
    t.Length = 2; assert(!model.Initialize(&asset)); assert(!model.TextureCoordinates());
    t.Length = 3; uv[1].X = std::numeric_limits<float>::infinity(); assert(!model.Initialize(&asset));
    asset.TexCoords = nullptr; assert(model.Initialize(&asset)); assert(!model.TextureCoordinates());
    model.Dispose(); model.Dispose();
    Ps1RenderManager2D owner;
    auto* material = new Ps1RuntimeMaterial(255, 128, 64, true, false);
    auto* owned = new Ps1RuntimeTexture;
    material->SetTexture(owner, owned); assert(material->ResolvePrimaryTexture() == owned);
    material->SetTexture(owner, owned); assert(owner.Releases == 0);
    Ps1RuntimeTexture borrowed; material->SetPrimaryTexture(&borrowed);
    assert(material->ResolvePrimaryTexture() == &borrowed);
    material->Dispose(); material->Dispose(); delete material;
    assert(owner.Releases == 1 && RuntimeMaterial::DisposeCount == 1 && !borrowed.get_IsDisposed());
    Ps1RuntimeMaterial rejected(1, 2, 3, false, false);
    bool failed = false; try { rejected.SetTexture(owner, nullptr); } catch (const std::runtime_error&) { failed = true; }
    assert(failed); rejected.Dispose();
    std::puts("PS1_TEXTURED_ASSET_PASS uv_owned=1 uv_invalid=2 untextured=1 dispose_once=1 borrowed_override=1 null_reject=1");
}
