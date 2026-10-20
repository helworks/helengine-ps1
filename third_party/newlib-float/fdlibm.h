#ifndef HELENGINE_PS1_FLOAT_FDLIBM_H
#define HELENGINE_PS1_FLOAT_FDLIBM_H

/* Minimal IEEE binary32 integration header for the unmodified Newlib sources.
 * Bit-access macros follow Newlib's fdlibm convention (Sun Microsystems,
 * permission notice retained in the individual source files). */
typedef signed int __int32_t;
typedef unsigned int __uint32_t;
typedef union { float value; __uint32_t word; } ieee_float_shape_type;
_Static_assert(sizeof(float) == 4 && sizeof(__uint32_t) == 4, "IEEE binary32 required");
#define GET_FLOAT_WORD(i,d) do { ieee_float_shape_type u; u.value=(d); (i)=u.word; } while (0)
#define SET_FLOAT_WORD(d,i) do { ieee_float_shape_type u; u.word=(i); (d)=u.value; } while (0)
#define FLT_UWORD_IS_FINITE(x) ((x)<0x7f800000L)
#define FLT_UWORD_IS_NAN(x) ((x)>0x7f800000L)
#define FLT_UWORD_IS_INFINITE(x) ((x)==0x7f800000L)
#define FLT_UWORD_IS_ZERO(x) ((x)==0)
#define FLT_UWORD_IS_SUBNORMAL(x) ((x)<0x00800000L)
#define FLT_LARGEST_EXP 254
#define FLT_SMALLEST_EXP (-22)

/* IEEE cores are the public freestanding API; no errno wrappers or double calls. */
#define __ieee754_sqrtf sqrtf
#define __ieee754_asinf asinf
#define __ieee754_atan2f atan2f
#define __ieee754_fmodf fmodf
float sinf(float);
float cosf(float);
float tanf(float);
float sqrtf(float);
float asinf(float);
float atanf(float);
float atan2f(float, float);
float floorf(float);
float ceilf(float);
float fabsf(float);
float fmodf(float, float);
float scalbnf(float, int);
float copysignf(float, float);
int __ieee754_rem_pio2f(float, float*);
int __kernel_rem_pio2f(float*, float*, int, int, int, const __int32_t*);
float __kernel_sinf(float, float, int);
float __kernel_cosf(float, float);
float __kernel_tanf(float, float, int);
#endif
