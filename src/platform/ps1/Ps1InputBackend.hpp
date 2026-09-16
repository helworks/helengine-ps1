#pragma once

#include "IInputBackend.hpp"

namespace helengine::ps1 {

/// <summary>Input backend for the authored scene milestone: reports an idle frame every tick.</summary>
class Ps1InputBackend final : public ::IInputBackend {
public:
    /// <summary>Returns a default (no input) frame state; controller sampling is a later milestone.</summary>
    ::InputFrameState CaptureFrame() override;
};

}  // namespace helengine::ps1
