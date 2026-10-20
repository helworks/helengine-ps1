// Verifies the freestanding single-precision API without hosted math headers.
#include "src/platform/ps1/Ps1Math.hpp"
extern "C" float VerifyFloatMath(float value, float other) {
    return sinf(value) + cosf(value) + tanf(value) + asinf(value)
        + atan2f(value, other) + sqrtf(value) + floorf(value)
        + ceilf(value) + fabsf(value) + fmodf(value, other);
}
