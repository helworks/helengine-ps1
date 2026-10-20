# Freestanding binary32 math

Unmodified single-precision fdlibm sources from Newlib 2.5.0, pinned by
`upstream.json`. Each source retains its original Sun/Cygnus permission notice.
`sf_floor`, `sf_ceil`, `sf_fabs`, `sf_scalbn` and `sf_copysign` originate in
`newlib/libm/common`; the other files originate in `newlib/libm/math`.

The local `fdlibm.h` provides only their IEEE binary32 bit access, internal
prototypes and direct IEEE entry aliases. No errno/hosted libc is required.
The existing pinned SDK continues to provide the separate double API.
Compile these C files with `-fsingle-precision-constant`, `-fno-builtin` and
`-ffp-contract=off`: floating literals participate in binary32 expressions,
functions do not redirect through hosted builtins, and operations do not fuse.
The target link test rejects references to double arithmetic helper routines.
These sources include binary32 argument reduction for large trig inputs.

Accuracy gate (50,000 deterministic inputs spanning binary32 exponents plus
explicit boundary/regression vectors): sqrt, floor, ceil, fabs and fmod require
exact rounded reference bits; asin and atan2 require at most four ULP. Trig
requires four ULP for |angle| <= 1024. Beyond that, the float-only reduction is
validated to absolute error <= 2^-22 for sin/cos, or <= 2^-21 radians in
atan(tan) phase. This is not a universal four-ULP trig implementation: the
measured sample contains 257-ULP sin error near zero for a large argument,
while its absolute error remains below binary32 unit-amplitude epsilon.
The independent oracle uses Python's double libm, then rounds to binary32.
These are tested bounds on the fixture set, not an exhaustive proof.
