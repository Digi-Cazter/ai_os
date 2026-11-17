.section .text.boot,"ax"
.global _start
_start:
    msr daifset, #0xf
    /* The supported QEMU virt configuration starts one CPU at EL1. */
    adrp x0, stack_top
    add x0, x0, :lo12:stack_top
    mov sp, x0
    adrp x0, __bss_start
    add x0, x0, :lo12:__bss_start
    adrp x1, __bss_end
    add x1, x1, :lo12:__bss_end
1:  cmp x0, x1
    b.hs 2f
    str xzr, [x0], #8
    b 1b
2:  bl kernel_main
3:  wfi
    b 3b
.section .bss,"aw",%nobits
.balign 16
stack_bottom: .skip 4096
stack_top:
.section .note.GNU-stack,"",%progbits
