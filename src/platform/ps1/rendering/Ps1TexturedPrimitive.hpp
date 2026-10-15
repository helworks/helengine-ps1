#pragma once

#include "platform/ps1/rendering/Ps1TexturedMesh.hpp"
#include "psyqo/primitives/triangles.hh"

namespace helengine::ps1 {

/** Converts a diffuse/lighting color into PS1 texture modulation without altering its hue. */
inline psyqo::Color Ps1TextureModulationColor(psyqo::Color color) {
    return psyqo::Color{{.r = Ps1TextureModulation(color.r), .g = Ps1TextureModulation(color.g),
                         .b = Ps1TextureModulation(color.b)}};
}

/** Builds the actual GPU command, including depth-specific page, CLUT, and unity modulation. */
inline bool BuildPs1TexturedTriangle(psyqo::Prim::GouraudTexturedTriangle& primitive,
                                    const psyqo::Vertex (&points)[3], const Ps1MeshUv (&uvs)[3],
                                    const psyqo::Color (&colors)[3], const Ps1MeshTexture& texture) {
    std::uint8_t u[3], v[3];
    for (unsigned index = 0; index < 3; ++index) {
        if (!TryBuildPs1MeshUv(uvs[index], texture, u[index], v[index])) return false;
    }
    primitive = psyqo::Prim::GouraudTexturedTriangle(Ps1TextureModulationColor(colors[0]));
    primitive.setColorB(Ps1TextureModulationColor(colors[1])).setColorC(Ps1TextureModulationColor(colors[2])).setOpaque();
    primitive.pointA = points[0]; primitive.pointB = points[1]; primitive.pointC = points[2];
    primitive.uvA = psyqo::PrimPieces::UVCoords{u[0], v[0]};
    primitive.uvB = psyqo::PrimPieces::UVCoords{u[1], v[1]};
    primitive.uvC = psyqo::PrimPieces::UVCoordsPadded{u[2], v[2], 0};
    primitive.clutIndex = psyqo::PrimPieces::ClutIndex(texture.ClutX / 16u, texture.ClutY);
    psyqo::PrimPieces::TPageAttr page;
    page.setPageX(texture.VramX / 64u).setPageY(texture.VramY / 256u);
    page.set(texture.PixelsPerWord == 4 ? psyqo::Prim::TPageAttr::Tex4Bits :
             texture.PixelsPerWord == 2 ? psyqo::Prim::TPageAttr::Tex8Bits : psyqo::Prim::TPageAttr::Tex16Bits);
    primitive.tpage = page;
    return true;
}

}  // namespace helengine::ps1
