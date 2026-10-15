#pragma once
#include "RuntimeTexture.hpp"
namespace helengine::ps1 {
class Ps1RenderManager2D {
public:
    unsigned Releases = 0;
    void ReleaseTexture(RuntimeTexture* texture) { ++Releases; texture->Dispose(); delete texture; }
};
}
