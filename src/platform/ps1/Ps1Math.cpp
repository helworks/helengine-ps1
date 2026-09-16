#include "Ps1Math.hpp"

// This fdlibm revision predates log2. Preserve its IEEE domain and infinity
// behavior while converting its natural logarithm into a base-two result.
extern "C" double log2(double value) {
    return log(value) * 1.44269504088896340735992468100189214;
}
