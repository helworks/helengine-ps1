#pragma once
#include "float2.hpp"
#include "float3.hpp"
#include "runtime/array.hpp"
#include <cstdint>
class ModelAsset {
public:
    Array<float3>* Positions = nullptr;
    Array<float3>* Normals = nullptr;
    Array<float2>* TexCoords = nullptr;
    Array<std::uint16_t>* Indices16 = nullptr;
};
