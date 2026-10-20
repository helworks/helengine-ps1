#include "platform/ps1/Ps1RenderManager2D.hpp"

// Compile with the real generated Core and MIPS ABI. This budget keeps the eight
// final text caches while preventing a second retained copy of their geometry.
static_assert(sizeof(void*) == 4, "Run this fixture with the PS1 target compiler");
static_assert(sizeof(helengine::ps1::Ps1RenderManager2D) <= 84 * 1024,
              "PS1 2D renderer exceeded its single-copy text geometry budget");
