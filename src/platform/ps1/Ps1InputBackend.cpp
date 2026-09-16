#include "platform/ps1/Ps1InputBackend.hpp"

#include "InputFrameState.hpp"

namespace helengine::ps1 {

::InputFrameState Ps1InputBackend::CaptureFrame() {
    return ::InputFrameState();
}

}  // namespace helengine::ps1
