.section .text,"ax"
.global timer_interrupt
.type timer_interrupt, @function
timer_interrupt:
    /* The CPU saved a return frame, but NOT all general-purpose registers.
     * Unlike an ordinary call, an IRQ can interrupt code using ANY register. */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    cld /* C expects forward string operations; IRETQ restores the old flags. */
    /* Interrupts can arrive at any stack alignment. RBX is preserved by C. */
    movq %rsp, %rbx
    andq $-16, %rsp
    call timer_tick
    movq %rbx, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    iretq /* Restore interrupted RIP, CS, RFLAGS, RSP, and SS, not just RIP. */
.size timer_interrupt, .-timer_interrupt

.global unexpected_interrupt
.type unexpected_interrupt, @function
unexpected_interrupt:
    /* Stop on exceptions/unhandled vectors so GDB can inspect the CPU.
     * Some exceptions push an error code. We never return, so no common
     * error-code normalization or full exception reporting is needed yet. */
    cli
1:  hlt
    jmp 1b
.size unexpected_interrupt, .-unexpected_interrupt
.section .note.GNU-stack,"",@progbits
