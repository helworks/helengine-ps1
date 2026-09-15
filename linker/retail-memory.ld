/* Appended after PSYQo's scripts so the allocator ceiling is retail-safe. */
__stack_start = __ram_top - 64K;
ASSERT(__ram_top == 0x80200000, "Retail PS1 RAM must be 2 MiB")
ASSERT(__heap_start <= __stack_start, "Static image overlaps stack reserve")
ASSERT(__sp >= __stack_start && __sp < __ram_top, "Initial SP outside stack reserve")
