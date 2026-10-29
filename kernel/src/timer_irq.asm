bits 64

section .text
global timer_irq_stub
extern timer_irq_handler

timer_irq_stub:
    cld

    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Save original stack pointer on the stack.
    mov rax, rsp
    and rsp, -16
    sub rsp, 16
    mov [rsp], rax

    call timer_irq_handler

    mov rsp, [rsp]

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax

    iretq
