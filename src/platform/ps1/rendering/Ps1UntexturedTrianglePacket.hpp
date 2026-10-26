#pragma once

#include "platform/ps1/rendering/Ps1FrameContext.hpp"

namespace helengine::ps1 {

/** Reserves the packet that exactly represents three final GPU vertex colours. */
struct Ps1UntexturedTrianglePacket {
    Ps1FrameContext::TriangleFragment* Flat;
    Ps1FrameContext::GouraudFragment* Gouraud;

    static Ps1UntexturedTrianglePacket Reserve(Ps1FrameContext& context, std::uint32_t rgb0,
                                               std::uint32_t rgb1, std::uint32_t rgb2) {
        constexpr std::uint32_t rgbMask = 0x00ffffffu;
        if ((rgb0 & rgbMask) == (rgb1 & rgbMask) && (rgb0 & rgbMask) == (rgb2 & rgbMask)) {
            return {context.TryReserveTriangle(), nullptr};
        }
        return {nullptr, context.TryReserveGouraud()};
    }
};

} // namespace helengine::ps1
