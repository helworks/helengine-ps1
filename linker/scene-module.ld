/* One fixed-VMA, non-PIC load unit. The native planner supplies both defsyms. */
OUTPUT_FORMAT("elf32-littlemips")
OUTPUT_ARCH(mips)
ENTRY(he_ps1_module_register)
SECTIONS {
    . = MODULE_VMA;
    __module_start = .;
    .text : { *(.text .text.* .gnu.linkonce.t.*) }
    .rodata ALIGN(4) : { *(.rodata .rodata.* .gnu.linkonce.r.*) }
    .data ALIGN(4) : {
        *(.data .data.* .sdata .sdata.* .gnu.linkonce.d.* .gnu.linkonce.s.*)
        . = ALIGN(4);
        __module_init_start = .;
        KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array))
        KEEP(*(SORT(.ctors.*))) KEEP(*(.ctors))
        __module_init_end = .;
        __module_fini_start = .;
        KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array))
        KEEP(*(SORT(.dtors.*))) KEEP(*(.dtors))
        __module_fini_end = .;
        . = ALIGN(4);
    }
    __module_load_end = .;
    .bss ALIGN(4) (NOLOAD) : { *(.bss .bss.* .sbss .sbss.* .scommon) *(COMMON) }
    . = ALIGN(4);
    __module_end = .;
    /DISCARD/ : { *(.MIPS.abiflags .reginfo .pdr .comment .note.* .eh_frame .eh_frame_hdr) }
}
ASSERT(MODULE_VMA >= 0x80010000, "Module starts below retail application RAM")
ASSERT(MODULE_VMA + MODULE_CAPACITY <= 0x801f0000, "Module overlaps retail stack reserve")
ASSERT(__module_end - __module_start <= MODULE_CAPACITY, "Module exceeds assigned arena")
