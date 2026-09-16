#include "platform/ps1/Ps1Hash.hpp"
#include <limits>
int main() {
    using helengine::ps1::Ps1Hash;
    if (Ps1Hash<float>{}(0.0f) != Ps1Hash<float>{}(-0.0f)) return 1;
    if (Ps1Hash<double>{}(0.0) != Ps1Hash<double>{}(-0.0)) return 2;
    volatile double large = 1e300;
    if (Ps1Hash<double>{}(large) != Ps1Hash<double>{}(large)) return 3;
    double nan = std::numeric_limits<double>::quiet_NaN();
    if (Ps1Hash<double>{}(nan) != Ps1Hash<double>{}(-nan)) return 4;
    volatile double infinite = std::numeric_limits<double>::infinity();
    return Ps1Hash<double>{}(infinite) == Ps1Hash<double>{}(infinite) ? 0 : 5;
}

