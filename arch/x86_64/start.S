.section .text.boot,"ax"
.global _start
.code16
_start:
    cli
    cld
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    /* Fast A20 gate; BIOS PC/QEMU platform. */
    inb $0x92, %al
    orb $2, %al
    andb $0xfe, %al
    outb %al, $0x92
    addr32 lgdt gdt_pointer
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmpl $0x08, $protected_mode

.code32
protected_mode:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movl $stack_top, %esp
    movl $0x80000000, %eax
    cpuid
    cmpl $0x80000001, %eax
    jb unsupported
    movl $0x80000001, %eax
    cpuid
    testl $(1 << 29), %edx
    jz unsupported
    /* BIOS loaded only file bytes; explicitly clear BSS and paging tables. */
    movl $__bss_start, %edi
    movl $__bss_end, %ecx
    subl %edi, %ecx
    xorl %eax, %eax
    rep stosb
    movl $pdpt, %eax
    orl $3, %eax
    movl %eax, pml4
    movl $page_directory, %eax
    orl $3, %eax
    movl %eax, pdpt
    xorl %ecx, %ecx
1:  movl %ecx, %eax
    shll $21, %eax
    orl $0x83, %eax
    movl %eax, page_directory(,%ecx,8)
    incl %ecx
    cmpl $512, %ecx
    jne 1b
    movl %cr4, %eax
    orl $(1 << 5), %eax
    movl %eax, %cr4
    movl $pml4, %eax
    movl %eax, %cr3
    movl $0xc0000080, %ecx
    rdmsr
    orl $(1 << 8), %eax
    wrmsr
    movl %cr0, %eax
    orl $(1 << 31), %eax
    movl %eax, %cr0
    ljmpl $0x18, $long_mode
unsupported:
    movw $0x4f45, 0xb8000
2:  hlt
    jmp 2b

.code64
long_mode:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs
    movq $stack_top, %rsp
    xorq %rbp, %rbp
    call kernel_main
3:  cli
    hlt
    jmp 3b

.balign 8
gdt:
    .quad 0
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
    .quad 0x00af9a000000ffff
gdt_end:
gdt_pointer:
    .word gdt_end - gdt - 1
    .long gdt

.section .bss,"aw",@nobits
.balign 4096
pml4: .skip 4096
pdpt: .skip 4096
page_directory: .skip 4096
.balign 16
stack_bottom: .skip 4096
stack_top:
.section .note.GNU-stack,"",@progbits
