#pragma once

#include "psyqo/gpu.hh"

namespace helengine::ps1 {

/**
 * Stateless adapter for the PSYQo-owned GPU.  Application owns the single
 * GPU instance; this class only centralizes the approved v0.1 configuration.
 */
class Ps1Video final {
  public:
    static void Initialize(psyqo::GPU& gpu);
};

}  // namespace helengine::ps1
