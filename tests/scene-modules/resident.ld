OUTPUT_FORMAT("elf32-littlemips")
OUTPUT_ARCH(mips)
ENTRY(_start)
SECTIONS {
    . = 0x80010000;
    .text : { *(.text .text.*) }
    .data ALIGN(4) : { *(.data .sdata) }
    .bss ALIGN(4) (NOLOAD) : { *(.bss .sbss) *(COMMON) }
    /DISCARD/ : { *(.MIPS.abiflags .reginfo .pdr .comment .note.* .eh_frame) }
}
