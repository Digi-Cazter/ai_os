ENTRY(_start)
PHDRS {
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}
SECTIONS {
    . = 0x10000;
    .text : { KEEP(*(.text.boot)) *(.text .text.*) } :text
    .rodata : { *(.rodata .rodata.*) } :text
    . = ALIGN(4096);
    .data : { *(.data .data.*) } :data
    .bss (NOLOAD) : {
        . = ALIGN(4096);
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        __bss_end = .;
    } :data
    /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) }
    ASSERT(__bss_end < 0x90000, "Kernel exceeds bootstrap low-memory region")
}
