/* BIOS stage 1: load 64 sectors from LBA 1 at physical 0x10000. */
.section .text
.code16
.global _start
_start:
    ljmp $0, $start
start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7c00, %sp
    sti
    cld
    movb %dl, drive
    movw $0x55aa, %bx
    movb $0x41, %ah
    int $0x13
    jc disk_error
    cmpw $0xaa55, %bx
    jne disk_error
    testw $1, %cx
    jz disk_error
    movb drive, %dl
    movw $packet, %si
    movb $0x42, %ah
    int $0x13
    jc disk_error
    cli
    ljmp $0x1000, $0
disk_error:
    movw $error, %si
1:  lodsb
    testb %al, %al
    jz 2f
    movb $0x0e, %ah
    movw $7, %bx
    int $0x10
    jmp 1b
2:  cli
    hlt
    jmp 2b
.balign 4
packet:
    .byte 0x10, 0
    .word 64
    .word 0, 0x1000
    .quad 1
drive: .byte 0
error: .asciz "AI OS: BIOS disk read failed"
.org 510
.word 0xaa55
.section .note.GNU-stack,"",@progbits
