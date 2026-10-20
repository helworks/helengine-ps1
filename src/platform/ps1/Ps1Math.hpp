#pragma once

// The pinned PS1 toolchain is built --without-headers, so no math.h exists and the C ABI
// functions come from the pinned uC-sdk fdlibm sources compiled into the executable. Hosted
// builds of the same headers (host tests) must use the platform libm declarations instead of
// redeclaring them, because hosted C++ libraries add exception specifiers to those signatures.
#if defined(__has_include)
#if __has_include(<math.h>)
#define PS1_MATH_HAS_HOSTED_MATH_HEADER 1
#endif
#endif

#if defined(PS1_MATH_HAS_HOSTED_MATH_HEADER)
#include <math.h>
#else
extern "C" {

// Genuine binary32 entry points linked from the pinned Newlib float sources.
float sinf(float value);
float cosf(float value);
float tanf(float value);
float asinf(float value);
float atan2f(float y, float x);
float sqrtf(float value);
float floorf(float value);
float ceilf(float value);
float fabsf(float value);
float fmodf(float value, float divisor);

double ceil(double value);
double floor(double value);
double fabs(double value);
double acos(double value);
double asin(double value);
double sin(double value);
double cos(double value);
double tan(double value);
double sqrt(double value);
double log(double value);
double log2(double value);
double fmod(double value, double divisor);
double atan2(double y, double x);
int finite(double value);
}
#endif
