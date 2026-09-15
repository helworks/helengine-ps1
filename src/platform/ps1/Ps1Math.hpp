#pragma once

// C ABI functions supplied by the pinned uC-sdk fdlibm sources.
extern "C" {
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
