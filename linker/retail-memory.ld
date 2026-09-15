/* Appended after PSYQo's scripts so the allocator ceiling is retail-safe. */
__stack_start = __ram_top - 64K;
ASSERT(__ram_top == 0x80200000, "Retail PS1 RAM must be 2 MiB")
ASSERT(__heap_start <= __stack_start, "Static image overlaps stack reserve")
ASSERT(__sp >= __stack_start && __sp < __ram_top, "Initial SP outside stack reserve")

/* libsupc++ RTTI objects (pbase/pointer type info catch matchers) carry gp-relative
   .sdata references. PSYQo builds with -mno-gpopt and never loads $gp, and those
   matchers only execute during exception catching, which stays disabled. Define the
   link-time gp so the relocations resolve, and keep .data small enough to stay
   within its signed 16-bit reach. */
_gp = __data_start + 0x7ff0;
ASSERT(__data_end - __data_start <= 0xfff0, "Data section exceeds gp-relative small-data reach")
